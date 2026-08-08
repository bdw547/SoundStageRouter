#pragma once

#include "AudioTypes.h"

namespace soundstage::audio
{
    // FL/FR outputs include FC at -3 dB and LFE at -6 dB. Native BL/BR
    // always win; rear fill is used only while both native rear channels
    // are silent.
    [[nodiscard]] RoleFrame RouteSurroundFrame(
        const SurroundFrame& input, RearFillMode rearFill) noexcept;
}
