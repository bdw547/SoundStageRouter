#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace soundstage
{
    struct AudioEndpoint
    {
        std::wstring id;
        std::wstring name;
        std::wstring formatDescription;
        std::uint32_t channels = 0;
        std::uint32_t sampleRate = 0;
        std::uint16_t bitsPerSample = 0;
        std::uint32_t channelMask = 0;
        bool isDefault = false;
        bool isFloatingPoint = false;
        bool isVirtualEndpoint = false;
        bool virtualContractValid = false;
    };

    class AudioEndpointService
    {
    public:
        [[nodiscard]] static std::vector<AudioEndpoint> EnumerateRenderEndpoints();
    };
}
