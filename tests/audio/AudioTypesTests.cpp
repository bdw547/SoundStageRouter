#include "../TestHarness.h"
#include "../../src/audio/AudioTypes.h"

using namespace soundstage::audio;

TEST(AudioTypes_ClampsDelayToMilestoneRange)
{
    EXPECT_EQ(ClampDelayMs(-1), 0u);
    EXPECT_EQ(ClampDelayMs(750), 750u);
    EXPECT_EQ(ClampDelayMs(2500), 2000u);
}

TEST(AudioTypes_ConvertsMillisecondsOnTheMasterTimeline)
{
    EXPECT_EQ(MillisecondsToFrames(0), 0ull);
    EXPECT_EQ(MillisecondsToFrames(10), 480ull);
    EXPECT_EQ(MillisecondsToFrames(2000), 96000ull);
}

TEST(AudioTypes_DefaultConfigurationReferencesRear)
{
    EXPECT_EQ(RunConfiguration{}.clockReferenceRole, SpeakerRole::Rear);
}
