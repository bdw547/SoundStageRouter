#pragma once
#include "AudioTypes.h"
#include <cstdint>
#include <span>

namespace soundstage::audio
{
    class TestPatternGenerator
    {
    public:
        void Render(TestPattern pattern, std::uint64_t startFrame,
                    std::span<RoleFrame> output) const noexcept;

    private:
        [[nodiscard]] static float ShapedPulse(std::uint64_t frameInPulse,
                                               double frequencyHz) noexcept;
        [[nodiscard]] static float Tone(std::uint64_t absoluteFrame,
                                        double frequencyHz) noexcept;
    };
}
