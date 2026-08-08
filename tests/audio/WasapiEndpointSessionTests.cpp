#include "../TestHarness.h"
#include "../../src/audio/WasapiEndpointSession.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

using namespace soundstage::audio;

namespace
{
    struct FakeBackendState
    {
        std::atomic<unsigned> stopCalls{0};
        std::atomic<unsigned> beginCalls{0};
        std::atomic<unsigned> startCalls{0};
        std::atomic<bool> blockActivation{false};
        std::atomic<bool> activationEntered{false};
        std::atomic<unsigned> failBeginAt{0};
        std::uint32_t beginFaultCode = 0;
        std::array<std::byte, 64 * 8> buffer{};
        std::uint64_t position = 0;
    };

    class FakeWasapiBackend final : public IWasapiBackend
    {
    public:
        explicit FakeWasapiBackend(std::shared_ptr<FakeBackendState> state)
            : state_(std::move(state))
        {
        }

        BackendResult ActivateDevice(
            const std::wstring&, const std::stop_token stopToken) override
        {
            state_->activationEntered = true;
            while (state_->blockActivation && !stopToken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return stopToken.stop_requested()
                ? BackendResult{false, 0xE100u}
                : BackendResult{};
        }
        BackendResult DiscoverFormat() override { return {}; }
        EndpointMixFormat MixFormat() const noexcept override
        {
            return {48000, 2, SampleEncoding::Float32, 8};
        }
        std::uint32_t BufferFrames() const noexcept override { return 64; }
        double BufferDurationMs() const noexcept override { return 4.0; }
        BackendResult InitializeSharedMode() override { return {}; }
        BackendResult PrimeSilence() override { return {}; }
        BackendResult StartAt(std::uint64_t) override
        {
            ++state_->startCalls;
            return {};
        }
        BackendWaitResult WaitForRender(
            std::chrono::milliseconds) override
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return BackendWaitResult::BufferReady;
        }
        BackendResult BeginRender(BackendBuffer& buffer) override
        {
            const unsigned call = ++state_->beginCalls;
            if (state_->failBeginAt != 0 &&
                call >= state_->failBeginAt)
            {
                return {false, state_->beginFaultCode};
            }
            buffer.bytes = state_->buffer;
            buffer.frames = 64;
            return {};
        }
        BackendResult EndRender(std::uint32_t, bool) override { return {}; }
        ClockSnapshot ReadClock() noexcept override
        {
            state_->position += 64;
            return {state_->position, 48000, state_->position * 100, 1, true};
        }
        void Stop() noexcept override { ++state_->stopCalls; }

    private:
        std::shared_ptr<FakeBackendState> state_;
    };

    struct FaultHookContext
    {
        SessionPhase phase;
        std::uint32_t faultCode;
    };

    void ThrowAtPhase(const SessionPhase phase, void* rawContext)
    {
        const auto& context =
            *static_cast<FaultHookContext*>(rawContext);
        if (phase == context.phase)
        {
            throw InjectedSessionFailure{context.faultCode};
        }
    }

    template <typename Predicate>
    bool WaitFor(Predicate predicate)
    {
        const auto deadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(1);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return predicate();
    }

    SessionResult PrepareAndAdvance(WasapiEndpointSession& session,
                                    const SessionPhase phase)
    {
        const EndpointRoute route{
            SpeakerRole::Front, L"fake-front", 0, false};
        SessionResult result = session.Prepare(
            route, TestPattern::PairedClicks, {});
        if (!result.ok || phase <= SessionPhase::InitializeClient)
        {
            return result;
        }
        result = session.Prime();
        if (!result.ok || phase == SessionPhase::Prime)
        {
            return result;
        }
        result = session.ArmStart(0);
        if (!result.ok)
        {
            return result;
        }
        if (phase == SessionPhase::FirstRender ||
            phase == SessionPhase::StableRender)
        {
            WaitFor([&] { return session.Snapshot().faultCode != 0; });
            const EndpointTelemetry telemetry = session.Snapshot();
            return telemetry.faultCode == 0
                ? SessionResult::Success()
                : SessionResult::Failure(
                    telemetry.faultCode, telemetry.role, L"injected");
        }
        session.Stop();
        const EndpointTelemetry telemetry = session.Snapshot();
        return telemetry.faultCode == 0
            ? SessionResult::Success()
            : SessionResult::Failure(
                telemetry.faultCode, telemetry.role, L"injected");
    }
}

