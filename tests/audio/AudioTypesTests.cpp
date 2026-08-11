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

TEST(SurroundUi_PresentsRunningSevenPointOneRouting)
{
    EngineStatus status;
    status.state = PlaybackState::Running;
    status.surroundFormat = VirtualSurroundFormat::SevenPointOne;
    status.clockHealth = ClockHealth::Active;

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_EQ(ui.formatText, std::wstring(L"7.1 detected"));
    EXPECT_EQ(ui.routeStateText, std::wstring(L"Routing"));
    EXPECT_EQ(ui.syncText, std::wstring(L"Aligned"));
    EXPECT_TRUE(ui.sideLevelEnabled);
}

TEST(SurroundUi_ExplainsDisabledSideLevelForFivePointOne)
{
    EngineStatus status;
    status.state = PlaybackState::Running;
    status.surroundFormat = VirtualSurroundFormat::FivePointOne;

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_TRUE(!ui.sideLevelEnabled);
    EXPECT_EQ(ui.sideLevelHint,
              std::wstring(L"Used when Windows is set to 7.1."));
}

TEST(SurroundUi_PresentsSettlingClockInPlainLanguage)
{
    EngineStatus status;
    status.state = PlaybackState::Preparing;
    status.surroundFormat = VirtualSurroundFormat::SevenPointOne;
    status.clockHealth = ClockHealth::Settling;

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_EQ(ui.syncText, std::wstring(L"Synchronizing outputs..."));
}

TEST(SurroundUi_PresentsStoppedRoutingAsReady)
{
    EngineStatus status;
    status.state = PlaybackState::Stopped;
    status.surroundFormat = VirtualSurroundFormat::SevenPointOne;

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_EQ(ui.routeStateText, std::wstring(L"Ready"));
    EXPECT_TRUE(ui.startEnabled);
    EXPECT_TRUE(!ui.stopEnabled);
    EXPECT_TRUE(ui.deviceSelectionEnabled);
}

TEST(SurroundUi_GuidesRecoveryWhenVirtualEndpointIsMissing)
{
    EngineStatus status;
    status.state = PlaybackState::Stopped;
    status.surroundFormat = VirtualSurroundFormat::Unsupported;
    status.virtualEndpointReady = false;

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_EQ(ui.formatText,
              std::wstring(L"Surround format unavailable"));
    EXPECT_EQ(ui.formatSeverity, UiSeverity::Warning);
    EXPECT_EQ(ui.routeStateText, std::wstring(L"Setup required"));
    EXPECT_EQ(ui.routeSeverity, UiSeverity::Warning);
    EXPECT_TRUE(!ui.startEnabled);
    EXPECT_TRUE(!ui.recoveryText.empty());
}

TEST(SurroundUi_PresentsFaultWithRecoveryGuidance)
{
    EngineStatus status;
    status.state = PlaybackState::Faulted;
    status.surroundFormat = VirtualSurroundFormat::SevenPointOne;
    status.lastFault.code = 1;
    status.lastFault.message = L"Chair output disconnected.";

    const SurroundUiState ui = BuildSurroundUiState(
        status, PlaybackMode::SystemAudio);

    EXPECT_EQ(ui.routeSeverity, UiSeverity::Fault);
    EXPECT_TRUE(!ui.recoveryText.empty());
    EXPECT_TRUE(!ui.stopEnabled);
    EXPECT_TRUE(ui.deviceSelectionEnabled);
}
