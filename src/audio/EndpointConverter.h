#pragma once

#include "AudioTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace soundstage::audio
{
    enum class SampleEncoding { Float32, Pcm16, Pcm24, Pcm32, Unsupported };

    struct EndpointMixFormat
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        SampleEncoding encoding = SampleEncoding::Unsupported;
        std::uint16_t blockAlign = 0;
    };

    class EndpointConverter
    {
    public:
        explicit EndpointConverter(EndpointMixFormat format) noexcept;
        [[nodiscard]] bool IsSupported() const noexcept;
        [[nodiscard]] std::size_t RequiredBytes(std::size_t frameCount) const noexcept;
        [[nodiscard]] bool Convert(std::span<const StereoFrame> input,
                                   std::span<std::byte> output) const noexcept;

    private:
        EndpointMixFormat format_{};
        bool supported_ = false;
    };
}