TEST(WasapiSession_ConvertsEveryInjectedPhaseExceptionToFault)
{
    for (const SessionPhase phase : {
             SessionPhase::WorkerStart,
             SessionPhase::ActivateDevice,
             SessionPhase::DiscoverFormat,
             SessionPhase::AllocateBuffers,
             SessionPhase::InitializeClient,
             SessionPhase::Prime,
             SessionPhase::FirstRender,
             SessionPhase::StableRender,
             SessionPhase::Stop})
    {
        auto state = std::make_shared<FakeBackendState>();
        FaultHookContext context{
            phase, 0xE000u + static_cast<std::uint32_t>(phase)};
        WasapiEndpointSession session(
            SpeakerRole::Front,
            std::make_unique<FakeWasapiBackend>(state),
            &ThrowAtPhase, &context);
        const SessionResult result = PrepareAndAdvance(session, phase);
        if (result.ok)
        {
            throw std::runtime_error(
                "phase did not fault: " +
                std::to_string(static_cast<unsigned>(phase)));
        }
        EXPECT_EQ(result.fault.code, context.faultCode);
        session.Stop();
        EXPECT_EQ(state->stopCalls.load(), 1u);
    }
}

TEST(WasapiSession_StopDuringActivationCancelsAndJoins)
{
    auto state = std::make_shared<FakeBackendState>();
    state->blockActivation = true;
    WasapiEndpointSession session(
        SpeakerRole::Front,
        std::make_unique<FakeWasapiBackend>(state), nullptr, nullptr);
    std::stop_source cancellation;
    SessionResult result;
    std::thread prepare([&] {
        result = session.Prepare(
            {SpeakerRole::Front, L"fake-front", 0, false},
            TestPattern::PairedClicks, cancellation.get_token());
    });
    EXPECT_TRUE(WaitFor([&] { return state->activationEntered.load(); }));
    cancellation.request_stop();
    prepare.join();
    session.Stop();
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(state->stopCalls.load(), 1u);
}

TEST(WasapiSession_CancellationWhileAwaitingPrimeStopsBeforeStart)
{
    auto state = std::make_shared<FakeBackendState>();
    WasapiEndpointSession session(
        SpeakerRole::Front,
        std::make_unique<FakeWasapiBackend>(state), nullptr, nullptr);
    std::stop_source cancellation;

    EXPECT_TRUE(session.Prepare(
        {SpeakerRole::Front, L"fake-front", 0, false},
        TestPattern::PairedClicks, cancellation.get_token()).ok);
    cancellation.request_stop();
    EXPECT_TRUE(WaitFor([&] {
        return session.Snapshot().faultCode != 0;
    }));

    EXPECT_TRUE(!session.Prime().ok);
    session.Stop();
    EXPECT_EQ(state->startCalls.load(), 0u);
}

TEST(WasapiSession_PublishesStableRenderFailure)
{
    auto state = std::make_shared<FakeBackendState>();
    state->failBeginAt = 2;
    state->beginFaultCode = 0x8888u;
    WasapiEndpointSession session(
        SpeakerRole::Rear,
        std::make_unique<FakeWasapiBackend>(state), nullptr, nullptr);
    EXPECT_TRUE(session.Prepare(
        {SpeakerRole::Rear, L"fake-rear", 10, true},
        TestPattern::RearTone, {}).ok);
    EXPECT_TRUE(session.Prime().ok);
    EXPECT_TRUE(session.ArmStart(0).ok);
    EXPECT_TRUE(WaitFor([&] {
        return session.Snapshot().faultCode == 0x8888u;
    }));
    session.Stop();
    EXPECT_EQ(state->stopCalls.load(), 1u);
}

TEST(WasapiSession_ForwardsLiveControlsAndPublishesTelemetry)
{
    auto state = std::make_shared<FakeBackendState>();
    WasapiEndpointSession session(
        SpeakerRole::Rear,
        std::make_unique<FakeWasapiBackend>(state), nullptr, nullptr);
    EXPECT_TRUE(session.Prepare(
        {SpeakerRole::Rear, L"fake-rear", 10, true},
        TestPattern::RearTone, {}).ok);
    EXPECT_TRUE(session.Prime().ok);
    session.SetDelayMs(25);
    session.SetCorrectionPpm(100.0);
    EXPECT_TRUE(session.ArmStart(0).ok);
    EXPECT_TRUE(WaitFor([&] {
        const EndpointTelemetry value = session.Snapshot();
        return value.running && value.sampleRate == 48000;
    }));
    session.Stop();
    EXPECT_EQ(state->stopCalls.load(), 1u);
}
