#include "AudioEndpoints.h"

#include <audioclient.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <wrl/client.h>

#include <iomanip>
#include <sstream>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace
{
    void ThrowIfFailed(const HRESULT result, const char* operation)
    {
        if (FAILED(result))
        {
            std::ostringstream message;
            message << operation << " failed with HRESULT 0x" << std::hex
                    << static_cast<unsigned long>(result);
            throw std::runtime_error(message.str());
        }
    }

    std::wstring GetDeviceId(IMMDevice* device)
    {
        LPWSTR value = nullptr;
        ThrowIfFailed(device->GetId(&value), "IMMDevice::GetId");
        const std::wstring result(value == nullptr ? L"" : value);
        CoTaskMemFree(value);
        return result;
    }

    std::wstring GetPropertyString(IMMDevice* device, const PROPERTYKEY& key,
                                   const wchar_t* fallback)
    {
        ComPtr<IPropertyStore> properties;
        ThrowIfFailed(device->OpenPropertyStore(STGM_READ, properties.GetAddressOf()),
                      "IMMDevice::OpenPropertyStore");

        PROPVARIANT value;
        PropVariantInit(&value);
        ThrowIfFailed(properties->GetValue(key, &value),
                      "IPropertyStore::GetValue");

        std::wstring result = fallback;
        if (value.vt == VT_LPWSTR && value.pwszVal != nullptr)
        {
            result = value.pwszVal;
        }
        PropVariantClear(&value);
        return result;
    }

    bool IsFloatingPoint(const WAVEFORMATEX* format)
    {
        if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            return true;
        }

        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format);
            return IsEqualGUID(extensible->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) != FALSE;
        }

        return false;
    }

    DWORD GetChannelMask(const WAVEFORMATEX* format)
    {
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
            format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX))
        {
            return reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(format)->dwChannelMask;
        }
        return 0;
    }

    std::wstring DescribeFormat(const WAVEFORMATEX* format)
    {
        std::wostringstream description;
        description << format->nChannels << L" ch  |  "
                    << std::fixed << std::setprecision(1)
                    << static_cast<double>(format->nSamplesPerSec) / 1000.0
                    << L" kHz  |  " << format->wBitsPerSample << L"-bit "
                    << (IsFloatingPoint(format) ? L"float" : L"PCM");
        return description.str();
    }

    void PopulateFormat(IMMDevice* device, soundstage::AudioEndpoint& endpoint)
    {
        ComPtr<IAudioClient> audioClient;
        ThrowIfFailed(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                       reinterpret_cast<void**>(audioClient.GetAddressOf())),
                      "IMMDevice::Activate(IAudioClient)");

        WAVEFORMATEX* format = nullptr;
        ThrowIfFailed(audioClient->GetMixFormat(&format), "IAudioClient::GetMixFormat");
        endpoint.channels = format->nChannels;
        endpoint.sampleRate = format->nSamplesPerSec;
        endpoint.bitsPerSample = format->wBitsPerSample;
        endpoint.channelMask = GetChannelMask(format);
        endpoint.isFloatingPoint = IsFloatingPoint(format);
        endpoint.formatDescription = DescribeFormat(format);
        CoTaskMemFree(format);
    }
}

namespace soundstage
{
    std::vector<AudioEndpoint> AudioEndpointService::EnumerateRenderEndpoints()
    {
        ComPtr<IMMDeviceEnumerator> enumerator;
        ThrowIfFailed(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                       IID_PPV_ARGS(enumerator.GetAddressOf())),
                      "CoCreateInstance(MMDeviceEnumerator)");

        std::wstring defaultDeviceId;
        ComPtr<IMMDevice> defaultDevice;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                          defaultDevice.GetAddressOf())))
        {
            defaultDeviceId = GetDeviceId(defaultDevice.Get());
        }

        ComPtr<IMMDeviceCollection> collection;
        ThrowIfFailed(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                                     collection.GetAddressOf()),
                      "IMMDeviceEnumerator::EnumAudioEndpoints");

        UINT count = 0;
        ThrowIfFailed(collection->GetCount(&count), "IMMDeviceCollection::GetCount");

        std::vector<AudioEndpoint> endpoints;
        endpoints.reserve(count);

        for (UINT index = 0; index < count; ++index)
        {
            ComPtr<IMMDevice> device;
            ThrowIfFailed(collection->Item(index, device.GetAddressOf()),
                          "IMMDeviceCollection::Item");

            AudioEndpoint endpoint;
            endpoint.id = GetDeviceId(device.Get());
            endpoint.name = GetPropertyString(
                device.Get(), PKEY_Device_FriendlyName,
                L"Unnamed audio device");
            const std::wstring interfaceName = GetPropertyString(
                device.Get(), PKEY_DeviceInterface_FriendlyName, L"");
            endpoint.isVirtualEndpoint =
                interfaceName == L"SoundStage Router 5.1" ||
                interfaceName ==
                    L"SoundStage Router Virtual Audio (WDM)";
            endpoint.isDefault = endpoint.id == defaultDeviceId;
            PopulateFormat(device.Get(), endpoint);
            endpoint.virtualContractValid =
                endpoint.isVirtualEndpoint &&
                endpoint.channels == 6 &&
                endpoint.sampleRate == 48000 &&
                endpoint.bitsPerSample == 32 &&
                endpoint.channelMask == 0x3F &&
                endpoint.isFloatingPoint;
            endpoints.push_back(std::move(endpoint));
        }

        return endpoints;
    }
}
