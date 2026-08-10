#pragma once

#include "AudioTypes.h"

#include <cstdint>

namespace soundstage::audio
{
    enum class VirtualSurroundFormat : std::uint8_t
    {
        Unsupported,
        FivePointOne,
        SevenPointOne
    };

    struct VirtualFormatDescription
    {
        std::uint32_t sampleRate;
        std::uint16_t channels;
        std::uint16_t bitsPerSample;
        std::uint16_t blockAlign;
        std::uint32_t channelMask;
        bool floatingPoint;
    };

    [[nodiscard]] constexpr VirtualSurroundFormat
    DetectVirtualSurroundFormat(const VirtualFormatDescription value) noexcept
    {
        if (value.sampleRate == MasterSampleRate &&
            value.channels == 6 &&
            value.bitsPerSample == 32 &&
            value.blockAlign == 24 &&
            value.channelMask == 0x003Fu &&
            value.floatingPoint)
        {
            return VirtualSurroundFormat::FivePointOne;
        }

        if (value.sampleRate == MasterSampleRate &&
            value.channels == 8 &&
            value.bitsPerSample == 32 &&
            value.blockAlign == 32 &&
            value.channelMask == 0x063Fu &&
            value.floatingPoint)
        {
            return VirtualSurroundFormat::SevenPointOne;
        }

        return VirtualSurroundFormat::Unsupported;
    }
}
