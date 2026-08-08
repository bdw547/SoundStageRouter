#include "../TestHarness.h"
#include "../../src/audio/AdaptiveResampler.h"
#include <array>
#include <cmath>

using namespace soundstage::audio;

namespace
{
    class RampSource final : public IFrameSource
    {
    public:
        [[nodiscard]] StereoFrame NextFrame() noexcept override
        {
            const float value = static_cast<float>(next_++);
            return { value, -value };
        }

        [[nodiscard]] std::uint64_t FramesRead() const noexcept
        {
            return next_;
        }

    private:
        std::uint64_t next_ = 0;
    };
}

TEST(Resampler_ResetPrimesExactlyTwoFrames)
{
    RampSource source;
    AdaptiveResampler resampler;

    resampler.Reset(1.0, source);

    EXPECT_EQ(source.FramesRead(), 2ull);
}

TEST(Resampler_UnityRatioProducesSequentialFrames)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    std::array<StereoFrame, 4> output{};

    resampler.Render(output, source);

    EXPECT_NEAR(output[0].left, 0.0, 1e-6);
    EXPECT_NEAR(output[1].left, 1.0, 1e-6);
    EXPECT_NEAR(output[3].left, 3.0, 1e-6);
    EXPECT_NEAR(output[3].right, -3.0, 1e-6);
}

TEST(Resampler_ClampsAndSlewsPositiveCorrectionAtFiftyPpmPerSecond)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(5000.0);
    std::array<StereoFrame, MasterSampleRate> oneSecond{};

    resampler.Render(oneSecond, source);

    EXPECT_NEAR(resampler.CurrentCorrectionPpm(), 50.0, 1e-6);
    EXPECT_TRUE(resampler.CurrentCorrectionPpm() <= MaximumCorrectionPpm);
}

TEST(Resampler_ClampsAndSlewsNegativeCorrectionAtFiftyPpmPerSecond)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(-5000.0);
    std::array<StereoFrame, MasterSampleRate> oneSecond{};

    resampler.Render(oneSecond, source);

    EXPECT_NEAR(resampler.CurrentCorrectionPpm(), -50.0, 1e-6);
    EXPECT_TRUE(resampler.CurrentCorrectionPpm() >= -MaximumCorrectionPpm);
}

TEST(Resampler_PositiveCorrectionConsumesInputFaster)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(MaximumCorrectionPpm);
    std::array<StereoFrame, MasterSampleRate> output{};

    resampler.Render(output, source);

    EXPECT_TRUE(output.back().left > 47999.0f);
}

TEST(Resampler_RemainsContinuousAcrossRenderBlocks)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(-100.0);
    std::array<StereoFrame, 64> first{};
    std::array<StereoFrame, 64> second{};

    resampler.Render(first, source);
    resampler.Render(second, source);

    EXPECT_TRUE(second.front().left > first.back().left);
    EXPECT_TRUE(second.front().left - first.back().left < 1.1f);
}

TEST(Resampler_InvalidNominalRatioPreservesForwardProgress)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetNominalRatio(0.0);
    std::array<StereoFrame, 2> output{};

    resampler.Render(output, source);

    EXPECT_TRUE(std::isfinite(output[1].left));
    EXPECT_TRUE(output[1].left > output[0].left);
}
