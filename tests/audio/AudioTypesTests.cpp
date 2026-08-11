#include "../TestHarness.h"
#include "../../src/audio/AudioTypes.h"
#include "../../src/audio/SurroundUiState.h"
#include "../../src/audio/VirtualSurroundContract.h"

using namespace soundstage::audio;

TEST(AudioTypes_ClampsDelayToMilestoneRange)
{
    EXPECT_EQ(ClampDelayMs(-1), 0u);
    EXPECT_EQ(ClampDelayMs(750), 750u);
    EXPECT_EQ(ClampDelayMs(2500), 2000u);
}

TEST(AudioTypes_ConvertsMillisecondsOnTheMasterTimeline)
{
    EXPECT_EQ(MillisecondsToFrames(0), 0ull);
    EXPECT_EQ(MillisecondsToFrames(10), 480ull);
    EXPECT_EQ(MillisecondsToFrames(2000), 96000ull);
}

TEST(AudioTypes_DefaultConfigurationReferencesRear)
{
    EXPECT_EQ(RunConfiguration{}.clockReferenceRole, SpeakerRole::Rear);
}

TEST(AudioTypes_DetectsOnlySupportedVirtualSurroundFormats)
{
    EXPECT_EQ(DetectVirtualSurroundFormat(
        {48000, 6, 32, 24, 0x003Fu, true}),
        VirtualSurroundFormat::FivePointOne);
    EXPECT_EQ(DetectVirtualSurroundFormat(
        {48000, 8, 32, 32, 0x063Fu, true}),
        VirtualSurroundFormat::SevenPointOne);
    EXPECT_EQ(DetectVirtualSurroundFormat(
        {48000, 8, 32, 32, 0x003Fu, true}),
        VirtualSurroundFormat::Unsupported);
}

TEST(SurroundUi_UsesDiscoveredFormatWhileStopped)
{
    const SurroundUiState state = BuildSurroundUiState(
        PlaybackMode::SystemAudio,
        VirtualSurroundFormat::Unsupported,
        VirtualSurroundFormat::FivePointOne,
        false);

    EXPECT_EQ(state.format, VirtualSurroundFormat::FivePointOne);
    EXPECT_TRUE(!state.sideEnabled);
    EXPECT_EQ(state.badge, std::wstring_view(L"5.1 detected"));
    EXPECT_EQ(state.hint,
              std::wstring_view(L"Used when Windows is set to 7.1."));
}

TEST(SurroundUi_EnablesSideOnlyForKnownSevenPointOneOrTestMode)
{
    EXPECT_TRUE(!BuildSurroundUiState(
        PlaybackMode::SystemAudio,
        VirtualSurroundFormat::Unsupported,
        VirtualSurroundFormat::Unsupported,
        false).sideEnabled);
    EXPECT_TRUE(BuildSurroundUiState(
        PlaybackMode::SystemAudio,
        VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::FivePointOne,
        false).sideEnabled);
    EXPECT_TRUE(BuildSurroundUiState(
        PlaybackMode::TestSignals,
        VirtualSurroundFormat::Unsupported,
        VirtualSurroundFormat::Unsupported,
        false).sideEnabled);
}

TEST(SurroundUi_OffersFormatSpecificManualRestartCopy)
{
    const SurroundUiState state = BuildSurroundUiState(
        PlaybackMode::SystemAudio,
        VirtualSurroundFormat::Unsupported,
        VirtualSurroundFormat::SevenPointOne,
        true);

    EXPECT_EQ(state.restartAction,
              std::wstring_view(L"Restart in 7.1"));
    EXPECT_EQ(state.recovery,
              std::wstring_view(
                  L"Routing stopped. Review the fault, then restart manually in 7.1."));
}

TEST(SurroundUi_RefreshesDiscoveryOnceWhenRoutingStopsOrFaults)
{
    EXPECT_TRUE(ShouldRefreshSurroundDiscovery(
        PlaybackState::Running, PlaybackState::Stopped));
    EXPECT_TRUE(ShouldRefreshSurroundDiscovery(
        PlaybackState::Preparing, PlaybackState::Faulted));
    EXPECT_TRUE(!ShouldRefreshSurroundDiscovery(
        PlaybackState::Stopped, PlaybackState::Stopped));
    EXPECT_TRUE(!ShouldRefreshSurroundDiscovery(
        PlaybackState::Preparing, PlaybackState::Running));
}
