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
    struct FakeCoordinatorCaptureState
    {
        std::atomic<unsigned> mixCalls{0};
        std::atomic<unsigned> startCalls{0};
        SurroundMixLevels lastMixLevels{};
    };

    class FakeCoordinatorCapture final : public ILoopbackCapture
    {
    public:
        explicit FakeCoordinatorCapture(FakeCoordinatorCaptureState& state)
            : state_(state) {}

        SessionResult Prepare(MasterFrameRingBuffer&, RearFillMode,
                              std::stop_token) override
        {
            return SessionResult::Success();
        }
        SessionResult Start() override
        {
            ++state_.startCalls;
            return SessionResult::Success();
        }
        void SetSurroundMixLevels(SurroundMixLevels levels) noexcept override
        {
            state_.lastMixLevels = levels;
            ++state_.mixCalls;
        }
        CaptureTelemetry Snapshot() noexcept override { return {}; }
        void Stop() noexcept override {}

    private:
        FakeCoordinatorCaptureState& state_;
    };

    class FakeCoordinatorCaptureFactory final : public ILoopbackCaptureFactory
    {
    public:
        std::unique_ptr<ILoopbackCapture> Create() override
        {
            return std::make_unique<FakeCoordinatorCapture>(state);
        }

        FakeCoordinatorCaptureState state;
    };

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
    EXPECT_TRUE(
        coordinator.Status()->state == PlaybackState::Preparing ||
        coordinator.Status()->state == PlaybackState::Running);
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

TEST(Coordinator_SerializesLiveSurroundMixWithoutRestartingCapture)
{
    auto outputs = std::make_unique<FakeEndpointSessionFactory>();
    auto captures = std::make_unique<FakeCoordinatorCaptureFactory>();
    FakeCoordinatorCaptureFactory* observed = captures.get();
    AudioEngineCoordinator coordinator(
        std::move(outputs), std::move(captures));
    RunConfiguration configuration = ValidRunConfiguration();
    configuration.mode = PlaybackMode::SystemAudio;
    configuration.virtualEndpointId = L"fake-virtual";
    configuration.surroundMix = {0.4f, 0.75f};

    coordinator.PostStart(std::move(configuration));
    EXPECT_TRUE(WaitFor([&] {
        return coordinator.Status()->state == PlaybackState::Running &&
               observed->state.mixCalls.load() == 1;
    }));
    EXPECT_NEAR(observed->state.lastMixLevels.back, 0.4, 1e-6);
    EXPECT_NEAR(observed->state.lastMixLevels.side, 0.75, 1e-6);

    coordinator.PostSurroundMixLevels({0.2f, 1.0f});

    EXPECT_TRUE(WaitFor([&] {
        return observed->state.mixCalls.load() == 2;
    }));
    EXPECT_NEAR(observed->state.lastMixLevels.back, 0.2, 1e-6);
    EXPECT_NEAR(observed->state.lastMixLevels.side, 1.0, 1e-6);
    EXPECT_EQ(observed->state.startCalls.load(), 1u);
}
