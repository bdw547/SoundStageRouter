#include "../TestHarness.h"
#include "../../src/audio/VirtualSurroundContract.h"
#include "../../src/ui/CommandDeckTheme.h"

using soundstage::audio::VirtualSurroundFormat;
using soundstage::ui::ComputeCommandDeckLayout;
using soundstage::ui::HasValidPhysicalRouteSelection;
using soundstage::ui::IsCommandDeckFormatChange;
using soundstage::ui::IsCommandDeckUnsupportedFormatFault;

namespace
{
    int Height(const RECT& rect)
    {
        return rect.bottom - rect.top;
    }
}

TEST(CommandDeckLayout_UsesApprovedWideDashboardRegions)
{
    const auto layout = ComputeCommandDeckLayout(1240, 780, 96, false);

    EXPECT_TRUE(!layout.stacked);
    EXPECT_TRUE(!layout.detailsVisible);
    EXPECT_EQ(layout.header.left, 32L);
    EXPECT_EQ(layout.header.top, 32L);
    EXPECT_EQ(Height(layout.header), 72);
    EXPECT_EQ(layout.frontCard.left, 32L);
    EXPECT_EQ(layout.frontCard.right, 800L);
    EXPECT_EQ(layout.mixCard.left, 824L);
    EXPECT_EQ(layout.mixCard.right, 1208L);
    EXPECT_EQ(layout.frontCard.bottom + 24, layout.chairCard.top);
    EXPECT_EQ(layout.mixCard.top, layout.frontCard.top);
    EXPECT_EQ(layout.mixCard.bottom, layout.chairCard.bottom);
    EXPECT_EQ(layout.actionBar.top, 660L);
}

TEST(CommandDeckLayout_StacksMixBelowDeviceCardsAtMinimumWidth)
{
    const auto layout = ComputeCommandDeckLayout(720, 720, 96, false);

    EXPECT_TRUE(layout.stacked);
    EXPECT_EQ(layout.frontCard.left, 16L);
    EXPECT_EQ(layout.frontCard.right, 704L);
    EXPECT_EQ(layout.frontCard.bottom + 12, layout.chairCard.top);
    EXPECT_EQ(layout.chairCard.bottom + 12, layout.mixCard.top);
    EXPECT_TRUE(layout.mixCard.bottom < layout.actionBar.top);
    EXPECT_TRUE(Height(layout.frontCard) >= 132);
    EXPECT_TRUE(Height(layout.mixCard) >= 240);
}

TEST(CommandDeckLayout_PreservesDipGeometryAtTwoHundredPercent)
{
    const auto layout = ComputeCommandDeckLayout(2480, 1560, 192, false);

    EXPECT_TRUE(!layout.stacked);
    EXPECT_EQ(layout.header.left, 64L);
    EXPECT_EQ(layout.header.top, 64L);
    EXPECT_EQ(Height(layout.header), 144);
    EXPECT_EQ(layout.mixCard.left - layout.frontCard.right, 48L);
    EXPECT_EQ(layout.actionBar.top, 1320L);
    EXPECT_EQ(layout.minimumClientSize.cx, 1440L);
    EXPECT_EQ(layout.minimumClientSize.cy, 960L);
}

TEST(CommandDeckLayout_FloorsShortClientsToAScrollableCanvas)
{
    const auto layout = ComputeCommandDeckLayout(1240, 500, 96, true);

    EXPECT_EQ(layout.virtualHeight, 984);
    EXPECT_EQ(layout.actionBar.bottom, 952L);
    EXPECT_TRUE(Height(layout.frontCard) >= 212);
    EXPECT_TRUE(Height(layout.chairCard) >= 212);
    EXPECT_TRUE(layout.frontCard.bottom < layout.chairCard.top);
    EXPECT_TRUE(layout.chairCard.bottom <= layout.detailsCard.top - 24);
    EXPECT_TRUE(layout.detailsCard.bottom <= layout.actionBar.top - 24);

    const auto tall = ComputeCommandDeckLayout(1240, 1200, 96, true);
    EXPECT_EQ(tall.virtualHeight, 1200);
    EXPECT_EQ(tall.actionBar.bottom, 1168L);
}

TEST(CommandDeckLayout_ExpandedDetailsAddsPanelWithoutMovingMainCards)
{
    const auto collapsed = ComputeCommandDeckLayout(1240, 780, 96, false);
    const auto expanded = ComputeCommandDeckLayout(1240, 1044, 96, true);

    EXPECT_TRUE(expanded.detailsVisible);
    EXPECT_EQ(expanded.frontCard.top, collapsed.frontCard.top);
    EXPECT_EQ(expanded.frontCard.bottom, collapsed.frontCard.bottom);
    EXPECT_EQ(Height(expanded.detailsCard), 240);
    EXPECT_EQ(expanded.detailsCard.top, collapsed.actionBar.top);
    EXPECT_EQ(expanded.actionBar.top, 924L);
    EXPECT_EQ(expanded.preferredClientSize.cy, 1044L);
}

TEST(CommandDeckRoute_RequiresTwoDistinctPhysicalSelections)
{
    EXPECT_TRUE(HasValidPhysicalRouteSelection(0, 1));
    EXPECT_TRUE(!HasValidPhysicalRouteSelection(-1, 1));
    EXPECT_TRUE(!HasValidPhysicalRouteSelection(0, -1));
    EXPECT_TRUE(!HasValidPhysicalRouteSelection(2, 2));
}

TEST(CommandDeckRecovery_LabelsOnlyAnActualDetectedFormatChange)
{
    EXPECT_TRUE(IsCommandDeckFormatChange(
        true, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::FivePointOne));
    EXPECT_TRUE(!IsCommandDeckFormatChange(
        true, VirtualSurroundFormat::FivePointOne,
        VirtualSurroundFormat::FivePointOne));
    EXPECT_TRUE(!IsCommandDeckFormatChange(
        true, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::Unsupported));
    EXPECT_TRUE(!IsCommandDeckFormatChange(
        false, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::FivePointOne));
}

TEST(CommandDeckRecovery_GuidesAnUnsupportedDetectedFormatWithoutRestartLabel)
{
    EXPECT_TRUE(IsCommandDeckUnsupportedFormatFault(
        true, true, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::Unsupported));
    EXPECT_TRUE(IsCommandDeckUnsupportedFormatFault(
        true, true, VirtualSurroundFormat::FivePointOne,
        VirtualSurroundFormat::Unsupported));
    EXPECT_TRUE(!IsCommandDeckUnsupportedFormatFault(
        true, true, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::FivePointOne));
    EXPECT_TRUE(!IsCommandDeckUnsupportedFormatFault(
        false, true, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::Unsupported));
    EXPECT_TRUE(!IsCommandDeckUnsupportedFormatFault(
        true, false, VirtualSurroundFormat::SevenPointOne,
        VirtualSurroundFormat::Unsupported));
}
