#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace soundstage::audio
{
    inline constexpr std::uint32_t MasterSampleRate = 48000;
    inline constexpr std::uint32_t MaximumDelayMs = 2000;
    inline constexpr int MaximumLevelPercent = 100;
    inline constexpr double MaximumCorrectionPpm = 500.0;

    enum class SpeakerRole { Front, Rear };
    enum class PlaybackMode { SystemAudio, TestSignals };
    enum class RearFillMode { Off, Duplicate, Ambient };
    enum class TestPattern { PairedClicks, AlternatingClicks, FrontTone, RearTone };
    enum class PlaybackState { Stopped, Preparing, Primed, Running, Stopping, Faulted };
    enum class ClockHealth { Settling, Active, Unavailable };

    struct StereoFrame { float left = 0.0f; float right = 0.0f; };
    struct RoleFrame { StereoFrame front{}; StereoFrame rear{}; };
    struct SurroundFrame
    {
        float frontLeft = 0.0f;
        float frontRight = 0.0f;
        float frontCenter = 0.0f;
        float lfe = 0.0f;
        float backLeft = 0.0f;
        float backRight = 0.0f;
        float sideLeft = 0.0f;
        float sideRight = 0.0f;
    };

    struct SurroundMixLevels
    {
        float back = 1.0f;
        float side = 1.0f;
    };

    struct EndpointRoute
    {
        SpeakerRole role = SpeakerRole::Front;
        std::wstring endpointId;
        std::uint32_t delayMs = 0;
        bool isClockReference = false;
        float gain = 1.0f;
    };

    struct RunConfiguration
    {
        PlaybackMode mode = PlaybackMode::TestSignals;
        RearFillMode rearFill = RearFillMode::Off;
        TestPattern pattern = TestPattern::PairedClicks;
        std::wstring virtualEndpointId;
        SpeakerRole clockReferenceRole = SpeakerRole::Rear;
        std::vector<EndpointRoute> routes;
    };

    struct ClockSnapshot
    {
        std::uint64_t devicePosition = 0;
        std::uint64_t deviceFrequency = 0;
        std::uint64_t qpc100ns = 0;
        std::uint32_t paddingFrames = 0;
        bool available = false;
    };

    struct EndpointTelemetry
    {
        SpeakerRole role = SpeakerRole::Front;
        bool prepared = false;
        bool running = false;
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        double bufferDurationMs = 0.0;
        std::uint32_t delayMs = 0;
        std::uint64_t underrunCount = 0;
        ClockSnapshot clock{};
        std::uint32_t faultCode = 0;
    };

    struct EngineFault
    {
        std::uint32_t code = 0;
        SpeakerRole role = SpeakerRole::Front;
        std::wstring message;
    };

    struct EngineStatus
    {
        PlaybackState state = PlaybackState::Stopped;
        ClockHealth clockHealth = ClockHealth::Settling;
        double relativePpm = 0.0;
        double correctionPpm = 0.0;
        std::array<EndpointTelemetry, 2> endpoints{};
        bool virtualEndpointReady = false;
        std::uint64_t captureOverflowCount = 0;
        std::uint64_t captureUnderrunCount = 0;
        EngineFault lastFault{};
    };

    [[nodiscard]] constexpr std::uint32_t ClampDelayMs(const int value) noexcept
    {
        return value < 0 ? 0u :
               value > static_cast<int>(MaximumDelayMs) ? MaximumDelayMs :
               static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] constexpr std::uint64_t MillisecondsToFrames(
        const std::uint32_t milliseconds) noexcept
    {
        return static_cast<std::uint64_t>(milliseconds) * MasterSampleRate / 1000;
    }

    [[nodiscard]] constexpr int ClampLevelPercent(
        const int value) noexcept
    {
        return value < 0 ? 0 :
               value > MaximumLevelPercent ? MaximumLevelPercent :
               value;
    }
}
