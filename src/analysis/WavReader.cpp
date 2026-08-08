#include "WavReader.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>

namespace soundstage::analysis
{
    namespace
    {
        constexpr std::uint16_t WaveFormatPcm = 0x0001;
        constexpr std::uint16_t WaveFormatIeeeFloat = 0x0003;
        constexpr std::uint16_t WaveFormatExtensible = 0xfffe;

        struct Format
        {
            std::uint16_t encoding = 0;
            std::uint16_t channels = 0;
            std::uint32_t sampleRate = 0;
            std::uint16_t blockAlign = 0;
            std::uint16_t bitsPerSample = 0;
        };

        [[nodiscard]] bool Matches(
            const std::vector<std::byte>& bytes,
            const std::size_t offset,
            const std::string_view expected) noexcept
        {
            if (offset + expected.size() > bytes.size())
            {
                return false;
            }
            for (std::size_t index = 0; index < expected.size(); ++index)
            {
                if (std::to_integer<unsigned char>(bytes[offset + index]) !=
                    static_cast<unsigned char>(expected[index]))
                {
                    return false;
                }
            }
            return true;
        }

        void RequireRange(const std::vector<std::byte>& bytes,
                          const std::size_t offset,
                          const std::size_t count,
                          const char* reason)
        {
            if (offset > bytes.size() || count > bytes.size() - offset)
            {
                throw WavReadError(reason);
            }
        }

        [[nodiscard]] std::uint16_t ReadU16(
            const std::vector<std::byte>& bytes,
            const std::size_t offset,
            const char* reason = "truncated little-endian field")
        {
            RequireRange(bytes, offset, 2, reason);
            return static_cast<std::uint16_t>(
                std::to_integer<unsigned>(bytes[offset]) |
                (std::to_integer<unsigned>(bytes[offset + 1]) << 8));
        }

        [[nodiscard]] std::uint32_t ReadU32(
            const std::vector<std::byte>& bytes,
            const std::size_t offset,
            const char* reason = "truncated little-endian field")
        {
            RequireRange(bytes, offset, 4, reason);
            return std::to_integer<std::uint32_t>(bytes[offset]) |
                (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8) |
                (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16) |
                (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24);
        }

        [[nodiscard]] bool IsExtensibleSubformat(
            const std::vector<std::byte>& bytes,
            const std::size_t offset,
            const std::uint32_t data1) noexcept
        {
            constexpr std::array<unsigned char, 12> GuidTail{
                0x00, 0x00, 0x10, 0x00, 0x80, 0x00,
                0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71};
            if (offset + 16 > bytes.size() ||
                ReadU32(bytes, offset) != data1)
            {
                return false;
            }
            for (std::size_t index = 0; index < GuidTail.size(); ++index)
            {
                if (std::to_integer<unsigned char>(
                        bytes[offset + 4 + index]) != GuidTail[index])
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] Format ParseFormat(
            const std::vector<std::byte>& bytes,
            const std::size_t offset,
            const std::size_t size)
        {
            if (size < 16)
            {
                throw WavReadError("fmt chunk is shorter than 16 bytes");
            }
            RequireRange(bytes, offset, size, "truncated fmt chunk");
            Format format;
            const std::uint16_t tag = ReadU16(bytes, offset);
            format.channels = ReadU16(bytes, offset + 2);
            format.sampleRate = ReadU32(bytes, offset + 4);
            format.blockAlign = ReadU16(bytes, offset + 12);
            format.bitsPerSample = ReadU16(bytes, offset + 14);

            if (tag == WaveFormatPcm || tag == WaveFormatIeeeFloat)
            {
                format.encoding = tag;
            }
            else if (tag == WaveFormatExtensible)
            {
                if (size < 40 || ReadU16(bytes, offset + 16) < 22)
                {
                    throw WavReadError(
                        "extensible fmt chunk is incomplete");
                }
                const std::size_t subformat = offset + 24;
                if (IsExtensibleSubformat(
                        bytes, subformat, WaveFormatPcm))
                {
                    format.encoding = WaveFormatPcm;
                }
                else if (IsExtensibleSubformat(
                             bytes, subformat,
                             WaveFormatIeeeFloat))
                {
                    format.encoding = WaveFormatIeeeFloat;
                }
                else
                {
                    throw WavReadError(
                        "unsupported extensible WAV subformat");
                }
            }
            else
            {
                throw WavReadError("compressed WAV input is unsupported");
            }

            if (format.sampleRate != 48000 ||
                (format.channels != 1 && format.channels != 2))
            {
                throw WavReadError(
                    "recording must be 48 kHz mono or stereo");
            }
            const bool validEncoding =
                (format.encoding == WaveFormatPcm &&
                 format.bitsPerSample == 16) ||
                (format.encoding == WaveFormatIeeeFloat &&
                 format.bitsPerSample == 32);
            if (!validEncoding)
            {
                throw WavReadError(
                    "recording must use PCM16 or IEEE float32");
            }
            const std::uint16_t expectedAlign =
                static_cast<std::uint16_t>(
                    format.channels * (format.bitsPerSample / 8));
            if (format.blockAlign != expectedAlign)
            {
                throw WavReadError(
                    "WAV block alignment is inconsistent");
            }
            return format;
        }

        [[nodiscard]] std::vector<std::byte> ReadFile(
            const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
            {
                throw WavReadError("unable to open WAV file");
            }
            const std::streampos end = stream.tellg();
            if (end < 0 ||
                static_cast<std::uintmax_t>(end) >
                    std::numeric_limits<std::size_t>::max())
            {
                throw WavReadError("WAV file is too large");
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            stream.seekg(0);
            if (!bytes.empty() &&
                !stream.read(reinterpret_cast<char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size())))
            {
                throw WavReadError("unable to read complete WAV file");
            }
            return bytes;
        }
    }

