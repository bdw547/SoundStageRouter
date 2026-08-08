#pragma once
#include "AudioTypes.h"
#include <span>

namespace soundstage::audio
{
    class IFrameSource
    {
    public:
        virtual ~IFrameSource() = default;
        [[nodiscard]] virtual StereoFrame NextFrame() noexcept = 0;
    };

    class AdaptiveResampler
    {
    public:
        void Reset(double nominalInputFramesPerOutputFrame,
                   IFrameSource& source) noexcept;
        void SetNominalRatio(double ratio) noexcept;
        void SetTargetCorrectionPpm(double ppm) noexcept;
        [[nodiscard]] double CurrentCorrectionPpm() const noexcept;
        void Render(std::span<StereoFrame> output, IFrameSource& source) noexcept;

    private:
        [[nodiscard]] static double ClampNominalRatio(double ratio) noexcept;

        StereoFrame previous_{};
        StereoFrame next_{};
        double nominalRatio_ = 1.0;
        double targetCorrectionPpm_ = 0.0;
        double currentCorrectionPpm_ = 0.0;
        double phase_ = 0.0;
    };
}
