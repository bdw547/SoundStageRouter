#include "EndpointConverter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace soundstage::audio
{
    namespace
    {
        [[nodiscard]] std::uint16_t BytesPerSample(const SampleEncoding encoding) noexcept
        {
            switch (encoding)
            {
            case SampleEncoding::Float32:
            case SampleEncoding::Pcm32:
                return 4;
            case SampleEncoding::Pcm16:
                return 2;
            case SampleEncoding::Pcm24:
                return 3;
            case SampleEncoding::Unsupported:
                return 0;
            }
            return 0;
        }

        template <typename Value>
        void WriteValue(std::byte* destination, const Value value) noexcept
        {
            std::memcpy(destination, &value, sizeof(value));
        }
    }

    EndpointConverter::EndpointConverter(const EndpointMixFormat format) noexcept
        : format_(format)
    {
        const std::uint16_t bytesPerSample = BytesPerSample(format_.encoding);
        supported_ = format_.sampleRate > 0 && format_.channels > 0 &&
            bytesPerSample > 0 &&
            static_cast<std::uint32_t>(format_.channels) * bytesPerSample ==
                format_.blockAlign;
    }

    bool EndpointConverter::IsSupported() const noexcept
    {
        return supported_;
    }

    std::size_t EndpointConverter::RequiredBytes(const std::size_t frameCount) const noexcept
    {
        if (!supported_ ||
            frameCount > std::numeric_limits<std::size_t>::max() / format_.blockAlign)
        {
            return 0;
        }
        return frameCount * format_.blockAlign;
    }

    bool EndpointConverter::Convert(const std::span<const StereoFrame> input,
                                    const std::span<std::byte> output) const noexcept
    {
        if (!supported_ || output.size() != RequiredBytes(input.size()))
        {
            return false;
        }

        const std::size_t bytesPerSample = BytesPerSample(format_.encoding);
        std::byte* destination = output.data();
        for (const StereoFrame& frame : input)
        {
            const float mono = std::clamp(
                (frame.left + frame.right) * 0.5f, -1.0f, 1.0f);
            for (std::uint16_t channel = 0; channel < format_.channels; ++channel)
            {
                const float selected = format_.channels == 1
                    ? mono
                    : channel == 0 ? frame.left
                    : channel == 1 ? frame.right
                    : 0.0f;
                const float sample = std::clamp(selected, -1.0f, 1.0f);

                switch (format_.encoding)
                {
                case SampleEncoding::Float32:
                    WriteValue(destination, sample);
                    break;
                case SampleEncoding::Pcm16:
                    WriteValue(destination, static_cast<std::int16_t>(
                        std::lrint(sample * 32767.0f)));
                    break;
                case SampleEncoding::Pcm24:
                {
                    const auto pcm24 = static_cast<std::int32_t>(
                        std::lrint(sample * 8388607.0f));
                    destination[0] = std::byte(pcm24 & 0xff);
                    destination[1] = std::byte((pcm24 >> 8) & 0xff);
                    destination[2] = std::byte((pcm24 >> 16) & 0xff);
                    break;
                }
                case SampleEncoding::Pcm32:
                    WriteValue(destination, static_cast<std::int32_t>(
                        std::llround(static_cast<double>(sample) * 2147483647.0)));
                    break;
                case SampleEncoding::Unsupported:
                    return false;
                }
                destination += bytesPerSample;
            }
        }
        return true;
    }
}
