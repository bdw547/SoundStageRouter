#pragma once

namespace soundstage::driver
{
    using UInt8 = unsigned char;
    using UInt16 = unsigned short;
    using UInt32 = unsigned int;
    using UInt64 = unsigned long long;

    inline constexpr UInt32 ExactDataFormatSize = 104;
    inline constexpr UInt32 ExactAudioDataRangeSize = 88;
    inline constexpr UInt16 ExtensibleFormatTag = 0xFFFE;
    inline constexpr UInt16 ExactExtensionSize = 22;
    inline constexpr UInt32 ExactSampleRate = 48'000;
    inline constexpr UInt16 ExactBitsPerSample = 32;
    inline constexpr UInt16 FivePointOneChannels = 6;
    inline constexpr UInt16 SevenPointOneChannels = 8;
    inline constexpr UInt16 FivePointOneBlockAlign = 24;
    inline constexpr UInt16 SevenPointOneBlockAlign = 32;
    inline constexpr UInt32 FivePointOneAverageBytesPerSecond = 1'152'000;
    inline constexpr UInt32 SevenPointOneAverageBytesPerSecond = 1'536'000;
    inline constexpr UInt32 FivePointOneChannelMask = 0x003F;
    inline constexpr UInt32 SevenPointOneChannelMask = 0x063F;

    enum class SurroundLayout : UInt8
    {
        Unsupported,
        FivePointOne,
        SevenPointOne
    };

    enum class DataRangePointerKind : UInt8
    {
        Audio,
        AudioWithAttributes,
        AttributeList
    };

    enum class StreamReservationResult : UInt8
    {
        Reserved,
        FormatSwitchInProgress,
        NoCapacity
    };

    struct StreamCapacityState
    {
        UInt32 allocated;
        UInt32 maximum;
    };

    [[nodiscard]] constexpr StreamReservationResult TryReserveStreamSlot(
        StreamCapacityState& state,
        const bool formatSwitchInProgress) noexcept
    {
        if (formatSwitchInProgress)
        {
            return StreamReservationResult::FormatSwitchInProgress;
        }
        if (state.allocated >= state.maximum)
        {
            return StreamReservationResult::NoCapacity;
        }

        ++state.allocated;
        return StreamReservationResult::Reserved;
    }

    constexpr void FinishStreamReservation(
        StreamCapacityState& state,
        const bool streamCreated) noexcept
    {
        if (!streamCreated && state.allocated > 0)
        {
            --state.allocated;
        }
    }

    using Size = decltype(sizeof(0));

    template <Size Count>
    [[nodiscard]] constexpr bool AttributesFollowEveryFlaggedRange(
        const DataRangePointerKind (&sequence)[Count]) noexcept
    {
        for (Size index = 0; index < Count; ++index)
        {
            if (sequence[index] ==
                DataRangePointerKind::AudioWithAttributes)
            {
                if (index + 1 >= Count ||
                    sequence[index + 1] !=
                        DataRangePointerKind::AttributeList)
                {
                    return false;
                }
                ++index;
            }
            else if (sequence[index] ==
                     DataRangePointerKind::AttributeList)
            {
                return false;
            }
        }
        return true;
    }

    struct ExactPcmFormat
    {
        UInt32 dataFormatSize;
        bool majorFormatAudio;
        bool dataSubformatPcm;
        bool waveFormatSpecifier;
        UInt16 formatTag;
        UInt16 channels;
        UInt32 samplesPerSecond;
        UInt32 averageBytesPerSecond;
        UInt16 blockAlign;
        UInt16 bitsPerSample;
        UInt16 extensionSize;
        UInt16 validBitsPerSample;
        UInt32 channelMask;
        bool waveSubformatPcm;
    };

    struct AudioDataRange
    {
        UInt32 dataRangeSize;
        bool majorFormatAudio;
        bool dataSubformatPcm;
        bool waveFormatSpecifier;
        UInt32 maximumChannels;
        UInt32 minimumBitsPerSample;
        UInt32 maximumBitsPerSample;
        UInt32 minimumSampleFrequency;
        UInt32 maximumSampleFrequency;
    };

    enum class DataRangeIntersectionStatus : UInt8
    {
        Success,
        BufferOverflow,
        BufferTooSmall,
        NoMatch
    };

    struct DataRangeIntersectionResult
    {
        DataRangeIntersectionStatus status;
        UInt32 resultantFormatLength;
        ExactPcmFormat resultantFormat;
    };

    [[nodiscard]] constexpr bool DataRangeChannelsIntersect(
        const UInt32 clientChannels,
        const UInt32 driverChannels) noexcept
    {
        return (clientChannels == FivePointOneChannels ||
                clientChannels == SevenPointOneChannels) &&
               clientChannels == driverChannels;
    }

