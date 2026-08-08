#include "WasapiEndpointSession.h"

#include <objbase.h>
#include <audioclient.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>

namespace soundstage::audio
{
    EngineFault ClassifyWasapiFailure(
        const HRESULT result,
        const SpeakerRole role) noexcept
    {
        return {
            FAILED(result)
                ? static_cast<std::uint32_t>(result) : 0u,
            role,
            {}
        };
    }

    std::wstring FormatFault(const EngineFault& fault)
    {
        const std::uint32_t deviceInvalidated =
            static_cast<std::uint32_t>(
                AUDCLNT_E_DEVICE_INVALIDATED);
        const std::uint32_t resourcesInvalidated =
            static_cast<std::uint32_t>(
                AUDCLNT_E_RESOURCES_INVALIDATED);
        if (fault.code == deviceInvalidated)
        {
            return fault.role == SpeakerRole::Front
                ? L"Front output disconnected"
                : L"Rear output disconnected";
        }
        if (fault.code == resourcesInvalidated)
        {
            return L"Endpoint resources invalidated";
        }
        switch (fault.code)
        {
        case UnsupportedFormatCode:
            return L"Unsupported endpoint format";
        case RepeatedUnderrunCode:
            return L"Render deadline repeatedly missed";
        case ClockUnavailableCode:
            return L"Endpoint clock unavailable";
        default:
            break;
        }
        if (!fault.message.empty())
        {
            return fault.message;
        }
        std::wostringstream message;
        message << L"Audio failure 0x"
                << std::hex << fault.code;
        return message.str();
    }

    namespace
    {
        struct BackendFailure
        {
            std::uint32_t code;
        };

        [[nodiscard]] SessionResult ResultFor(
            const std::uint32_t code,
            const SpeakerRole role)
        {
            return code == 0
                ? SessionResult::Success()
                : SessionResult::Failure(
                    code, role, L"Endpoint session failed");
        }
    }

    struct WasapiEndpointSession::Impl
    {
        Impl(const SpeakerRole valueRole,
             std::unique_ptr<IWasapiBackend> valueBackend,
             const PhaseHook valueHook,
             void* valueHookContext)
            : role(valueRole),
              backend(std::move(valueBackend)),
              hook(valueHook),
              hookContext(valueHookContext)
        {
            cached.role = role;
        }

        void InvokeHook(const SessionPhase phase)
        {
            if (hook != nullptr)
            {
                hook(phase, hookContext);
            }
        }

        void Require(const BackendResult result)
        {
            if (!result.ok)
            {
                throw BackendFailure{result.faultCode};
            }
        }

        void Publish(const EndpointTelemetry& value) noexcept
        {
            queue.Push(value);
        }

        void SetFault(const std::uint32_t code) noexcept
        {
            if (code == 0)
            {
                return;
            }
            terminalFault.store(code, std::memory_order_release);
            EndpointTelemetry value;
            value.role = role;
            value.faultCode = code;
            Publish(value);
        }

        void SignalCompletion() noexcept
        {
            std::lock_guard lock(mutex);
            prepareDone = true;
            primeDone = true;
            workerDone = true;
            condition.notify_all();
        }

