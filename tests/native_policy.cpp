#include "../UnlockerStub/FramePolicy.h"
#include "../UnlockerStub/Utils.h"
#include <cassert>
#include <iostream>
#include <vector>

std::vector<uint8_t> Image()
{
    std::vector<uint8_t> b(256);
    b[21] = 0xE8; int32_t d = 64 - 26; std::memcpy(b.data()+22, &d, 4);
    b[64] = 0xE9; d = 96 - 69; std::memcpy(b.data()+65, &d, 4);
    b[96] = 0x89; b[97] = 0x0D; d = 192 - 102; std::memcpy(b.data()+98, &d, 4);
    return b;
}

int main()
{
    using namespace FramePolicy;
    assert(sizeof(IpcData) == 24 && offsetof(IpcData, Options) == 20);
    assert(Target(144, false, true) == 144);
    assert(Target(5, false, true) == 10);
    assert(Target(2000, false, true) == 1000);
    assert(Target(144, true, false) == 10);
    assert(Target(144, true, true) == 144);
    assert(Target(144, false, false) == 144);
    auto b = Image(); assert(Trace(b,16) == 192);
    b[96] = 0x90; assert(!Trace(b,16));
    b = Image(); b[97] = 0xC0; assert(!Trace(b,16));
    b = Image(); int32_t d = -5; std::memcpy(b.data()+65,&d,4); assert(!Trace(b,16));
    b = Image(); d = 10000; std::memcpy(b.data()+22,&d,4); assert(!Trace(b,16));
    b = Image(); d = -10000; std::memcpy(b.data()+22,&d,4); assert(!Trace(b,16));
    b = Image(); d = 191-102; std::memcpy(b.data()+98,&d,4); assert(!Trace(b,16));
    b = Image(); d = 256-102; std::memcpy(b.data()+98,&d,4); assert(!Trace(b,16));
    b = Image(); b.resize(99); assert(!Trace(b,16));
    b = Image(); assert(!Trace(b,255));
    b = Image(); assert(!Trace(b,16,[](const void*,size_t){return false;}));
    b = Image(); d = -29; std::memcpy(b.data()+41,&d,4); assert(Relative(b,40,1,5)==16);
    Session s{60}; assert(s.NeedsWrite(60,144)); assert(!s.NeedsWrite(144,144));
    assert(!s.CanRestore(144)); s.Written(144); assert(s.CanRestore(144)); assert(!s.CanRestore(60));
    int writes=0, observed=60; Session steady{60};
    for(int i=0;i<1000;i++) if(steady.NeedsWrite(observed,144)) {observed=144;steady.Written(144);writes++;}
    assert(writes==1);
    observed=60; if(steady.NeedsWrite(observed,144)) writes++; assert(writes==2);
    // Put the production scanner at a no-access page boundary: SIMD may not overread.
    SYSTEM_INFO info{}; GetSystemInfo(&info);
    auto pages = static_cast<uint8_t*>(VirtualAlloc(nullptr, info.dwPageSize*2, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE));
    assert(pages); DWORD old=0; assert(VirtualProtect(pages+info.dwPageSize,info.dwPageSize,PAGE_NOACCESS,&old));
    for(size_t length : {1u,6u,15u,16u,17u,31u,32u,33u})
    {
        auto tail = pages + info.dwPageSize - length;
        std::memset(tail,0x7F,length);
        std::string pattern;
        for(size_t i=0;i<length;i++) pattern += "7F ";
        const auto matches = Utils::PatternScanAll({tail,length},pattern.c_str());
        assert(matches.size()==1 && matches.front()==tail);
    }
    assert(Utils::PatternScanAll({pages,3},"00 00 00 00").empty());
    assert(Utils::PatternScanAll({pages,3},"").empty());
    assert(VirtualFree(pages,0,MEM_RELEASE));
    std::cout << "Native policy/scanner assertions PASS; 1000 stable polls require 1 write.\n";
}
