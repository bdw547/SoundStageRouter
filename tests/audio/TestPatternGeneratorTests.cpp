#include "../TestHarness.h"
#include "../../src/audio/TestPatternGenerator.h"
#include <array>
#include <cmath>

using namespace soundstage::audio;

TEST(TestPattern_PairedClickContainsDistinctFrontAndRearEnergy)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 240> frames{};
    generator.Render(TestPattern::PairedClicks, 0, frames);
    double frontEnergy = 0.0;
    double rearEnergy = 0.0;
    double difference = 0.0;
    for (const auto& frame : frames)
    {
        frontEnergy += frame.front.left * frame.front.left;
        rearEnergy += frame.rear.left * frame.rear.left;
        difference += std::abs(frame.front.left - frame.rear.left);
    }
    EXPECT_TRUE(frontEnergy > 0.01);
    EXPECT_TRUE(rearEnergy > 0.01);
    EXPECT_TRUE(difference > 0.1);
}

TEST(TestPattern_AlternatesRolesOncePerSecond)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 240> first{};
    std::array<RoleFrame, 240> second{};
    generator.Render(TestPattern::AlternatingClicks, 0, first);
    generator.Render(TestPattern::AlternatingClicks, MasterSampleRate, second);
    EXPECT_TRUE(std::abs(first[10].front.left) > 0.0f);
    EXPECT_EQ(first[10].rear.left, 0.0f);
    EXPECT_EQ(second[10].front.left, 0.0f);
    EXPECT_TRUE(std::abs(second[10].rear.left) > 0.0f);
}

TEST(TestPattern_RandomAccessMatchesContiguousRendering)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 512> contiguous{};
    std::array<RoleFrame, 256> tail{};
    generator.Render(TestPattern::PairedClicks, 0, contiguous);
    generator.Render(TestPattern::PairedClicks, 64, tail);
    for (std::size_t index = 0; index < tail.size(); ++index)
    {
        EXPECT_NEAR(tail[index].front.left, contiguous[index + 64].front.left, 1e-7);
        EXPECT_NEAR(tail[index].rear.left, contiguous[index + 64].rear.left, 1e-7);
    }
}

TEST(TestPattern_ToneIsBoundedFadedAndRoleSpecific)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 480> fadeIn{};
    std::array<RoleFrame, 1> off{};
    generator.Render(TestPattern::FrontTone, 0, fadeIn);
    generator.Render(TestPattern::FrontTone, 24000, off);
    EXPECT_NEAR(fadeIn.front().front.left, 0.0, 1e-7);
    for (const auto& frame : fadeIn)
    {
        EXPECT_TRUE(std::abs(frame.front.left) <= 0.25f);
        EXPECT_NEAR(frame.rear.left, 0.0, 1e-7);
    }
    EXPECT_NEAR(off.front().front.left, 0.0, 1e-7);
}
