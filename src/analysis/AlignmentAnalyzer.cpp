#include "AlignmentAnalyzer.h"

#include "../audio/AudioTypes.h"
#include "../audio/TestPatternGenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>

namespace soundstage::analysis
{
    namespace
    {
        using soundstage::audio::MasterSampleRate;
        using soundstage::audio::RoleFrame;
        using soundstage::audio::TestPattern;
        using soundstage::audio::TestPatternGenerator;

        constexpr std::size_t SignatureFrames = 240;
        constexpr std::size_t EventSearchFrames = 2400;
        constexpr std::size_t RoleSearchFrames = 720;
        constexpr std::size_t MinimumEventSeparationFrames =
            MasterSampleRate / 2;
        constexpr double MinimumCorrelation = 0.35;

        using Signature = std::array<float, SignatureFrames>;

        [[nodiscard]] bool NormalizeZeroMean(
            Signature& signature) noexcept
        {
            double mean = 0.0;
            for (const float sample : signature)
            {
                mean += sample;
            }
            mean /= signature.size();

            double energy = 0.0;
            for (float& sample : signature)
            {
                sample = static_cast<float>(sample - mean);
                energy += static_cast<double>(sample) * sample;
            }
            if (!(energy > 0.0))
            {
                return false;
            }
            const double norm = std::sqrt(energy);
            for (float& sample : signature)
            {
                sample = static_cast<float>(sample / norm);
            }
            return true;
        }

        [[nodiscard]] double CorrelationAt(
            const std::span<const float> samples,
            const std::size_t offset,
            const std::span<const float> signature) noexcept
        {
            double mean = 0.0;
            for (std::size_t index = 0;
                 index < signature.size(); ++index)
            {
                mean += samples[offset + index];
            }
            mean /= signature.size();

            double dot = 0.0;
            double energy = 0.0;
            for (std::size_t index = 0;
                 index < signature.size(); ++index)
            {
                const double centered =
                    samples[offset + index] - mean;
                dot += centered * signature[index];
                energy += centered * centered;
            }
            return energy == 0.0
                ? 0.0 : dot / std::sqrt(energy);
        }

        struct Peak
        {
            std::size_t offset = 0;
            double correlation = 0.0;
        };

        [[nodiscard]] Peak FindRolePeak(
            const std::span<const float> samples,
            const Signature& signature,
            const std::size_t center,
            const std::size_t radius) noexcept
        {
            const std::size_t lastOffset =
                samples.size() - signature.size();
            const std::size_t first =
                center > radius ? center - radius : 0;
            const std::size_t last =
                std::min(lastOffset, center + radius);
            Peak best{first, 0.0};
            for (std::size_t offset = first;
                 offset <= last; ++offset)
            {
                const double correlation = std::abs(
                    CorrelationAt(samples, offset, signature));
                if (correlation > best.correlation)
                {
                    best = {offset, correlation};
                }
            }
            return best;
        }

        [[nodiscard]] Peak FindCombinedPeak(
            const std::span<const float> samples,
            const Signature& front,
            const Signature& rear,
            const std::size_t first,
            const std::size_t last) noexcept
        {
            Peak best{first, 0.0};
            for (std::size_t offset = first;
                 offset <= last; ++offset)
            {
                const double correlation =
                    std::abs(CorrelationAt(samples, offset, front)) +
                    std::abs(CorrelationAt(samples, offset, rear));
                if (correlation > best.correlation)
                {
                    best = {offset, correlation};
                }
            }
            return best;
        }

        [[nodiscard]] double Median(
            std::vector<double> values)
        {
            std::sort(values.begin(), values.end());
            const std::size_t middle = values.size() / 2;
            return values.size() % 2 == 0
                ? (values[middle - 1] + values[middle]) * 0.5
                : values[middle];
        }
    }

