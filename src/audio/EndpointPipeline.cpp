#include "EndpointPipeline.h"

#include <algorithm>
#include <cmath>
#include <new>

namespace soundstage::audio
{
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<double>::is_always_lock_free);

    EndpointPipeline::EndpointPipeline()
        : delay_(static_cast<std::size_t>(MillisecondsToFrames(MaximumDelayMs))),
          converter_({})
    {
    }

    bool EndpointPipeline::Reset(const PipelineConfiguration& configuration)
    {
        ready_ = false;
        const EndpointConverter converter(configuration.mixFormat);
        if (!converter.IsSupported() || configuration.maximumRenderFrames == 0)
        {
            return false;
        }

        try
        {
            scratch_.assign(configuration.maximumRenderFrames, StereoFrame{});
        }
        catch (const std::bad_alloc&)
        {
            scratch_.clear();
            return false;
        }

        role_ = configuration.role;
        pattern_ = configuration.pattern;
        sourceFrame_ = 0;
        converter_ = converter;
        const std::uint32_t delayMs =
            std::min(configuration.delayMs, MaximumDelayMs);
        delayMs_.store(delayMs, std::memory_order_relaxed);
        correctionPpm_.store(0.0, std::memory_order_relaxed);
        delay_.Reset(static_cast<double>(MillisecondsToFrames(delayMs)));
        resampler_.Reset(
            static_cast<double>(MasterSampleRate) /
                static_cast<double>(configuration.mixFormat.sampleRate),
            *this);
        ready_ = true;
        return true;
    }

    void EndpointPipeline::SetDelayMs(const std::uint32_t delayMs) noexcept
    {
        delayMs_.store(std::min(delayMs, MaximumDelayMs),
                       std::memory_order_relaxed);
    }

    void EndpointPipeline::SetCorrectionPpm(const double ppm) noexcept
    {
        correctionPpm_.store(ppm, std::memory_order_relaxed);
    }

    bool EndpointPipeline::Render(const std::span<std::byte> output,
                                  const std::uint32_t endpointFrameCount,
                                  float startGain,
                                  float endGain) noexcept
    {
        if (!ready_ || endpointFrameCount > scratch_.size() ||
            output.size() != converter_.RequiredBytes(endpointFrameCount))
        {
            return false;
        }

        delay_.SetDelayFrames(static_cast<double>(MillisecondsToFrames(
            delayMs_.load(std::memory_order_relaxed))));
        resampler_.SetTargetCorrectionPpm(
            correctionPpm_.load(std::memory_order_relaxed));
        auto frames = std::span(scratch_).first(endpointFrameCount);
        resampler_.Render(frames, *this);

        startGain = std::clamp(startGain, 0.0f, 1.0f);
        endGain = std::clamp(endGain, 0.0f, 1.0f);
        for (std::size_t index = 0; index < frames.size(); ++index)
        {
            const float fraction = frames.size() == 1
                ? 1.0f
                : static_cast<float>(index) /
                    static_cast<float>(frames.size() - 1);
            const float gain = std::lerp(startGain, endGain, fraction);
            frames[index].left *= gain;
            frames[index].right *= gain;
        }
        return converter_.Convert(frames, output);
    }

    std::uint32_t EndpointPipeline::CurrentDelayMs() const noexcept
    {
        return static_cast<std::uint32_t>(std::lround(
            delay_.CurrentDelayFrames() * 1000.0 / MasterSampleRate));
    }

    StereoFrame EndpointPipeline::NextFrame() noexcept
    {
        RoleFrame frame{};
        generator_.Render(pattern_, sourceFrame_, std::span(&frame, 1));
        ++sourceFrame_;
        return delay_.ProcessFrame(
            role_ == SpeakerRole::Front ? frame.front : frame.rear);
    }
}
