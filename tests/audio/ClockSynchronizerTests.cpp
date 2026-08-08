#include "../TestHarness.h"
#include "../../src/audio/ClockSynchronizer.h"

#include <cmath>

using namespace soundstage::audio;

namespace
{
    ClockSnapshot Snapshot(const double seconds, const double driftPpm)
    {
        ClockSnapshot value;
        value.deviceFrequency = MasterSampleRate;
        value.devicePosition = static_cast<std::uint64_t>(
            seconds * MasterSampleRate * (1.0 + driftPpm / 1'000'000.0));
        value.qpc100ns = static_cast<std::uint64_t>(seconds * 10'000'000.0);
        value.available = true;
        return value;
    }

    SyncEstimate Simulate(const double driftPpm, const int samples)
    {
        ClockSynchronizer sync;
        sync.Reset();
        for (int sample = 0; sample <= samples; ++sample)
        {
            const double seconds = sample * 0.1;
            sync.Observe(Snapshot(seconds, 0.0),
                         Snapshot(seconds, driftPpm));
        }
        return sync.Current();
    }
}

TEST(ClockSync_ConvergesAndStaysBoundedForThirtyMinutes)
{
    const SyncEstimate estimate = Simulate(120.0, 18000);
    EXPECT_EQ(estimate.health, ClockHealth::Active);
    EXPECT_NEAR(estimate.relativePpm, 120.0, 2.0);
    EXPECT_NEAR(estimate.correctionPpm, -120.0, 2.0);
    EXPECT_TRUE(std::abs(estimate.correctionPpm) <= MaximumCorrectionPpm);
}

TEST(ClockSync_ConvergesForNegativeDrift)
{
    const SyncEstimate estimate = Simulate(-90.0, 600);
    EXPECT_EQ(estimate.health, ClockHealth::Active);
    EXPECT_NEAR(estimate.relativePpm, -90.0, 2.0);
    EXPECT_NEAR(estimate.correctionPpm, 90.0, 2.0);
}

TEST(ClockSync_RejectsImplausibleOutlier)
{
    ClockSynchronizer sync;
    sync.Reset();
    for (int sample = 0; sample <= 100; ++sample)
    {
        const double seconds = sample * 0.1;
        sync.Observe(Snapshot(seconds, 0.0), Snapshot(seconds, 100.0));
    }
    const SyncEstimate before = sync.Current();
    sync.Observe(Snapshot(10.1, 0.0), Snapshot(10.1, 6000.0));
    const SyncEstimate after = sync.Current();
    EXPECT_NEAR(after.relativePpm, before.relativePpm, 1e-9);
    EXPECT_EQ(after.acceptedSamples, before.acceptedSamples);
}

TEST(ClockSync_UnavailableClockDisablesCorrection)
{
    ClockSynchronizer sync;
    sync.Reset();
    for (int sample = 0; sample <= 50; ++sample)
    {
        const double seconds = sample * 0.1;
        sync.Observe(Snapshot(seconds, 0.0), Snapshot(seconds, 75.0));
    }
    const double diagnostic = sync.Current().relativePpm;
    sync.MarkClockUnavailable();
    const SyncEstimate estimate = sync.Current();
    EXPECT_EQ(estimate.health, ClockHealth::Unavailable);
    EXPECT_NEAR(estimate.correctionPpm, 0.0, 1e-12);
    EXPECT_NEAR(estimate.relativePpm, diagnostic, 1e-12);
}

TEST(ClockSync_ManualDelayPreservesRateAndClearsPhase)
{
    ClockSynchronizer sync;
    sync.Reset();
    for (int sample = 0; sample <= 100; ++sample)
    {
        const double seconds = sample * 0.1;
        sync.Observe(Snapshot(seconds, 0.0), Snapshot(seconds, 100.0));
    }
    const double relativePpm = sync.Current().relativePpm;
    EXPECT_TRUE(std::abs(sync.Current().phaseErrorFrames) > 0.0);
    sync.NotifyManualDelayChanged();
    const SyncEstimate estimate = sync.Current();
    EXPECT_NEAR(estimate.relativePpm, relativePpm, 1e-12);
    EXPECT_NEAR(estimate.phaseErrorFrames, 0.0, 1e-12);
    EXPECT_EQ(estimate.health, ClockHealth::Settling);
    EXPECT_NEAR(estimate.correctionPpm, 0.0, 1e-12);
}

TEST(ClockSync_RequiresThreeSecondsToActivate)
{
    const SyncEstimate estimate = Simulate(100.0, 29);
    EXPECT_EQ(estimate.health, ClockHealth::Settling);
    EXPECT_NEAR(estimate.correctionPpm, 0.0, 1e-12);
}
