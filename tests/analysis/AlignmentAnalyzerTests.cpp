#include "../TestHarness.h"
#include "../../src/analysis/AlignmentAnalyzer.h"
#include "../../src/audio/TestPatternGenerator.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <vector>

using namespace soundstage::analysis;
using namespace soundstage::audio;

namespace
{
    constexpr std::size_t SignatureFrames = 240;
    constexpr std::size_t EventLeadFrames = 2400;

    WavRecording SyntheticPairedClicks(
        const std::vector<int>& offsets,
        const bool addNoise = false,
        const std::vector<unsigned>& omitted = {})
    {
        WavRecording recording;
        recording.sampleRate = MasterSampleRate;
        recording.monoSamples.assign(
            offsets.size() * MasterSampleRate + EventLeadFrames +
                SignatureFrames + 1000,
            0.0f);

        std::array<RoleFrame, SignatureFrames> frames{};
        TestPatternGenerator{}.Render(
            TestPattern::PairedClicks, 0, frames);

        if (addNoise)
        {
            std::uint32_t random = 0x1a2b3c4du;
            for (float& sample : recording.monoSamples)
            {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                const float unit =
                    static_cast<float>(random & 0xffffu) / 32767.5f - 1.0f;
                sample = unit * 0.001f;
            }
        }

        for (std::size_t event = 0; event < offsets.size(); ++event)
        {
            bool skip = false;
            for (const unsigned omittedEvent : omitted)
            {
                skip = skip || omittedEvent == event;
            }
            if (skip) continue;

            const std::int64_t front =
                static_cast<std::int64_t>(
                    EventLeadFrames + event * MasterSampleRate);
            const std::int64_t rear = front + offsets[event];
            for (std::size_t frame = 0; frame < frames.size(); ++frame)
            {
                recording.monoSamples[
                    static_cast<std::size_t>(front) + frame] +=
                    frames[frame].front.left;
                recording.monoSamples[
                    static_cast<std::size_t>(rear) + frame] +=
                    frames[frame].rear.left;
            }
        }
        return recording;
    }

    WavRecording SyntheticPairedClicks(
        const unsigned seconds, const int offsetFrames,
        const bool addNoise = false)
    {
        return SyntheticPairedClicks(
            std::vector<int>(seconds, offsetFrames), addNoise);
    }
}

TEST(AlignmentAnalyzer_PassesFiveMillisecondOffset)
{
    const AlignmentResult result = AnalyzeAlignment(
        SyntheticPairedClicks(30, 240));
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.detectedPairs, 30u);
    EXPECT_NEAR(result.medianSignedMs, 5.0, 0.1);
    EXPECT_NEAR(result.percentile95AbsoluteMs, 5.0, 0.1);
    EXPECT_TRUE(result.passed);
}

TEST(AlignmentAnalyzer_ReportsEarlyRearWithNegativeSign)
{
    const AlignmentResult result = AnalyzeAlignment(
        SyntheticPairedClicks(30, -240));
    EXPECT_EQ(result.detectedPairs, 30u);
    EXPECT_NEAR(result.medianSignedMs, -5.0, 0.1);
    EXPECT_TRUE(result.passed);
}

TEST(AlignmentAnalyzer_FailsWhenMoreThanFivePercentExceedTenMilliseconds)
{
    std::vector<int> offsets(30, 240);
    offsets[0] = offsets[1] = offsets[2] = 720;
    const AlignmentResult result =
        AnalyzeAlignment(SyntheticPairedClicks(offsets));
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(!result.passed);
    EXPECT_TRUE(result.percentile95AbsoluteMs > 10.0);
    EXPECT_NEAR(result.maximumAbsoluteMs, 15.0, 0.1);
}

TEST(AlignmentAnalyzer_RejectsTooFewDetectedPairs)
{
    const AlignmentResult result = AnalyzeAlignment(
        SyntheticPairedClicks(10, 0));
    EXPECT_TRUE(!result.valid);
    EXPECT_EQ(result.detectedPairs, 10u);
    EXPECT_TRUE(!result.error.empty());
}

TEST(AlignmentAnalyzer_FixedNoisePreservesDetection)
{
    const AlignmentResult result = AnalyzeAlignment(
        SyntheticPairedClicks(30, 240, true));
    if (result.detectedPairs != 30u)
    {
        throw std::runtime_error(
            "detected " + std::to_string(result.detectedPairs) +
            " noisy pairs");
    }
    EXPECT_NEAR(result.percentile95AbsoluteMs, 5.0, 0.1);
}

TEST(AlignmentAnalyzer_SkipsMissingEventsAndContinues)
{
    std::vector<int> offsets(30, 0);
    const AlignmentResult result = AnalyzeAlignment(
        SyntheticPairedClicks(offsets, false, {4, 9, 17}));
    EXPECT_EQ(result.detectedPairs, 27u);
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.passed);
}

TEST(AlignmentAnalyzer_RejectsWrongSampleRate)
{
    WavRecording recording;
    recording.sampleRate = 44100;
    const AlignmentResult result = AnalyzeAlignment(recording);
    EXPECT_TRUE(!result.valid);
    EXPECT_TRUE(!result.error.empty());
}
