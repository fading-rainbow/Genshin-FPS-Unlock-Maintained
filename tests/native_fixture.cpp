// Isolated synthetic PE host. Never launches or opens the installed game.
#include "../UnlockerStub/FramePolicy.h"
#include <Windows.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>

__attribute__((section("il2cpp"))) unsigned char FixtureCode[128] = {};
volatile LONG FixtureFps = 60;

bool WaitState(volatile FramePolicy::IpcData* data, FramePolicy::Status state)
{
    const auto deadline = GetTickCount64()+5000;
    while(GetTickCount64()<deadline) { if(data->State==state) return true; Sleep(10); }
    return false;
}
bool WaitFps(LONG value)
{
    const auto deadline = GetTickCount64()+3000;
    while(GetTickCount64()<deadline) { if(FixtureFps==value) return true; Sleep(10); }
    return false;
}
int main(int argc, char** argv)
{
    if(argc==2 && std::string(argv[1])=="--controller") {Sleep(1500);return 0;}
    if(argc!=4 || std::string(argv[1])!="--fixture") return 2;
    const std::string mode=argv[3];
    const unsigned char pattern[]={0xB9,0x3C,0,0,0,0xE8};
    std::memcpy(FixtureCode+16,pattern,6);
    int32_t displacement=64-26; std::memcpy(FixtureCode+22,&displacement,4);
    FixtureCode[64]=0xE9; displacement=96-69; std::memcpy(FixtureCode+65,&displacement,4);
    FixtureCode[96]=0x89; FixtureCode[97]=0x0D;
    auto wideDisp=reinterpret_cast<const unsigned char*>(const_cast<const LONG*>(&FixtureFps))-(FixtureCode+102);
    assert(wideDisp>=INT32_MIN && wideDisp<=INT32_MAX);
    displacement=static_cast<int32_t>(wideDisp); std::memcpy(FixtureCode+98,&displacement,4);
    const auto name="Local\\2DE95FDC-6AB7-4593-BFE6-760DD4AB422B."+std::to_string(GetCurrentProcessId());
    auto mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,name.c_str());assert(mapping);
    auto data=static_cast<volatile FramePolicy::IpcData*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,0));assert(data);
    PROCESS_INFORMATION controller{};
    if(mode=="controller-exit")
    {
        STARTUPINFOA info{};info.cb=sizeof(info);
        std::string command="\""+std::string(argv[0])+"\" --controller";
        assert(CreateProcessA(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&info,&controller));
        CloseHandle(controller.hThread);
    }
    data->Version=mode=="bad-protocol"?99:FramePolicy::ProtocolVersion;
    data->ProcessId=GetCurrentProcessId();
    data->ControllerProcessId=controller.hProcess?controller.dwProcessId:GetCurrentProcessId();
    data->State=FramePolicy::Status::None;data->Framerate=144;data->Options=0;
    auto module=LoadLibraryA(argv[2]);assert(module);
    if(mode=="bad-protocol")
    {assert(WaitState(data,FramePolicy::Status::Error));assert(FixtureFps==60);}
    else
    {
        assert(WaitState(data,FramePolicy::Status::Ready));assert(WaitFps(144));
        if(mode=="controller-exit")
        {assert(WaitState(data,FramePolicy::Status::Stopped));assert(FixtureFps==60);}
        else
        {
            data->Framerate=240;assert(WaitFps(240));
            FixtureFps=60;assert(WaitFps(240));
            data->Framerate=5;assert(WaitFps(10));
            data->Framerate=2000;assert(WaitFps(1000));
            data->Options=FramePolicy::Stop;
            assert(WaitState(data,FramePolicy::Status::Stopped));assert(FixtureFps==60);
        }
    }
    Sleep(100);FreeLibrary(module);UnmapViewOfFile(const_cast<FramePolicy::IpcData*>(data));CloseHandle(mapping);
    if(controller.hProcess){WaitForSingleObject(controller.hProcess,5000);CloseHandle(controller.hProcess);}
    std::cout<<"Synthetic native worker fixture "<<mode<<": PASS (no installed game accessed).\n";
}
