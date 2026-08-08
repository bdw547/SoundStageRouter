#include "../TestHarness.h"
#include "../../src/analysis/WavReader.h"

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

using namespace soundstage::analysis;

namespace
{
    std::atomic<unsigned> fixtureNumber{0};

    void U16(std::vector<std::byte>& bytes, const std::uint16_t value)
    {
        bytes.push_back(std::byte(value & 0xff));
        bytes.push_back(std::byte((value >> 8) & 0xff));
    }

    void U32(std::vector<std::byte>& bytes, const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes.push_back(std::byte((value >> shift) & 0xff));
        }
    }

    void FourCc(std::vector<std::byte>& bytes, const char* value)
    {
        for (int index = 0; index < 4; ++index)
        {
            bytes.push_back(std::byte(value[index]));
        }
    }

    void PatchU32(std::vector<std::byte>& bytes, const std::size_t offset,
                  const std::uint32_t value)
    {
        for (unsigned shift = 0; shift < 32; shift += 8)
        {
            bytes[offset + shift / 8] =
                std::byte((value >> shift) & 0xff);
        }
    }

    struct Fixture
    {
        std::filesystem::path path;
        explicit Fixture(std::filesystem::path value)
            : path(std::move(value))
        {
        }
        Fixture(const Fixture&) = delete;
        Fixture& operator=(const Fixture&) = delete;
        Fixture(Fixture&& other) noexcept
            : path(std::move(other.path))
        {
            other.path.clear();
        }
        ~Fixture()
        {
            if (path.empty()) return;
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };

    Fixture WriteFixture(const std::vector<std::byte>& bytes)
    {
        const std::filesystem::path directory =
            std::filesystem::path("build") / "tests" / "generated";
        std::filesystem::create_directories(directory);
        Fixture fixture{
            directory /
            ("wav-reader-" +
             std::to_string(fixtureNumber.fetch_add(1)) + ".wav")};
        std::ofstream stream(fixture.path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return fixture;
    }

    std::vector<std::byte> FormatChunk(
        const std::uint16_t tag, const std::uint16_t channels,
        const std::uint32_t sampleRate, const std::uint16_t bits,
        const bool extensible = false)
    {
        std::vector<std::byte> format;
        const std::uint16_t bytesPerSample = bits / 8;
        const std::uint16_t blockAlign =
            static_cast<std::uint16_t>(channels * bytesPerSample);
        U16(format, extensible ? 0xfffe : tag);
        U16(format, channels);
        U32(format, sampleRate);
        U32(format, sampleRate * blockAlign);
        U16(format, blockAlign);
        U16(format, bits);
        if (extensible)
        {
            U16(format, 22);
            U16(format, bits);
            U32(format, 0);
            U32(format, tag);
            U16(format, 0);
            U16(format, 0x0010);
            for (const unsigned value :
                 {0x80u, 0x00u, 0x00u, 0xaau,
                  0x00u, 0x38u, 0x9bu, 0x71u})
            {
                format.push_back(std::byte(value));
            }
        }
        return format;
    }

    std::vector<std::byte> Wave(
        const std::vector<std::byte>& format,
        const std::vector<std::byte>& data,
        const bool junk = true)
    {
        std::vector<std::byte> bytes;
        FourCc(bytes, "RIFF");
        U32(bytes, 0);
        FourCc(bytes, "WAVE");
        FourCc(bytes, "fmt ");
        U32(bytes, static_cast<std::uint32_t>(format.size()));
        bytes.insert(bytes.end(), format.begin(), format.end());
        if (junk)
        {
            FourCc(bytes, "JUNK");
            U32(bytes, 3);
            bytes.insert(bytes.end(),
                         {std::byte{1}, std::byte{2}, std::byte{3},
                          std::byte{0}});
        }
        FourCc(bytes, "data");
        U32(bytes, static_cast<std::uint32_t>(data.size()));
        bytes.insert(bytes.end(), data.begin(), data.end());
        if (data.size() % 2 != 0)
        {
            bytes.push_back(std::byte{0});
        }
        PatchU32(bytes, 4,
                 static_cast<std::uint32_t>(bytes.size() - 8));
        return bytes;
    }

    void AppendI16(std::vector<std::byte>& bytes, const std::int16_t value)
    {
        U16(bytes, static_cast<std::uint16_t>(value));
    }

    void AppendFloat(std::vector<std::byte>& bytes, const float value)
    {
        U32(bytes, std::bit_cast<std::uint32_t>(value));
    }

    void ExpectReadError(const std::vector<std::byte>& bytes)
    {
        const Fixture fixture = WriteFixture(bytes);
        bool threw = false;
        try
        {
            (void)ReadWavFile(fixture.path);
        }
        catch (const WavReadError&)
        {
            threw = true;
        }
        EXPECT_TRUE(threw);
    }
}

TEST(WavReader_DecodesPcm16AndOddChunkPadding)
{
    std::vector<std::byte> samples;
    AppendI16(samples, std::numeric_limits<std::int16_t>::min());
    AppendI16(samples, 0);
    AppendI16(samples, std::numeric_limits<std::int16_t>::max());
    const Fixture fixture =
        WriteFixture(Wave(FormatChunk(1, 1, 48000, 16), samples));
    const WavRecording recording = ReadWavFile(fixture.path);
    EXPECT_EQ(recording.sampleRate, 48000u);
    EXPECT_EQ(recording.monoSamples.size(), 3u);
    EXPECT_NEAR(recording.monoSamples[0], -1.0, 1e-7);
    EXPECT_NEAR(recording.monoSamples[1], 0.0, 1e-7);
    EXPECT_NEAR(recording.monoSamples[2], 32767.0 / 32768.0, 1e-7);
}

TEST(WavReader_AveragesStereoFloatAtFortyEightKilohertz)
{
    std::vector<std::byte> samples;
    AppendFloat(samples, 0.5f);
    AppendFloat(samples, -0.5f);
    AppendFloat(samples, 1.0f);
    AppendFloat(samples, 0.0f);
    const Fixture fixture =
        WriteFixture(Wave(FormatChunk(3, 2, 48000, 32), samples));
    const WavRecording recording = ReadWavFile(fixture.path);
    EXPECT_EQ(recording.sampleRate, 48000u);
    EXPECT_EQ(recording.monoSamples.size(), 2u);
    EXPECT_NEAR(recording.monoSamples[0], 0.0, 1e-7);
    EXPECT_NEAR(recording.monoSamples[1], 0.5, 1e-7);
}

TEST(WavReader_AcceptsExtensiblePcmAndFloat)
{
    std::vector<std::byte> pcm;
    AppendI16(pcm, 16384);
    const Fixture pcmFixture =
        WriteFixture(Wave(FormatChunk(1, 1, 48000, 16, true), pcm));
    EXPECT_NEAR(ReadWavFile(pcmFixture.path).monoSamples[0], 0.5, 1e-7);

    std::vector<std::byte> floating;
    AppendFloat(floating, -0.25f);
    const Fixture floatFixture =
        WriteFixture(Wave(FormatChunk(3, 1, 48000, 32, true), floating));
    EXPECT_NEAR(ReadWavFile(floatFixture.path).monoSamples[0], -0.25, 1e-7);
}

TEST(WavReader_RejectsUnsupportedOrMalformedInputs)
{
    ExpectReadError(Wave(FormatChunk(6, 1, 48000, 16), {}));
    ExpectReadError(Wave(FormatChunk(1, 1, 44100, 16), {}));
    ExpectReadError(Wave(FormatChunk(1, 3, 48000, 16), {}));

    std::vector<std::byte> truncated =
        Wave(FormatChunk(1, 1, 48000, 16), {});
    PatchU32(truncated, 16, 1000);
    ExpectReadError(truncated);

    ExpectReadError({
        std::byte{'N'}, std::byte{'O'}, std::byte{'P'}, std::byte{'E'}});
}

TEST(WavReader_RejectsNonFiniteFloatSamples)
{
    std::vector<std::byte> samples;
    AppendFloat(samples, std::numeric_limits<float>::infinity());
    ExpectReadError(
        Wave(FormatChunk(3, 1, 48000, 32), samples));
}
