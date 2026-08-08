#include "../src/analysis/AlignmentAnalyzer.h"
#include "../src/analysis/WavReader.h"

#include <filesystem>
#include <iostream>

int wmain(const int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr
            << L"Usage: SoundStageAlignmentAnalyzer <recording.wav>\n";
        return 1;
    }
    try
    {
        const std::filesystem::path path(argv[1]);
        const auto recording =
            soundstage::analysis::ReadWavFile(path);
        const auto result =
            soundstage::analysis::AnalyzeAlignment(recording);
        std::cout << soundstage::analysis::FormatAlignmentReport(
            path, result);
        if (!result.valid)
        {
            return 1;
        }
        return result.passed ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Analysis error: "
                  << error.what() << '\n';
        return 1;
    }
}