    AlignmentResult AnalyzeAlignment(
        const WavRecording& recording)
    {
        AlignmentResult result;
        if (recording.sampleRate != MasterSampleRate)
        {
            result.error =
                "recording sample rate must be exactly 48000 Hz";
            return result;
        }
        if (recording.monoSamples.size() < SignatureFrames)
        {
            result.error =
                "recording is too short for paired-click analysis";
            return result;
        }

        std::array<RoleFrame, SignatureFrames> frames{};
        TestPatternGenerator{}.Render(
            TestPattern::PairedClicks, 0, frames);
        Signature front{};
        Signature rear{};
        for (std::size_t index = 0; index < frames.size(); ++index)
        {
            front[index] = frames[index].front.left;
            rear[index] = frames[index].rear.left;
        }
        if (!NormalizeZeroMean(front) ||
            !NormalizeZeroMean(rear))
        {
            result.error =
                "internal click signature has zero energy";
            return result;
        }

        const std::span<const float> samples =
            recording.monoSamples;
        const std::size_t lastOffset =
            samples.size() - SignatureFrames;
        const std::size_t initialLast = std::min(
            lastOffset,
            static_cast<std::size_t>(2) * MasterSampleRate);
        Peak firstEvent = FindCombinedPeak(
            samples, front, rear, 0, initialLast);
        if (firstEvent.correlation < MinimumCorrelation)
        {
            result.error = "no paired clicks detected";
            return result;
        }
        while (firstEvent.offset >= MasterSampleRate)
        {
            const std::size_t priorPrediction =
                firstEvent.offset - MasterSampleRate;
            const std::size_t priorFirst =
                priorPrediction > EventSearchFrames
                    ? priorPrediction - EventSearchFrames : 0;
            const std::size_t priorLast = std::min(
                lastOffset, priorPrediction + EventSearchFrames);
            const Peak prior = FindCombinedPeak(
                samples, front, rear, priorFirst, priorLast);
            const Peak priorFront = FindRolePeak(
                samples, front, prior.offset, RoleSearchFrames);
            const Peak priorRear = FindRolePeak(
                samples, rear, prior.offset, RoleSearchFrames);
            if (prior.correlation < MinimumCorrelation ||
                priorFront.correlation < MinimumCorrelation ||
                priorRear.correlation < MinimumCorrelation)
            {
                break;
            }
            firstEvent = prior;
        }

        std::size_t previousAccepted = 0;
        bool hasPrevious = false;
        for (std::size_t predicted = firstEvent.offset;
             predicted <= lastOffset;)
        {
            const std::size_t searchFirst =
                predicted > EventSearchFrames
                    ? predicted - EventSearchFrames : 0;
            const std::size_t searchLast = std::min(
                lastOffset, predicted + EventSearchFrames);
            const Peak event = FindCombinedPeak(
                samples, front, rear, searchFirst, searchLast);
            if (event.correlation >= MinimumCorrelation &&
                (!hasPrevious ||
                 (event.offset >= previousAccepted &&
                  event.offset - previousAccepted >=
                    MinimumEventSeparationFrames)))
            {
                const Peak frontPeak = FindRolePeak(
                    samples, front, event.offset,
                    RoleSearchFrames);
                const Peak rearPeak = FindRolePeak(
                    samples, rear, event.offset,
                    RoleSearchFrames);
                if (frontPeak.correlation >= MinimumCorrelation &&
                    rearPeak.correlation >= MinimumCorrelation)
                {
                    const auto signedFrames =
                        static_cast<std::int64_t>(rearPeak.offset) -
                        static_cast<std::int64_t>(frontPeak.offset);
                    result.pairs.push_back({
                        static_cast<double>(event.offset) /
                            MasterSampleRate,
                        static_cast<double>(signedFrames) * 1000.0 /
                            MasterSampleRate
                    });
                    previousAccepted = event.offset;
                    hasPrevious = true;
                }
            }

            if (predicted >
                lastOffset - std::min<std::size_t>(
                    lastOffset, MasterSampleRate))
            {
                break;
            }
            predicted += MasterSampleRate;
        }

        result.detectedPairs =
            static_cast<std::uint32_t>(result.pairs.size());
        if (result.pairs.empty())
        {
            result.error = "no paired clicks detected";
            return result;
        }

        std::vector<double> signedOffsets;
        std::vector<double> absoluteOffsets;
        signedOffsets.reserve(result.pairs.size());
        absoluteOffsets.reserve(result.pairs.size());
        for (const AlignmentPair& pair : result.pairs)
        {
            signedOffsets.push_back(pair.signedRearMinusFrontMs);
            absoluteOffsets.push_back(
                std::abs(pair.signedRearMinusFrontMs));
        }
        std::sort(absoluteOffsets.begin(), absoluteOffsets.end());
        const std::size_t percentileIndex =
            static_cast<std::size_t>(
                std::ceil(0.95 * absoluteOffsets.size())) - 1;
        result.medianSignedMs = Median(std::move(signedOffsets));
        result.percentile95AbsoluteMs =
            absoluteOffsets[percentileIndex];
        result.maximumAbsoluteMs = absoluteOffsets.back();
        result.valid = result.detectedPairs >= 20;
        result.passed =
            result.valid &&
            result.percentile95AbsoluteMs <= 10.0;
        if (!result.valid)
        {
            result.error =
                "at least 20 paired clicks are required";
        }
        return result;
    }
}
