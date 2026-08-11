#include "EngineController.h"

#include <windows.h>

#include <algorithm>
#include <utility>

namespace soundstage::audio
{
    namespace
    {
        constexpr std::uint64_t StartLeadTime100ns = 1'000'000;
        constexpr std::uint64_t UnderrunWindow100ns = 50'000'000;

        [[nodiscard]] std::uint64_t QpcNow100ns() noexcept
        {
            LARGE_INTEGER counter{};
            LARGE_INTEGER frequency{};
            QueryPerformanceCounter(&counter);
            QueryPerformanceFrequency(&frequency);
            if (frequency.QuadPart <= 0)
            {
                return 0;
            }
            return static_cast<std::uint64_t>(
                static_cast<long double>(counter.QuadPart) * 10'000'000.0L /
                static_cast<long double>(frequency.QuadPart));
        }
    }

    EngineController::EngineController(
        IEndpointSessionFactory& factory,
        ILoopbackCaptureFactory* const captureFactory)
        : factory_(factory), captureFactory_(captureFactory)
    {
    }

    SessionResult EngineController::Start(
        const RunConfiguration& configuration,
        const std::stop_token stopToken)
    {
        if (sessions_[0] || sessions_[1])
        {
            Stop();
        }
        status_ = {};
        synchronizer_.Reset();
        lastUnderrunCounts_ = {};
        underrunTimes_ = {};
        underrunTimeCount_ = 0;

        const SessionResult validation = Validate(configuration);
        if (!validation.ok)
        {
            return Fail(validation.fault);
        }

        status_.state = PlaybackState::Preparing;
        if (configuration.mode == PlaybackMode::SystemAudio)
        {
            try
            {
                masterFrames_ =
                    std::make_unique<MasterFrameRingBuffer>(
                        MasterSampleRate);
                capture_ = captureFactory_->Create();
            }
            catch (...)
            {
                return Fail({SessionCreationCode, SpeakerRole::Front,
                             L"Unable to create loopback capture"});
            }
            if (!capture_)
            {
                return Fail({SessionCreationCode, SpeakerRole::Front,
                             L"Unable to create loopback capture"});
            }
            capture_->SetSurroundMixLevels(configuration.surroundMix);
        }
        const auto cancelStart = [&]() {
            Stop();
            return SessionResult::Failure(
                static_cast<std::uint32_t>(E_ABORT),
                configuration.clockReferenceRole,
                L"Playback start cancelled");
        };
        for (const EndpointRoute& route : configuration.routes)
        {
            const std::size_t index = RoleIndex(route.role);
            routes_[index] = route;
            status_.endpoints[index].role = route.role;
            status_.endpoints[index].delayMs =
                std::min(route.delayMs, MaximumDelayMs);
            try
            {
                sessions_[index] = factory_.Create(route.role);
            }
            catch (...)
            {
                return Fail({SessionCreationCode, route.role,
                             L"Unable to create endpoint session"});
            }
            if (!sessions_[index])
            {
                return Fail({SessionCreationCode, route.role,
                             L"Unable to create endpoint session"});
            }
        }

        for (const EndpointRoute& route : configuration.routes)
        {
            const std::size_t index = RoleIndex(route.role);
            const SessionResult prepared = sessions_[index]->Prepare(
                route, configuration.pattern, configuration.mode,
                masterFrames_.get(), stopToken);
            if (!prepared.ok)
            {
                if (stopToken.stop_requested())
                {
                    Stop();
                    return prepared;
                }
                return Fail(prepared.fault);
            }
            status_.endpoints[index].prepared = true;
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
        }

        for (const EndpointRoute& route : configuration.routes)
        {
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
            const std::size_t index = RoleIndex(route.role);
            const SessionResult primed = sessions_[index]->Prime();
            if (!primed.ok)
            {
                return Fail(primed.fault);
            }
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
        }
        status_.state = PlaybackState::Primed;

        if (stopToken.stop_requested())
        {
            return cancelStart();
        }
        if (capture_)
        {
            SessionResult captured = capture_->Prepare(
                *masterFrames_, configuration.rearFill, stopToken);
            if (!captured.ok)
            {
                if (stopToken.stop_requested())
                {
                    Stop();
                    return captured;
                }
                return Fail(captured.fault);
            }
            status_.virtualEndpointReady = true;
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
            captured = capture_->Start();
            if (!captured.ok)
            {
                return Fail(captured.fault);
            }
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
        }
        const std::uint64_t startQpc100ns =
            QpcNow100ns() + StartLeadTime100ns;
        for (const EndpointRoute& route : configuration.routes)
        {
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
            const std::size_t index = RoleIndex(route.role);
            const SessionResult armed =
                sessions_[index]->ArmStart(startQpc100ns);
            if (!armed.ok)
            {
                return Fail(armed.fault);
            }
            status_.endpoints[index].running = true;
            if (stopToken.stop_requested())
            {
                return cancelStart();
            }
        }
        status_.state = PlaybackState::Running;
        return SessionResult::Success();
    }

