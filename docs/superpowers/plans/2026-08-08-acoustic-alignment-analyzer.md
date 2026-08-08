# Acoustic Alignment Analyzer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Build an offline command-line tool that measures the front/rear onset difference in an external paired-click WAV recording and applies the approved 10 ms at 95% acceptance rule.

**Architecture:** A strict RIFF/WAVE reader converts PCM16 or float32 recordings into mono float samples. A matched-filter analyzer derives the known front and rear click signatures from TestPatternGenerator, detects paired events, calculates signed onset offsets, and reports percentile statistics without opening a microphone or audio endpoint.

**Tech Stack:** C++20, standard library, existing TestPatternGenerator, MSBuild/Visual Studio 2026 v145, Windows SDK 10.0.28000.0, dependency-free native tests.

## Global Constraints

- Execute this plan after Tasks 1 and 2 of the synchronized playback plan have created AudioTypes and TestPatternGenerator.
- Build only x64 Debug and Release configurations with PlatformToolset v145 and WindowsTargetPlatformVersion 10.0.28000.0.
- Accept uncompressed RIFF/WAVE PCM16 or IEEE float32 input at exactly 48,000 Hz with one or two channels.
- Never capture microphone audio; the user records WAV files with an external recorder or separate recording application.
- Derive reference signatures from the same deterministic TestPatternGenerator used by SoundStage Router.
- Require at least 20 detected paired clicks before producing a pass/fail result.
- Pass only when the absolute onset difference is at most 10 ms for at least 95% of detected pairs.
- Return process exit code 0 for PASS, 2 for FAIL, and 1 for invalid input or analysis error.
- Add no third-party runtime or test dependency.

## File map

- Create src/analysis/WavReader.h and .cpp for validated RIFF/WAVE parsing and mono conversion.
- Create src/analysis/AlignmentAnalyzer.h and .cpp for signature correlation, event detection, offsets, and percentiles.
- Create tools/AlignmentAnalyzerMain.cpp for the command-line contract.
- Create tests/analysis/WavReaderTests.cpp and AlignmentAnalyzerTests.cpp.
- Create SoundStageAlignmentAnalyzer.vcxproj and register it in SoundStageRouter.sln.
- Modify SoundStageRouter.Tests.vcxproj to compile analysis tests and implementation.
- Create docs/testing/hardware-acceptance.md for the two-recording protocol.
- Modify README.md to link the executable and protocol.

---

### Task 1: Strict WAV input and mono conversion

**Files:**
- Create: src/analysis/WavReader.h
- Create: src/analysis/WavReader.cpp
- Create: tests/analysis/WavReaderTests.cpp
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: a filesystem path to an external recording.
- Produces: WavRecording and ReadWavFile(const filesystem::path&).

- [ ] **Step 1: Write failing PCM16 and float32 reader tests**

Create small RIFF files in the test process temporary directory, including fmt, an unrelated JUNK chunk with odd-byte padding, and data. Assert PCM16 values -32768, 0, and 32767 map to -1.0, 0.0, and approximately 0.99997. Assert stereo float32 maps to mono by averaging channels. Cover both classic WAVE_FORMAT_PCM/WAVE_FORMAT_IEEE_FLOAT and WAVE_FORMAT_EXTENSIBLE with the matching PCM/IEEE-float subformat GUID. Add invalid tests for compressed format, 44.1 kHz, three channels, truncated chunk, and non-RIFF input.

~~~cpp
TEST(WavReader_AveragesStereoFloatAtFortyEightKilohertz)
{
    const auto path = WriteFloatWave({{0.5f, -0.5f}, {1.0f, 0.0f}}, 48000);
    const WavRecording recording = ReadWavFile(path);
    EXPECT_EQ(recording.sampleRate, 48000u);
    EXPECT_EQ(recording.monoSamples.size(), 2u);
    EXPECT_NEAR(recording.monoSamples[0], 0.0, 1e-7);
    EXPECT_NEAR(recording.monoSamples[1], 0.5, 1e-7);
}
~~~

- [ ] **Step 2: Run the test build and verify the reader is absent**

Expected: compilation fails because src/analysis/WavReader.h does not exist.

- [ ] **Step 3: Define the WAV result and explicit error**

~~~cpp
struct WavRecording
{
    std::uint32_t sampleRate = 0;
    std::vector<float> monoSamples;
};

class WavReadError : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

[[nodiscard]] WavRecording ReadWavFile(const std::filesystem::path& path);
~~~

- [ ] **Step 4: Implement bounded RIFF chunk parsing**

