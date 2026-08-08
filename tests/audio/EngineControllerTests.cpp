#include "../TestHarness.h"
#include "FakeEndpointSession.h"
#include "../../src/audio/EngineController.h"

using namespace soundstage::audio;
using namespace test_audio;

TEST(Engine_RejectsInvalidRouteConfigurations)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);

    RunConfiguration tooFew = ValidRunConfiguration();
    tooFew.routes.pop_back();
    EXPECT_TRUE(!engine.Start(tooFew, {}).ok);

    RunConfiguration tooMany = ValidRunConfiguration();
    tooMany.routes.push_back(
        {SpeakerRole::Front, L"third", 0, false});
    EXPECT_TRUE(!engine.Start(tooMany, {}).ok);

    RunConfiguration duplicate = ValidRunConfiguration();
    duplicate.routes[1].endpointId = duplicate.routes[0].endpointId;
    EXPECT_TRUE(!engine.Start(duplicate, {}).ok);

    RunConfiguration missingReference = ValidRunConfiguration();
    missingReference.routes[1].isClockReference = false;
    EXPECT_TRUE(!engine.Start(missingReference, {}).ok);
}

TEST(Engine_StartFailureTearsDownPreparedPeer)
{
    FakeEndpointSessionFactory factory;
    factory.Session(SpeakerRole::Rear).prepareResult =
        SessionResult::Failure(41, SpeakerRole::Rear, L"rear failed");
    EngineController engine(factory);
    const SessionResult result = engine.Start(ValidRunConfiguration(), {});
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(engine.Status().state, PlaybackState::Faulted);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).stopCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).stopCalls.load(), 1u);
    EXPECT_EQ(engine.Status().lastFault.code, 41u);
}

TEST(Engine_NormalLifecycleArmsBothAndStopsIdempotently)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);
    EXPECT_TRUE(engine.Start(ValidRunConfiguration(), {}).ok);
    EXPECT_EQ(engine.Status().state, PlaybackState::Running);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).prepareCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).primeCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).armCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).armedStart,
              factory.Session(SpeakerRole::Rear).armedStart);

    engine.Stop();
    engine.Stop();
    EXPECT_EQ(engine.Status().state, PlaybackState::Stopped);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).stopCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).stopCalls.load(), 1u);
}

TEST(Engine_CancellationAfterPrepareNeverPrimesOrArms)
{
    FakeEndpointSessionFactory factory;
    std::stop_source cancellation;
    factory.Session(SpeakerRole::Front).cancelAfterPrepare = &cancellation;
    EngineController engine(factory);

    const SessionResult result =
        engine.Start(ValidRunConfiguration(), cancellation.get_token());

    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(engine.Status().state, PlaybackState::Stopped);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).primeCalls.load(), 0u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).primeCalls.load(), 0u);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).armCalls.load(), 0u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).armCalls.load(), 0u);
}

TEST(Engine_CancellationAfterPrimeNeverArms)
{
    FakeEndpointSessionFactory factory;
    std::stop_source cancellation;
    factory.Session(SpeakerRole::Front).cancelAfterPrime = &cancellation;
    EngineController engine(factory);

    const SessionResult result =
        engine.Start(ValidRunConfiguration(), cancellation.get_token());

    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(engine.Status().state, PlaybackState::Stopped);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).primeCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).primeCalls.load(), 0u);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).armCalls.load(), 0u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).armCalls.load(), 0u);
}

TEST(Engine_DeviceFaultStopsBothSessions)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);
    EXPECT_TRUE(engine.Start(ValidRunConfiguration(), {}).ok);
    factory.Session(SpeakerRole::Rear).telemetry.faultCode = 77;
    engine.Tick(10'000'000);
    EXPECT_EQ(engine.Status().state, PlaybackState::Faulted);
    EXPECT_EQ(engine.Status().lastFault.code, 77u);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).stopCalls.load(), 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).stopCalls.load(), 1u);
}

TEST(Engine_ThreeUnderrunsWithinFiveSecondsFaultTheRun)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);
    EXPECT_TRUE(engine.Start(ValidRunConfiguration(), {}).ok);
    for (std::uint64_t count = 1; count <= 3; ++count)
    {
        factory.Session(SpeakerRole::Front).telemetry.underrunCount = count;
        engine.Tick(count * 10'000'000);
    }
    EXPECT_EQ(engine.Status().state, PlaybackState::Faulted);
    EXPECT_TRUE(engine.Status().lastFault.code != 0);
}

TEST(Engine_MissingClockLeavesPlaybackRunning)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);
    EXPECT_TRUE(engine.Start(ValidRunConfiguration(), {}).ok);
    engine.Tick(10'000'000);
    EXPECT_EQ(engine.Status().state, PlaybackState::Running);
    EXPECT_EQ(engine.Status().clockHealth, ClockHealth::Unavailable);
    EXPECT_NEAR(factory.Session(SpeakerRole::Front).lastCorrectionPpm, 0.0, 1e-12);
}

TEST(Engine_ForwardsClampedLiveDelay)
{
    FakeEndpointSessionFactory factory;
    EngineController engine(factory);
    EXPECT_TRUE(engine.Start(ValidRunConfiguration(), {}).ok);
    engine.SetDelayMs(SpeakerRole::Front, 9000);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).lastDelayMs, MaximumDelayMs);
}
