#include "TestPatternGenerator.h"
#include <cmath>
#include <numbers>

namespace soundstage::audio
{
    namespace
    {
        constexpr std::uint64_t ClickPeriodFrames = MasterSampleRate;
        constexpr std::uint64_t PulseFrames = 240;
        constexpr std::uint64_t ToneOnFrames = MasterSampleRate / 2;
        constexpr std::uint64_t ToneCycleFrames = MasterSampleRate;
        constexpr std::uint64_t ToneFadeFrames = 480;

        [[nodiscard]] float ToneEnvelope(const std::uint64_t frameInCycle) noexcept
        {
            if (frameInCycle >= ToneOnFrames)
            {
                return 0.0f;
            }

            const std::uint64_t framesFromStart = frameInCycle;
            const std::uint64_t framesToEnd = ToneOnFrames - 1 - frameInCycle;
            const std::uint64_t fadeFrame =
                framesFromStart < ToneFadeFrames ? framesFromStart :
                framesToEnd < ToneFadeFrames ? framesToEnd : ToneFadeFrames - 1;
            const double phase = std::numbers::pi * static_cast<double>(fadeFrame) /
                                 static_cast<double>(ToneFadeFrames - 1);
            return static_cast<float>(0.5 - 0.5 * std::cos(phase));
        }

        void WriteFront(RoleFrame& frame, const float value) noexcept
        {
            frame.front.left = value;
            frame.front.right = value;
        }

        void WriteRear(RoleFrame& frame, const float value) noexcept
        {
            frame.rear.left = value;
            frame.rear.right = value;
        }
    }

    void TestPatternGenerator::Render(const TestPattern pattern,
                                      const std::uint64_t startFrame,
                                      std::span<RoleFrame> output) const noexcept
    {
        for (std::size_t index = 0; index < output.size(); ++index)
        {
            RoleFrame frame{};
            const std::uint64_t absoluteFrame = startFrame + index;
            const std::uint64_t frameInSecond = absoluteFrame % ClickPeriodFrames;

            switch (pattern)
            {
            case TestPattern::PairedClicks:
                if (frameInSecond < PulseFrames)
                {
                    WriteFront(frame, ShapedPulse(frameInSecond, 1200.0));
                    WriteRear(frame, ShapedPulse(frameInSecond, 2400.0));
                }
                break;

            case TestPattern::AlternatingClicks:
                if (frameInSecond < PulseFrames)
                {
                    const float pulse = ShapedPulse(frameInSecond,
                        (absoluteFrame / ClickPeriodFrames) % 2 == 0 ? 1200.0 : 2400.0);
                    if ((absoluteFrame / ClickPeriodFrames) % 2 == 0)
                    {
                        WriteFront(frame, pulse);
                    }
                    else
                    {
                        WriteRear(frame, pulse);
                    }
                }
                break;

            case TestPattern::FrontTone:
                WriteFront(frame, Tone(absoluteFrame, 440.0) *
                                  ToneEnvelope(absoluteFrame % ToneCycleFrames));
                break;

            case TestPattern::RearTone:
                WriteRear(frame, Tone(absoluteFrame, 660.0) *
                                 ToneEnvelope(absoluteFrame % ToneCycleFrames));
                break;
            }

            output[index] = frame;
        }
    }

    float TestPatternGenerator::ShapedPulse(const std::uint64_t frameInPulse,
                                             const double frequencyHz) noexcept
    {
        const double phase = 2.0 * std::numbers::pi * frequencyHz *
                             static_cast<double>(frameInPulse) / MasterSampleRate;
        const double window = 0.5 - 0.5 * std::cos(
            2.0 * std::numbers::pi * static_cast<double>(frameInPulse) / 239.0);
        return static_cast<float>(0.25 * window * std::sin(phase));
    }

    float TestPatternGenerator::Tone(const std::uint64_t absoluteFrame,
                                     const double frequencyHz) noexcept
    {
        const double phase = 2.0 * std::numbers::pi * frequencyHz *
                             static_cast<double>(absoluteFrame) / MasterSampleRate;
        return static_cast<float>(0.25 * std::sin(phase));
    }
}
