#pragma once

#include "EndpointPipeline.h"
#include "EndpointSession.h"
#include "WasapiBackend.h"

#include <windows.h>

#include <cstdint>
#include <memory>

namespace soundstage::audio
{
    [[nodiscard]] EngineFault ClassifyWasapiFailure(
        HRESULT result, SpeakerRole role) noexcept;
    [[nodiscard]] std::wstring FormatFault(
        const EngineFault& fault);

    struct InjectedSessionFailure
    {
        std::uint32_t faultCode;
    };

    using PhaseHook = void (*)(SessionPhase phase, void* context);

    class WasapiEndpointSession final : public IEndpointSession
    {
    public:
        explicit WasapiEndpointSession(SpeakerRole role);
        WasapiEndpointSession(
            SpeakerRole role,
            std::unique_ptr<IWasapiBackend> backend,
            PhaseHook hook,
            void* hookContext);
        ~WasapiEndpointSession() override;

        SessionResult Prepare(
            const EndpointRoute&, TestPattern, PlaybackMode,
            MasterFrameRingBuffer*, std::stop_token) override;
        SessionResult Prepare(
            const EndpointRoute& route, TestPattern pattern,
            std::stop_token stopToken)
        {
            return Prepare(
                route, pattern, PlaybackMode::TestSignals,
                nullptr, stopToken);
        }
        SessionResult Prime() override;
        SessionResult ArmStart(std::uint64_t startQpc100ns) override;
        void SetDelayMs(std::uint32_t) noexcept override;
        void SetCorrectionPpm(double) noexcept override;
        EndpointTelemetry Snapshot() noexcept override;
        void Stop() noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class WasapiEndpointSessionFactory final
        : public IEndpointSessionFactory
    {
    public:
        std::unique_ptr<IEndpointSession> Create(
            SpeakerRole role) override;
    };
}
