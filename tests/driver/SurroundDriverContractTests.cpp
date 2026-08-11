#include "../TestHarness.h"
#include "../../driver/SoundStageRouterVirtualAudio/EndpointsCommon/SoundStageSurroundContract.h"

using namespace soundstage::driver;

namespace
{
    AudioDataRange SevenPointOneRange()
    {
        return {
            88,
            true,
            true,
            true,
            8,
            32,
            32,
            48'000,
            48'000};
    }

    AudioDataRange FivePointOneRange()
    {
        AudioDataRange value = SevenPointOneRange();
        value.maximumChannels = 6;
        return value;
    }

    void ExpectNoIntersection(
        const AudioDataRange& client,
        const AudioDataRange& miniport)
    {
        const auto result = IntersectExactSurroundDataRanges(
            client, miniport, 104);
        EXPECT_EQ(result.status, DataRangeIntersectionStatus::NoMatch);
        EXPECT_EQ(result.resultantFormatLength, 0u);
    }

    void ExpectSevenPointOneDescriptor(const ExactPcmFormat& value)
    {
        EXPECT_EQ(value.dataFormatSize, 104u);
        EXPECT_TRUE(value.majorFormatAudio);
        EXPECT_TRUE(value.dataSubformatPcm);
        EXPECT_TRUE(value.waveFormatSpecifier);
        EXPECT_EQ(value.formatTag, 0xFFFEu);
        EXPECT_EQ(value.channels, 8u);
        EXPECT_EQ(value.samplesPerSecond, 48'000u);
        EXPECT_EQ(value.averageBytesPerSecond, 1'536'000u);
        EXPECT_EQ(value.blockAlign, 32u);
        EXPECT_EQ(value.bitsPerSample, 32u);
        EXPECT_EQ(value.extensionSize, 22u);
        EXPECT_EQ(value.validBitsPerSample, 32u);
        EXPECT_EQ(value.channelMask, 0x063Fu);
        EXPECT_TRUE(value.waveSubformatPcm);
    }
}

TEST(DriverContract_IntersectsBothExactChannelRanges)
{
    EXPECT_TRUE(DataRangeChannelsIntersect(8, 8));
    EXPECT_TRUE(DataRangeChannelsIntersect(6, 6));
    EXPECT_TRUE(!DataRangeChannelsIntersect(6, 8));
    EXPECT_TRUE(!DataRangeChannelsIntersect(8, 6));
    EXPECT_TRUE(!DataRangeChannelsIntersect(2, 2));
}

TEST(DriverContract_ProducesExactSevenPointOneIntersection)
{
    const auto result = IntersectExactSurroundDataRanges(
        SevenPointOneRange(), SevenPointOneRange(), 104);

    EXPECT_EQ(result.status, DataRangeIntersectionStatus::Success);
    EXPECT_EQ(result.resultantFormatLength, 104u);
    ExpectSevenPointOneDescriptor(result.resultantFormat);
}

TEST(DriverContract_ProducesExactFivePointOneIntersection)
{
    const auto result = IntersectExactSurroundDataRanges(
        FivePointOneRange(), FivePointOneRange(), 104);

    EXPECT_EQ(result.status, DataRangeIntersectionStatus::Success);
    EXPECT_EQ(result.resultantFormatLength, 104u);
    EXPECT_EQ(result.resultantFormat.dataFormatSize, 104u);
    EXPECT_TRUE(result.resultantFormat.majorFormatAudio);
    EXPECT_TRUE(result.resultantFormat.dataSubformatPcm);
    EXPECT_TRUE(result.resultantFormat.waveFormatSpecifier);
    EXPECT_EQ(result.resultantFormat.formatTag, 0xFFFEu);
    EXPECT_EQ(result.resultantFormat.channels, 6u);
    EXPECT_EQ(result.resultantFormat.samplesPerSecond, 48'000u);
    EXPECT_EQ(result.resultantFormat.averageBytesPerSecond, 1'152'000u);
    EXPECT_EQ(result.resultantFormat.blockAlign, 24u);
    EXPECT_EQ(result.resultantFormat.bitsPerSample, 32u);
    EXPECT_EQ(result.resultantFormat.extensionSize, 22u);
    EXPECT_EQ(result.resultantFormat.validBitsPerSample, 32u);
    EXPECT_EQ(result.resultantFormat.channelMask, 0x003Fu);
    EXPECT_TRUE(result.resultantFormat.waveSubformatPcm);
}

TEST(DriverContract_ReportsPortClsIntersectionBufferStatuses)
{
    const auto sizeQuery = IntersectExactSurroundDataRanges(
        SevenPointOneRange(), SevenPointOneRange(), 0);
    EXPECT_EQ(sizeQuery.status,
              DataRangeIntersectionStatus::BufferOverflow);
    EXPECT_EQ(sizeQuery.resultantFormatLength, 104u);

    const auto undersized = IntersectExactSurroundDataRanges(
        SevenPointOneRange(), SevenPointOneRange(), 103);
    EXPECT_EQ(undersized.status,
              DataRangeIntersectionStatus::BufferTooSmall);
    EXPECT_EQ(undersized.resultantFormatLength, 0u);
}

TEST(DriverContract_RejectsMismatchedOrUnsupportedChannelRanges)
{
    ExpectNoIntersection(SevenPointOneRange(), FivePointOneRange());
    ExpectNoIntersection(FivePointOneRange(), SevenPointOneRange());

    AudioDataRange unsupported = SevenPointOneRange();
    unsupported.maximumChannels = 2;
    ExpectNoIntersection(unsupported, unsupported);
}

TEST(DriverContract_RejectsUnsupportedClientOrMiniportAudioRanges)
{
    const AudioDataRange valid = SevenPointOneRange();
    AudioDataRange unsupported = valid;

    unsupported.majorFormatAudio = false;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.dataSubformatPcm = false;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.waveFormatSpecifier = false;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.minimumBitsPerSample = 24;
    unsupported.maximumBitsPerSample = 24;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.minimumSampleFrequency = 44'100;
    unsupported.maximumSampleFrequency = 44'100;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.minimumBitsPerSample = 33;
    unsupported.maximumBitsPerSample = 32;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);

    unsupported = valid;
    unsupported.minimumSampleFrequency = 48'001;
    unsupported.maximumSampleFrequency = 48'000;
    ExpectNoIntersection(unsupported, valid);
    ExpectNoIntersection(valid, unsupported);
}

TEST(DriverContract_RejectsMalformedClientOrMiniportRangeSizes)
{
    const AudioDataRange valid = SevenPointOneRange();
    AudioDataRange malformed = valid;
    malformed.dataRangeSize = 64;
    ExpectNoIntersection(malformed, valid);

    malformed = valid;
    malformed.dataRangeSize = 85;
    ExpectNoIntersection(valid, malformed);
}

TEST(DriverContract_EachProcessingRangeIsImmediatelyFollowedByAttributes)
{
    constexpr DataRangePointerKind correct[] = {
        DataRangePointerKind::AudioWithAttributes,
        DataRangePointerKind::AttributeList,
        DataRangePointerKind::AudioWithAttributes,
        DataRangePointerKind::AttributeList};
    constexpr DataRangePointerKind missingFirstAttributes[] = {
        DataRangePointerKind::AudioWithAttributes,
        DataRangePointerKind::AudioWithAttributes,
        DataRangePointerKind::AttributeList};

    EXPECT_TRUE(AttributesFollowEveryFlaggedRange(correct));
    EXPECT_TRUE(!AttributesFollowEveryFlaggedRange(
        missingFirstAttributes));
}

TEST(DriverContract_RejectsMutationOfEveryProducedValidatorField)
{
    const auto sevenPointOne = IntersectExactSurroundDataRanges(
        SevenPointOneRange(), SevenPointOneRange(), 104);
    const auto fivePointOne = IntersectExactSurroundDataRanges(
        FivePointOneRange(), FivePointOneRange(), 104);

    EXPECT_EQ(IdentifyExactPcmFormat(sevenPointOne.resultantFormat),
              SurroundLayout::SevenPointOne);
    EXPECT_EQ(IdentifyExactPcmFormat(fivePointOne.resultantFormat),
              SurroundLayout::FivePointOne);

    ExactPcmFormat value = sevenPointOne.resultantFormat;
    value.dataFormatSize = 64;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.majorFormatAudio = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.dataSubformatPcm = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.waveFormatSpecifier = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.formatTag = 1;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.channels = 6;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.samplesPerSecond = 44'100;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.averageBytesPerSecond = 1'152'000;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.blockAlign = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.bitsPerSample = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.extensionSize = 0;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.validBitsPerSample = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.channelMask = 0x003F;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = sevenPointOne.resultantFormat;
    value.waveSubformatPcm = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
}

TEST(DriverContract_SwitchesEverySharedLayoutAndResetsLoopback)
{
    SharedFormatState state = DefaultSharedFormatState();
    state.loopbackWritePosition = 9'216;

    EXPECT_TRUE(TrySwitchSharedFormat(
        state, SurroundLayout::FivePointOne, true));
    EXPECT_EQ(state.device, SurroundLayout::FivePointOne);
    EXPECT_EQ(state.mix, SurroundLayout::FivePointOne);
    EXPECT_EQ(state.host, SurroundLayout::FivePointOne);
    EXPECT_EQ(state.offload, SurroundLayout::FivePointOne);
    EXPECT_EQ(state.loopback, SurroundLayout::FivePointOne);
    EXPECT_EQ(state.blockAlign, 24u);
    EXPECT_EQ(state.loopbackWritePosition, 0u);

    state.loopbackWritePosition = 12'288;
    EXPECT_TRUE(TrySwitchSharedFormat(
        state, SurroundLayout::SevenPointOne, true));
    EXPECT_EQ(state.device, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.mix, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.host, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.offload, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.loopback, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.blockAlign, 32u);
    EXPECT_EQ(state.loopbackWritePosition, 0u);
}

TEST(DriverContract_RejectsSwitchWhileStreamsExistWithoutMutation)
{
    SharedFormatState state = DefaultSharedFormatState();
    state.loopbackWritePosition = 4'096;

    EXPECT_TRUE(!TrySwitchSharedFormat(
        state, SurroundLayout::FivePointOne, false));
    EXPECT_EQ(state.device, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.mix, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.host, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.offload, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.loopback, SurroundLayout::SevenPointOne);
    EXPECT_EQ(state.blockAlign, 32u);
    EXPECT_EQ(state.loopbackWritePosition, 4'096u);
}

TEST(DriverContract_ReservesStreamCapacityAtomically)
{
    StreamCapacityState capacity{0, 1};

    EXPECT_EQ(TryReserveStreamSlot(capacity, false),
              StreamReservationResult::Reserved);
    EXPECT_EQ(capacity.allocated, 1u);
    EXPECT_EQ(TryReserveStreamSlot(capacity, false),
              StreamReservationResult::NoCapacity);
    EXPECT_EQ(capacity.allocated, 1u);

    FinishStreamReservation(capacity, false);
    EXPECT_EQ(capacity.allocated, 0u);
    EXPECT_EQ(TryReserveStreamSlot(capacity, true),
              StreamReservationResult::FormatSwitchInProgress);
    EXPECT_EQ(capacity.allocated, 0u);
}
