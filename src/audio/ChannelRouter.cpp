#include "ChannelRouter.h"

#include <algorithm>
#include <cmath>

namespace soundstage::audio
{
    namespace
    {
        constexpr float CenterGain = 0.70710678f;
        constexpr float LfeGain = 0.5f;
        constexpr float DuplicateGain = 0.50118723f;
        constexpr float SilenceThreshold = 1.0e-7f;

        [[nodiscard]] float Limit(const float value) noexcept
        {
            return std::clamp(value, -1.0f, 1.0f);
        }
    }

    RoleFrame RouteSurroundFrame(
        const SurroundFrame& input, const RearFillMode rearFill,
        const SurroundMixLevels levels) noexcept
    {
        RoleFrame output;
        output.front.left = Limit(
            input.frontLeft + CenterGain * input.frontCenter +
            LfeGain * input.lfe);
        output.front.right = Limit(
            input.frontRight + CenterGain * input.frontCenter +
            LfeGain * input.lfe);

        const float backGain = std::clamp(levels.back, 0.0f, 1.0f);
        const float sideGain = std::clamp(levels.side, 0.0f, 1.0f);
        const bool nativeRear =
            std::abs(input.backLeft) > SilenceThreshold ||
            std::abs(input.backRight) > SilenceThreshold ||
            std::abs(input.sideLeft) > SilenceThreshold ||
            std::abs(input.sideRight) > SilenceThreshold;
        if (nativeRear || rearFill == RearFillMode::Off)
        {
            output.rear = {
                Limit(backGain * input.backLeft + sideGain * input.sideLeft),
                Limit(backGain * input.backRight + sideGain * input.sideRight)};
        }
        else if (rearFill == RearFillMode::Duplicate)
        {
            output.rear = {
                Limit(DuplicateGain * input.frontLeft),
                Limit(DuplicateGain * input.frontRight)};
        }
        else
        {
            output.rear = {
                Limit(0.5f * (input.frontLeft - input.frontRight)),
                Limit(0.5f * (input.frontRight - input.frontLeft))};
        }
        return output;
    }
}
