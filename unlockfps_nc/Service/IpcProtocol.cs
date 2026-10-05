using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;

namespace unlockfps_nc.Service;

public enum IpcStatus { None, Error, Ready, Stopped }
[Flags]
public enum IpcFlags : uint { None = 0, PowerSave = 1, MobileUI = 2, Stop = 4 }

[StructLayout(LayoutKind.Sequential, Pack = 4)]
public struct IpcData
{
    public int Version;
    public int ProcessId;
    public int ControllerProcessId;
    public IpcStatus Status;
    public int Framerate;
    public IpcFlags Flags;
}

public static class IpcProtocol
{
    public const int Version = 1;
    public const int StatusOffset = 12;
    public const int FramerateOffset = 16;
    public const int FlagsOffset = 20;
    public static string MappingName(int processId) => $@"Local\2DE95FDC-6AB7-4593-BFE6-760DD4AB422B.{processId}";

    public static void Update(MemoryMappedViewAccessor view, int framerate, bool powerSave, bool mobileUi)
    {
        if (view.ReadInt32(FramerateOffset) != framerate) view.Write(FramerateOffset, framerate);
        var oldFlags = (IpcFlags)view.ReadUInt32(FlagsOffset);
        var flags = (oldFlags & IpcFlags.Stop) | (powerSave ? IpcFlags.PowerSave : IpcFlags.None) |
                    (mobileUi ? IpcFlags.MobileUI : IpcFlags.None);
        if (flags != oldFlags) view.Write(FlagsOffset, (uint)flags);
    }

    public static void RequestStop(MemoryMappedViewAccessor view) =>
        view.Write(FlagsOffset, view.ReadUInt32(FlagsOffset) | (uint)IpcFlags.Stop);
}
