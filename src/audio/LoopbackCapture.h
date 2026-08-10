#pragma once

#include "AudioTypes.h"
#include "EndpointSession.h"
#include "MasterFrameRingBuffer.h"
#include "VirtualSurroundContract.h"
#include "WasapiBackend.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>

namespace soundstage::audio
{
    inline constexpr wchar_t VirtualEndpointName[] =
        L"SoundStage Router Surround";
    inline constexpr wchar_t VirtualDriverInterfaceName[] =
        L"SoundStage Router Virtual Audio (WDM)";
    inline constexpr std::uint32_t VirtualEndpointMissingCode = 0x3001u;
    inline constexpr std::uint32_t VirtualEndpointDuplicateCode = 0x3002u;
    inline constexpr std::uint32_t VirtualEndpointFormatCode = 0x3003u;
    inline constexpr std::uint32_t CaptureWorkerExceptionCode = 0x3004u;

    struct CaptureFormat
    {
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        std::uint16_t bitsPerSample = 0;
        std::uint16_t blockAlign = 0;
        std::uint32_t channelMask = 0;
        bool floatingPoint = false;
    };

    [[nodiscard]] bool IsVirtualCaptureFormat(
        const CaptureFormat& format) noexcept;

    struct CapturePacket
    {
        std::span<const float> samples;
        std::uint32_t frames = 0;
        bool silent = false;
        float masterGain = 1.0f;
    };

    class ILoopbackCaptureBackend
    {
    public:
        virtual ~ILoopbackCaptureBackend() = default;
        virtual BackendResult DiscoverVirtualEndpoint(
            std::stop_token stopToken) = 0;
        virtual CaptureFormat Format() const noexcept = 0;
        virtual BackendResult InitializeSharedLoopback() = 0;
        virtual BackendResult Start() = 0;
        virtual BackendWaitResult WaitForCapture(
            std::chrono::milliseconds timeout) = 0;
        virtual BackendResult GetPacket(CapturePacket& packet) = 0;
        virtual BackendResult ReleasePacket(std::uint32_t frames) = 0;
        virtual void Stop() noexcept = 0;
    };

    struct CaptureTelemetry
    {
        bool prepared = false;
        bool running = false;
        std::uint32_t faultCode = 0;
        std::uint64_t packetCount = 0;
        std::uint64_t silentFrameCount = 0;
        VirtualSurroundFormat surroundFormat =
            VirtualSurroundFormat::Unsupported;
    };

    class ILoopbackCapture
    {
    public:
        virtual ~ILoopbackCapture() = default;
        virtual SessionResult Prepare(
            MasterFrameRingBuffer& ring, RearFillMode rearFill,
            std::stop_token stopToken) = 0;
        virtual SessionResult Start() = 0;
        virtual void SetSurroundMixLevels(
            SurroundMixLevels) noexcept {}
        virtual CaptureTelemetry Snapshot() noexcept = 0;
        virtual void Stop() noexcept = 0;
    };

    class ILoopbackCaptureFactory
    {
    public:
        virtual ~ILoopbackCaptureFactory() = default;
        virtual std::unique_ptr<ILoopbackCapture> Create() = 0;
    };

    class WindowsLoopbackCaptureBackend final
        : public ILoopbackCaptureBackend
    {
    public:
        WindowsLoopbackCaptureBackend();
        ~WindowsLoopbackCaptureBackend() override;
        BackendResult DiscoverVirtualEndpoint(
            std::stop_token stopToken) override;
        CaptureFormat Format() const noexcept override;
        BackendResult InitializeSharedLoopback() override;
        BackendResult Start() override;
        BackendWaitResult WaitForCapture(
            std::chrono::milliseconds timeout) override;
        BackendResult GetPacket(CapturePacket& packet) override;
        BackendResult ReleasePacket(std::uint32_t frames) override;
        void Stop() noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class WasapiLoopbackCapture final : public ILoopbackCapture
    {
    public:
        WasapiLoopbackCapture();
        explicit WasapiLoopbackCapture(
            std::unique_ptr<ILoopbackCaptureBackend> backend);
        ~WasapiLoopbackCapture() override;
        SessionResult Prepare(
            MasterFrameRingBuffer& ring, RearFillMode rearFill,
            std::stop_token stopToken) override;
        SessionResult Start() override;
        void SetSurroundMixLevels(
            SurroundMixLevels levels) noexcept override;
        CaptureTelemetry Snapshot() noexcept override;
        void Stop() noexcept override;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    class WasapiLoopbackCaptureFactory final
        : public ILoopbackCaptureFactory
    {
    public:
        std::unique_ptr<ILoopbackCapture> Create() override;
    };
}
