#include "ClockSynchronizer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace soundstage::audio
{
    namespace
    {
        constexpr double MinimumObservationSeconds = 3.0;
        constexpr double MaximumPlausibleRatePpm = 5000.0;
        constexpr double RateEstimateAlpha = 0.1;

        [[nodiscard]] bool IsUsable(const ClockSnapshot& value) noexcept
        {
            return value.available && value.deviceFrequency != 0;
        }
    }

    void ClockSynchronizer::Reset() noexcept
    {
        observations_ = {};
        observationCount_ = 0;
        nextObservation_ = 0;
        hasRateEstimate_ = false;
        estimate_ = {};
    }

    void ClockSynchronizer::Observe(const ClockSnapshot& reference,
                                    const ClockSnapshot& follower) noexcept
    {
        if (!IsUsable(reference) || !IsUsable(follower))
        {
            MarkClockUnavailable();
            return;
        }

        if (observationCount_ != 0)
        {
            const std::size_t newestIndex =
                (nextObservation_ + observations_.size() - 1) %
                observations_.size();
            const ObservationPair& newest = observations_[newestIndex];
            if (reference.devicePosition <= newest.reference.devicePosition ||
                follower.devicePosition <= newest.follower.devicePosition ||
                reference.qpc100ns <= newest.reference.qpc100ns ||
                follower.qpc100ns <= newest.follower.qpc100ns)
            {
                return;
            }
        }

        const std::size_t oldestIndex =
            observationCount_ < observations_.size() ? 0 : nextObservation_;
        if (observationCount_ != 0)
        {
            const ObservationPair& oldest = observations_[oldestIndex];
            const double qpcSeconds = static_cast<double>(
                reference.qpc100ns - oldest.reference.qpc100ns) / 10'000'000.0;
            if (qpcSeconds >= MinimumObservationSeconds)
            {
                const double referenceSeconds = static_cast<double>(
                    reference.devicePosition - oldest.reference.devicePosition) /
                    static_cast<double>(reference.deviceFrequency);
                const double followerSeconds = static_cast<double>(
                    follower.devicePosition - oldest.follower.devicePosition) /
                    static_cast<double>(follower.deviceFrequency);
                const double referenceRate = referenceSeconds / qpcSeconds;
                const double followerRate = followerSeconds / qpcSeconds;
                if (!(referenceRate > 0.0) || !(followerRate > 0.0))
                {
                    return;
                }
                const double rawPpm =
                    (followerRate / referenceRate - 1.0) * 1'000'000.0;
                if (!std::isfinite(rawPpm) ||
                    std::abs(rawPpm) > MaximumPlausibleRatePpm)
                {
                    return;
                }

                estimate_.phaseErrorFrames =
                    (followerSeconds - referenceSeconds) * MasterSampleRate;
                estimate_.relativePpm = hasRateEstimate_
                    ? std::lerp(estimate_.relativePpm, rawPpm, RateEstimateAlpha)
                    : rawPpm;
                hasRateEstimate_ = true;
                estimate_.correctionPpm = std::clamp(
                    -estimate_.relativePpm,
                    -MaximumCorrectionPpm,
                    MaximumCorrectionPpm);
                estimate_.health = ClockHealth::Active;
            }
            else
            {
                estimate_.health = ClockHealth::Settling;
                estimate_.correctionPpm = 0.0;
            }
        }

        observations_[nextObservation_] = {reference, follower};
        nextObservation_ = (nextObservation_ + 1) % observations_.size();
        if (observationCount_ < observations_.size())
        {
            ++observationCount_;
        }
        if (estimate_.acceptedSamples !=
            std::numeric_limits<std::uint32_t>::max())
        {
            ++estimate_.acceptedSamples;
        }
    }

    void ClockSynchronizer::MarkClockUnavailable() noexcept
    {
        estimate_.health = ClockHealth::Unavailable;
        estimate_.correctionPpm = 0.0;
    }

    void ClockSynchronizer::NotifyManualDelayChanged() noexcept
    {
        observationCount_ = 0;
        nextObservation_ = 0;
        estimate_.health = ClockHealth::Settling;
        estimate_.correctionPpm = 0.0;
        estimate_.phaseErrorFrames = 0.0;
        estimate_.acceptedSamples = 0;
    }

    SyncEstimate ClockSynchronizer::Current() const noexcept
    {
        return estimate_;
    }
}