Read binary little-endian fields with helpers that first verify remaining file length. Require RIFF and WAVE IDs. Walk chunks until fmt and data are found, skipping unknown chunks plus one pad byte when chunkSize is odd. Accept WAVE_FORMAT_PCM with 16 bits, WAVE_FORMAT_IEEE_FLOAT with 32 bits, and WAVE_FORMAT_EXTENSIBLE only when its subformat GUID identifies one of those two encodings. Require one or two channels, sample rate 48000, and consistent blockAlign. Reject data whose size is not a multiple of blockAlign. Reserve the final mono vector once, decode each frame, average stereo, and reject non-finite float samples.

~~~cpp
using FourCc = std::array<char, 4>;
constexpr FourCc FmtChunk{'f', 'm', 't', ' '};
constexpr FourCc DataChunk{'d', 'a', 't', 'a'};
while (stream && (!formatFound || !dataFound))
{
    const FourCc chunkId = ReadFourCc(stream);
    const std::uint32_t chunkBytes = ReadU32(stream);
    const std::streampos chunkStart = stream.tellg();
    if (chunkId == FmtChunk) { format = ReadFormat(stream, chunkBytes); formatFound = true; }
    else if (chunkId == DataChunk) { dataOffset = chunkStart; dataBytes = chunkBytes; dataFound = true; }
    stream.seekg(chunkStart + static_cast<std::streamoff>(chunkBytes + (chunkBytes & 1u)));
}
if (!formatFound || !dataFound) throw WavReadError("missing fmt or data chunk");
if (format.sampleRate != 48000 || (format.channels != 1 && format.channels != 2))
    throw WavReadError("recording must be 48 kHz mono or stereo");
~~~

- [ ] **Step 5: Run all reader tests**

Expected: valid files decode to exact frame counts and every malformed fixture throws WavReadError with a specific reason.

- [ ] **Step 6: Commit the reader**

~~~powershell
git add src/analysis/WavReader.* tests/analysis/WavReaderTests.cpp SoundStageRouter.Tests.vcxproj
git commit -m "feat: read external alignment recordings"
~~~

---

### Task 2: Paired-click matched-filter analysis

**Files:**
- Create: src/analysis/AlignmentAnalyzer.h
- Create: src/analysis/AlignmentAnalyzer.cpp
- Create: tests/analysis/AlignmentAnalyzerTests.cpp
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: WavRecording and deterministic click frames from TestPatternGenerator.
- Produces: AlignmentResult and AnalyzeAlignment(const WavRecording&).

- [ ] **Step 1: Write synthetic offset and acceptance tests**

Build 30-second mono fixtures by rendering the first 240 frames of PairedClicks, placing the front signature at each whole second, and placing the rear signature at a controlled signed offset. Add deterministic low-amplitude noise.

~~~cpp
TEST(AlignmentAnalyzer_PassesFiveMillisecondOffset)
{
    const WavRecording recording = SyntheticPairedClicks(30, 240);
    const AlignmentResult result = AnalyzeAlignment(recording);
    EXPECT_EQ(result.detectedPairs, 30u);
    EXPECT_NEAR(result.percentile95AbsoluteMs, 5.0, 0.1);
    EXPECT_TRUE(result.passed);
}

TEST(AlignmentAnalyzer_FailsWhenMoreThanFivePercentExceedTenMilliseconds)
{
    std::vector<int> offsets(30, 240);
    offsets[0] = offsets[1] = offsets[2] = 720;
    const AlignmentResult result = AnalyzeAlignment(SyntheticPairedClicks(offsets));
    EXPECT_TRUE(!result.passed);
    EXPECT_TRUE(result.percentile95AbsoluteMs > 10.0);
}

TEST(AlignmentAnalyzer_RejectsTooFewDetectedPairs)
{
    const AlignmentResult result = AnalyzeAlignment(SyntheticPairedClicks(10, 0));
    EXPECT_TRUE(!result.valid);
    EXPECT_EQ(result.detectedPairs, 10u);
}
~~~

- [ ] **Step 2: Run tests to verify the analyzer is missing**

Expected: AlignmentAnalyzer.h not found.

- [ ] **Step 3: Define the result contract**

~~~cpp
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

[[nodiscard]] AlignmentResult AnalyzeAlignment(const WavRecording& recording);
~~~

- [ ] **Step 4: Derive and normalize both reference signatures**

Render 240 PairedClicks frames at master frame zero. Extract front.left and rear.left into separate arrays. Remove each signature's mean and divide by its Euclidean norm. Reject an unexpectedly zero norm as an internal analysis error.

~~~cpp
std::array<RoleFrame, 240> frames{};
TestPatternGenerator{}.Render(TestPattern::PairedClicks, 0, frames);
for (std::size_t index = 0; index < frames.size(); ++index)
{
    frontSignature[index] = frames[index].front.left;
    rearSignature[index] = frames[index].rear.left;
}
NormalizeZeroMean(frontSignature);
NormalizeZeroMean(rearSignature);
~~~

