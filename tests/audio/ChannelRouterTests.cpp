#include "../TestHarness.h"
#include "../../src/audio/ChannelRouter.h"

using namespace soundstage::audio;

TEST(ChannelRouter_AppliesDocumentedMatrixAndLimits)
{
    const RoleFrame result = RouteSurroundFrame(
        {0.2f, -0.2f, 0.4f, 0.2f, 0.7f, -0.8f},
        RearFillMode::Duplicate, {1.0f, 1.0f});
    EXPECT_NEAR(result.front.left, 0.5828427, 1e-6);
    EXPECT_NEAR(result.front.right, 0.1828427, 1e-6);
    EXPECT_NEAR(result.rear.left, 0.7, 1e-6);
    EXPECT_NEAR(result.rear.right, -0.8, 1e-6);

    const RoleFrame limited = RouteSurroundFrame(
        {1.0f, 1.0f, 1.0f, 1.0f, 2.0f, -2.0f},
        RearFillMode::Off, {1.0f, 1.0f});
    EXPECT_NEAR(limited.front.left, 1.0, 1e-7);
    EXPECT_NEAR(limited.rear.right, -1.0, 1e-7);
}

TEST(ChannelRouter_RearFillModesAreDeterministic)
{
    const SurroundFrame stereo{0.8f, 0.2f, 0, 0, 0, 0};
    const RoleFrame off =
        RouteSurroundFrame(stereo, RearFillMode::Off, {1.0f, 1.0f});
    EXPECT_NEAR(off.rear.left, 0.0, 1e-7);
    const RoleFrame duplicate =
        RouteSurroundFrame(stereo, RearFillMode::Duplicate, {1.0f, 1.0f});
    EXPECT_NEAR(duplicate.rear.left, 0.4009498, 1e-6);
    EXPECT_NEAR(duplicate.rear.right, 0.1002374, 1e-6);
    const RoleFrame ambient =
        RouteSurroundFrame(stereo, RearFillMode::Ambient, {1.0f, 1.0f});
    EXPECT_NEAR(ambient.rear.left, 0.3, 1e-6);
    EXPECT_NEAR(ambient.rear.right, -0.3, 1e-6);
}

TEST(ChannelRouter_MixesBackAndSideChannelsUsingConfiguredLevels)
{
    const SurroundFrame input{0, 0, 0, 0, 0.8f, -0.6f, 0.4f, 0.2f};
    const RoleFrame full = RouteSurroundFrame(
        input, RearFillMode::Off, {1.0f, 1.0f});
    EXPECT_NEAR(full.rear.left, 1.0, 1e-7);
    EXPECT_NEAR(full.rear.right, -0.4, 1e-7);

    const RoleFrame balanced = RouteSurroundFrame(
        input, RearFillMode::Off, {0.5f, 0.25f});
    EXPECT_NEAR(balanced.rear.left, 0.5, 1e-7);
    EXPECT_NEAR(balanced.rear.right, -0.25, 1e-7);
}

TEST(ChannelRouter_SideOnlyContentSuppressesRearFillBeforeGain)
{
    const SurroundFrame sideOnly{0.8f, 0.2f, 0, 0, 0, 0, 0.4f, -0.3f};

    const RoleFrame result = RouteSurroundFrame(
        sideOnly, RearFillMode::Duplicate, {1.0f, 0.0f});

    EXPECT_NEAR(result.rear.left, 0.0, 1e-7);
    EXPECT_NEAR(result.rear.right, 0.0, 1e-7);
}

TEST(ChannelRouter_ClampsBackAndSideLevelsBeforeMixing)
{
    const SurroundFrame input{0, 0, 0, 0, 0.5f, 0.0f, 0.0f, 0.5f};

    const RoleFrame result = RouteSurroundFrame(
        input, RearFillMode::Off, {-1.0f, 2.0f});

    EXPECT_NEAR(result.rear.left, 0.0, 1e-7);
    EXPECT_NEAR(result.rear.right, 0.5, 1e-7);

    const RoleFrame inverse = RouteSurroundFrame(
        input, RearFillMode::Off, {2.0f, -1.0f});

    EXPECT_NEAR(inverse.rear.left, 0.5, 1e-7);
    EXPECT_NEAR(inverse.rear.right, 0.0, 1e-7);
}
