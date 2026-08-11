#include "../TestHarness.h"
#include "../../driver/SoundStageRouterVirtualAudio/EndpointsCommon/SoundStageSurroundContract.h"

using namespace soundstage::driver;

namespace
{
    ExactPcmFormat SevenPointOneFormat()
    {
        return {
            104,
            true,
            true,
            true,
            0xFFFE,
            8,
            48'000,
            1'536'000,
            32,
            32,
            22,
            32,
            0x063F,
            true};
    }

    ExactPcmFormat FivePointOneFormat()
    {
        ExactPcmFormat value = SevenPointOneFormat();
        value.channels = 6;
        value.averageBytesPerSecond = 1'152'000;
        value.blockAlign = 24;
        value.channelMask = 0x003F;
        return value;
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

TEST(DriverContract_AcceptsOnlyExactExtensiblePcmLayouts)
{
    EXPECT_EQ(IdentifyExactPcmFormat(SevenPointOneFormat()),
              SurroundLayout::SevenPointOne);
    EXPECT_EQ(IdentifyExactPcmFormat(FivePointOneFormat()),
              SurroundLayout::FivePointOne);

    ExactPcmFormat value = SevenPointOneFormat();
    value.dataFormatSize = 64;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.majorFormatAudio = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.dataSubformatPcm = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.waveFormatSpecifier = false;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.formatTag = 1;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.channels = 6;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.samplesPerSecond = 44'100;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.averageBytesPerSecond = 1'152'000;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.blockAlign = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.bitsPerSample = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.extensionSize = 0;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.validBitsPerSample = 24;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
    value.channelMask = 0x003F;
    EXPECT_EQ(IdentifyExactPcmFormat(value), SurroundLayout::Unsupported);
    value = SevenPointOneFormat();
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