- [ ] **Step 5: Detect click events and per-role peaks**

Search the first two seconds for the largest sum of absolute normalized front and rear correlation. Treat it as the first event center. For each predicted one-second event thereafter, search ±2400 frames and choose the local maximum combined correlation. Reject a candidate below 0.35 normalized correlation or within 0.5 seconds of the prior accepted event.

Within ±720 frames of each accepted event center, independently choose the maximum absolute correlation position for the front signature and rear signature so microphone or speaker polarity inversion cannot move the detected onset. Record (rearIndex - frontIndex) * 1000 / 48000 as signedRearMinusFrontMs.

~~~cpp
double CorrelationAt(std::span<const float> samples, std::size_t offset,
                     std::span<const float> normalizedSignature)
{
    double mean = 0.0;
    for (std::size_t i = 0; i < normalizedSignature.size(); ++i)
        mean += samples[offset + i];
    mean /= normalizedSignature.size();
    double dot = 0.0;
    double energy = 0.0;
    for (std::size_t i = 0; i < normalizedSignature.size(); ++i)
    {
        const double centered = samples[offset + i] - mean;
        dot += centered * normalizedSignature[i];
        energy += centered * centered;
    }
    return energy == 0.0 ? 0.0 : dot / std::sqrt(energy);
}
~~~

- [ ] **Step 6: Calculate the exact pass rule**

Sort absolute offsets. Set percentile index to ceil(0.95 * count) - 1. Set valid only when count >= 20. Set passed only when valid and percentile95AbsoluteMs <= 10.0. Calculate medianSignedMs from a separately sorted signed-offset copy and maximumAbsoluteMs from the final absolute value.

~~~cpp
std::sort(absoluteOffsets.begin(), absoluteOffsets.end());
if (absoluteOffsets.empty())
{
    result.error = "no paired clicks detected";
    return result;
}
const std::size_t percentileIndex =
    static_cast<std::size_t>(std::ceil(0.95 * absoluteOffsets.size())) - 1;
result.detectedPairs = static_cast<std::uint32_t>(absoluteOffsets.size());
result.valid = result.detectedPairs >= 20;
result.percentile95AbsoluteMs = absoluteOffsets[percentileIndex];
result.maximumAbsoluteMs = absoluteOffsets.back();
result.passed = result.valid && result.percentile95AbsoluteMs <= 10.0;
~~~

- [ ] **Step 7: Run clean, noisy, early-rear, late-rear, and missing-event tests**

Expected: ±5 ms cases report the correct sign and pass; a 15 ms 95th percentile fails; fewer than 20 detections is invalid; fixed-seed noise at -35 dB relative to the click does not change detection count.

- [ ] **Step 8: Commit the analyzer core**

~~~powershell
git add src/analysis/AlignmentAnalyzer.* tests/analysis/AlignmentAnalyzerTests.cpp SoundStageRouter.Tests.vcxproj
git commit -m "feat: measure paired-click acoustic alignment"
~~~

---

### Task 3: Command-line tool and hardware acceptance protocol

**Files:**
- Create: tools/AlignmentAnalyzerMain.cpp
- Create: SoundStageAlignmentAnalyzer.vcxproj
- Modify: SoundStageRouter.sln
- Create: docs/testing/hardware-acceptance.md
- Modify: README.md

**Interfaces:**
- Consumes: one 48 kHz PCM16 or float32 WAV path.
- Produces: human-readable event statistics, PASS/FAIL, and process exit code 0/2/1.

- [ ] **Step 1: Add a command-line formatting test**

Extract FormatAlignmentReport(const filesystem::path&, const AlignmentResult&) into AlignmentAnalyzer.h/.cpp. Assert the report contains file name, detected pair count, signed median with Front leads or Rear leads wording, 95th percentile absolute offset, maximum absolute offset, threshold 10.00 ms, and final PASS or FAIL.

~~~cpp
TEST(AlignmentReport_ContainsDecisionEvidence)
{
    AlignmentResult result{true, true, 30, 5.0, 6.0, 7.0};
    const std::string report = FormatAlignmentReport("start.wav", result);
    EXPECT_TRUE(report.find("start.wav") != std::string::npos);
    EXPECT_TRUE(report.find("30") != std::string::npos);
    EXPECT_TRUE(report.find("Front leads by 5.00 ms") != std::string::npos);
    EXPECT_TRUE(report.find("95th percentile: 6.00 ms") != std::string::npos);
    EXPECT_TRUE(report.find("threshold: 10.00 ms") != std::string::npos);
    EXPECT_TRUE(report.find("PASS") != std::string::npos);
}
~~~

