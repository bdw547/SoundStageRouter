#pragma once

#include "ClockSynchronizer.h"
#include "EndpointSession.h"

#include <array>
#include <cstdint>
#include <memory>
#include <stop_token>

namespace soundstage::audio
{
    class EngineController
    {
    public:
        explicit EngineController(IEndpointSessionFactory& factory);
        SessionResult Start(const RunConfiguration& configuration,
                            std::stop_token stopToken);
        void Stop() noexcept;
        void SetDelayMs(SpeakerRole role, std::uint32_t delayMs) noexcept;
        void Tick(std::uint64_t qpc100ns) noexcept;
        [[nodiscard]] EngineStatus Status() const;

    private:
        [[nodiscard]] static std::size_t RoleIndex(SpeakerRole role) noexcept;
        [[nodiscard]] SessionResult Validate(
            const RunConfiguration& configuration) const;
        SessionResult Fail(const EngineFault& fault) noexcept;
        void RecordUnderruns(std::size_t endpointIndex,
                             std::uint64_t underrunCount,
                             std::uint64_t qpc100ns) noexcept;

        IEndpointSessionFactory& factory_;
        std::array<std::unique_ptr<IEndpointSession>, 2> sessions_{};
        std::array<EndpointRoute, 2> routes_{};
        std::array<std::uint64_t, 2> lastUnderrunCounts_{};
        std::array<std::uint64_t, 3> underrunTimes_{};
        std::size_t underrunTimeCount_ = 0;
        ClockSynchronizer synchronizer_{};
        EngineStatus status_{};
    };
}
