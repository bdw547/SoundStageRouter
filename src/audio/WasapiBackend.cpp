#include "WasapiBackend.h"

#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace soundstage::audio
{
    namespace
    {
        [[nodiscard]] BackendResult ResultFrom(const HRESULT result) noexcept
        {
            return {
                SUCCEEDED(result),
                SUCCEEDED(result) ? 0u : static_cast<std::uint32_t>(result)
            };
        }

        [[nodiscard]] EndpointMixFormat ParseMixFormat(
            const WAVEFORMATEX& format) noexcept
        {
            if (format.nSamplesPerSec == 0 || format.nChannels == 0)
            {
                return {};
            }
            const bool extensible =
                format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                format.cbSize >=
                    sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
            const GUID subformat = extensible
                ? reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat
                : GUID_NULL;
            const bool isFloat =
                format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
                (extensible &&
                 IsEqualGUID(subformat,
                             KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
            const bool isPcm =
                format.wFormatTag == WAVE_FORMAT_PCM ||
                (extensible &&
                 IsEqualGUID(subformat, KSDATAFORMAT_SUBTYPE_PCM));

            SampleEncoding encoding = SampleEncoding::Unsupported;
            if (isFloat && format.wBitsPerSample == 32)
            {
                encoding = SampleEncoding::Float32;
            }
            else if (isPcm)
            {
                switch (format.wBitsPerSample)
                {
                case 16: encoding = SampleEncoding::Pcm16; break;
                case 24: encoding = SampleEncoding::Pcm24; break;
                case 32: encoding = SampleEncoding::Pcm32; break;
                default: break;
                }
            }

            const EndpointMixFormat parsed{
                format.nSamplesPerSec,
                format.nChannels,
                encoding,
                format.nBlockAlign
            };
            return EndpointConverter(parsed).IsSupported()
                ? parsed : EndpointMixFormat{};
        }

        [[nodiscard]] std::uint64_t QpcNow100ns() noexcept
        {
            LARGE_INTEGER counter{};
            LARGE_INTEGER frequency{};
            QueryPerformanceCounter(&counter);
            QueryPerformanceFrequency(&frequency);
            return frequency.QuadPart <= 0 ? 0 :
                static_cast<std::uint64_t>(
                    static_cast<long double>(counter.QuadPart) *
                    10'000'000.0L /
                    static_cast<long double>(frequency.QuadPart));
        }
    }

    struct WindowsWasapiBackend::Impl
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> audioClient;
        ComPtr<IAudioRenderClient> renderClient;
        ComPtr<IAudioClock> audioClock;
        WAVEFORMATEX* waveFormat = nullptr;
        EndpointMixFormat mixFormat{};
        HANDLE renderEvent = nullptr;
        std::uint32_t bufferFrames = 0;
        std::uint32_t pendingFrames = 0;
        double bufferDurationMs = 0.0;
        bool started = false;
        bool stopped = false;
    };

    WindowsWasapiBackend::WindowsWasapiBackend()
        : impl_(std::make_unique<Impl>())
    {
    }

    WindowsWasapiBackend::~WindowsWasapiBackend()
    {
        Stop();
    }

    BackendResult WindowsWasapiBackend::ActivateDevice(
        const std::wstring& endpointId,
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
            return ResultFrom(result);
        }
        result = impl_->enumerator->GetDevice(
            endpointId.c_str(), impl_->device.GetAddressOf());
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        result = impl_->device->Activate(
            __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
            reinterpret_cast<void**>(
                impl_->audioClient.GetAddressOf()));
        return ResultFrom(result);
    }

    BackendResult WindowsWasapiBackend::DiscoverFormat()
    {
        const HRESULT result =
            impl_->audioClient->GetMixFormat(&impl_->waveFormat);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        impl_->mixFormat = ParseMixFormat(*impl_->waveFormat);
        if (!EndpointConverter(impl_->mixFormat).IsSupported())
        {
            return {false, UnsupportedFormatCode};
        }
        return {};
    }

    EndpointMixFormat WindowsWasapiBackend::MixFormat() const noexcept
    {
        return impl_->mixFormat;
    }

    std::uint32_t WindowsWasapiBackend::BufferFrames() const noexcept
    {
        return impl_->bufferFrames;
    }

    double WindowsWasapiBackend::BufferDurationMs() const noexcept
    {
        return impl_->bufferDurationMs;
    }

    BackendResult WindowsWasapiBackend::InitializeSharedMode()
    {
        impl_->renderEvent = CreateEventW(
            nullptr, FALSE, FALSE, nullptr);
        if (impl_->renderEvent == nullptr)
        {
            return {false, GetLastError()};
        }
        HRESULT result = impl_->audioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                AUDCLNT_STREAMFLAGS_NOPERSIST,
            0, 0, impl_->waveFormat, nullptr);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        result = impl_->audioClient->SetEventHandle(impl_->renderEvent);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        result = impl_->audioClient->GetBufferSize(&impl_->bufferFrames);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        result = impl_->audioClient->GetService(
            __uuidof(IAudioRenderClient),
            reinterpret_cast<void**>(
                impl_->renderClient.GetAddressOf()));
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        impl_->audioClient->GetService(
            __uuidof(IAudioClock),
            reinterpret_cast<void**>(
                impl_->audioClock.GetAddressOf()));
        impl_->bufferDurationMs =
            static_cast<double>(impl_->bufferFrames) * 1000.0 /
            impl_->mixFormat.sampleRate;
        return {};
    }

    BackendResult WindowsWasapiBackend::PrimeSilence()
    {
        BYTE* bytes = nullptr;
        HRESULT result = impl_->renderClient->GetBuffer(
            impl_->bufferFrames, &bytes);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        result = impl_->renderClient->ReleaseBuffer(
            impl_->bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        return ResultFrom(result);
    }

    BackendResult WindowsWasapiBackend::StartAt(
        const std::uint64_t qpcTarget100ns)
    {
        const std::uint64_t now = QpcNow100ns();
        if (qpcTarget100ns > now)
        {
            HANDLE timer = CreateWaitableTimerW(
                nullptr, TRUE, nullptr);
            if (timer == nullptr)
            {
                return {false, GetLastError()};
            }
            const std::uint64_t delay = qpcTarget100ns - now;
            LARGE_INTEGER due{};
            due.QuadPart = -static_cast<LONGLONG>(
                std::min<std::uint64_t>(
                    delay,
                    static_cast<std::uint64_t>(
                        std::numeric_limits<LONGLONG>::max())));
            if (SetWaitableTimer(
                    timer, &due, 0, nullptr, nullptr, FALSE) == FALSE)
            {
                const std::uint32_t code = GetLastError();
                CloseHandle(timer);
                return {false, code};
            }
            WaitForSingleObject(timer, INFINITE);
            CloseHandle(timer);
        }
        const HRESULT result = impl_->audioClient->Start();
        impl_->started = SUCCEEDED(result);
        return ResultFrom(result);
    }

    BackendWaitResult WindowsWasapiBackend::WaitForRender(
        const std::chrono::milliseconds timeout)
    {
        const auto count = std::clamp<long long>(
            timeout.count(), 0, MAXDWORD - 1);
        return WaitForSingleObject(
                   impl_->renderEvent, static_cast<DWORD>(count)) ==
                WAIT_OBJECT_0
            ? BackendWaitResult::BufferReady
            : BackendWaitResult::Timeout;
    }

    BackendResult WindowsWasapiBackend::BeginRender(
        BackendBuffer& buffer)
    {
        UINT32 padding = 0;
        HRESULT result =
            impl_->audioClient->GetCurrentPadding(&padding);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        const UINT32 available = padding >= impl_->bufferFrames
            ? 0 : impl_->bufferFrames - padding;
        if (available == 0)
        {
            buffer = {};
            return {};
        }
        BYTE* bytes = nullptr;
        result = impl_->renderClient->GetBuffer(available, &bytes);
        if (FAILED(result))
        {
            return ResultFrom(result);
        }
        impl_->pendingFrames = available;
        buffer.frames = available;
        buffer.bytes = std::span(
            reinterpret_cast<std::byte*>(bytes),
            static_cast<std::size_t>(available) *
                impl_->mixFormat.blockAlign);
        return {};
    }

    BackendResult WindowsWasapiBackend::EndRender(
        const std::uint32_t frames,
        const bool silent)
    {
        if (frames == 0)
        {
            return {};
        }
        const HRESULT result = impl_->renderClient->ReleaseBuffer(
            frames, silent ? AUDCLNT_BUFFERFLAGS_SILENT : 0);
        impl_->pendingFrames = 0;
        return ResultFrom(result);
    }

    ClockSnapshot WindowsWasapiBackend::ReadClock() noexcept
    {
        ClockSnapshot snapshot;
        UINT32 padding = 0;
        if (impl_->audioClient &&
            SUCCEEDED(impl_->audioClient->GetCurrentPadding(&padding)))
        {
            snapshot.paddingFrames = padding;
        }
        if (!impl_->audioClock)
        {
            return snapshot;
        }
        UINT64 frequency = 0;
        UINT64 position = 0;
        UINT64 qpc100ns = 0;
        if (FAILED(impl_->audioClock->GetFrequency(&frequency)) ||
            FAILED(impl_->audioClock->GetPosition(
                &position, &qpc100ns)))
        {
            return snapshot;
        }
        snapshot.devicePosition = position;
        snapshot.deviceFrequency = frequency;
        snapshot.qpc100ns = qpc100ns;
        snapshot.available = frequency != 0;
        return snapshot;
    }

    void WindowsWasapiBackend::Stop() noexcept
    {
        if (!impl_ || impl_->stopped)
        {
            return;
        }
        impl_->stopped = true;
        if (impl_->audioClient && impl_->started)
        {
            impl_->audioClient->Stop();
        }
        impl_->audioClock.Reset();
        impl_->renderClient.Reset();
        impl_->audioClient.Reset();
        impl_->device.Reset();
        impl_->enumerator.Reset();
        if (impl_->waveFormat != nullptr)
        {
            CoTaskMemFree(impl_->waveFormat);
            impl_->waveFormat = nullptr;
        }
        if (impl_->renderEvent != nullptr)
        {
            CloseHandle(impl_->renderEvent);
            impl_->renderEvent = nullptr;
        }
    }
}
