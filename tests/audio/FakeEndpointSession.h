#pragma once

#include "../../src/audio/EndpointSession.h"

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace test_audio
{
    using namespace soundstage::audio;

    struct FakeSessionState
    {
        SessionResult prepareResult = SessionResult::Success();
        SessionResult primeResult = SessionResult::Success();
        SessionResult armResult = SessionResult::Success();
        EndpointTelemetry telemetry{};
        std::atomic<unsigned> prepareCalls{0};
        std::atomic<unsigned> primeCalls{0};
        std::atomic<unsigned> armCalls{0};
        std::atomic<unsigned> stopCalls{0};
        std::atomic<unsigned> delayCalls{0};
        std::atomic<unsigned> correctionCalls{0};
        std::atomic<bool> waitForCancellation{false};
        std::stop_source* cancelAfterPrepare = nullptr;
        std::stop_source* cancelAfterPrime = nullptr;
        std::uint64_t armedStart = 0;
        std::uint32_t lastDelayMs = 0;
        double lastCorrectionPpm = 0.0;
    };

    class FakeEndpointSession final : public IEndpointSession
    {
    public:
        explicit FakeEndpointSession(FakeSessionState& state) : state_(state) {}

        SessionResult Prepare(
            const EndpointRoute&, TestPattern, PlaybackMode,
            MasterFrameRingBuffer*,
            const std::stop_token stopToken) override
        {
            ++state_.prepareCalls;
            while (state_.waitForCancellation.load() &&
                   !stopToken.stop_requested())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (stopToken.stop_requested())
            {
                return SessionResult::Failure(900, state_.telemetry.role,
                                              L"preparation cancelled");
            }
            if (state_.cancelAfterPrepare)
            {
                state_.cancelAfterPrepare->request_stop();
            }
            return state_.prepareResult;
        }

        SessionResult Prime() override
        {
            ++state_.primeCalls;
            if (state_.cancelAfterPrime)
            {
                state_.cancelAfterPrime->request_stop();
            }
            return state_.primeResult;
        }

        SessionResult ArmStart(const std::uint64_t startQpc100ns) override
        {
            ++state_.armCalls;
            state_.armedStart = startQpc100ns;
            return state_.armResult;
        }

        void SetDelayMs(const std::uint32_t value) noexcept override
        {
            ++state_.delayCalls;
            state_.lastDelayMs = value;
        }

        void SetCorrectionPpm(const double value) noexcept override
        {
            ++state_.correctionCalls;
            state_.lastCorrectionPpm = value;
        }

        EndpointTelemetry Snapshot() noexcept override
        {
            return state_.telemetry;
        }

        void Stop() noexcept override
        {
            ++state_.stopCalls;
        }

    private:
        FakeSessionState& state_;
    };

    class FakeEndpointSessionFactory final : public IEndpointSessionFactory
    {
    public:
        FakeEndpointSessionFactory()
        {
            states_[0].telemetry.role = SpeakerRole::Front;
            states_[1].telemetry.role = SpeakerRole::Rear;
        }

        std::unique_ptr<IEndpointSession> Create(const SpeakerRole role) override
        {
            return std::make_unique<FakeEndpointSession>(Session(role));
        }

        FakeSessionState& Session(const SpeakerRole role)
        {
            return states_[role == SpeakerRole::Front ? 0 : 1];
        }

    private:
        std::array<FakeSessionState, 2> states_{};
    };

    inline RunConfiguration ValidRunConfiguration()
    {
        RunConfiguration configuration;
        configuration.routes = {
            {SpeakerRole::Front, L"fake-front", 10, false},
            {SpeakerRole::Rear, L"fake-rear", 20, true}
        };
        return configuration;
    }
}
