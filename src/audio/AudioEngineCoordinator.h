#pragma once

#include "EngineController.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace soundstage::audio
{
    class AudioEngineCoordinator
    {
    public:
        AudioEngineCoordinator();
        explicit AudioEngineCoordinator(
            std::unique_ptr<IEndpointSessionFactory> factory);
        ~AudioEngineCoordinator();

        AudioEngineCoordinator(const AudioEngineCoordinator&) = delete;
        AudioEngineCoordinator& operator=(const AudioEngineCoordinator&) = delete;

        void PostStart(RunConfiguration configuration);
        void PostStop() noexcept;
        void PostDelay(SpeakerRole role, std::uint32_t delayMs) noexcept;
        [[nodiscard]] std::shared_ptr<const EngineStatus> Status() const noexcept;

    private:
        enum class CommandType { Start, Stop, Delay };
        struct Command
        {
            CommandType type = CommandType::Stop;
            RunConfiguration configuration{};
            SpeakerRole role = SpeakerRole::Front;
            std::uint32_t delayMs = 0;
        };

        void WorkerMain(std::stop_token stopToken) noexcept;
        void PublishStatus();

        std::unique_ptr<IEndpointSessionFactory> factory_;
        std::unique_ptr<EngineController> engine_;
        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<Command> commands_;
        std::stop_source preparationStopSource_;
        std::atomic<bool> cancellationRequested_{false};
        std::atomic<std::shared_ptr<const EngineStatus>> status_;
        std::jthread worker_;
    };
}
