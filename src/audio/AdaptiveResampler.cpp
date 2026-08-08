#include "AdaptiveResampler.h"
#include <algorithm>
#include <cmath>

namespace soundstage::audio
{
    namespace
    {
        constexpr double MaximumCorrectionSlewPpmPerSecond = 50.0;
    }

    void AdaptiveResampler::Reset(const double nominalInputFramesPerOutputFrame,
                                  IFrameSource& source) noexcept
    {
        nominalRatio_ = ClampNominalRatio(nominalInputFramesPerOutputFrame);
        targetCorrectionPpm_ = 0.0;
        currentCorrectionPpm_ = 0.0;
        phase_ = 0.0;
        previous_ = source.NextFrame();
        next_ = source.NextFrame();
    }

    void AdaptiveResampler::SetNominalRatio(const double ratio) noexcept
    {
        nominalRatio_ = ClampNominalRatio(ratio);
    }

    void AdaptiveResampler::SetTargetCorrectionPpm(const double ppm) noexcept
    {
        targetCorrectionPpm_ = std::isfinite(ppm)
            ? std::clamp(ppm, -MaximumCorrectionPpm, MaximumCorrectionPpm)
            : 0.0;
    }

    double AdaptiveResampler::CurrentCorrectionPpm() const noexcept
    {
        return currentCorrectionPpm_;
    }

    void AdaptiveResampler::Render(const std::span<StereoFrame> output,
                                   IFrameSource& source) noexcept
    {
        const double ppmStep = MaximumCorrectionSlewPpmPerSecond * nominalRatio_ /
            static_cast<double>(MasterSampleRate);

        for (StereoFrame& frame : output)
        {
            currentCorrectionPpm_ += std::clamp(
                targetCorrectionPpm_ - currentCorrectionPpm_, -ppmStep, ppmStep);

            const float fraction = static_cast<float>(phase_);
            frame.left = std::lerp(previous_.left, next_.left, fraction);
            frame.right = std::lerp(previous_.right, next_.right, fraction);

            phase_ += nominalRatio_ *
                (1.0 + currentCorrectionPpm_ / 1'000'000.0);
            while (phase_ >= 1.0)
            {
                previous_ = next_;
                next_ = source.NextFrame();
                phase_ -= 1.0;
            }
        }
    }

    double AdaptiveResampler::ClampNominalRatio(const double ratio) noexcept
    {
        return std::isfinite(ratio) && ratio > 0.0 ? ratio : 1.0;
    }
}
