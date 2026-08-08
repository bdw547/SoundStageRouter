#include "../TestHarness.h"
#include "../../src/audio/ClockSynchronizer.h"
#include "../../src/audio/WasapiEndpointSession.h"

#include <audioclient.h>

using namespace soundstage::audio;

TEST(WasapiFault_DeviceInvalidationIsTerminal)
{
    const EngineFault front = ClassifyWasapiFailure(
        AUDCLNT_E_DEVICE_INVALIDATED, SpeakerRole::Front);
    const EngineFault rear = ClassifyWasapiFailure(
        AUDCLNT_E_DEVICE_INVALIDATED, SpeakerRole::Rear);
    EXPECT_EQ(front.role, SpeakerRole::Front);
    EXPECT_EQ(rear.role, SpeakerRole::Rear);
    EXPECT_TRUE(front.code != 0);
    EXPECT_EQ(FormatFault(front), L"Front output disconnected");
    EXPECT_EQ(FormatFault(rear), L"Rear output disconnected");
}

TEST(WasapiFault_ResourceInvalidationHasNamedMessage)
{
    const EngineFault fault = ClassifyWasapiFailure(
        AUDCLNT_E_RESOURCES_INVALIDATED, SpeakerRole::Rear);
    EXPECT_EQ(
        FormatFault(fault),
        L"Endpoint resources invalidated");
}

TEST(WasapiFault_MissingClockIsDegradedWithoutRenderFault)
{
    ClockSynchronizer synchronizer;
    synchronizer.Reset();
    synchronizer.MarkClockUnavailable();
    EXPECT_EQ(
        synchronizer.Current().health,
        ClockHealth::Unavailable);
    EXPECT_NEAR(
        synchronizer.Current().correctionPpm, 0.0, 1e-12);
    const ClockSnapshot missingClock;
    EXPECT_TRUE(!missingClock.available);
}

TEST(WasapiFault_FormatsKnownEngineFaults)
{
    EXPECT_EQ(
        FormatFault({UnsupportedFormatCode, SpeakerRole::Front, L""}),
        L"Unsupported endpoint format");
    EXPECT_EQ(
        FormatFault({RepeatedUnderrunCode, SpeakerRole::Rear, L""}),
        L"Render deadline repeatedly missed");
    EXPECT_EQ(
        FormatFault({ClockUnavailableCode, SpeakerRole::Rear, L""}),
        L"Endpoint clock unavailable");
}
