#include "TestHarness.h"
#include "../src/RouterSettings.h"

#include <filesystem>

using soundstage::RouterSettings;
using soundstage::TestPatternFromString;
using soundstage::TestPatternToString;
using soundstage::audio::TestPattern;
using soundstage::audio::PlaybackMode;
using soundstage::audio::RearFillMode;

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
    EXPECT_EQ(settings.mode, PlaybackMode::SystemAudio);
    EXPECT_EQ(settings.rearFill, RearFillMode::Off);
}

TEST(RouterSettings_ModeNamesRoundTrip)
{
    EXPECT_EQ(soundstage::PlaybackModeToString(PlaybackMode::SystemAudio),
              std::wstring_view(L"SystemAudio"));
    EXPECT_EQ(soundstage::PlaybackModeToString(PlaybackMode::TestSignals),
              std::wstring_view(L"TestSignals"));
    EXPECT_EQ(soundstage::RearFillModeToString(RearFillMode::Ambient),
              std::wstring_view(L"Ambient"));
}

TEST(RouterSettings_FileRoundTripsModeAndRearFill)
{
    const std::wstring path =
        std::filesystem::absolute(L"routing-test.ini").wstring();
    std::filesystem::remove(path);
    soundstage::RouterSettingsStore store(path);
    RouterSettings expected;
    expected.frontEndpointId = L"front-id";
    expected.rearEndpointId = L"rear-id";
    expected.frontDelayMs = 12;
    expected.rearDelayMs = 345;
    expected.lastPattern = TestPattern::RearTone;
    expected.mode = PlaybackMode::TestSignals;
    expected.rearFill = RearFillMode::Ambient;
    store.Save(expected);
    const RouterSettings actual = store.Load();
    EXPECT_EQ(actual.frontEndpointId, expected.frontEndpointId);
    EXPECT_EQ(actual.rearEndpointId, expected.rearEndpointId);
    EXPECT_EQ(actual.frontDelayMs, expected.frontDelayMs);
    EXPECT_EQ(actual.rearDelayMs, expected.rearDelayMs);
    EXPECT_EQ(actual.lastPattern, expected.lastPattern);
    EXPECT_EQ(actual.mode, expected.mode);
    EXPECT_EQ(actual.rearFill, expected.rearFill);
    EXPECT_TRUE(!actual.loadAdjustedValues);
    std::filesystem::remove(path);
}
