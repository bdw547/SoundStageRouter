#pragma once

#include "AudioTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace soundstage::audio
{
    struct SyncEstimate
    {
        ClockHealth health = ClockHealth::Settling;
        double relativePpm = 0.0;
        double correctionPpm = 0.0;
        double phaseErrorFrames = 0.0;
        std::uint32_t acceptedSamples = 0;
    };

    class ClockSynchronizer
    {
    public:
        void Reset() noexcept;
        void Observe(const ClockSnapshot& reference,
                     const ClockSnapshot& follower) noexcept;
        void MarkClockUnavailable() noexcept;
        void NotifyManualDelayChanged() noexcept;
        [[nodiscard]] SyncEstimate Current() const noexcept;

    private:
        struct ObservationPair
        {
            ClockSnapshot reference;
            ClockSnapshot follower;
        };

        std::array<ObservationPair, 101> observations_{};
        std::size_t observationCount_ = 0;
        std::size_t nextObservation_ = 0;
        bool hasRateEstimate_ = false;
        SyncEstimate estimate_{};
    };
}
