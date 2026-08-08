#pragma once

#include "AudioTypes.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <type_traits>

namespace soundstage::audio
{
    inline constexpr std::uint32_t InvalidConfigurationCode = 0x1001u;
    inline constexpr std::uint32_t SessionCreationCode = 0x1002u;
    inline constexpr std::uint32_t RepeatedUnderrunCode = 0x1003u;
    inline constexpr std::uint32_t ClockUnavailableCode = 0x1004u;

    static_assert(std::is_trivially_copyable_v<EndpointTelemetry>);

    class TelemetryQueue
    {
    public:
        bool Push(const EndpointTelemetry& value) noexcept
        {
            const std::size_t write =
                write_.load(std::memory_order_relaxed);
            const std::size_t next = (write + 1) % values_.size();
            if (next == read_.load(std::memory_order_acquire))
            {
                return false;
            }
            values_[write] = value;
            write_.store(next, std::memory_order_release);
            return true;
        }

        bool Pop(EndpointTelemetry& value) noexcept
        {
            const std::size_t read =
                read_.load(std::memory_order_relaxed);
            if (read == write_.load(std::memory_order_acquire))
            {
                return false;
            }
            value = values_[read];
            read_.store((read + 1) % values_.size(),
                        std::memory_order_release);
            return true;
        }

    private:
        std::array<EndpointTelemetry, 64> values_{};
        std::atomic<std::size_t> write_{0};
        std::atomic<std::size_t> read_{0};
    };

    struct SessionResult
    {
        bool ok = true;
        EngineFault fault{};

        [[nodiscard]] static SessionResult Success() noexcept
        {
            return {};
        }

        [[nodiscard]] static SessionResult Failure(
            const std::uint32_t code,
            const SpeakerRole role,
            std::wstring message)
        {
            SessionResult result;
            result.ok = false;
            result.fault = {code, role, std::move(message)};
            return result;
        }
    };

    class IEndpointSession
    {
    public:
        virtual ~IEndpointSession() = default;
        virtual SessionResult Prepare(const EndpointRoute&, TestPattern,
                                      std::stop_token) = 0;
        virtual SessionResult Prime() = 0;
        virtual SessionResult ArmStart(std::uint64_t startQpc100ns) = 0;
        virtual void SetDelayMs(std::uint32_t) noexcept = 0;
        virtual void SetCorrectionPpm(double) noexcept = 0;
        virtual EndpointTelemetry Snapshot() noexcept = 0;
        virtual void Stop() noexcept = 0;
    };

    class IEndpointSessionFactory
    {
    public:
        virtual ~IEndpointSessionFactory() = default;
        virtual std::unique_ptr<IEndpointSession> Create(SpeakerRole role) = 0;
    };
}