    WavRecording ReadWavFile(const std::filesystem::path& path)
    {
        const std::vector<std::byte> bytes = ReadFile(path);
        if (bytes.size() < 12 || !Matches(bytes, 0, "RIFF") ||
            !Matches(bytes, 8, "WAVE"))
        {
            throw WavReadError("input is not a RIFF/WAVE file");
        }

        const std::uint64_t riffEnd64 =
            8ull + ReadU32(bytes, 4, "truncated RIFF header");
        if (riffEnd64 < 12 || riffEnd64 > bytes.size())
        {
            throw WavReadError("truncated RIFF payload");
        }
        const std::size_t riffEnd = static_cast<std::size_t>(riffEnd64);
        std::size_t position = 12;
        bool formatFound = false;
        bool dataFound = false;
        Format format;
        std::size_t dataOffset = 0;
        std::size_t dataBytes = 0;

        while (position < riffEnd)
        {
            if (riffEnd - position < 8)
            {
                throw WavReadError("truncated RIFF chunk header");
            }
            const std::uint32_t chunkBytes = ReadU32(bytes, position + 4);
            const std::size_t chunkOffset = position + 8;
            if (chunkBytes > riffEnd - chunkOffset)
            {
                throw WavReadError("truncated RIFF chunk payload");
            }

            if (Matches(bytes, position, "fmt ") && !formatFound)
            {
                format = ParseFormat(bytes, chunkOffset, chunkBytes);
                formatFound = true;
            }
            else if (Matches(bytes, position, "data") && !dataFound)
            {
                dataOffset = chunkOffset;
                dataBytes = chunkBytes;
                dataFound = true;
            }

            const std::size_t paddedBytes =
                static_cast<std::size_t>(chunkBytes) + (chunkBytes & 1u);
            if (paddedBytes > riffEnd - chunkOffset)
            {
                throw WavReadError("missing RIFF chunk pad byte");
            }
            position = chunkOffset + paddedBytes;
        }

        if (!formatFound || !dataFound)
        {
            throw WavReadError("missing fmt or data chunk");
        }
        if (dataBytes % format.blockAlign != 0)
        {
            throw WavReadError(
                "data chunk is not frame aligned");
        }

        const std::size_t frameCount = dataBytes / format.blockAlign;
        WavRecording recording;
        recording.sampleRate = format.sampleRate;
        recording.monoSamples.reserve(frameCount);
        const std::size_t bytesPerSample =
            format.bitsPerSample / 8;
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            double sum = 0.0;
            for (std::uint16_t channel = 0;
                 channel < format.channels; ++channel)
            {
                const std::size_t sampleOffset =
                    dataOffset + frame * format.blockAlign +
                    channel * bytesPerSample;
                if (format.encoding == WaveFormatPcm)
                {
                    const auto value = static_cast<std::int16_t>(
                        ReadU16(bytes, sampleOffset));
                    sum += static_cast<double>(value) / 32768.0;
                }
                else
                {
                    const float value = std::bit_cast<float>(
                        ReadU32(bytes, sampleOffset));
                    if (!std::isfinite(value))
                    {
                        throw WavReadError(
                            "float WAV contains a non-finite sample");
                    }
                    sum += value;
                }
            }
            recording.monoSamples.push_back(static_cast<float>(
                sum / format.channels));
        }
        return recording;
    }
}
