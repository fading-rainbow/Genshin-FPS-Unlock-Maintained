using System.ComponentModel;
using System.IO.MemoryMappedFiles;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using unlockfps_nc.Utility;

namespace unlockfps_nc.Service;

public sealed class IpcService(ConfigService configService) : IDisposable
{
    private readonly object _gate = new();
    private MemoryMappedFile? _sharedMemory;
    private MemoryMappedViewAccessor? _view;
    private ModuleGuard _stubModule = IntPtr.Zero;
    private IntPtr _wndHook;
    private bool _disposed;

    public bool Start(int processId, CancellationToken cancellationToken = default)
    {
        lock (_gate)
        {
            if (_disposed || cancellationToken.IsCancellationRequested) return false;
            try
            {
                Cleanup(false);
                _sharedMemory = MemoryMappedFile.CreateNew(IpcProtocol.MappingName(processId), 4096);
                _view = _sharedMemory.CreateViewAccessor();
                var data = new IpcData { Version = IpcProtocol.Version, ProcessId = processId,
                    ControllerProcessId = Environment.ProcessId };
                _view.Write(0, ref data);
                Update();

                _stubModule = Native.LoadLibrary(GetUnlockerStubPath());
                if (_stubModule == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
                var hookProc = Native.GetProcAddress(_stubModule, "WndProc");
                var window = ProcessUtils.GetWindowFromProcessId(processId);
                var thread = Native.GetWindowThreadProcessId(window, out var foundProcessId);
                if (hookProc == IntPtr.Zero || window == IntPtr.Zero || thread == 0 || foundProcessId != processId)
                    throw new InvalidOperationException("The target window or hook export is invalid.");
                _wndHook = Native.SetWindowsHookEx(3, hookProc, _stubModule, thread);
                if (_wndHook == IntPtr.Zero || !Native.PostThreadMessage(thread, 0, IntPtr.Zero, IntPtr.Zero))
                    throw new Win32Exception(Marshal.GetLastWin32Error());

                var deadline = Environment.TickCount64 + 20_000;
                while (true)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    var state = (IpcStatus)_view.ReadInt32(IpcProtocol.StatusOffset);
                    if (state == IpcStatus.Ready) break;
                    if (state is IpcStatus.Error or IpcStatus.Stopped)
                        throw new InvalidOperationException("The native worker rejected this session.");
                    if (Environment.TickCount64 >= deadline) throw new TimeoutException("Native worker startup timed out.");
                    cancellationToken.WaitHandle.WaitOne(50);
                }
                if (!Native.UnhookWindowsHookEx(_wndHook)) throw new Win32Exception(Marshal.GetLastWin32Error());
                _wndHook = IntPtr.Zero;
                return true;
            }
            catch (OperationCanceledException) { Cleanup(true); return false; }
            catch (Exception error)
            {
                Cleanup(true);
                MessageBox.Show($"Failed to start the FPS worker: {error.Message}", "FPS Unlocker",
                    MessageBoxButtons.OK, MessageBoxIcon.Error);
                return false;
            }
        }
    }

    public void Update()
    {
        lock (_gate)
        {
            if (_disposed || _view == null) return;
            IpcProtocol.Update(_view, configService.Config.FPSTarget,
                configService.Config.UsePowerSave, configService.Config.UseMobileUI);
        }
    }

    public void OnGameExit() { lock (_gate) Cleanup(false); }

    private void Cleanup(bool waitForWorker)
    {
        if (_view != null)
        {
            IpcProtocol.RequestStop(_view);
            var deadline = Environment.TickCount64 + 2000;
            while (waitForWorker && Environment.TickCount64 < deadline)
            {
                var state = (IpcStatus)_view.ReadInt32(IpcProtocol.StatusOffset);
                if (state is IpcStatus.Stopped or IpcStatus.Error) break;
                Thread.Sleep(10);
            }
        }
        if (_wndHook != IntPtr.Zero) Native.UnhookWindowsHookEx(_wndHook);
        _wndHook = IntPtr.Zero;
        _stubModule.Dispose();
        _stubModule = IntPtr.Zero;
        _view?.Dispose(); _view = null;
        _sharedMemory?.Dispose(); _sharedMemory = null;
    }

    private static string GetUnlockerStubPath()
    {
        using var stream = Assembly.GetExecutingAssembly()
            .GetManifestResourceStream("unlockfps_nc.Resources.UnlockerStub.dll")
            ?? throw new InvalidOperationException("The embedded native worker is missing.");
        using var memory = new MemoryStream(); stream.CopyTo(memory);
        var bytes = memory.ToArray();
        var path = Path.Combine(AppContext.BaseDirectory, "UnlockerStub.dll");
        if (File.Exists(path) && SHA256.HashData(File.ReadAllBytes(path)).AsSpan().SequenceEqual(SHA256.HashData(bytes)))
            return path;
        var temporary = path + ".tmp";
        try
        {
            using (var file = new FileStream(temporary, FileMode.Create, FileAccess.Write, FileShare.None))
            { file.Write(bytes); file.Flush(true); }
            File.Move(temporary, path, true);
        }
        finally { if (File.Exists(temporary)) File.Delete(temporary); }
        return path;
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed) return;
            _disposed = true;
            Cleanup(true);
        }
    }
}
