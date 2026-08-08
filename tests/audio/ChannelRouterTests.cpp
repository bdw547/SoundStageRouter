#include "../TestHarness.h"
#include "../../src/audio/ChannelRouter.h"

using namespace soundstage::audio;

TEST(ChannelRouter_AppliesDocumentedMatrixAndLimits)
{
    const RoleFrame result = RouteSurroundFrame(
        {0.2f, -0.2f, 0.4f, 0.2f, 0.7f, -0.8f},
        RearFillMode::Duplicate);
    EXPECT_NEAR(result.front.left, 0.5828427, 1e-6);
    EXPECT_NEAR(result.front.right, 0.1828427, 1e-6);
    EXPECT_NEAR(result.rear.left, 0.7, 1e-6);
    EXPECT_NEAR(result.rear.right, -0.8, 1e-6);

    const RoleFrame limited = RouteSurroundFrame(
        {1.0f, 1.0f, 1.0f, 1.0f, 2.0f, -2.0f},
        RearFillMode::Off);
    EXPECT_NEAR(limited.front.left, 1.0, 1e-7);
    EXPECT_NEAR(limited.rear.right, -1.0, 1e-7);
}

TEST(ChannelRouter_RearFillModesAreDeterministic)
{
    const SurroundFrame stereo{0.8f, 0.2f, 0, 0, 0, 0};
    const RoleFrame off =
        RouteSurroundFrame(stereo, RearFillMode::Off);
    EXPECT_NEAR(off.rear.left, 0.0, 1e-7);
    const RoleFrame duplicate =
        RouteSurroundFrame(stereo, RearFillMode::Duplicate);
    EXPECT_NEAR(duplicate.rear.left, 0.4009498, 1e-6);
    EXPECT_NEAR(duplicate.rear.right, 0.1002374, 1e-6);
    const RoleFrame ambient =
        RouteSurroundFrame(stereo, RearFillMode::Ambient);
    EXPECT_NEAR(ambient.rear.left, 0.3, 1e-6);
    EXPECT_NEAR(ambient.rear.right, -0.3, 1e-6);
}
