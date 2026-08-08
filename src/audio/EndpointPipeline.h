#pragma once

#include "AdaptiveResampler.h"
#include "DelayLine.h"
#include "EndpointConverter.h"
#include "MasterFrameRingBuffer.h"
#include "TestPatternGenerator.h"

#include <atomic>
#include <cstdint>
#include <span>
#include <vector>

namespace soundstage::audio
{
    struct PipelineConfiguration
    {
        SpeakerRole role = SpeakerRole::Front;
        TestPattern pattern = TestPattern::PairedClicks;
        std::uint32_t delayMs = 0;
        EndpointMixFormat mixFormat{};
        std::uint32_t maximumRenderFrames = 0;
        PlaybackMode mode = PlaybackMode::TestSignals;
        MasterFrameRingBuffer* masterFrames = nullptr;
    };

    class EndpointPipeline final : private IFrameSource
    {
    public:
        EndpointPipeline();
        [[nodiscard]] bool Reset(const PipelineConfiguration& configuration);
        void SetDelayMs(std::uint32_t delayMs) noexcept;
        void SetCorrectionPpm(double ppm) noexcept;
        [[nodiscard]] bool Render(std::span<std::byte> output,
                                  std::uint32_t endpointFrameCount,
                                  float startGain = 1.0f,
                                  float endGain = 1.0f) noexcept;
        [[nodiscard]] std::uint32_t CurrentDelayMs() const noexcept;

    private:
        [[nodiscard]] StereoFrame NextFrame() noexcept override;

        SpeakerRole role_ = SpeakerRole::Front;
        TestPattern pattern_ = TestPattern::PairedClicks;
        std::uint64_t sourceFrame_ = 0;
        TestPatternGenerator generator_{};
        PlaybackMode mode_ = PlaybackMode::TestSignals;
        MasterFrameRingBuffer* masterFrames_ = nullptr;
        std::uint64_t lastMasterSequence_ = 0;
        DelayLine delay_;
        AdaptiveResampler resampler_{};
        EndpointConverter converter_;
        std::vector<StereoFrame> scratch_;
        std::atomic<std::uint32_t> delayMs_{0};
        std::atomic<double> correctionPpm_{0.0};
        bool ready_ = false;
    };
}
