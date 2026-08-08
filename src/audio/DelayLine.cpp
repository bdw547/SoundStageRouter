#include "DelayLine.h"
#include <algorithm>
#include <cmath>

namespace soundstage::audio
{
    namespace
    {
        constexpr double MaximumDelayChangePerOutputFrame = 0.25;
    }

    DelayLine::DelayLine(const std::size_t maximumDelayFrames)
        : buffer_(maximumDelayFrames + 1)
    {
    }

    void DelayLine::Reset(const double delayFrames) noexcept
    {
        std::fill(buffer_.begin(), buffer_.end(), StereoFrame{});
        writePosition_ = 0;
        currentDelayFrames_ = ClampDelayFrames(delayFrames);
        targetDelayFrames_ = currentDelayFrames_;
        lastReadPosition_ = -1.0 - currentDelayFrames_;
    }

    void DelayLine::SetDelayFrames(const double delayFrames) noexcept
    {
        targetDelayFrames_ = ClampDelayFrames(delayFrames);
    }

    double DelayLine::CurrentDelayFrames() const noexcept
    {
        return currentDelayFrames_;
    }

    StereoFrame DelayLine::ProcessFrame(const StereoFrame input) noexcept
    {
        const double difference = targetDelayFrames_ - currentDelayFrames_;
        currentDelayFrames_ += std::clamp(
            difference,
            -MaximumDelayChangePerOutputFrame,
            MaximumDelayChangePerOutputFrame);

        buffer_[writePosition_ % buffer_.size()] = input;
        const double readPosition = static_cast<double>(writePosition_) - currentDelayFrames_;
        lastReadPosition_ = readPosition;

        const double bufferFrames = static_cast<double>(buffer_.size());
        double wrappedReadPosition = std::fmod(readPosition, bufferFrames);
        if (wrappedReadPosition < 0.0)
        {
            wrappedReadPosition += bufferFrames;
        }

        const auto before = static_cast<std::size_t>(std::floor(wrappedReadPosition));
        const auto after = (before + 1) % buffer_.size();
        const float fraction = static_cast<float>(wrappedReadPosition - before);
        const StereoFrame& earlier = buffer_[before];
        const StereoFrame& later = buffer_[after];
        const StereoFrame output{
            earlier.left + (later.left - earlier.left) * fraction,
            earlier.right + (later.right - earlier.right) * fraction
        };

        ++writePosition_;
        return output;
    }

#ifdef SOUNDSTAGE_TESTING
    double DelayLine::DebugReadPosition() const noexcept
    {
        return lastReadPosition_;
    }
#endif

    double DelayLine::ClampDelayFrames(const double delayFrames) const noexcept
    {
        return std::clamp(delayFrames, 0.0,
                          static_cast<double>(buffer_.size() - 1));
    }
}