    void EngineController::Stop() noexcept
    {
        const bool preserveFault = status_.state == PlaybackState::Faulted;
        if (!sessions_[0] && !sessions_[1] && !capture_)
        {
            if (!preserveFault)
            {
                status_.state = PlaybackState::Stopped;
            }
            return;
        }

        status_.state = PlaybackState::Stopping;
        if (capture_)
        {
            capture_->Stop();
            capture_.reset();
        }
        for (std::unique_ptr<IEndpointSession>& session : sessions_)
        {
            if (session)
            {
                session->Stop();
                session.reset();
            }
        }
        for (EndpointTelemetry& endpoint : status_.endpoints)
        {
            endpoint.prepared = false;
            endpoint.running = false;
        }
        status_.virtualEndpointReady = false;
        masterFrames_.reset();
        status_.state =
            preserveFault ? PlaybackState::Faulted : PlaybackState::Stopped;
    }

    void EngineController::SetDelayMs(const SpeakerRole role,
                                      const std::uint32_t delayMs) noexcept
    {
        const std::size_t index = RoleIndex(role);
        const std::uint32_t clamped = std::min(delayMs, MaximumDelayMs);
        routes_[index].delayMs = clamped;
        status_.endpoints[index].delayMs = clamped;
        if (sessions_[index])
        {
            sessions_[index]->SetDelayMs(clamped);
            synchronizer_.NotifyManualDelayChanged();
            status_.clockHealth = ClockHealth::Settling;
            status_.correctionPpm = 0.0;
        }
    }

    void EngineController::SetSurroundMixLevels(
        const SurroundMixLevels levels) noexcept
    {
        if (capture_)
        {
            capture_->SetSurroundMixLevels(levels);
        }
    }

    void EngineController::Tick(const std::uint64_t qpc100ns) noexcept
    {
        if (status_.state != PlaybackState::Running)
        {
            return;
        }
        if (capture_)
        {
            const CaptureTelemetry capture = capture_->Snapshot();
            status_.virtualEndpointReady = capture.prepared;
            status_.captureOverflowCount =
                masterFrames_->OverflowCount();
            status_.captureUnderrunCount =
                masterFrames_->UnderrunCount(0) +
                masterFrames_->UnderrunCount(1);
            if (capture.faultCode != 0)
            {
                Fail({capture.faultCode, SpeakerRole::Front,
                      L"Virtual endpoint capture stopped"});
                return;
            }
        }

        for (std::size_t index = 0; index < sessions_.size(); ++index)
        {
            if (!sessions_[index])
            {
                continue;
            }
            const EndpointTelemetry telemetry = sessions_[index]->Snapshot();
            status_.endpoints[index] = telemetry;
            if (telemetry.faultCode != 0)
            {
                Fail({telemetry.faultCode, telemetry.role, L""});
                return;
            }
            RecordUnderruns(index, telemetry.underrunCount, qpc100ns);
            if (status_.state == PlaybackState::Faulted)
            {
                return;
            }
        }

        const std::size_t referenceIndex =
            routes_[0].isClockReference ? 0 : 1;
        const std::size_t followerIndex = 1 - referenceIndex;
        const ClockSnapshot& reference =
            status_.endpoints[referenceIndex].clock;
        const ClockSnapshot& follower =
            status_.endpoints[followerIndex].clock;
        if (!reference.available || !follower.available)
        {
            synchronizer_.MarkClockUnavailable();
        }
        else
        {
            synchronizer_.Observe(reference, follower);
        }
        const SyncEstimate estimate = synchronizer_.Current();
        status_.clockHealth = estimate.health;
        status_.relativePpm = estimate.relativePpm;
        status_.correctionPpm = estimate.correctionPpm;
        sessions_[followerIndex]->SetCorrectionPpm(estimate.correctionPpm);
    }

