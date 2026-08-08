#pragma once

#include "AudioTypes.h"
#include "EndpointConverter.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string>

namespace soundstage::audio
{
    inline constexpr std::uint32_t UnsupportedFormatCode = 0x2001u;
    inline constexpr std::uint32_t WorkerExceptionCode = 0x2002u;

    enum class SessionPhase
    {
        WorkerStart,
        ActivateDevice,
        DiscoverFormat,
        AllocateBuffers,
        InitializeClient,
        Prime,
        FirstRender,
        StableRender,
        Stop
    };

    struct BackendBuffer
    {
        std::span<std::byte> bytes;
        std::uint32_t frames = 0;
    };

    enum class BackendWaitResult { BufferReady, Timeout };

    struct BackendResult
    {
        bool ok = true;
        std::uint32_t faultCode = 0;
    };

    class IWasapiBackend
    {
    public:
        virtual ~IWasapiBackend() = default;
        virtual BackendResult ActivateDevice(
            const std::wstring& endpointId,
            std::stop_token stopToken) = 0;
        virtual BackendResult DiscoverFormat() = 0;
        virtual EndpointMixFormat MixFormat() const noexcept = 0;
        virtual std::uint32_t BufferFrames() const noexcept = 0;
        virtual double BufferDurationMs() const noexcept = 0;
        virtual BackendResult InitializeSharedMode() = 0;
        virtual BackendResult PrimeSilence() = 0;
        virtual BackendResult StartAt(
            std::uint64_t qpcTarget100ns) = 0;
        virtual BackendWaitResult WaitForRender(
            std::chrono::milliseconds timeout) = 0;
        virtual BackendResult BeginRender(BackendBuffer& buffer) = 0;
        virtual BackendResult EndRender(
            std::uint32_t frames, bool silent) = 0;
        virtual ClockSnapshot ReadClock() noexcept = 0;
        virtual void Stop() noexcept = 0;
    };

    class WindowsWasapiBackend final : public IWasapiBackend
    {
    public:
        WindowsWasapiBackend();
        ~WindowsWasapiBackend() override;
        BackendResult ActivateDevice(
            const std::wstring& endpointId,
            std::stop_token stopToken) override;
        BackendResult DiscoverFormat() override;
        EndpointMixFormat MixFormat() const noexcept override;
        std::uint32_t BufferFrames() const noexcept override;
        double BufferDurationMs() const noexcept override;
        BackendResult InitializeSharedMode() override;
        BackendResult PrimeSilence() override;
        BackendResult StartAt(
            std::uint64_t qpcTarget100ns) override;
        BackendWaitResult WaitForRender(
            std::chrono::milliseconds timeout) override;
        BackendResult BeginRender(BackendBuffer& buffer) override;
        BackendResult EndRender(
            std::uint32_t frames, bool silent) override;
        ClockSnapshot ReadClock() noexcept override;
        void Stop() noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
