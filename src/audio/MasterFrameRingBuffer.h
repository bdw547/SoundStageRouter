#pragma once

#include "AudioTypes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace soundstage::audio
{
    class MasterFrameRingBuffer
    {
    public:
        explicit MasterFrameRingBuffer(std::size_t capacityFrames);

        MasterFrameRingBuffer(const MasterFrameRingBuffer&) = delete;
        MasterFrameRingBuffer& operator=(const MasterFrameRingBuffer&) = delete;

        [[nodiscard]] bool Push(const RoleFrame& frame) noexcept;
        std::size_t Push(std::span<const RoleFrame> frames) noexcept;
        [[nodiscard]] bool Read(
            std::size_t reader, RoleFrame& frame,
            std::uint64_t& sequence) noexcept;
        void Reset() noexcept;

        [[nodiscard]] std::size_t Capacity() const noexcept;
        [[nodiscard]] std::uint64_t OverflowCount() const noexcept;
        [[nodiscard]] std::uint64_t UnderrunCount(
            std::size_t reader) const noexcept;

    private:
        std::unique_ptr<RoleFrame[]> frames_;
        std::size_t capacity_ = 0;
        std::atomic<std::uint64_t> writeSequence_{0};
        std::array<std::atomic<std::uint64_t>, 2> readSequences_{};
        std::atomic<std::uint64_t> overflowCount_{0};
        std::array<std::atomic<std::uint64_t>, 2> underrunCounts_{};
    };
}
