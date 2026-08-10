#include "LoopbackCapture.h"

#include "ChannelRouter.h"

#include <audioclient.h>
#include <endpointvolume.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <condition_variable>
#include <cmath>
#include <mutex>
#include <thread>

using Microsoft::WRL::ComPtr;

namespace soundstage::audio
{
    namespace
    {
        [[nodiscard]] BackendResult CaptureResult(
            const HRESULT result) noexcept
        {
            return {SUCCEEDED(result),
                    SUCCEEDED(result) ? 0u
                        : static_cast<std::uint32_t>(result)};
        }

        [[nodiscard]] bool IsFloatFormat(
            const WAVEFORMATEX& format) noexcept
        {
            if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
            {
                return true;
            }
            if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE ||
                format.cbSize <
                    sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
            {
                return false;
            }
            return IsEqualGUID(
                reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).
                    SubFormat,
                KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
        }

        [[nodiscard]] std::uint32_t ChannelMask(
            const WAVEFORMATEX& format) noexcept
        {
            return format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                   format.cbSize >=
                       sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)
                ? reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).
                      dwChannelMask
                : 0;
        }

        [[nodiscard]] const wchar_t* CaptureFailureMessage(
            const std::uint32_t code) noexcept
        {
            switch (code)
            {
            case VirtualEndpointMissingCode:
                return L"SoundStage Router 5.1 is not installed";
            case VirtualEndpointDuplicateCode:
                return L"Multiple SoundStage Router 5.1 endpoints found";
            case VirtualEndpointFormatCode:
                return L"SoundStage Router 5.1 has the wrong format";
            default:
                return L"Virtual endpoint capture failed";
            }
        }
    }

    bool IsVirtualCaptureFormat(const CaptureFormat& format) noexcept
    {
        return format.sampleRate == MasterSampleRate &&
               format.channels == 6 &&
               format.bitsPerSample == 32 &&
               format.blockAlign == 24 &&
               format.channelMask == VirtualChannelMask &&
               format.floatingPoint;
    }

    struct WindowsLoopbackCaptureBackend::Impl
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        ComPtr<IAudioEndpointVolume> endpointVolume;
        WAVEFORMATEX* waveFormat = nullptr;
        CaptureFormat format{};
        HANDLE eventHandle = nullptr;
        bool eventDriven = false;
        bool started = false;
        bool stopped = false;
        ULONGLONG lastVolumeReadMs = 0;
        float masterGain = 1.0f;
    };

    WindowsLoopbackCaptureBackend::WindowsLoopbackCaptureBackend()
        : impl_(std::make_unique<Impl>())
    {
    }

    WindowsLoopbackCaptureBackend::~WindowsLoopbackCaptureBackend()
    {
        Stop();
    }

    BackendResult WindowsLoopbackCaptureBackend::DiscoverVirtualEndpoint(
        const std::stop_token stopToken)
    {
        if (stopToken.stop_requested())
        {
            return {false, static_cast<std::uint32_t>(E_ABORT)};
        }
        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
            IID_PPV_ARGS(impl_->enumerator.GetAddressOf()));
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        ComPtr<IMMDeviceCollection> devices;
        result = impl_->enumerator->EnumAudioEndpoints(
            eRender, DEVICE_STATE_ACTIVE, devices.GetAddressOf());
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        UINT count = 0;
        result = devices->GetCount(&count);
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        unsigned matches = 0;
        for (UINT index = 0; index < count; ++index)
        {
            ComPtr<IMMDevice> candidate;
            if (FAILED(devices->Item(index, candidate.GetAddressOf())))
            {
                continue;
            }
            ComPtr<IPropertyStore> properties;
            if (FAILED(candidate->OpenPropertyStore(
                    STGM_READ, properties.GetAddressOf())))
            {
                continue;
            }
            PROPVARIANT name;
            PropVariantInit(&name);
            const HRESULT nameResult = properties->GetValue(
                PKEY_DeviceInterface_FriendlyName, &name);
            const bool matching =
                SUCCEEDED(nameResult) && name.vt == VT_LPWSTR &&
                name.pwszVal != nullptr &&
                (std::wstring_view(name.pwszVal) == VirtualEndpointName ||
                 std::wstring_view(name.pwszVal) ==
                     VirtualDriverInterfaceName);
            PropVariantClear(&name);
            if (matching)
            {
                ++matches;
                impl_->device = candidate;
            }
        }
        if (matches == 0)
        {
            return {false, VirtualEndpointMissingCode};
        }
        if (matches != 1)
        {
            return {false, VirtualEndpointDuplicateCode};
        }
        result = impl_->device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(impl_->client.GetAddressOf()));
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        result = impl_->device->Activate(
            __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(
                impl_->endpointVolume.GetAddressOf()));
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        result = impl_->client->GetMixFormat(&impl_->waveFormat);
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        const WAVEFORMATEX& wave = *impl_->waveFormat;
        impl_->format = {
            wave.nSamplesPerSec, wave.nChannels, wave.wBitsPerSample,
            wave.nBlockAlign, ChannelMask(wave), IsFloatFormat(wave)};
        return {};
    }

    CaptureFormat WindowsLoopbackCaptureBackend::Format() const noexcept
    {
        return impl_->format;
    }

    BackendResult
    WindowsLoopbackCaptureBackend::InitializeSharedLoopback()
    {
        if (!IsVirtualCaptureFormat(impl_->format))
        {
            return {false, VirtualEndpointFormatCode};
        }
        impl_->eventHandle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (impl_->eventHandle == nullptr)
        {
            return {false, GetLastError()};
        }
        HRESULT result = impl_->client->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK |
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST,
            0, 0, impl_->waveFormat, nullptr);
        if (SUCCEEDED(result))
        {
            result = impl_->client->SetEventHandle(impl_->eventHandle);
            impl_->eventDriven = SUCCEEDED(result);
        }
        if (FAILED(result))
        {
            impl_->client.Reset();
            result = impl_->device->Activate(
                __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                reinterpret_cast<void**>(impl_->client.GetAddressOf()));
            if (FAILED(result))
            {
                return CaptureResult(result);
            }
            result = impl_->client->Initialize(
                AUDCLNT_SHAREMODE_SHARED,
                AUDCLNT_STREAMFLAGS_LOOPBACK |
                    AUDCLNT_STREAMFLAGS_NOPERSIST,
                0, 0, impl_->waveFormat, nullptr);
            impl_->eventDriven = false;
        }
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        result = impl_->client->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(impl_->capture.GetAddressOf()));
        return CaptureResult(result);
    }

    BackendResult WindowsLoopbackCaptureBackend::Start()
    {
        const HRESULT result = impl_->client->Start();
        impl_->started = SUCCEEDED(result);
        return CaptureResult(result);
    }

    BackendWaitResult WindowsLoopbackCaptureBackend::WaitForCapture(
        const std::chrono::milliseconds timeout)
    {
        if (!impl_->eventDriven)
        {
            Sleep(static_cast<DWORD>(timeout.count()));
            return BackendWaitResult::BufferReady;
        }
        return WaitForSingleObject(
                   impl_->eventHandle,
                   static_cast<DWORD>(timeout.count())) == WAIT_OBJECT_0
            ? BackendWaitResult::BufferReady
            : BackendWaitResult::Timeout;
    }

    BackendResult WindowsLoopbackCaptureBackend::GetPacket(
        CapturePacket& packet)
    {
        UINT32 frames = 0;
        HRESULT result = impl_->capture->GetNextPacketSize(&frames);
        if (FAILED(result) || frames == 0)
        {
            packet = {};
            return CaptureResult(result);
        }
        BYTE* data = nullptr;
        DWORD flags = 0;
        result = impl_->capture->GetBuffer(
            &data, &frames, &flags, nullptr, nullptr);
        if (FAILED(result))
        {
            return CaptureResult(result);
        }
        packet.frames = frames;
        packet.silent =
            (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0;
        const ULONGLONG nowMs = GetTickCount64();
        if (nowMs - impl_->lastVolumeReadMs >= 100)
        {
            BOOL muted = FALSE;
            float volumeDb = 0.0f;
            if (SUCCEEDED(impl_->endpointVolume->GetMute(&muted)) &&
                (muted ||
                 SUCCEEDED(impl_->endpointVolume->GetMasterVolumeLevel(
                     &volumeDb))))
            {
                impl_->masterGain = muted
                    ? 0.0f
                    : std::pow(10.0f, volumeDb / 20.0f);
                impl_->lastVolumeReadMs = nowMs;
            }
        }
        packet.masterGain = impl_->masterGain;
        packet.samples = packet.silent
            ? std::span<const float>{}
            : std::span(
                  reinterpret_cast<const float*>(data),
                  static_cast<std::size_t>(frames) * 6);
        return {};
    }

    BackendResult WindowsLoopbackCaptureBackend::ReleasePacket(
        const std::uint32_t frames)
    {
        return CaptureResult(impl_->capture->ReleaseBuffer(frames));
    }

    void WindowsLoopbackCaptureBackend::Stop() noexcept
    {
        if (!impl_ || impl_->stopped)
        {
            return;
        }
        impl_->stopped = true;
        if (impl_->client && impl_->started)
        {
            impl_->client->Stop();
        }
        impl_->capture.Reset();
        impl_->endpointVolume.Reset();
        impl_->client.Reset();
        impl_->device.Reset();
        impl_->enumerator.Reset();
        if (impl_->waveFormat)
        {
            CoTaskMemFree(impl_->waveFormat);
            impl_->waveFormat = nullptr;
        }
        if (impl_->eventHandle)
        {
            CloseHandle(impl_->eventHandle);
            impl_->eventHandle = nullptr;
        }
    }

    struct WasapiLoopbackCapture::Impl
    {
        explicit Impl(std::unique_ptr<ILoopbackCaptureBackend> value)
            : backend(std::move(value))
        {
        }

        void Worker(const std::stop_token token) noexcept
        {
            const HRESULT com = CoInitializeEx(
                nullptr, COINIT_MULTITHREADED);
            const bool uninitialize = SUCCEEDED(com);
            try
            {
                if (FAILED(com))
                {
                    throw static_cast<std::uint32_t>(com);
                }
                BackendResult result =
                    backend->DiscoverVirtualEndpoint(preparationToken);
                if (!result.ok)
                {
                    throw result.faultCode;
                }
                if (!IsVirtualCaptureFormat(backend->Format()))
                {
                    throw VirtualEndpointFormatCode;
                }
                result = backend->InitializeSharedLoopback();
                if (!result.ok)
                {
                    throw result.faultCode;
                }
                {
                    std::lock_guard lock(mutex);
                    prepared.store(true, std::memory_order_release);
                    prepareDone = true;
                    condition.notify_all();
                }
                {
                    std::unique_lock lock(mutex);
                    condition.wait(lock, [&] {
                        return startRequested || token.stop_requested() ||
                               preparationToken.stop_requested();
                    });
                }
                if (token.stop_requested() ||
                    preparationToken.stop_requested())
                {
                    throw static_cast<std::uint32_t>(E_ABORT);
                }
                result = backend->Start();
                if (!result.ok)
                {
                    throw result.faultCode;
                }
                running.store(true, std::memory_order_release);
                startDone.store(true, std::memory_order_release);
                condition.notify_all();

                while (!token.stop_requested())
                {
                    if (backend->WaitForCapture(
                            std::chrono::milliseconds(10)) ==
                        BackendWaitResult::Timeout)
                    {
                        continue;
                    }
                    while (!token.stop_requested())
                    {
                        CapturePacket packet;
                        result = backend->GetPacket(packet);
                        if (!result.ok)
                        {
                            throw result.faultCode;
                        }
                        if (packet.frames == 0)
                        {
                            break;
                        }
                        for (std::uint32_t frame = 0;
                             frame < packet.frames; ++frame)
                        {
                            SurroundFrame input{};
                            if (!packet.silent)
                            {
                                const float* sample =
                                    packet.samples.data() +
                                    static_cast<std::size_t>(frame) * 6;
                                input = {sample[0], sample[1], sample[2],
                                         sample[3], sample[4], sample[5]};
                                input.frontLeft *= packet.masterGain;
                                input.frontRight *= packet.masterGain;
                                input.frontCenter *= packet.masterGain;
                                input.lfe *= packet.masterGain;
                                input.backLeft *= packet.masterGain;
                                input.backRight *= packet.masterGain;
                            }
                            static_cast<void>(ring->Push(
                                RouteSurroundFrame(input, rearFill)));
                        }
                        packetCount.fetch_add(1, std::memory_order_relaxed);
                        if (packet.silent)
                        {
                            silentFrameCount.fetch_add(
                                packet.frames, std::memory_order_relaxed);
                        }
                        result = backend->ReleasePacket(packet.frames);
                        if (!result.ok)
                        {
                            throw result.faultCode;
                        }
                    }
                }
            }
            catch (const std::uint32_t code)
            {
                if (code != static_cast<std::uint32_t>(E_ABORT))
                {
                    fault.store(code, std::memory_order_release);
                }
            }
            catch (...)
            {
                fault.store(
                    CaptureWorkerExceptionCode,
                    std::memory_order_release);
            }
            backend->Stop();
            running.store(false, std::memory_order_release);
            {
                std::lock_guard lock(mutex);
                prepareDone = true;
                workerDone = true;
                condition.notify_all();
            }
            if (uninitialize)
            {
                CoUninitialize();
            }
        }

        std::unique_ptr<ILoopbackCaptureBackend> backend;
        MasterFrameRingBuffer* ring = nullptr;
        RearFillMode rearFill = RearFillMode::Off;
        std::stop_token preparationToken;
        std::jthread worker;
        std::mutex mutex;
        std::condition_variable condition;
        std::atomic<bool> prepared{false};
        std::atomic<bool> running{false};
        std::atomic<std::uint64_t> packetCount{0};
        std::atomic<std::uint64_t> silentFrameCount{0};
        std::atomic<std::uint32_t> fault{0};
        std::atomic<bool> startDone{false};
        bool prepareDone = false;
        bool startRequested = false;
        bool workerDone = false;
    };

    WasapiLoopbackCapture::WasapiLoopbackCapture()
        : WasapiLoopbackCapture(
              std::make_unique<WindowsLoopbackCaptureBackend>())
    {
    }

    WasapiLoopbackCapture::WasapiLoopbackCapture(
        std::unique_ptr<ILoopbackCaptureBackend> backend)
        : impl_(std::make_unique<Impl>(std::move(backend)))
    {
    }

    WasapiLoopbackCapture::~WasapiLoopbackCapture()
    {
        Stop();
    }

    SessionResult WasapiLoopbackCapture::Prepare(
        MasterFrameRingBuffer& ring, const RearFillMode rearFill,
        const std::stop_token stopToken)
    {
        impl_->ring = &ring;
        impl_->rearFill = rearFill;
        impl_->preparationToken = stopToken;
        impl_->worker = std::jthread([this](const std::stop_token token) {
            impl_->Worker(token);
        });
        std::unique_lock lock(impl_->mutex);
        impl_->condition.wait(lock, [&] {
            return impl_->prepareDone || impl_->workerDone;
        });
        const std::uint32_t fault =
            impl_->fault.load(std::memory_order_acquire);
        return fault == 0 &&
               impl_->prepared.load(std::memory_order_acquire)
            ? SessionResult::Success()
            : SessionResult::Failure(
                  fault == 0 ? CaptureWorkerExceptionCode : fault,
                  SpeakerRole::Front,
                  CaptureFailureMessage(
                      fault == 0 ? CaptureWorkerExceptionCode : fault));
    }

    SessionResult WasapiLoopbackCapture::Start()
    {
        {
            std::lock_guard lock(impl_->mutex);
            impl_->startRequested = true;
        }
        impl_->condition.notify_all();
        std::unique_lock lock(impl_->mutex);
        impl_->condition.wait(lock, [&] {
            return impl_->startDone.load(std::memory_order_acquire) ||
                   impl_->workerDone;
        });
        const std::uint32_t fault =
            impl_->fault.load(std::memory_order_acquire);
        return impl_->startDone.load(std::memory_order_acquire)
            ? SessionResult::Success()
            : SessionResult::Failure(
                  fault == 0 ? CaptureWorkerExceptionCode : fault,
                  SpeakerRole::Front,
                  CaptureFailureMessage(
                      fault == 0 ? CaptureWorkerExceptionCode : fault));
    }

    CaptureTelemetry WasapiLoopbackCapture::Snapshot() noexcept
    {
        CaptureTelemetry result;
        result.prepared =
            impl_->prepared.load(std::memory_order_acquire);
        result.running =
            impl_->running.load(std::memory_order_acquire);
        result.packetCount =
            impl_->packetCount.load(std::memory_order_relaxed);
        result.silentFrameCount =
            impl_->silentFrameCount.load(std::memory_order_relaxed);
        result.faultCode = impl_->fault.load(std::memory_order_acquire);
        return result;
    }

    void WasapiLoopbackCapture::Stop() noexcept
    {
        if (!impl_ || !impl_->worker.joinable())
        {
            return;
        }
        impl_->worker.request_stop();
        impl_->condition.notify_all();
        impl_->worker.join();
    }

    std::unique_ptr<ILoopbackCapture>
    WasapiLoopbackCaptureFactory::Create()
    {
        return std::make_unique<WasapiLoopbackCapture>();
    }
}