- [ ] **Step 2: Run tests and verify the formatter is absent**

Expected: compilation fails on FormatAlignmentReport.

- [ ] **Step 3: Implement the console contract**

~~~cpp
int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: SoundStageAlignmentAnalyzer <recording.wav>\n";
        return 1;
    }
    try
    {
        const std::filesystem::path path(argv[1]);
        const auto recording = soundstage::analysis::ReadWavFile(path);
        const auto result = soundstage::analysis::AnalyzeAlignment(recording);
        std::cout << soundstage::analysis::FormatAlignmentReport(path, result);
        if (!result.valid) return 1;
        return result.passed ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Analysis error: " << error.what() << '\n';
        return 1;
    }
}
~~~

- [ ] **Step 4: Add the analyzer project to the solution**

Create an x64 Console project using v145, C++20, and SDK 10.0.28000.0. Compile AlignmentAnalyzerMain.cpp, WavReader.cpp, AlignmentAnalyzer.cpp, and TestPatternGenerator.cpp. Output build/$(Configuration)/SoundStageAlignmentAnalyzer.exe. Add Debug and Release x64 mappings to SoundStageRouter.sln.

~~~xml
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>18.0</VCProjectVersion>
    <ProjectGuid>{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}</ProjectGuid>
    <WindowsTargetPlatformVersion>10.0.28000.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType><PlatformToolset>v145</PlatformToolset>
    <UseDebugLibraries>true</UseDebugLibraries><CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType><PlatformToolset>v145</PlatformToolset>
    <UseDebugLibraries>false</UseDebugLibraries><WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(SolutionDir)build\$(Configuration)\</OutDir>
    <IntDir>$(SolutionDir)build\analyzer\obj\$(Configuration)\</IntDir>
    <TargetName>SoundStageAlignmentAnalyzer</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level4</WarningLevel><SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode><LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(SolutionDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN;NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link><SubSystem>Console</SubSystem></Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="tools\AlignmentAnalyzerMain.cpp" />
    <ClCompile Include="src\analysis\WavReader.cpp" />
    <ClCompile Include="src\analysis\AlignmentAnalyzer.cpp" />
    <ClCompile Include="src\audio\TestPatternGenerator.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
~~~

Add the analyzer Project/EndProject block and these exact ProjectConfigurationPlatforms entries to SoundStageRouter.sln:

~~~text
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "SoundStageAlignmentAnalyzer", "SoundStageAlignmentAnalyzer.vcxproj", "{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}"
EndProject
{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}.Debug|x64.ActiveCfg = Debug|x64
{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}.Debug|x64.Build.0 = Debug|x64
{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}.Release|x64.ActiveCfg = Release|x64
{7D2548A4-E1A6-4C5A-B8DD-C17DBDA97D23}.Release|x64.Build.0 = Release|x64
~~~

- [ ] **Step 5: Write the hardware protocol**

docs/testing/hardware-acceptance.md must instruct the tester to:

1. Select Realtek Front and Bluetooth Rear, choose PairedClicks, and align manually.
2. Place one microphone at the listening position and keep it stationary.
3. Record at 48 kHz PCM16 or float32 for at least 30 seconds immediately after alignment.
4. Keep playback continuous for 30 minutes without changing endpoints or delay.
5. Record a second 30-second WAV without moving the microphone.
6. Run the analyzer on both files and retain the console output.
7. Accept the timing criterion only if both commands exit 0 and both reports show at least 20 pairs with 95th percentile <= 10 ms.
8. Separately record the live-delay listening check and Bluetooth disconnect/reconnect result from the playback plan.

- [ ] **Step 6: Run fresh build, unit, and CLI verification**

~~~powershell
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
.\build\Release\SoundStageAlignmentAnalyzer.exe
git diff --check
~~~

Expected: both rebuilds and both test suites exit 0; invoking the analyzer without a path prints usage and exits 1; git diff --check reports no errors.

- [ ] **Step 7: Analyze the two target recordings**

Run the Release analyzer separately for the start and 30-minute WAV files. Expected for acceptance: exit 0, at least 20 detected pairs, and 95th percentile absolute offset no greater than 10.00 ms in both reports.

- [ ] **Step 8: Commit the tool and protocol**

~~~powershell
git add tools/AlignmentAnalyzerMain.cpp SoundStageAlignmentAnalyzer.vcxproj SoundStageRouter.sln docs/testing/hardware-acceptance.md README.md src/analysis/AlignmentAnalyzer.* tests/analysis
git commit -m "feat: add objective acoustic alignment check"
~~~
