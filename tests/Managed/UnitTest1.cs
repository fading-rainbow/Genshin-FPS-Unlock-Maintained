using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using unlockfps_nc.Service;

namespace Managed;

[TestClass]
public class ProtocolTests
{
    [TestMethod]
    public void LayoutMatchesNative()
    {
        Assert.AreEqual(24, Marshal.SizeOf<IpcData>());
        Assert.AreEqual(12, Marshal.OffsetOf<IpcData>(nameof(IpcData.Status)).ToInt32());
        Assert.AreEqual(16, Marshal.OffsetOf<IpcData>(nameof(IpcData.Framerate)).ToInt32());
        Assert.AreEqual(20, Marshal.OffsetOf<IpcData>(nameof(IpcData.Flags)).ToInt32());
    }

    [DataTestMethod]
    [DataRow(IpcStatus.None)]
    [DataRow(IpcStatus.Error)]
    [DataRow(IpcStatus.Ready)]
    [DataRow(IpcStatus.Stopped)]
    public void SettingsNeverEraseStatus(IpcStatus status)
    {
        using var mapping = MemoryMappedFile.CreateNew(null, 4096);
        using var view = mapping.CreateViewAccessor();
        var data = new IpcData { Version = 1, ProcessId = 123, ControllerProcessId = 456, Status = status };
        view.Write(0, ref data);
        IpcProtocol.Update(view, 144, true, true);
        view.Read(0, out data);
        Assert.AreEqual(status, data.Status);
        Assert.AreEqual(1, data.Version);
        Assert.AreEqual(123, data.ProcessId);
        Assert.AreEqual(456, data.ControllerProcessId);
        Assert.AreEqual(144, data.Framerate);
        Assert.AreEqual(IpcFlags.PowerSave | IpcFlags.MobileUI, data.Flags);
    }

    [DataTestMethod]
    [DataRow(false, false)]
    [DataRow(false, true)]
    [DataRow(true, false)]
    [DataRow(true, true)]
    public void UpdatingSettingsCannotCancelStop(bool powerSave, bool mobileUi)
    {
        using var mapping = MemoryMappedFile.CreateNew(null, 4096);
        using var view = mapping.CreateViewAccessor();
        IpcProtocol.RequestStop(view);
        IpcProtocol.Update(view, 144, powerSave, mobileUi);
        Assert.IsTrue(((IpcFlags)view.ReadUInt32(20)).HasFlag(IpcFlags.Stop));
    }

    [TestMethod]
    public void StopPreservesOtherFlagsAndStatus()
    {
        using var mapping = MemoryMappedFile.CreateNew(null, 4096);
        using var view = mapping.CreateViewAccessor();
        view.Write(12, (int)IpcStatus.Ready);
        IpcProtocol.Update(view, 144, true, true);
        IpcProtocol.RequestStop(view);
        Assert.AreEqual(IpcFlags.PowerSave | IpcFlags.MobileUI | IpcFlags.Stop, (IpcFlags)view.ReadUInt32(20));
        Assert.AreEqual((int)IpcStatus.Ready, view.ReadInt32(12));
    }

    [TestMethod]
    public void RepeatedUpdateIsIdempotent()
    {
        using var mapping = MemoryMappedFile.CreateNew(null, 4096);
        using var view = mapping.CreateViewAccessor();
        IpcProtocol.Update(view, 144, true, false);
        var before = new byte[24]; view.ReadArray(0, before, 0, 24);
        for (int i = 0; i < 1000; i++) IpcProtocol.Update(view, 144, true, false);
        var after = new byte[24]; view.ReadArray(0, after, 0, 24);
        CollectionAssert.AreEqual(before, after);
    }

    [TestMethod]
    public void MappingNamesAreScopedToProcess()
    {
        Assert.AreNotEqual(IpcProtocol.MappingName(123), IpcProtocol.MappingName(456));
        StringAssert.StartsWith(IpcProtocol.MappingName(123), "Local\\");
        StringAssert.EndsWith(IpcProtocol.MappingName(123), ".123");
    }
}
