#include "TestHarness.h"
#include "../src/RouterSettings.h"

using soundstage::RouterSettings;
using soundstage::TestPatternFromString;
using soundstage::TestPatternToString;
using soundstage::audio::TestPattern;

TEST(RouterSettings_TestPatternsRoundTrip)
{
    for (const TestPattern pattern : {
             TestPattern::PairedClicks,
             TestPattern::AlternatingClicks,
             TestPattern::FrontTone,
             TestPattern::RearTone})
    {
        EXPECT_EQ(
            TestPatternFromString(TestPatternToString(pattern)),
            pattern);
    }
    EXPECT_EQ(
        TestPatternFromString(L"unknown"),
        TestPattern::PairedClicks);
}

TEST(RouterSettings_DefaultsToPairedClicksWithoutAdjustment)
{
    const RouterSettings settings;
    EXPECT_EQ(settings.lastPattern, TestPattern::PairedClicks);
    EXPECT_TRUE(!settings.loadAdjustedValues);
}
