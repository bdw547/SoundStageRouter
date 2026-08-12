#include "../TestHarness.h"
#include "../../src/audio/EndpointPipeline.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace
{
    std::atomic<std::size_t> allocationCount{0};
}

void* operator new(const std::size_t size)
{
    allocationCount.fetch_add(1, std::memory_order_relaxed);
    if (void* memory = std::malloc(size))
    {
        return memory;
    }
    throw std::bad_alloc();
}

void operator delete(void* memory) noexcept
{
    std::free(memory);
}

void operator delete(void* memory, std::size_t) noexcept
{
    std::free(memory);
}

using namespace soundstage::audio;

TEST(Pipeline_FrontToneRendersFloatStereo)
{
    EndpointPipeline pipeline;
    const PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::FrontTone, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 480};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 480 * 8> output{};
    EXPECT_TRUE(pipeline.Render(output, 480));
    const float* samples = reinterpret_cast<const float*>(output.data());
    double energy = 0.0;
    for (const float sample : std::span(samples, 480 * 2))
    {
        energy += std::abs(sample);
    }
    EXPECT_TRUE(energy > 1.0);
}

TEST(Pipeline_RearToneRendersPcm16At44100)
{
    EndpointPipeline pipeline;
    const PipelineConfiguration config{
        SpeakerRole::Rear, TestPattern::RearTone, 0,
        EndpointMixFormat{44100, 2, SampleEncoding::Pcm16, 4}, 441};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 441 * 4> output{};
    EXPECT_TRUE(pipeline.Render(output, 441));
    const std::int16_t* samples =
        reinterpret_cast<const std::int16_t*>(output.data());
    bool nonzero = false;
    for (const std::int16_t sample : std::span(samples, 441 * 2))
    {
        nonzero = nonzero || sample != 0;
    }
    EXPECT_TRUE(nonzero);
}

TEST(Pipeline_FrontDelayStartsWithSilence)
{
    EndpointPipeline pipeline;
    const PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::FrontTone, 10,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 480};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 480 * 8> output{};
    EXPECT_TRUE(pipeline.Render(output, 480));
    const float* samples = reinterpret_cast<const float*>(output.data());
    for (std::size_t index = 0; index < 480 * 2; ++index)
    {
        EXPECT_NEAR(samples[index], 0.0, 1e-7);
    }
}

TEST(Pipeline_KeepSinkAwakeAddsInaudibleFloorToSilence)
{
    EndpointPipeline pipeline;
    PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::FrontTone, 10,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 480};
    config.keepSinkAwake = true;
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 480 * 8> output{};
    // The 10 ms delay makes this first block pure source silence.
    EXPECT_TRUE(pipeline.Render(output, 480));
    const float* samples = reinterpret_cast<const float*>(output.data());
    bool nonzero = false;
    float peak = 0.0f;
    for (std::size_t index = 0; index < 480 * 2; ++index)
    {
        const float magnitude =
            samples[index] < 0.0f ? -samples[index] : samples[index];
        nonzero = nonzero || magnitude > 0.0f;
        if (magnitude > peak)
        {
            peak = magnitude;
        }
    }
    EXPECT_TRUE(nonzero);
    EXPECT_TRUE(peak <= 2.6e-4f);

    // A fully faded-out block must still carry the keep-alive floor.
    EXPECT_TRUE(pipeline.Render(output, 480, 0.0f, 0.0f));
    bool mutedNonzero = false;
    for (std::size_t index = 0; index < 480 * 2; ++index)
    {
        mutedNonzero = mutedNonzero || samples[index] != 0.0f;
    }
    EXPECT_TRUE(mutedNonzero);
}

TEST(Pipeline_LiveDelayUpdateConverges)
{
    EndpointPipeline pipeline;
    const PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::FrontTone, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 480};
    EXPECT_TRUE(pipeline.Reset(config));
    pipeline.SetDelayMs(10);
    std::array<std::byte, 480 * 8> output{};
    for (int block = 0; block < 5; ++block)
    {
        EXPECT_TRUE(pipeline.Render(output, 480));
    }
    EXPECT_EQ(pipeline.CurrentDelayMs(), 10u);
}

TEST(Pipeline_RenderDoesNotAllocate)
{
    EndpointPipeline pipeline;
    const PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::PairedClicks, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 128};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 128 * 8> output{};
    const std::size_t before = allocationCount.load(std::memory_order_relaxed);
    for (int render = 0; render < 100; ++render)
    {
        EXPECT_TRUE(pipeline.Render(output, 128));
    }
    EXPECT_EQ(allocationCount.load(std::memory_order_relaxed), before);
}

TEST(Pipeline_ValidatesCapacityAndOutputSize)
{
    EndpointPipeline pipeline;
    PipelineConfiguration invalid{
        SpeakerRole::Front, TestPattern::PairedClicks, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 0};
    EXPECT_TRUE(!pipeline.Reset(invalid));

    invalid.maximumRenderFrames = 4;
    EXPECT_TRUE(pipeline.Reset(invalid));
    std::array<std::byte, 4 * 8> output{};
    EXPECT_TRUE(!pipeline.Render(output, 5));
    EXPECT_TRUE(!pipeline.Render(std::span(output).first(output.size() - 1), 4));
}

TEST(Pipeline_SystemModeConsumesMasterFrames)
{
    MasterFrameRingBuffer ring(16);
    for (int index = 0; index < 12; ++index)
    {
        EXPECT_TRUE(ring.Push({{0.25f, -0.25f}, {0.5f, -0.5f}}));
    }
    EndpointPipeline pipeline;
    PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::PairedClicks, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 4,
        PlaybackMode::SystemAudio, &ring};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 4 * 8> output{};
    EXPECT_TRUE(pipeline.Render(output, 4));
    const float* samples =
        reinterpret_cast<const float*>(output.data());
    EXPECT_NEAR(samples[0], 0.25, 1e-6);
    EXPECT_NEAR(samples[1], -0.25, 1e-6);
}

TEST(Pipeline_AppliesIndependentEndpointGain)
{
    MasterFrameRingBuffer ring(16);
    for (int index = 0; index < 12; ++index)
    {
        EXPECT_TRUE(ring.Push({{0.8f, -0.4f}, {0.0f, 0.0f}}));
    }
    EndpointPipeline pipeline;
    PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::PairedClicks, 0,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 4,
        PlaybackMode::SystemAudio, &ring, 0.25f};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 4 * 8> output{};
    EXPECT_TRUE(pipeline.Render(output, 4));
    const float* samples =
        reinterpret_cast<const float*>(output.data());
    EXPECT_NEAR(samples[0], 0.2, 1e-6);
    EXPECT_NEAR(samples[1], -0.1, 1e-6);
}