        void Worker(const std::stop_token workerToken) noexcept
        {
            HRESULT comResult = CoInitializeEx(
                nullptr, COINIT_MULTITHREADED);
            const bool uninitialize =
                SUCCEEDED(comResult);
            std::uint32_t workerFault = 0;
            try
            {
                if (FAILED(comResult))
                {
                    throw BackendFailure{
                        static_cast<std::uint32_t>(comResult)};
                }
                InvokeHook(SessionPhase::WorkerStart);
                InvokeHook(SessionPhase::ActivateDevice);
                Require(backend->ActivateDevice(
                    route.endpointId, preparationToken));
                InvokeHook(SessionPhase::DiscoverFormat);
                Require(backend->DiscoverFormat());
                InvokeHook(SessionPhase::InitializeClient);
                Require(backend->InitializeSharedMode());
                InvokeHook(SessionPhase::AllocateBuffers);
                const EndpointMixFormat mixFormat =
                    backend->MixFormat();
                const std::uint32_t bufferFrames =
                    backend->BufferFrames();
                if (!pipeline.Reset({
                        role, pattern, route.delayMs,
                        mixFormat, bufferFrames}))
                {
                    throw BackendFailure{UnsupportedFormatCode};
                }

                EndpointTelemetry prepared;
                prepared.role = role;
                prepared.prepared = true;
                prepared.sampleRate = mixFormat.sampleRate;
                prepared.channels = mixFormat.channels;
                prepared.bufferDurationMs =
                    backend->BufferDurationMs();
                prepared.delayMs = std::min(
                    route.delayMs, MaximumDelayMs);
                Publish(prepared);
                {
                    std::lock_guard lock(mutex);
                    prepareDone = true;
                    condition.notify_all();
                }

                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&] {
                        return primeRequested ||
                               workerToken.stop_requested() ||
                               stopRequested.load(
                                   std::memory_order_acquire);
                    });
                }
                if (workerToken.stop_requested() ||
                    stopRequested.load(std::memory_order_acquire))
                {
                    throw BackendFailure{
                        static_cast<std::uint32_t>(E_ABORT)};
                }
                InvokeHook(SessionPhase::Prime);
                Require(backend->PrimeSilence());
                {
                    std::lock_guard lock(mutex);
                    primeDone = true;
                    condition.notify_all();
                }

                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&] {
                        return armRequested ||
                               workerToken.stop_requested() ||
                               stopRequested.load(
                                   std::memory_order_acquire);
                    });
                }
                if (workerToken.stop_requested() ||
                    stopRequested.load(std::memory_order_acquire))
                {
                    throw BackendFailure{
                        static_cast<std::uint32_t>(E_ABORT)};
                }
                InvokeHook(SessionPhase::FirstRender);
                Require(backend->StartAt(startQpc100ns));

                EndpointTelemetry telemetry = prepared;
                telemetry.running = true;
                Publish(telemetry);
                const std::uint32_t totalFadeFrames =
                    std::max(1u, mixFormat.sampleRate / 100);
                std::uint32_t fadeFramesRemaining =
                    totalFadeFrames;
                bool fading = false;
                bool runningOnce = false;
                bool stablePhaseEntered = false;
                std::uint64_t underrunCount = 0;
                const auto waitDuration =
                    std::chrono::milliseconds(std::max(
                        1, static_cast<int>(
                            backend->BufferDurationMs() + 0.5)));

                while (true)
                {
                    if (stopRequested.load(
                            std::memory_order_acquire) &&
                        !fading)
                    {
                        fading = true;
                        fadeFramesRemaining =
                            totalFadeFrames;
                    }
                    const BackendWaitResult wait =
                        backend->WaitForRender(waitDuration);
                    if (wait == BackendWaitResult::Timeout)
                    {
                        if (fading)
                        {
                            break;
                        }
                        continue;
                    }
                    if (runningOnce && !stablePhaseEntered)
                    {
                        InvokeHook(SessionPhase::StableRender);
                        stablePhaseEntered = true;
                    }

                    const ClockSnapshot before =
                        backend->ReadClock();
                    BackendBuffer buffer;
                    Require(backend->BeginRender(buffer));
                    if (buffer.frames == 0)
                    {
                        continue;
                    }
                    const bool underrun =
                        runningOnce &&
                        before.paddingFrames == 0;
                    if (underrun)
                    {
                        ++underrunCount;
                    }

                    float startGain = 1.0f;
                    float endGain = 1.0f;
                    if (fading)
                    {
                        startGain = static_cast<float>(
                            fadeFramesRemaining) /
                            totalFadeFrames;
                        const std::uint32_t rendered =
                            std::min(buffer.frames,
                                     fadeFramesRemaining);
                        fadeFramesRemaining -= rendered;
                        endGain = static_cast<float>(
                            fadeFramesRemaining) /
                            totalFadeFrames;
                    }
                    const bool rendered = pipeline.Render(
                        buffer.bytes, buffer.frames,
                        startGain, endGain);
                    Require(backend->EndRender(
                        buffer.frames,
                        underrun || !rendered));

                    telemetry.clock = backend->ReadClock();
                    telemetry.running = true;
                    telemetry.delayMs =
                        pipeline.CurrentDelayMs();
                    telemetry.underrunCount =
                        underrunCount;
                    telemetry.faultCode = 0;
                    Publish(telemetry);
                    runningOnce = true;
                    if (fading &&
                        fadeFramesRemaining == 0)
                    {
                        break;
                    }
                }
            }
            catch (const InjectedSessionFailure& failure)
            {
                workerFault = failure.faultCode;
            }
            catch (const BackendFailure& failure)
            {
                workerFault = failure.code;
            }
            catch (...)
            {
                workerFault = WorkerExceptionCode;
            }

            try
            {
                InvokeHook(SessionPhase::Stop);
            }
            catch (const InjectedSessionFailure& failure)
            {
                if (workerFault == 0 ||
                    workerFault ==
                        static_cast<std::uint32_t>(E_ABORT))
                {
                    workerFault = failure.faultCode;
                }
            }
            catch (...)
            {
                if (workerFault == 0 ||
                    workerFault ==
                        static_cast<std::uint32_t>(E_ABORT))
                {
                    workerFault = WorkerExceptionCode;
                }
            }
            backend->Stop();
            if (workerFault != 0)
            {
                SetFault(workerFault);
            }
            SignalCompletion();
            if (uninitialize)
            {
                CoUninitialize();
            }
        }

        SpeakerRole role;
        std::unique_ptr<IWasapiBackend> backend;
        PhaseHook hook = nullptr;
        void* hookContext = nullptr;
        EndpointPipeline pipeline;
        TelemetryQueue queue;
        EndpointTelemetry cached{};
        std::mutex mutex;
        std::condition_variable condition;
        EndpointRoute route{};
        TestPattern pattern = TestPattern::PairedClicks;
        std::stop_token preparationToken;
        std::jthread worker;
        bool prepareDone = false;
        bool primeRequested = false;
        bool primeDone = false;
        bool armRequested = false;
        bool workerDone = false;
        std::uint64_t startQpc100ns = 0;
        std::atomic<bool> stopRequested{false};
        std::atomic<std::uint32_t> terminalFault{0};
    };

    WasapiEndpointSession::WasapiEndpointSession(
        const SpeakerRole role)
        : WasapiEndpointSession(
            role, std::make_unique<WindowsWasapiBackend>(),
            nullptr, nullptr)
    {
    }

    WasapiEndpointSession::WasapiEndpointSession(
        const SpeakerRole role,
        std::unique_ptr<IWasapiBackend> backend,
        const PhaseHook hook,
        void* hookContext)
        : impl_(std::make_unique<Impl>(
            role, std::move(backend), hook, hookContext))
    {
    }

    WasapiEndpointSession::~WasapiEndpointSession()
    {
        Stop();
    }

    SessionResult WasapiEndpointSession::Prepare(
        const EndpointRoute& route,
        const TestPattern pattern,
        const std::stop_token stopToken)
    {
        impl_->route = route;
        impl_->pattern = pattern;
        impl_->preparationToken = stopToken;
        impl_->stopRequested.store(
            false, std::memory_order_release);
        impl_->worker = std::jthread(
            [this](const std::stop_token token) {
                impl_->Worker(token);
            });

        std::unique_lock lock(impl_->mutex);
        impl_->condition.wait(lock, [&] {
            return impl_->prepareDone ||
                   impl_->workerDone;
        });
        return ResultFor(
            impl_->terminalFault.load(
                std::memory_order_acquire),
            impl_->role);
    }

    SessionResult WasapiEndpointSession::Prime()
    {
        {
            std::lock_guard lock(impl_->mutex);
            if (impl_->workerDone)
            {
                return ResultFor(
                    impl_->terminalFault.load(
                        std::memory_order_acquire),
                    impl_->role);
            }
            impl_->primeRequested = true;
        }
        impl_->condition.notify_all();
        std::unique_lock lock(impl_->mutex);
        impl_->condition.wait(lock, [&] {
            return impl_->primeDone ||
                   impl_->workerDone;
        });
        return ResultFor(
            impl_->terminalFault.load(
                std::memory_order_acquire),
            impl_->role);
    }

    SessionResult WasapiEndpointSession::ArmStart(
        const std::uint64_t startQpc100ns)
    {
        std::lock_guard lock(impl_->mutex);
        const std::uint32_t fault =
            impl_->terminalFault.load(
                std::memory_order_acquire);
        if (impl_->workerDone || fault != 0)
        {
            return ResultFor(fault, impl_->role);
        }
        impl_->startQpc100ns = startQpc100ns;
        impl_->armRequested = true;
        impl_->condition.notify_all();
        return SessionResult::Success();
    }

    void WasapiEndpointSession::SetDelayMs(
        const std::uint32_t delayMs) noexcept
    {
        impl_->pipeline.SetDelayMs(delayMs);
    }

    void WasapiEndpointSession::SetCorrectionPpm(
        const double ppm) noexcept
    {
        impl_->pipeline.SetCorrectionPpm(ppm);
    }

    EndpointTelemetry WasapiEndpointSession::Snapshot() noexcept
    {
        EndpointTelemetry value;
        while (impl_->queue.Pop(value))
        {
            impl_->cached = value;
        }
        const std::uint32_t fault =
            impl_->terminalFault.load(
                std::memory_order_acquire);
        if (fault != 0)
        {
            impl_->cached.role = impl_->role;
            impl_->cached.running = false;
            impl_->cached.faultCode = fault;
        }
        return impl_->cached;
    }

    void WasapiEndpointSession::Stop() noexcept
    {
        if (!impl_ || !impl_->worker.joinable())
        {
            return;
        }
        impl_->stopRequested.store(
            true, std::memory_order_release);
        impl_->worker.request_stop();
        impl_->condition.notify_all();
        impl_->worker.join();
    }

    std::unique_ptr<IEndpointSession>
    WasapiEndpointSessionFactory::Create(
        const SpeakerRole role)
    {
        return std::make_unique<WasapiEndpointSession>(role);
    }
}
