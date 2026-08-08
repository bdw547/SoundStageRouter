#pragma once

#include "WavReader.h"

#include <cstdint>
#include <string>
#include <vector>

namespace soundstage::analysis
{
    struct AlignmentPair
    {
        double eventTimeSeconds = 0.0;
        double signedRearMinusFrontMs = 0.0;
    };

    struct AlignmentResult
    {
        bool valid = false;
        bool passed = false;
        std::uint32_t detectedPairs = 0;
        double medianSignedMs = 0.0;
        double percentile95AbsoluteMs = 0.0;
        double maximumAbsoluteMs = 0.0;
        std::vector<AlignmentPair> pairs;
        std::string error;
    };

    [[nodiscard]] AlignmentResult AnalyzeAlignment(
        const WavRecording& recording);
}
