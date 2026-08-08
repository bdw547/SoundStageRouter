#include "../TestHarness.h"
#include "../../src/audio/DelayLine.h"
#include <algorithm>
#include <cmath>
#include <numbers>

using namespace soundstage::audio;

TEST(DelayLine_ProducesAnExactStableDelay)
{
    DelayLine delay(96000);
    delay.Reset(480.0);
    StereoFrame output{};
    for (int frame = 0; frame <= 480; ++frame)
    {
        output = delay.ProcessFrame({ frame == 0 ? 1.0f : 0.0f,
                                      frame == 0 ? 1.0f : 0.0f });
        if (frame < 480) EXPECT_NEAR(output.left, 0.0, 1e-7);
    }
    EXPECT_NEAR(output.left, 1.0, 1e-7);
}

TEST(DelayLine_ProducesZeroDelayWithoutChangingInput)
{
    DelayLine delay(96000);
    delay.Reset(0.0);

    EXPECT_NEAR(delay.ProcessFrame({ 0.25f, -0.5f }).left, 0.25, 1e-7);
    EXPECT_NEAR(delay.ProcessFrame({ 0.25f, -0.5f }).right, -0.5, 1e-7);
}

TEST(DelayLine_InterpolatesAcrossTheCircularBufferBoundary)
{
    DelayLine delay(3);
    delay.Reset(0.25);

    const StereoFrame output = delay.ProcessFrame({ 1.0f, 1.0f });

    EXPECT_NEAR(output.left, 0.75, 1e-7);
    EXPECT_NEAR(output.right, 0.75, 1e-7);
}

TEST(DelayLine_ProducesTheMaximumStableDelay)
{
    DelayLine delay(96000);
    delay.Reset(96000.0);
    StereoFrame output{};
    for (int frame = 0; frame <= 96000; ++frame)
    {
        output = delay.ProcessFrame({ frame == 0 ? 1.0f : 0.0f,
                                      frame == 0 ? 1.0f : 0.0f });
        if (frame < 96000) EXPECT_NEAR(output.left, 0.0, 1e-7);
    }
    EXPECT_NEAR(output.left, 1.0, 1e-7);
}

TEST(DelayLine_SlewsWithoutMovingItsReadHeadBackward)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    double previousReadPosition = delay.DebugReadPosition();
    delay.SetDelayFrames(480.0);
    for (int frame = 0; frame < 2400; ++frame)
    {
        static_cast<void>(delay.ProcessFrame({ 0.1f, 0.1f }));
        EXPECT_TRUE(delay.DebugReadPosition() > previousReadPosition);
        previousReadPosition = delay.DebugReadPosition();
    }
    EXPECT_NEAR(delay.CurrentDelayFrames(), 480.0, 1e-6);
}

TEST(DelayLine_ClampsToAllocatedCapacity)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    delay.SetDelayFrames(100000.0);
    for (int frame = 0; frame < 400000; ++frame)
    {
        static_cast<void>(delay.ProcessFrame({}));
    }
    EXPECT_NEAR(delay.CurrentDelayFrames(), 96000.0, 1e-6);
}

TEST(DelayLine_TenMillisecondEditHasBoundedSampleSteps)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    StereoFrame previous{};
    double maximumStep = 0.0;
    for (int frame = 0; frame < 4800; ++frame)
    {
        if (frame == 960) delay.SetDelayFrames(480.0);
        const float sample = static_cast<float>(0.1 * std::sin(
            2.0 * std::numbers::pi * 440.0 * frame / MasterSampleRate));
        const StereoFrame current = delay.ProcessFrame({ sample, sample });
        maximumStep = std::max(maximumStep, static_cast<double>(
            std::abs(current.left - previous.left)));
        previous = current;
    }
    EXPECT_TRUE(maximumStep < 0.02);
}
