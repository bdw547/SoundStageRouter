#include "TestHarness.h"
#include "../src/RouterSettings.h"

#include <filesystem>
#include <fstream>

using soundstage::RouterSettings;
using soundstage::TestPatternFromString;
using soundstage::TestPatternToString;
using soundstage::audio::TestPattern;
using soundstage::audio::PlaybackMode;
using soundstage::audio::RearFillMode;

namespace
{
    void WriteRoutingFile(const std::wstring& path,
                          const std::wstring_view contents)
    {
        std::wofstream file{std::filesystem::path(path)};
        file << contents;
    }
}

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

TEST(RouterSettings_OlderFileDefaultsSurroundLevelsWithoutAdjustment)
{
    const std::wstring path =
        std::filesystem::absolute(L"routing-old-profile-test.ini").wstring();
    WriteRoutingFile(
        path,
        L"[Routing]\n"
        L"FrontDelayMs=0\nRearDelayMs=0\n"
        L"FrontLevelPercent=100\nRearLevelPercent=100\n"
        L"TestPattern=PairedClicks\nMode=SystemAudio\nRearFill=Off\n");

    const RouterSettings actual = soundstage::RouterSettingsStore(path).Load();

    EXPECT_EQ(actual.backLevelPercent, 100);
    EXPECT_EQ(actual.sideLevelPercent, 100);
    EXPECT_TRUE(!actual.loadAdjustedValues);
    std::filesystem::remove(path);
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
    expected.frontLevelPercent = 72;
    expected.rearLevelPercent = 43;
    expected.backLevelPercent = 40;
    expected.sideLevelPercent = 75;
    expected.lastPattern = TestPattern::RearTone;
    expected.mode = PlaybackMode::TestSignals;
    expected.rearFill = RearFillMode::Ambient;
    store.Save(expected);
    const RouterSettings actual = store.Load();
    EXPECT_EQ(actual.frontEndpointId, expected.frontEndpointId);
    EXPECT_EQ(actual.rearEndpointId, expected.rearEndpointId);
    EXPECT_EQ(actual.frontDelayMs, expected.frontDelayMs);
    EXPECT_EQ(actual.rearDelayMs, expected.rearDelayMs);
    EXPECT_EQ(actual.frontLevelPercent, expected.frontLevelPercent);
    EXPECT_EQ(actual.rearLevelPercent, expected.rearLevelPercent);
    EXPECT_EQ(actual.backLevelPercent, expected.backLevelPercent);
    EXPECT_EQ(actual.sideLevelPercent, expected.sideLevelPercent);
    EXPECT_EQ(actual.lastPattern, expected.lastPattern);
    EXPECT_EQ(actual.mode, expected.mode);
    EXPECT_EQ(actual.rearFill, expected.rearFill);
    EXPECT_TRUE(!actual.loadAdjustedValues);
    std::filesystem::remove(path);
}

TEST(RouterSettings_InvalidSurroundLevelsFallBackAndMarkAdjustment)
{
    struct TestCase
    {
        std::wstring back;
        std::wstring side;
        int expectedBack;
        int expectedSide;
    };
    const TestCase cases[] = {
        {L"-1", L"75", 100, 75},
        {L"40", L"101", 40, 100},
        {L"not-a-number", L"75", 100, 75},
    };
    const std::wstring path =
        std::filesystem::absolute(L"routing-invalid-level-test.ini").wstring();

    for (const TestCase& testCase : cases)
    {
        WriteRoutingFile(
            path,
            L"[Routing]\nBackLevelPercent=" + testCase.back +
            L"\nSideLevelPercent=" + testCase.side + L"\n");

        const RouterSettings actual =
            soundstage::RouterSettingsStore(path).Load();

        EXPECT_EQ(actual.backLevelPercent, testCase.expectedBack);
        EXPECT_EQ(actual.sideLevelPercent, testCase.expectedSide);
        EXPECT_TRUE(actual.loadAdjustedValues);
    }
    std::filesystem::remove(path);
}
