#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace FramePolicy
{
    constexpr int32_t ProtocolVersion = 1;
    enum class Status : int32_t { None, Error, Ready, Stopped };
    enum Flags : uint32_t { PowerSave = 1, MobileUI = 2, Stop = 4 };
    struct IpcData
    {
        int32_t Version;
        int32_t ProcessId;
        int32_t ControllerProcessId;
        Status State;
        int32_t Framerate;
        uint32_t Options;
    };
    static_assert(sizeof(IpcData) == 24);
    static_assert(offsetof(IpcData, State) == 12);
    static_assert(offsetof(IpcData, Framerate) == 16);
    static_assert(offsetof(IpcData, Options) == 20);

    inline int32_t Target(int32_t requested, bool powerSave, bool foreground)
    {
        return std::clamp(powerSave && !foreground ? 10 : requested, 10, 1000);
    }

    using RangeCheck = bool (*)(const void*, size_t);
    inline std::optional<size_t> Relative(std::span<const uint8_t> image,
        size_t instruction, size_t displacementOffset, size_t length, RangeCheck readable = nullptr)
    {
        if (instruction >= image.size() || displacementOffset > image.size() - instruction ||
            image.size() - instruction - displacementOffset < 4) return std::nullopt;
        if (readable && !readable(image.data() + instruction + displacementOffset, 4)) return std::nullopt;
        int32_t displacement;
        std::memcpy(&displacement, image.data() + instruction + displacementOffset, 4);
        const auto result = static_cast<int64_t>(instruction) + static_cast<int64_t>(length) + displacement;
        if (result < 0 || static_cast<uint64_t>(result) >= image.size()) return std::nullopt;
        return static_cast<size_t>(result);
    }

    inline std::optional<size_t> Trace(std::span<const uint8_t> image, size_t pattern, RangeCheck readable = nullptr)
    {
        if (pattern >= image.size() || image.size() - pattern < 10 ||
            (readable && !readable(image.data() + pattern, 10)) || image[pattern + 5] != 0xE8)
            return std::nullopt;
        auto next = Relative(image, pattern + 5, 1, 5, readable);
        if (!next || (readable && !readable(image.data() + *next, 1)) || image[*next] != 0xE9) return std::nullopt;
        size_t instruction = pattern + 5;
        std::array<size_t, 64> visited{};
        size_t count = 0;
        while (true)
        {
            if (readable && !readable(image.data() + instruction, 1)) return std::nullopt;
            if (image[instruction] != 0xE8 && image[instruction] != 0xE9) break;
            if (count == visited.size() ||
                std::find(visited.begin(), visited.begin() + count, instruction) != visited.begin() + count)
                return std::nullopt;
            visited[count++] = instruction;
            next = Relative(image, instruction, 1, 5, readable);
            if (!next) return std::nullopt;
            instruction = *next;
        }
        if (image.size() - instruction < 6 || (readable && !readable(image.data() + instruction, 6)) || image[instruction] != 0x89 ||
            (image[instruction + 1] & 0xC7) != 0x05) return std::nullopt;
        auto variable = Relative(image, instruction, 2, 6, readable);
        if (!variable || image.size() - *variable < sizeof(int32_t) || *variable % alignof(int32_t))
            return std::nullopt;
        return variable;
    }

    struct Session
    {
        int32_t Original;
        int32_t LastWritten = 0;
        bool HasWritten = false;
        bool NeedsWrite(int32_t observed, int32_t desired) const { return observed != desired; }
        void Written(int32_t value) { LastWritten = value; HasWritten = true; }
        bool CanRestore(int32_t observed) const { return HasWritten && observed == LastWritten; }
    };
}
