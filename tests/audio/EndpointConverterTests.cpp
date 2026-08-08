#include "../TestHarness.h"
#include "../../src/audio/EndpointConverter.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

using namespace soundstage::audio;

TEST(Converter_ConvertsFloatStereo)
{
    EndpointConverter converter({48000, 2, SampleEncoding::Float32, 8});
    std::array<StereoFrame, 2> input{{{0.25f, -0.5f}, {2.0f, -2.0f}}};
    std::array<std::byte, 16> bytes{};
    EXPECT_TRUE(converter.IsSupported());
    EXPECT_EQ(converter.RequiredBytes(input.size()), bytes.size());
    EXPECT_TRUE(converter.Convert(input, bytes));
    const float* samples = reinterpret_cast<const float*>(bytes.data());
    EXPECT_NEAR(samples[0], 0.25, 1e-7);
    EXPECT_NEAR(samples[1], -0.5, 1e-7);
    EXPECT_NEAR(samples[2], 1.0, 1e-7);
    EXPECT_NEAR(samples[3], -1.0, 1e-7);
}

TEST(Converter_DownmixesPcm16Mono)
{
    EndpointConverter converter({48000, 1, SampleEncoding::Pcm16, 2});
    std::array<StereoFrame, 2> input{{{1.0f, 0.0f}, {-2.0f, -2.0f}}};
    std::array<std::byte, 4> bytes{};
    EXPECT_TRUE(converter.Convert(input, bytes));
    const std::int16_t* samples = reinterpret_cast<const std::int16_t*>(bytes.data());
    EXPECT_EQ(samples[0], static_cast<std::int16_t>(16384));
    EXPECT_EQ(samples[1], static_cast<std::int16_t>(-32767));
}

TEST(Converter_WritesLittleEndianPcm24Stereo)
{
    EndpointConverter converter({48000, 2, SampleEncoding::Pcm24, 6});
    std::array<StereoFrame, 1> input{{{1.0f, -1.0f}}};
    std::array<std::byte, 6> bytes{};
    EXPECT_TRUE(converter.Convert(input, bytes));
    EXPECT_EQ(std::to_integer<unsigned>(bytes[0]), 0xffu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[1]), 0xffu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[2]), 0x7fu);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[3]), 0x01u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[4]), 0x00u);
    EXPECT_EQ(std::to_integer<unsigned>(bytes[5]), 0x80u);
}

TEST(Converter_ConvertsPcm32Stereo)
{
    EndpointConverter converter({44100, 2, SampleEncoding::Pcm32, 8});
    std::array<StereoFrame, 1> input{{{0.5f, -0.5f}}};
    std::array<std::byte, 8> bytes{};
    EXPECT_TRUE(converter.Convert(input, bytes));
    const std::int32_t* samples = reinterpret_cast<const std::int32_t*>(bytes.data());
    EXPECT_EQ(samples[0], static_cast<std::int32_t>(1073741824));
    EXPECT_EQ(samples[1], static_cast<std::int32_t>(-1073741824));
}

TEST(Converter_MapsStereoIntoSixChannelFloat)
{
    EndpointConverter converter({48000, 6, SampleEncoding::Float32, 24});
    std::array<StereoFrame, 1> input{{{0.25f, -0.5f}}};
    std::array<std::byte, 24> bytes{};
    EXPECT_TRUE(converter.Convert(input, bytes));
    const float* samples = reinterpret_cast<const float*>(bytes.data());
    EXPECT_NEAR(samples[0], 0.25, 1e-7);
    EXPECT_NEAR(samples[1], -0.5, 1e-7);
    for (int channel = 2; channel < 6; ++channel)
    {
        EXPECT_NEAR(samples[channel], 0.0, 1e-7);
    }
}

TEST(Converter_RejectsMalformedFormatsAndWrongBufferSizes)
{
    EndpointConverter unsupported({0, 2, SampleEncoding::Float32, 8});
    EndpointConverter misaligned({48000, 2, SampleEncoding::Pcm16, 8});
    EXPECT_TRUE(!unsupported.IsSupported());
    EXPECT_TRUE(!misaligned.IsSupported());

    EndpointConverter valid({48000, 2, SampleEncoding::Float32, 8});
    std::array<StereoFrame, 1> input{};
    std::array<std::byte, 7> shortOutput{};
    EXPECT_TRUE(!valid.Convert(input, shortOutput));
}
