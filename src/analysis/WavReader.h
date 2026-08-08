#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace soundstage::analysis
{
    struct WavRecording
    {
        std::uint32_t sampleRate = 0;
        std::vector<float> monoSamples;
    };

    class WavReadError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    [[nodiscard]] WavRecording ReadWavFile(
        const std::filesystem::path& path);
}
