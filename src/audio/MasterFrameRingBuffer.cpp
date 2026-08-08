#include "MasterFrameRingBuffer.h"

#include <algorithm>

namespace soundstage::audio
{
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

    MasterFrameRingBuffer::MasterFrameRingBuffer(
        const std::size_t capacityFrames)
        : frames_(capacityFrames == 0
              ? nullptr : std::make_unique<RoleFrame[]>(capacityFrames)),
          capacity_(capacityFrames)
    {
    }

    bool MasterFrameRingBuffer::Push(const RoleFrame& frame) noexcept
    {
        if (capacity_ == 0)
        {
            overflowCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const std::uint64_t write =
            writeSequence_.load(std::memory_order_relaxed);
        const std::uint64_t oldest = std::min(
            readSequences_[0].load(std::memory_order_acquire),
            readSequences_[1].load(std::memory_order_acquire));
        if (write - oldest >= capacity_)
        {
            overflowCount_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        frames_[write % capacity_] = frame;
        writeSequence_.store(write + 1, std::memory_order_release);
        return true;
    }

    std::size_t MasterFrameRingBuffer::Push(
        const std::span<const RoleFrame> frames) noexcept
    {
        std::size_t pushed = 0;
        for (const RoleFrame& frame : frames)
        {
            pushed += Push(frame) ? 1u : 0u;
        }
        return pushed;
    }

    bool MasterFrameRingBuffer::Read(
        const std::size_t reader, RoleFrame& frame,
        std::uint64_t& sequence) noexcept
    {
        if (reader >= readSequences_.size())
        {
            return false;
        }
        const std::uint64_t read =
            readSequences_[reader].load(std::memory_order_relaxed);
        if (read == writeSequence_.load(std::memory_order_acquire))
        {
            underrunCounts_[reader].fetch_add(
                1, std::memory_order_relaxed);
            return false;
        }
        frame = frames_[read % capacity_];
        sequence = read;
        readSequences_[reader].store(read + 1, std::memory_order_release);
        return true;
    }

    void MasterFrameRingBuffer::Reset() noexcept
    {
        writeSequence_.store(0, std::memory_order_relaxed);
        for (auto& value : readSequences_)
        {
            value.store(0, std::memory_order_relaxed);
        }
        overflowCount_.store(0, std::memory_order_relaxed);
        for (auto& value : underrunCounts_)
        {
            value.store(0, std::memory_order_relaxed);
        }
    }

    std::size_t MasterFrameRingBuffer::Capacity() const noexcept
    {
        return capacity_;
    }

    std::uint64_t MasterFrameRingBuffer::OverflowCount() const noexcept
    {
        return overflowCount_.load(std::memory_order_relaxed);
    }

    std::uint64_t MasterFrameRingBuffer::UnderrunCount(
        const std::size_t reader) const noexcept
    {
        return reader < underrunCounts_.size()
            ? underrunCounts_[reader].load(std::memory_order_relaxed) : 0;
    }
}