    EngineStatus EngineController::Status() const
    {
        return status_;
    }

    std::size_t EngineController::RoleIndex(const SpeakerRole role) noexcept
    {
        return role == SpeakerRole::Front ? 0 : 1;
    }

    SessionResult EngineController::Validate(
        const RunConfiguration& configuration) const
    {
        if (configuration.routes.size() != 2)
        {
            return SessionResult::Failure(
                InvalidConfigurationCode, SpeakerRole::Front,
                L"Exactly two endpoint routes are required");
        }
        const EndpointRoute& first = configuration.routes[0];
        const EndpointRoute& second = configuration.routes[1];
        if (configuration.mode == PlaybackMode::SystemAudio &&
            (captureFactory_ == nullptr ||
             configuration.virtualEndpointId.empty() ||
             first.endpointId == configuration.virtualEndpointId ||
             second.endpointId == configuration.virtualEndpointId))
        {
            return SessionResult::Failure(
                InvalidConfigurationCode, SpeakerRole::Front,
                L"System routing requires a distinct virtual input");
        }
        if (first.endpointId.empty() || second.endpointId.empty() ||
            first.endpointId == second.endpointId ||
            first.role == second.role)
        {
            return SessionResult::Failure(
                InvalidConfigurationCode, SpeakerRole::Front,
                L"Endpoint routes must be distinct and non-empty");
        }
        const unsigned referenceCount =
            static_cast<unsigned>(first.isClockReference) +
            static_cast<unsigned>(second.isClockReference);
        const EndpointRoute& reference =
            first.isClockReference ? first : second;
        if (referenceCount != 1 ||
            reference.role != configuration.clockReferenceRole)
        {
            return SessionResult::Failure(
                InvalidConfigurationCode, configuration.clockReferenceRole,
                L"Exactly one matching clock reference is required");
        }
        return SessionResult::Success();
    }

    SessionResult EngineController::Fail(const EngineFault& fault) noexcept
    {
        if (capture_)
        {
            capture_->Stop();
            capture_.reset();
        }
        for (std::unique_ptr<IEndpointSession>& session : sessions_)
        {
            if (session)
            {
                session->Stop();
                session.reset();
            }
        }
        status_.virtualEndpointReady = false;
        masterFrames_.reset();
        for (EndpointTelemetry& endpoint : status_.endpoints)
        {
            endpoint.prepared = false;
            endpoint.running = false;
        }
        status_.lastFault = fault;
        status_.state = PlaybackState::Faulted;
        return {false, fault};
    }

    void EngineController::RecordUnderruns(
        const std::size_t endpointIndex,
        const std::uint64_t underrunCount,
        const std::uint64_t qpc100ns) noexcept
    {
        std::uint64_t newUnderruns =
            underrunCount > lastUnderrunCounts_[endpointIndex]
                ? underrunCount - lastUnderrunCounts_[endpointIndex]
                : 0;
        lastUnderrunCounts_[endpointIndex] = underrunCount;

        while (newUnderruns-- > 0)
        {
            if (underrunTimeCount_ < underrunTimes_.size())
            {
                underrunTimes_[underrunTimeCount_++] = qpc100ns;
            }
            else
            {
                underrunTimes_[0] = underrunTimes_[1];
                underrunTimes_[1] = underrunTimes_[2];
                underrunTimes_[2] = qpc100ns;
            }
            if (underrunTimeCount_ == underrunTimes_.size() &&
                underrunTimes_[2] - underrunTimes_[0] <=
                    UnderrunWindow100ns)
            {
                Fail({RepeatedUnderrunCode, routes_[endpointIndex].role,
                      L"Render deadline repeatedly missed"});
                return;
            }
        }
    }
}
