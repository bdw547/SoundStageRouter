#include "../TestHarness.h"
#include "FakeEndpointSession.h"
#include "../../src/audio/AudioEngineCoordinator.h"

#include <chrono>
#include <memory>
#include <thread>

using namespace soundstage::audio;
using namespace test_audio;

namespace
{
    template <typename Predicate>
    bool WaitFor(Predicate predicate,
                 const std::chrono::milliseconds timeout =
                     std::chrono::milliseconds(1000))
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    }
}

TEST(Coordinator_PostStartIsNonBlockingAndStopCancelsPreparation)
{
    auto factory = std::make_unique<FakeEndpointSessionFactory>();
    FakeEndpointSessionFactory* observed = factory.get();
    observed->Session(SpeakerRole::Front).waitForCancellation = true;

    AudioEngineCoordinator coordinator(std::move(factory));
    const auto before = std::chrono::steady_clock::now();
    coordinator.PostStart(ValidRunConfiguration());
    const auto elapsed = std::chrono::steady_clock::now() - before;
    EXPECT_TRUE(elapsed < std::chrono::milliseconds(10));
    EXPECT_TRUE(WaitFor([&] {
        return observed->Session(SpeakerRole::Front).prepareCalls.load() == 1;
    }));

    coordinator.PostStop();
    EXPECT_TRUE(WaitFor([&] {
        return observed->Session(SpeakerRole::Front).stopCalls.load() == 1 &&
               observed->Session(SpeakerRole::Rear).stopCalls.load() == 1;
    }));
}

TEST(Coordinator_SerializesStartDelayAndStopCommands)
{
    auto factory = std::make_unique<FakeEndpointSessionFactory>();
    FakeEndpointSessionFactory* observed = factory.get();
    AudioEngineCoordinator coordinator(std::move(factory));
    coordinator.PostStart(ValidRunConfiguration());
    EXPECT_TRUE(WaitFor([&] {
        return coordinator.Status()->state == PlaybackState::Running;
    }));
    coordinator.PostDelay(SpeakerRole::Rear, 123);
    EXPECT_TRUE(WaitFor([&] {
        return observed->Session(SpeakerRole::Rear).lastDelayMs == 123;
    }));
    coordinator.PostStop();
    EXPECT_TRUE(WaitFor([&] {
        return coordinator.Status()->state == PlaybackState::Stopped;
    }));
}
