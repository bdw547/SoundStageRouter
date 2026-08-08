#pragma once
#include "AudioTypes.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace soundstage::audio
{
    class DelayLine
    {
    public:
        explicit DelayLine(std::size_t maximumDelayFrames);
        void Reset(double delayFrames) noexcept;
        void SetDelayFrames(double delayFrames) noexcept;
        [[nodiscard]] double CurrentDelayFrames() const noexcept;
        [[nodiscard]] StereoFrame ProcessFrame(StereoFrame input) noexcept;
#ifdef SOUNDSTAGE_TESTING
        [[nodiscard]] double DebugReadPosition() const noexcept;
#endif

    private:
        [[nodiscard]] double ClampDelayFrames(double delayFrames) const noexcept;

        std::vector<StereoFrame> buffer_;
        std::uint64_t writePosition_ = 0;
        double currentDelayFrames_ = 0.0;
        double targetDelayFrames_ = 0.0;
        double lastReadPosition_ = 0.0;
    };
}
