#include "../TestHarness.h"
#include "../../src/audio/MasterFrameRingBuffer.h"

using namespace soundstage::audio;

TEST(MasterRing_WrapsAndGivesBothReadersSameSequence)
{
    MasterFrameRingBuffer ring(2);
    RoleFrame first{{1, 2}, {3, 4}};
    RoleFrame second{{5, 6}, {7, 8}};
    EXPECT_TRUE(ring.Push(first));
    EXPECT_TRUE(ring.Push(second));
    for (std::size_t reader = 0; reader < 2; ++reader)
    {
        RoleFrame frame;
        std::uint64_t sequence = 99;
        EXPECT_TRUE(ring.Read(reader, frame, sequence));
        EXPECT_EQ(sequence, 0u);
        EXPECT_NEAR(frame.front.left, 1.0, 1e-7);
        EXPECT_TRUE(ring.Read(reader, frame, sequence));
        EXPECT_EQ(sequence, 1u);
    }
    EXPECT_TRUE(ring.Push(first));
    RoleFrame frame;
    std::uint64_t sequence = 0;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_EQ(sequence, 2u);
}

TEST(MasterRing_OverflowDoesNotDesynchronizeReaders)
{
    MasterFrameRingBuffer ring(1);
    EXPECT_TRUE(ring.Push({{1, 1}, {2, 2}}));
    EXPECT_TRUE(!ring.Push({{3, 3}, {4, 4}}));
    EXPECT_EQ(ring.OverflowCount(), 1u);
    RoleFrame front;
    RoleFrame rear;
    std::uint64_t frontSequence = 9;
    std::uint64_t rearSequence = 9;
    EXPECT_TRUE(ring.Read(0, front, frontSequence));
    EXPECT_TRUE(ring.Read(1, rear, rearSequence));
    EXPECT_EQ(frontSequence, rearSequence);
    EXPECT_NEAR(front.rear.left, rear.rear.left, 1e-7);
    EXPECT_TRUE(!ring.Read(0, front, frontSequence));
    EXPECT_EQ(ring.UnderrunCount(0), 1u);
}