    [[nodiscard]] constexpr DataRangeIntersectionResult
    IntersectExactSurroundDataRanges(
        const AudioDataRange& client,
        const AudioDataRange& miniport,
        const UInt32 outputBufferLength) noexcept
    {
        if (outputBufferLength == 0)
        {
            return {
                DataRangeIntersectionStatus::BufferOverflow,
                ExactDataFormatSize,
                {}};
        }
        if (outputBufferLength < ExactDataFormatSize)
        {
            return {
                DataRangeIntersectionStatus::BufferTooSmall,
                0,
                {}};
        }

        const auto supportsExactPcmPoint = [](const AudioDataRange& range)
        {
            return range.dataRangeSize == ExactAudioDataRangeSize &&
                   range.majorFormatAudio &&
                   range.dataSubformatPcm &&
                   range.waveFormatSpecifier &&
                   range.minimumBitsPerSample <= ExactBitsPerSample &&
                   range.maximumBitsPerSample >= ExactBitsPerSample &&
                   range.minimumBitsPerSample <=
                       range.maximumBitsPerSample &&
                   range.minimumSampleFrequency <= ExactSampleRate &&
                   range.maximumSampleFrequency >= ExactSampleRate &&
                   range.minimumSampleFrequency <=
                       range.maximumSampleFrequency;
        };

        if (!supportsExactPcmPoint(client) ||
            !supportsExactPcmPoint(miniport) ||
            !DataRangeChannelsIntersect(
                client.maximumChannels,
                miniport.maximumChannels))
        {
            return {
                DataRangeIntersectionStatus::NoMatch,
                0,
                {}};
        }

        const bool fivePointOne =
            client.maximumChannels == FivePointOneChannels;
        return {
            DataRangeIntersectionStatus::Success,
            ExactDataFormatSize,
            {
                ExactDataFormatSize,
                true,
                true,
                true,
                ExtensibleFormatTag,
                static_cast<UInt16>(client.maximumChannels),
                ExactSampleRate,
                fivePointOne
                    ? FivePointOneAverageBytesPerSecond
                    : SevenPointOneAverageBytesPerSecond,
                fivePointOne
                    ? FivePointOneBlockAlign
                    : SevenPointOneBlockAlign,
                ExactBitsPerSample,
                ExactExtensionSize,
                ExactBitsPerSample,
                fivePointOne
                    ? FivePointOneChannelMask
                    : SevenPointOneChannelMask,
                true}};
    }

    [[nodiscard]] constexpr SurroundLayout IdentifyExactPcmFormat(
        const ExactPcmFormat& value) noexcept
    {
        if (value.dataFormatSize != ExactDataFormatSize ||
            !value.majorFormatAudio ||
            !value.dataSubformatPcm ||
            !value.waveFormatSpecifier ||
            value.formatTag != ExtensibleFormatTag ||
            value.samplesPerSecond != ExactSampleRate ||
            value.bitsPerSample != ExactBitsPerSample ||
            value.extensionSize != ExactExtensionSize ||
            value.validBitsPerSample != ExactBitsPerSample ||
            !value.waveSubformatPcm)
        {
            return SurroundLayout::Unsupported;
        }

        if (value.channels == FivePointOneChannels &&
            value.averageBytesPerSecond ==
                FivePointOneAverageBytesPerSecond &&
            value.blockAlign == FivePointOneBlockAlign &&
            value.channelMask == FivePointOneChannelMask)
        {
            return SurroundLayout::FivePointOne;
        }

        if (value.channels == SevenPointOneChannels &&
            value.averageBytesPerSecond ==
                SevenPointOneAverageBytesPerSecond &&
            value.blockAlign == SevenPointOneBlockAlign &&
            value.channelMask == SevenPointOneChannelMask)
        {
            return SurroundLayout::SevenPointOne;
        }

        return SurroundLayout::Unsupported;
    }

    struct SharedFormatState
    {
        SurroundLayout device;
        SurroundLayout mix;
        SurroundLayout host;
        SurroundLayout offload;
        SurroundLayout loopback;
        UInt16 blockAlign;
        UInt64 loopbackWritePosition;
    };

    [[nodiscard]] constexpr SharedFormatState
    DefaultSharedFormatState() noexcept
    {
        return {
            SurroundLayout::SevenPointOne,
            SurroundLayout::SevenPointOne,
            SurroundLayout::SevenPointOne,
            SurroundLayout::SevenPointOne,
            SurroundLayout::SevenPointOne,
            SevenPointOneBlockAlign,
            0};
    }

    [[nodiscard]] constexpr bool TrySwitchSharedFormat(
        SharedFormatState& state,
        const SurroundLayout requested,
        const bool noStreamsOrCreations) noexcept
    {
        if (!noStreamsOrCreations ||
            (requested != SurroundLayout::FivePointOne &&
             requested != SurroundLayout::SevenPointOne))
        {
            return false;
        }

        state.device = requested;
        state.mix = requested;
        state.host = requested;
        state.offload = requested;
        state.loopback = requested;
        state.blockAlign = requested == SurroundLayout::FivePointOne
            ? FivePointOneBlockAlign
            : SevenPointOneBlockAlign;
        state.loopbackWritePosition = 0;
        return true;
    }
}
