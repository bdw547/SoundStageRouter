#include "AudioEngineCoordinator.h"
#include "WasapiEndpointSession.h"

#include <windows.h>

#include <chrono>
#include <utility>

namespace soundstage::audio
{
    namespace
    {
        [[nodiscard]] std::uint64_t QpcNow100ns() noexcept
        {
            LARGE_INTEGER counter{};
            LARGE_INTEGER frequency{};
            QueryPerformanceCounter(&counter);
            QueryPerformanceFrequency(&frequency);
            if (frequency.QuadPart <= 0)
            {
                return 0;
            }
            return static_cast<std::uint64_t>(
                static_cast<long double>(counter.QuadPart) * 10'000'000.0L /
                static_cast<long double>(frequency.QuadPart));
        }
    }

    AudioEngineCoordinator::AudioEngineCoordinator()
        : AudioEngineCoordinator(
            std::make_unique<WasapiEndpointSessionFactory>())
    {
    }

    AudioEngineCoordinator::AudioEngineCoordinator(
        std::unique_ptr<IEndpointSessionFactory> factory)
        : factory_(std::move(factory)),
          engine_(std::make_unique<EngineController>(*factory_)),
          status_(std::make_shared<const EngineStatus>()),
          worker_([this](const std::stop_token token) { WorkerMain(token); })
    {
    }

    AudioEngineCoordinator::~AudioEngineCoordinator()
    {
        cancellationRequested_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(mutex_);
            preparationStopSource_.request_stop();
        }
        worker_.request_stop();
        condition_.notify_one();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void AudioEngineCoordinator::PostStart(RunConfiguration configuration)
    {
        cancellationRequested_.store(false, std::memory_order_release);
        {
            std::lock_guard lock(mutex_);
            commands_.push_back(
                {CommandType::Start, std::move(configuration),
                 SpeakerRole::Front, 0});
        }
        condition_.notify_one();
    }

    void AudioEngineCoordinator::PostStop() noexcept
    {
        cancellationRequested_.store(true, std::memory_order_release);
        try
        {
            std::lock_guard lock(mutex_);
            preparationStopSource_.request_stop();
            commands_.push_back({CommandType::Stop});
        }
        catch (...)
        {
        }
        condition_.notify_one();
    }

    void AudioEngineCoordinator::PostDelay(
        const SpeakerRole role,
        const std::uint32_t delayMs) noexcept
    {
        try
        {
            std::lock_guard lock(mutex_);
            Command command;
            command.type = CommandType::Delay;
            command.role = role;
            command.delayMs = delayMs;
            commands_.push_back(std::move(command));
        }
        catch (...)
        {
        }
        condition_.notify_one();
    }

    std::shared_ptr<const EngineStatus>
    AudioEngineCoordinator::Status() const noexcept
    {
        return status_.load(std::memory_order_acquire);
    }

    void AudioEngineCoordinator::WorkerMain(
        const std::stop_token stopToken) noexcept
    {
        try
        {
            while (!stopToken.stop_requested())
            {
                Command command;
                bool hasCommand = false;
                {
                    std::unique_lock lock(mutex_);
                    const bool running =
                        engine_->Status().state == PlaybackState::Running;
                    if (running)
                    {
                        condition_.wait_for(
                            lock, std::chrono::milliseconds(100),
                            [&] {
                                return stopToken.stop_requested() ||
                                       !commands_.empty();
                            });
                    }
                    else
                    {
                        condition_.wait(lock, [&] {
                            return stopToken.stop_requested() ||
                                   !commands_.empty();
                        });
                    }
                    if (stopToken.stop_requested())
                    {
                        break;
                    }
                    if (!commands_.empty())
                    {
                        command = std::move(commands_.front());
                        commands_.pop_front();
                        hasCommand = true;
                    }
                }

                if (hasCommand)
                {
                    switch (command.type)
                    {
                    case CommandType::Start:
                    {
                        std::stop_token preparationToken;
                        {
                            std::lock_guard lock(mutex_);
                            preparationStopSource_ = std::stop_source{};
                            if (cancellationRequested_.load(
                                    std::memory_order_acquire))
                            {
                                preparationStopSource_.request_stop();
                            }
                            preparationToken =
                                preparationStopSource_.get_token();
                        }
                        engine_->Start(command.configuration, preparationToken);
                        break;
                    }
                    case CommandType::Stop:
                        engine_->Stop();
                        break;
                    case CommandType::Delay:
                        engine_->SetDelayMs(command.role, command.delayMs);
                        break;
                    }
                }
                else if (engine_->Status().state == PlaybackState::Running)
                {
                    engine_->Tick(QpcNow100ns());
                }
                PublishStatus();
            }
        }
        catch (...)
        {
        }
        engine_->Stop();
        PublishStatus();
    }

    void AudioEngineCoordinator::PublishStatus()
    {
        status_.store(
            std::make_shared<const EngineStatus>(engine_->Status()),
            std::memory_order_release);
    }
}
