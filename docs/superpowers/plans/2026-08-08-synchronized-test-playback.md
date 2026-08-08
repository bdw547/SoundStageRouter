# Synchronized Test Playback Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (\`- [ ]\`) syntax for tracking.

**Goal:** Add generated front/rear test playback that renders to two shared-mode WASAPI endpoints, supports live manual delay, and holds their clock relationship with bounded drift correction.

**Architecture:** Pure C++20 DSP components generate role-addressed master frames, slew manual delay, resample, and convert into each endpoint mix format. A deterministic engine state machine coordinates two injected endpoint sessions, while the production session owns one event-driven WASAPI worker and publishes fixed-size telemetry to a non-render control thread and Win32 UI.

**Tech Stack:** C++20, Win32, MMDevice API, shared-mode event-driven WASAPI, WRL ComPtr, MSBuild/Visual Studio 2026 v145, Windows SDK 10.0.28000.0, dependency-free native test executable.

## Global Constraints

- Build only x64 Debug and Release configurations with PlatformToolset v145 and WindowsTargetPlatformVersion 10.0.28000.0.
- Keep the master timeline at exactly 48,000 Hz with 32-bit floating-point frames.
- Support exactly two UI roles in this milestone: Front and Rear; internal run configuration uses role/endpoint collections.
- Clamp persisted and live manual delays to the inclusive range 0–2000 ms.
- Use shared-mode, event-driven WASAPI; do not add exclusive mode.
- Generate test audio only; do not open capture endpoints, perform loopback capture, route system audio, decode media, or add a virtual device.
- Never allocate, log, write files, format strings, touch HWND values, or take a contended application mutex inside a render callback.
- Treat the Rear/Bluetooth session as the default clock reference, but represent the reference role explicitly in RunConfiguration.
- Express coordinator timestamps and common-start targets as unsigned 100 ns units derived from QueryPerformanceCounter/QueryPerformanceFrequency.
- Apply drift correction only as a bounded, smoothed fractional-resampling adjustment; manual delay remains the authority for acoustic alignment.
- A fault that invalidates one endpoint stops both endpoint sessions and never auto-resumes after reconnection.
- Preserve current user settings in %LOCALAPPDATA%\SoundStageRouter\routing.ini.

## File map

- Create src/audio/AudioTypes.h for shared constants, roles, patterns, run configuration, telemetry, and faults.
- Create src/audio/TestPatternGenerator.h and .cpp for deterministic random-access master frames.
- Create src/audio/DelayLine.h and .cpp for allocation-free, slew-limited live delay.
- Create src/audio/AdaptiveResampler.h and .cpp for streaming fractional resampling.
- Create src/audio/EndpointConverter.h and .cpp for endpoint layout and sample-format conversion.
- Create src/audio/EndpointPipeline.h and .cpp to compose generator, role selection, delay, resampling, and conversion.
- Create src/audio/ClockSynchronizer.h and .cpp for relative-rate estimation and bounded correction.
- Create src/audio/EndpointSession.h for the injected endpoint-session boundary.
- Create src/audio/EngineController.h and .cpp for the deterministic playback state machine.
- Create src/audio/AudioEngineCoordinator.h and .cpp for the non-UI command thread.
- Create src/audio/WasapiEndpointSession.h and .cpp for the production render worker.
- Create tests/TestHarness.h and tests/TestMain.cpp for the dependency-free test runner.
- Create one focused test source beside each component under tests/audio.
- Modify RouterSettings.h/.cpp to persist TestPattern.
- Modify AppWindow.h/.cpp to add playback controls, live status, and coordinator ownership.
- Modify SoundStageRouter.vcxproj and SoundStageRouter.sln as sources and the test target are added.
- Modify README.md with build, test, and hardware-boundary instructions.

---

### Task 1: Shared audio contracts and native test target

**Files:**
- Create: src/audio/AudioTypes.h
- Create: tests/TestHarness.h
- Create: tests/TestMain.cpp
- Create: tests/audio/AudioTypesTests.cpp
- Create: SoundStageRouter.Tests.vcxproj
- Modify: SoundStageRouter.sln

**Interfaces:**
- Produces: soundstage::audio::SpeakerRole, TestPattern, StereoFrame, RoleFrame, PlaybackState, ClockHealth, RunConfiguration, EndpointTelemetry, EngineStatus, EngineFault, ClampDelayMs(), and MillisecondsToFrames().
- Consumes: no new project interfaces.

- [ ] **Step 1: Write the first contract tests**

Create tests/audio/AudioTypesTests.cpp:

~~~cpp
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
~~~

- [ ] **Step 2: Run the missing test target to confirm the red state**

Run:

~~~powershell
msbuild SoundStageRouter.Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
~~~

Expected: FAIL with MSB1009 because SoundStageRouter.Tests.vcxproj does not exist.

- [ ] **Step 3: Add the dependency-free test harness**

Create tests/TestHarness.h and tests/TestMain.cpp with this registry shape:

~~~cpp
// tests/TestHarness.h
#pragma once
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace test
{
    using Function = void (*)();
    inline std::vector<std::pair<std::string, Function>>& Registry()
    {
        static std::vector<std::pair<std::string, Function>> tests;
        return tests;
    }

    struct Registrar
    {
        Registrar(const char* name, Function function)
        {
            Registry().emplace_back(name, function);
        }
    };

    template <typename Actual, typename Expected>
    void ExpectEqual(const Actual& actual, const Expected& expected,
                     const char* actualText, const char* expectedText)
    {
        if (!(actual == expected))
        {
            throw std::runtime_error(std::string(actualText) + " != " + expectedText);
        }
    }

    inline void ExpectNear(double actual, double expected, double tolerance)
    {
        if (std::abs(actual - expected) > tolerance)
        {
            throw std::runtime_error("values differ outside tolerance");
        }
    }
}

#define TEST(name) \
    static void name(); \
    static test::Registrar name##_registration(#name, &name); \
    static void name()
#define EXPECT_EQ(actual, expected) test::ExpectEqual((actual), (expected), #actual, #expected)
#define EXPECT_NEAR(actual, expected, tolerance) test::ExpectNear((actual), (expected), (tolerance))
#define EXPECT_TRUE(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)
~~~

~~~cpp
// tests/TestMain.cpp
#include "TestHarness.h"

int main()
{
    int failures = 0;
    for (const auto& [name, function] : test::Registry())
    {
        try
        {
            function();
            std::cout << "PASS " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "FAIL " << name << ": " << error.what() << '\n';
        }
    }
    std::cout << test::Registry().size() - failures << "/"
              << test::Registry().size() << " passed\n";
    return failures == 0 ? 0 : 1;
}
~~~

- [ ] **Step 4: Define the shared contracts**

Create src/audio/AudioTypes.h with these public names and defaults:

~~~cpp
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace soundstage::audio
{
    inline constexpr std::uint32_t MasterSampleRate = 48000;
    inline constexpr std::uint32_t MaximumDelayMs = 2000;
    inline constexpr double MaximumCorrectionPpm = 500.0;

    enum class SpeakerRole { Front, Rear };
    enum class TestPattern { PairedClicks, AlternatingClicks, FrontTone, RearTone };
    enum class PlaybackState { Stopped, Preparing, Primed, Running, Stopping, Faulted };
    enum class ClockHealth { Settling, Active, Unavailable };

    struct StereoFrame { float left = 0.0f; float right = 0.0f; };
    struct RoleFrame { StereoFrame front{}; StereoFrame rear{}; };

    struct EndpointRoute
    {
        SpeakerRole role = SpeakerRole::Front;
        std::wstring endpointId;
        std::uint32_t delayMs = 0;
        bool isClockReference = false;
    };

    struct RunConfiguration
    {
        TestPattern pattern = TestPattern::PairedClicks;
        SpeakerRole clockReferenceRole = SpeakerRole::Rear;
        std::vector<EndpointRoute> routes;
    };

    struct ClockSnapshot
    {
        std::uint64_t devicePosition = 0;
        std::uint64_t deviceFrequency = 0;
        std::uint64_t qpc100ns = 0;
        std::uint32_t paddingFrames = 0;
        bool available = false;
    };

    struct EndpointTelemetry
    {
        SpeakerRole role = SpeakerRole::Front;
        bool prepared = false;
        bool running = false;
        std::uint32_t sampleRate = 0;
        std::uint16_t channels = 0;
        double bufferDurationMs = 0.0;
        std::uint32_t delayMs = 0;
        std::uint64_t underrunCount = 0;
        ClockSnapshot clock{};
        std::uint32_t faultCode = 0;
    };

    struct EngineFault
    {
        std::uint32_t code = 0;
        SpeakerRole role = SpeakerRole::Front;
        std::wstring message;
    };

    struct EngineStatus
    {
        PlaybackState state = PlaybackState::Stopped;
        ClockHealth clockHealth = ClockHealth::Settling;
        double relativePpm = 0.0;
        double correctionPpm = 0.0;
        std::array<EndpointTelemetry, 2> endpoints{};
        EngineFault lastFault{};
    };

    [[nodiscard]] constexpr std::uint32_t ClampDelayMs(const int value) noexcept
    {
        return value < 0 ? 0u :
               value > static_cast<int>(MaximumDelayMs) ? MaximumDelayMs :
               static_cast<std::uint32_t>(value);
    }

    [[nodiscard]] constexpr std::uint64_t MillisecondsToFrames(
        const std::uint32_t milliseconds) noexcept
    {
        return static_cast<std::uint64_t>(milliseconds) * MasterSampleRate / 1000;
    }
}
~~~

- [ ] **Step 5: Create and register the test project**

Create SoundStageRouter.Tests.vcxproj as an x64 Console application using v145, C++20, and SDK 10.0.28000.0. Include tests/TestMain.cpp and tests/audio/AudioTypesTests.cpp, set AdditionalIncludeDirectories to the repository root, and write outputs to build/tests/$(Configuration). Add the project to SoundStageRouter.sln with a new GUID and Debug/Release x64 mappings.

~~~xml
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup Label="ProjectConfigurations">
    <ProjectConfiguration Include="Debug|x64"><Configuration>Debug</Configuration><Platform>x64</Platform></ProjectConfiguration>
    <ProjectConfiguration Include="Release|x64"><Configuration>Release</Configuration><Platform>x64</Platform></ProjectConfiguration>
  </ItemGroup>
  <PropertyGroup Label="Globals">
    <VCProjectVersion>18.0</VCProjectVersion>
    <ProjectGuid>{93A64544-B10B-4DC5-B52E-B19B2655AF10}</ProjectGuid>
    <WindowsTargetPlatformVersion>10.0.28000.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  <PropertyGroup Condition="'$(Configuration)'=='Debug'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType><UseDebugLibraries>true</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset><CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <PropertyGroup Condition="'$(Configuration)'=='Release'" Label="Configuration">
    <ConfigurationType>Application</ConfigurationType><UseDebugLibraries>false</UseDebugLibraries>
    <PlatformToolset>v145</PlatformToolset><WholeProgramOptimization>true</WholeProgramOptimization>
    <CharacterSet>Unicode</CharacterSet>
  </PropertyGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  <PropertyGroup>
    <OutDir>$(SolutionDir)build\tests\$(Configuration)\</OutDir>
    <IntDir>$(SolutionDir)build\tests\obj\$(Configuration)\</IntDir>
    <TargetName>SoundStageRouter.Tests</TargetName>
  </PropertyGroup>
  <ItemDefinitionGroup>
    <ClCompile>
      <WarningLevel>Level4</WarningLevel><SDLCheck>true</SDLCheck>
      <ConformanceMode>true</ConformanceMode><LanguageStandard>stdcpp20</LanguageStandard>
      <AdditionalIncludeDirectories>$(SolutionDir);%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
      <PreprocessorDefinitions>SOUNDSTAGE_TESTING;UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN;NOMINMAX;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    </ClCompile>
    <Link><SubSystem>Console</SubSystem></Link>
  </ItemDefinitionGroup>
  <ItemGroup>
    <ClCompile Include="tests\TestMain.cpp" />
    <ClCompile Include="tests\audio\AudioTypesTests.cpp" />
  </ItemGroup>
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
</Project>
~~~

~~~text
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "SoundStageRouter.Tests", "SoundStageRouter.Tests.vcxproj", "{93A64544-B10B-4DC5-B52E-B19B2655AF10}"
EndProject
~~~

Add these entries under GlobalSection(ProjectConfigurationPlatforms):

~~~text
{93A64544-B10B-4DC5-B52E-B19B2655AF10}.Debug|x64.ActiveCfg = Debug|x64
{93A64544-B10B-4DC5-B52E-B19B2655AF10}.Debug|x64.Build.0 = Debug|x64
{93A64544-B10B-4DC5-B52E-B19B2655AF10}.Release|x64.ActiveCfg = Release|x64
{93A64544-B10B-4DC5-B52E-B19B2655AF10}.Release|x64.Build.0 = Release|x64
~~~

- [ ] **Step 6: Build and run the contract tests**

Run:

~~~powershell
msbuild SoundStageRouter.Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
~~~

Expected: build exit 0 and 2/2 passed.

- [ ] **Step 7: Commit the contracts and test target**

~~~powershell
git add SoundStageRouter.sln SoundStageRouter.Tests.vcxproj src/audio/AudioTypes.h tests
git commit -m "test: establish native audio contracts"
~~~

---

### Task 2: Deterministic role-addressed test signals

**Files:**
- Create: src/audio/TestPatternGenerator.h
- Create: src/audio/TestPatternGenerator.cpp
- Create: tests/audio/TestPatternGeneratorTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: TestPattern, RoleFrame, and MasterSampleRate from src/audio/AudioTypes.h.
- Produces: TestPatternGenerator::Render(TestPattern, uint64_t, span<RoleFrame>) noexcept.

- [ ] **Step 1: Add failing generator tests**

Create tests/audio/TestPatternGeneratorTests.cpp:

~~~cpp
#include "../TestHarness.h"
#include "../../src/audio/TestPatternGenerator.h"
#include <array>
#include <cmath>

using namespace soundstage::audio;

TEST(TestPattern_PairedClickContainsDistinctFrontAndRearEnergy)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 240> frames{};
    generator.Render(TestPattern::PairedClicks, 0, frames);
    double frontEnergy = 0.0;
    double rearEnergy = 0.0;
    double difference = 0.0;
    for (const auto& frame : frames)
    {
        frontEnergy += frame.front.left * frame.front.left;
        rearEnergy += frame.rear.left * frame.rear.left;
        difference += std::abs(frame.front.left - frame.rear.left);
    }
    EXPECT_TRUE(frontEnergy > 0.01);
    EXPECT_TRUE(rearEnergy > 0.01);
    EXPECT_TRUE(difference > 0.1);
}

TEST(TestPattern_AlternatesRolesOncePerSecond)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 240> first{};
    std::array<RoleFrame, 240> second{};
    generator.Render(TestPattern::AlternatingClicks, 0, first);
    generator.Render(TestPattern::AlternatingClicks, MasterSampleRate, second);
    EXPECT_TRUE(std::abs(first[10].front.left) > 0.0f);
    EXPECT_EQ(first[10].rear.left, 0.0f);
    EXPECT_EQ(second[10].front.left, 0.0f);
    EXPECT_TRUE(std::abs(second[10].rear.left) > 0.0f);
}

TEST(TestPattern_RandomAccessMatchesContiguousRendering)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 512> contiguous{};
    std::array<RoleFrame, 256> tail{};
    generator.Render(TestPattern::PairedClicks, 0, contiguous);
    generator.Render(TestPattern::PairedClicks, 64, tail);
    for (std::size_t index = 0; index < tail.size(); ++index)
    {
        EXPECT_NEAR(tail[index].front.left, contiguous[index + 64].front.left, 1e-7);
        EXPECT_NEAR(tail[index].rear.left, contiguous[index + 64].rear.left, 1e-7);
    }
}

TEST(TestPattern_ToneIsBoundedFadedAndRoleSpecific)
{
    TestPatternGenerator generator;
    std::array<RoleFrame, 480> fadeIn{};
    std::array<RoleFrame, 1> off{};
    generator.Render(TestPattern::FrontTone, 0, fadeIn);
    generator.Render(TestPattern::FrontTone, 24000, off);
    EXPECT_NEAR(fadeIn.front().front.left, 0.0, 1e-7);
    for (const auto& frame : fadeIn)
    {
        EXPECT_TRUE(std::abs(frame.front.left) <= 0.25f);
        EXPECT_NEAR(frame.rear.left, 0.0, 1e-7);
    }
    EXPECT_NEAR(off.front().front.left, 0.0, 1e-7);
}
~~~

- [ ] **Step 2: Build to verify the generator tests fail**

Run the Debug test build and executable. Expected: compilation fails because TestPatternGenerator.h does not exist.

- [ ] **Step 3: Declare the deterministic generator**

~~~cpp
// src/audio/TestPatternGenerator.h
#pragma once
#include "AudioTypes.h"
#include <cstdint>
#include <span>

namespace soundstage::audio
{
    class TestPatternGenerator
    {
    public:
        void Render(TestPattern pattern, std::uint64_t startFrame,
                    std::span<RoleFrame> output) const noexcept;

    private:
        [[nodiscard]] static float ShapedPulse(std::uint64_t frameInPulse,
                                               double frequencyHz) noexcept;
        [[nodiscard]] static float Tone(std::uint64_t absoluteFrame,
                                        double frequencyHz) noexcept;
    };
}
~~~

- [ ] **Step 4: Implement exact signal timing**

Implement TestPatternGenerator.cpp with a one-second click period, 240-frame Hann-windowed pulses, a 1200 Hz front signature, a 2400 Hz rear signature, 440 Hz front tone, 660 Hz rear tone, and peak amplitude 0.25. PairedClicks writes both signatures at every whole second; AlternatingClicks writes front on even seconds and rear on odd seconds. FrontTone and RearTone use a repeating 500 ms on/500 ms off envelope with a 480-frame Hann fade at both edges and write only their named role. Mirror each mono signal into left and right. Calculate every value from absoluteFrame so split render calls are bitwise deterministic.

Use this pulse equation:

~~~cpp
const double phase = 2.0 * std::numbers::pi * frequencyHz *
                     static_cast<double>(frameInPulse) / MasterSampleRate;
const double window = 0.5 - 0.5 * std::cos(
    2.0 * std::numbers::pi * static_cast<double>(frameInPulse) / 239.0);
return static_cast<float>(0.25 * window * std::sin(phase));
~~~

- [ ] **Step 5: Register sources and run tests**

Add TestPatternGenerator.cpp to both project files and the test source to the test project. Build and run the Debug tests.

Expected: all AudioTypes and TestPattern tests pass.

- [ ] **Step 6: Commit the generator**

~~~powershell
git add src/audio/TestPatternGenerator.* tests/audio/TestPatternGeneratorTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: generate deterministic role test signals"
~~~

---

### Task 3: Slew-limited live delay line

**Files:**
- Create: src/audio/DelayLine.h
- Create: src/audio/DelayLine.cpp
- Create: tests/audio/DelayLineTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: StereoFrame and MaximumDelayMs.
- Produces: DelayLine(maxDelayFrames), Reset(), SetDelayFrames(double), CurrentDelayFrames(), and ProcessFrame(StereoFrame).

- [ ] **Step 1: Write exact-offset and live-change tests**

~~~cpp
#include "../TestHarness.h"
#include "../../src/audio/DelayLine.h"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

using namespace soundstage::audio;

TEST(DelayLine_ProducesAnExactStableDelay)
{
    DelayLine delay(96000);
    delay.Reset(480.0);
    StereoFrame output{};
    for (int frame = 0; frame <= 480; ++frame)
    {
        output = delay.ProcessFrame({frame == 0 ? 1.0f : 0.0f,
                                     frame == 0 ? 1.0f : 0.0f});
        if (frame < 480) EXPECT_NEAR(output.left, 0.0, 1e-7);
    }
    EXPECT_NEAR(output.left, 1.0, 1e-7);
}

TEST(DelayLine_SlewsWithoutMovingItsReadHeadBackward)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    double previousReadPosition = delay.DebugReadPosition();
    delay.SetDelayFrames(480.0);
    for (int frame = 0; frame < 2400; ++frame)
    {
        delay.ProcessFrame({0.1f, 0.1f});
        EXPECT_TRUE(delay.DebugReadPosition() > previousReadPosition);
        previousReadPosition = delay.DebugReadPosition();
    }
    EXPECT_NEAR(delay.CurrentDelayFrames(), 480.0, 1e-6);
}

TEST(DelayLine_ClampsToAllocatedCapacity)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    delay.SetDelayFrames(100000.0);
    for (int frame = 0; frame < 400000; ++frame) delay.ProcessFrame({});
    EXPECT_NEAR(delay.CurrentDelayFrames(), 96000.0, 1e-6);
}

TEST(DelayLine_TenMillisecondEditHasBoundedSampleSteps)
{
    DelayLine delay(96000);
    delay.Reset(0.0);
    StereoFrame previous{};
    double maximumStep = 0.0;
    for (int frame = 0; frame < 4800; ++frame)
    {
        if (frame == 960) delay.SetDelayFrames(480.0);
        const float sample = 0.1f * std::sin(
            2.0 * std::numbers::pi * 440.0 * frame / MasterSampleRate);
        const StereoFrame current = delay.ProcessFrame({sample, sample});
        maximumStep = std::max(maximumStep,
                               std::abs(current.left - previous.left));
        previous = current;
    }
    EXPECT_TRUE(maximumStep < 0.02);
}
~~~

- [ ] **Step 2: Run the tests to verify the missing component**

Expected: compilation fails because DelayLine.h does not exist.

- [ ] **Step 3: Implement the fixed-capacity fractional delay**

Use a vector allocated only in the constructor, a monotonically increasing write counter, modulo indexing, and linear interpolation between the two samples surrounding writePosition minus currentDelayFrames. Reset fills the vector with silence and sets current and target delay to the clamped value.

Expose DebugReadPosition() only under SOUNDSTAGE_TESTING. Set SOUNDSTAGE_TESTING in the test project's preprocessor definitions.

~~~cpp
class DelayLine
{
public:
    explicit DelayLine(std::size_t maximumDelayFrames);
    void Reset(double delayFrames) noexcept;
    void SetDelayFrames(double delayFrames) noexcept;
    [[nodiscard]] double CurrentDelayFrames() const noexcept;
    [[nodiscard]] StereoFrame ProcessFrame(StereoFrame input) noexcept;
#ifdef SOUNDSTAGE_TESTING
    [[nodiscard]] double DebugReadPosition() const noexcept;
#endif
private:
    std::vector<StereoFrame> buffer_;
    std::uint64_t writePosition_ = 0;
    double currentDelayFrames_ = 0.0;
    double targetDelayFrames_ = 0.0;
    double lastReadPosition_ = 0.0;
};
~~~

Apply this bounded slew before each read:

~~~cpp
constexpr double MaximumDelayChangePerOutputFrame = 0.25;
const double difference = targetDelayFrames_ - currentDelayFrames_;
currentDelayFrames_ += std::clamp(
    difference,
    -MaximumDelayChangePerOutputFrame,
    MaximumDelayChangePerOutputFrame);
~~~

Because the delay changes by less than one frame per output frame, the read position always moves forward. A 10 ms change settles in about 40 ms and a larger edit settles proportionally rather than jumping or replaying a click.

- [ ] **Step 4: Run the focused and full test executable**

Build Debug and run SoundStageRouter.Tests.exe. Expected: all tests pass, including 0, 480, and 96000-frame delay behavior.

- [ ] **Step 5: Commit the delay line**

~~~powershell
git add src/audio/DelayLine.* tests/audio/DelayLineTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: add smooth per-endpoint delay line"
~~~

---

### Task 4: Streaming adaptive resampler

**Files:**
- Create: src/audio/AdaptiveResampler.h
- Create: src/audio/AdaptiveResampler.cpp
- Create: tests/audio/AdaptiveResamplerTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: StereoFrame and MaximumCorrectionPpm.
- Produces: IFrameSource::NextFrame(), AdaptiveResampler::Reset(), SetNominalRatio(), SetTargetCorrectionPpm(), CurrentCorrectionPpm(), and Render().

- [ ] **Step 1: Add failing unity, correction, and continuity tests**

Define a RampSource implementing IFrameSource and add tests that assert:

~~~cpp
TEST(Resampler_UnityRatioProducesSequentialFrames)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    std::array<StereoFrame, 4> output{};
    resampler.Render(output, source);
    EXPECT_NEAR(output[0].left, 0.0, 1e-6);
    EXPECT_NEAR(output[1].left, 1.0, 1e-6);
    EXPECT_NEAR(output[3].left, 3.0, 1e-6);
}

TEST(Resampler_ClampsAndSlewsCorrection)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(5000.0);
    std::array<StereoFrame, 48000> oneSecond{};
    resampler.Render(oneSecond, source);
    EXPECT_TRUE(resampler.CurrentCorrectionPpm() > 0.0);
    EXPECT_TRUE(resampler.CurrentCorrectionPpm() <= MaximumCorrectionPpm);
}

TEST(Resampler_RemainsContinuousAcrossRenderBlocks)
{
    RampSource source;
    AdaptiveResampler resampler;
    resampler.Reset(1.0, source);
    resampler.SetTargetCorrectionPpm(-100.0);
    std::array<StereoFrame, 64> first{};
    std::array<StereoFrame, 64> second{};
    resampler.Render(first, source);
    resampler.Render(second, source);
    EXPECT_TRUE(second.front().left > first.back().left);
    EXPECT_TRUE(second.front().left - first.back().left < 1.1f);
}
~~~

- [ ] **Step 2: Verify compilation fails on the missing resampler**

Run the Debug test build. Expected: AdaptiveResampler.h not found.

- [ ] **Step 3: Define the pull-based source boundary**

~~~cpp
class IFrameSource
{
public:
    virtual ~IFrameSource() = default;
    [[nodiscard]] virtual StereoFrame NextFrame() noexcept = 0;
};

class AdaptiveResampler
{
public:
    void Reset(double nominalInputFramesPerOutputFrame, IFrameSource& source) noexcept;
    void SetNominalRatio(double ratio) noexcept;
    void SetTargetCorrectionPpm(double ppm) noexcept;
    [[nodiscard]] double CurrentCorrectionPpm() const noexcept;
    void Render(std::span<StereoFrame> output, IFrameSource& source) noexcept;
};
~~~

- [ ] **Step 4: Implement streaming linear interpolation**

Keep previousFrame, nextFrame, and a fractional phase. For each output frame, slew currentCorrectionPpm toward its clamped target at no more than 50 ppm per rendered second, calculate ratio as nominalRatio * (1 + correctionPpm / 1,000,000), interpolate previous and next, add ratio to phase, and pull source frames while phase is at least 1.0. Reset primes previous and next exactly once. No allocation occurs in Render.

~~~cpp
for (StereoFrame& frame : output)
{
    const double ppmStep = 50.0 * nominalRatio_ / MasterSampleRate;
    currentCorrectionPpm_ += std::clamp(
        targetCorrectionPpm_ - currentCorrectionPpm_, -ppmStep, ppmStep);
    const float fraction = static_cast<float>(phase_);
    frame.left = std::lerp(previous_.left, next_.left, fraction);
    frame.right = std::lerp(previous_.right, next_.right, fraction);
    phase_ += nominalRatio_ * (1.0 + currentCorrectionPpm_ / 1'000'000.0);
    while (phase_ >= 1.0)
    {
        previous_ = next_;
        next_ = source.NextFrame();
        phase_ -= 1.0;
    }
}
~~~

- [ ] **Step 5: Run all native tests**

Expected: unity, ±100 ppm, clamp, slew, and block-boundary tests pass.

- [ ] **Step 6: Commit the resampler**

~~~powershell
git add src/audio/AdaptiveResampler.* tests/audio/AdaptiveResamplerTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: add bounded adaptive resampling"
~~~

---

### Task 5: Endpoint sample-format and channel conversion

**Files:**
- Create: src/audio/EndpointConverter.h
- Create: src/audio/EndpointConverter.cpp
- Create: tests/audio/EndpointConverterTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: spans of StereoFrame.
- Produces: SampleEncoding, EndpointMixFormat, EndpointConverter::IsSupported(), RequiredBytes(), and Convert() noexcept.

- [ ] **Step 1: Add failing conversion tests**

Cover float stereo, PCM16 mono, PCM24 stereo, PCM32 stereo, and six-channel float. Use exact expectations: mono is (left + right) * 0.5; stereo preserves left/right; multichannel writes left/right into indices 0/1 and zero into every remaining channel; integer conversion clamps to [-1, 1] before scaling.

~~~cpp
TEST(Converter_MapsStereoIntoSixChannelFloat)
{
    EndpointConverter converter({48000, 6, SampleEncoding::Float32, 24});
    std::array<StereoFrame, 1> input{{{0.25f, -0.5f}}};
    std::array<std::byte, 24> bytes{};
    EXPECT_TRUE(converter.Convert(input, bytes));
    const float* samples = reinterpret_cast<const float*>(bytes.data());
    EXPECT_NEAR(samples[0], 0.25, 1e-7);
    EXPECT_NEAR(samples[1], -0.5, 1e-7);
    for (int channel = 2; channel < 6; ++channel) EXPECT_NEAR(samples[channel], 0.0, 1e-7);
}
~~~

- [ ] **Step 2: Verify the tests fail because the converter is absent**

Run the Debug test build. Expected: EndpointConverter.h not found.

- [ ] **Step 3: Define the endpoint format contract**

~~~cpp
enum class SampleEncoding { Float32, Pcm16, Pcm24, Pcm32, Unsupported };

struct EndpointMixFormat
{
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    SampleEncoding encoding = SampleEncoding::Unsupported;
    std::uint16_t blockAlign = 0;
};

class EndpointConverter
{
public:
    explicit EndpointConverter(EndpointMixFormat format) noexcept;
    [[nodiscard]] bool IsSupported() const noexcept;
    [[nodiscard]] std::size_t RequiredBytes(std::size_t frameCount) const noexcept;
    [[nodiscard]] bool Convert(std::span<const StereoFrame> input,
                               std::span<std::byte> output) const noexcept;
};
~~~

- [ ] **Step 4: Implement saturating, allocation-free conversion**

Validate sample rate > 0, channels > 0, and blockAlign equals channels times bytes per sample. Write little-endian PCM24 explicitly one byte at a time. Return false if the output span is not exactly large enough or the format is unsupported. Do not throw.

~~~cpp
const float mono = std::clamp((frame.left + frame.right) * 0.5f, -1.0f, 1.0f);
const float sample = channel == 0 ? frame.left :
                     channel == 1 ? frame.right : 0.0f;
const auto pcm16 = static_cast<std::int16_t>(
    std::lrint(std::clamp(sample, -1.0f, 1.0f) * 32767.0f));
const auto pcm24 = static_cast<std::int32_t>(
    std::lrint(std::clamp(sample, -1.0f, 1.0f) * 8388607.0f));
destination[0] = std::byte(pcm24 & 0xff);
destination[1] = std::byte((pcm24 >> 8) & 0xff);
destination[2] = std::byte((pcm24 >> 16) & 0xff);
~~~

Select mono for the one-channel case before entering the encoding switch; otherwise use the per-channel sample expression above.

- [ ] **Step 5: Run conversion tests in Debug and Release**

~~~powershell
msbuild SoundStageRouter.Tests.vcxproj -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
msbuild SoundStageRouter.Tests.vcxproj -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
~~~

Expected: both configurations pass.

- [ ] **Step 6: Commit the converter**

~~~powershell
git add src/audio/EndpointConverter.* tests/audio/EndpointConverterTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: convert master frames to endpoint formats"
~~~

---

### Task 6: Allocation-free endpoint pipeline

**Files:**
- Create: src/audio/EndpointPipeline.h
- Create: src/audio/EndpointPipeline.cpp
- Create: tests/audio/EndpointPipelineTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: EndpointRoute, TestPatternGenerator, DelayLine, AdaptiveResampler, and EndpointConverter.
- Produces: PipelineConfiguration, EndpointPipeline::SetDelayMs(), SetCorrectionPpm(), Reset(), and gain-ramped Render().

- [ ] **Step 1: Write end-to-end pipeline tests**

Test a FrontTone route at 48 kHz float stereo, a RearTone route at 44.1 kHz PCM16 stereo, a 10 ms delay that begins with silence, and a live 10 ms update whose CurrentDelayMs converges. Verify a sentinel allocation counter remains unchanged across 100 Render calls by placing all scratch storage in Reset.

~~~cpp
TEST(Pipeline_FrontDelayStartsWithSilence)
{
    EndpointPipeline pipeline;
    PipelineConfiguration config{
        SpeakerRole::Front, TestPattern::FrontTone, 10,
        EndpointMixFormat{48000, 2, SampleEncoding::Float32, 8}, 480};
    EXPECT_TRUE(pipeline.Reset(config));
    std::array<std::byte, 480 * 8> output{};
    EXPECT_TRUE(pipeline.Render(output, 480));
    const float* samples = reinterpret_cast<const float*>(output.data());
    for (std::size_t index = 0; index < 480 * 2; ++index)
        EXPECT_NEAR(samples[index], 0.0, 1e-7);
}
~~~

- [ ] **Step 2: Confirm the pipeline test fails to compile**

Expected: EndpointPipeline.h not found.

- [ ] **Step 3: Define the pipeline configuration**

~~~cpp
struct PipelineConfiguration
{
    SpeakerRole role = SpeakerRole::Front;
    TestPattern pattern = TestPattern::PairedClicks;
    std::uint32_t delayMs = 0;
    EndpointMixFormat mixFormat{};
    std::uint32_t maximumRenderFrames = 0;
};

class EndpointPipeline final : private IFrameSource
{
public:
    [[nodiscard]] bool Reset(const PipelineConfiguration& configuration);
    void SetDelayMs(std::uint32_t delayMs) noexcept;
    void SetCorrectionPpm(double ppm) noexcept;
    [[nodiscard]] bool Render(std::span<std::byte> output,
                              std::uint32_t endpointFrameCount,
                              float startGain = 1.0f,
                              float endGain = 1.0f) noexcept;
    [[nodiscard]] std::uint32_t CurrentDelayMs() const noexcept;
private:
    [[nodiscard]] StereoFrame NextFrame() noexcept override;
};
~~~

- [ ] **Step 4: Compose the render path**

Reset validates format and maximum frame count, allocates a StereoFrame scratch vector once, resets sourceFrame to zero, creates a DelayLine with 96000 frames, and resets the resampler with nominal ratio 48000.0 / endpointSampleRate. NextFrame renders one absolute RoleFrame, increments sourceFrame, chooses front or rear, and returns DelayLine::ProcessFrame for that stereo value. Render checks destination size, applies atomically published delay/correction values, resamples endpointFrameCount frames into scratch, multiplies scratch by a linear startGain-to-endGain ramp, then converts scratch into the destination span. Clamp both gains to [0, 1].

Use atomic uint32 delayMs and atomic double correctionPpm as the only live controls read by Render. No vector resize is permitted after Reset.

~~~cpp
bool EndpointPipeline::Render(std::span<std::byte> output,
                              std::uint32_t frameCount,
                              float startGain,
                              float endGain) noexcept
{
    if (frameCount > scratch_.size() ||
        output.size() != converter_.RequiredBytes(frameCount)) return false;
    delay_.SetDelayFrames(static_cast<double>(
        MillisecondsToFrames(delayMs_.load(std::memory_order_relaxed))));
    resampler_.SetTargetCorrectionPpm(
        correctionPpm_.load(std::memory_order_relaxed));
    auto frames = std::span(scratch_).first(frameCount);
    resampler_.Render(frames, *this);
    for (std::size_t index = 0; index < frames.size(); ++index)
    {
        const float gain = std::lerp(startGain, endGain,
            frames.size() == 1 ? 1.0f :
            static_cast<float>(index) / static_cast<float>(frames.size() - 1));
        frames[index].left *= gain;
        frames[index].right *= gain;
    }
    return converter_.Convert(frames, output);
}
~~~

- [ ] **Step 5: Run the full DSP suite**

Expected: generator, delay, resampler, conversion, and pipeline tests all pass in Debug.

- [ ] **Step 6: Commit the composed pipeline**

~~~powershell
git add src/audio/EndpointPipeline.* tests/audio/EndpointPipelineTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: compose endpoint render pipeline"
~~~

---

### Task 7: Relative clock-rate estimator

**Files:**
- Create: src/audio/ClockSynchronizer.h
- Create: src/audio/ClockSynchronizer.cpp
- Create: tests/audio/ClockSynchronizerTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: paired ClockSnapshot values and MaximumCorrectionPpm.
- Produces: SyncEstimate, ClockSynchronizer::Reset(), Observe(), MarkClockUnavailable(), NotifyManualDelayChanged(), and Current().

- [ ] **Step 1: Add a 30-minute fake-clock simulation**

Create a helper that advances observations every 100 ms. Reference position advances at 48,000 units/second and follower position at reference * (1 + driftPpm / 1,000,000); both report frequency 48,000 and the same QPC time.

~~~cpp
ClockSnapshot Snapshot(double seconds, double driftPpm)
{
    ClockSnapshot value;
    value.deviceFrequency = MasterSampleRate;
    value.devicePosition = static_cast<std::uint64_t>(
        seconds * MasterSampleRate * (1.0 + driftPpm / 1'000'000.0));
    value.qpc100ns = static_cast<std::uint64_t>(seconds * 10'000'000.0);
    value.available = true;
    return value;
}
~~~

~~~cpp
TEST(ClockSync_ConvergesAndStaysBoundedForThirtyMinutes)
{
    ClockSynchronizer sync;
    sync.Reset();
    for (int sample = 0; sample <= 18000; ++sample)
    {
        const double seconds = sample * 0.1;
        sync.Observe(Snapshot(seconds, 0.0), Snapshot(seconds, 120.0));
    }
    const SyncEstimate estimate = sync.Current();
    EXPECT_EQ(estimate.health, ClockHealth::Active);
    EXPECT_NEAR(estimate.relativePpm, 120.0, 2.0);
    EXPECT_NEAR(estimate.correctionPpm, -120.0, 2.0);
    EXPECT_TRUE(std::abs(estimate.correctionPpm) <= MaximumCorrectionPpm);
}
~~~

Also test -90 ppm, a +6000 ppm outlier rejection, unavailable clock degradation, and NotifyManualDelayChanged preserving relativePpm while clearing phaseErrorFrames.

- [ ] **Step 2: Run tests to establish the red state**

Expected: ClockSynchronizer.h not found.

- [ ] **Step 3: Define the estimator contract**

~~~cpp
struct SyncEstimate
{
    ClockHealth health = ClockHealth::Settling;
    double relativePpm = 0.0;
    double correctionPpm = 0.0;
    double phaseErrorFrames = 0.0;
    std::uint32_t acceptedSamples = 0;
};

class ClockSynchronizer
{
public:
    void Reset() noexcept;
    void Observe(const ClockSnapshot& reference,
                 const ClockSnapshot& follower) noexcept;
    void MarkClockUnavailable() noexcept;
    void NotifyManualDelayChanged() noexcept;
    [[nodiscard]] SyncEstimate Current() const noexcept;
private:
    struct ObservationPair
    {
        ClockSnapshot reference;
        ClockSnapshot follower;
    };
    std::array<ObservationPair, 101> observations_{};
    std::size_t observationCount_ = 0;
    std::size_t nextObservation_ = 0;
    bool hasRateEstimate_ = false;
    SyncEstimate estimate_{};
};
~~~

- [ ] **Step 4: Implement normalized rate estimation**

Reject unavailable samples, zero frequencies, non-increasing positions, and non-increasing QPC values. Keep a fixed circular window of 101 paired observations, which represents 10 seconds at the coordinator's 100 ms tick. Calculate each normalized rate from the oldest and newest observations in the current window: normalizedRate = (deltaPosition / deviceFrequency) / deltaQpcSeconds. Calculate relativePpm = (followerRate / referenceRate - 1) * 1,000,000. Reject a raw magnitude above 5000 ppm. Calculate phaseErrorFrames from the difference between follower and reference normalized elapsed seconds multiplied by MasterSampleRate. Remain Settling until the observation span reaches 3 seconds, then update relativePpm with an EWMA alpha of 0.1 as the window grows to 10 seconds. Publish correctionPpm as clamp(-relativePpm, -500, +500). NotifyManualDelayChanged clears the observation window and phaseErrorFrames but preserves the EWMA rate. Missing clock sets Unavailable and correction 0 without clearing the last diagnostic relativePpm.

~~~cpp
const std::size_t oldestIndex =
    observationCount_ < observations_.size() ? 0 : nextObservation_;
const ObservationPair& oldest = observations_[oldestIndex];
const double referenceSeconds =
    static_cast<double>(reference.devicePosition - oldest.reference.devicePosition) /
    static_cast<double>(reference.deviceFrequency);
const double followerSeconds =
    static_cast<double>(follower.devicePosition - oldest.follower.devicePosition) /
    static_cast<double>(follower.deviceFrequency);
const double qpcSeconds =
    static_cast<double>(reference.qpc100ns - oldest.reference.qpc100ns) / 10'000'000.0;
const double referenceRate = referenceSeconds / qpcSeconds;
const double followerRate = followerSeconds / qpcSeconds;
const double rawPpm = (followerRate / referenceRate - 1.0) * 1'000'000.0;
estimate_.phaseErrorFrames =
    (followerSeconds - referenceSeconds) * MasterSampleRate;
estimate_.relativePpm = !hasRateEstimate_
    ? rawPpm
    : std::lerp(estimate_.relativePpm, rawPpm, 0.1);
estimate_.correctionPpm =
    std::clamp(-estimate_.relativePpm, -MaximumCorrectionPpm, MaximumCorrectionPpm);
~~~

- [ ] **Step 5: Run focused simulation and the whole test target**

Expected: the 30-minute simulation completes in under one second of wall time and all cases pass.

- [ ] **Step 6: Commit the synchronizer**

~~~powershell
git add src/audio/ClockSynchronizer.* tests/audio/ClockSynchronizerTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: estimate and correct endpoint clock drift"
~~~

---

### Task 8: Deterministic engine lifecycle with fake sessions

**Files:**
- Create: src/audio/EndpointSession.h
- Create: src/audio/EngineController.h
- Create: src/audio/EngineController.cpp
- Create: src/audio/AudioEngineCoordinator.h
- Create: src/audio/AudioEngineCoordinator.cpp
- Create: tests/audio/FakeEndpointSession.h
- Create: tests/audio/EngineControllerTests.cpp
- Create: tests/audio/AudioEngineCoordinatorTests.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: RunConfiguration, EndpointTelemetry, ClockSynchronizer, and stop_token.
- Produces: IEndpointSession, IEndpointSessionFactory, EngineController synchronous state transitions, and AudioEngineCoordinator non-blocking UI commands.

- [ ] **Step 1: Write validation and peer-teardown tests**

Add tests for: fewer or more than two routes; duplicate endpoint IDs; missing reference; prepare failure on the second session stopping the first; normal Stopped → Preparing → Primed → Running; idempotent Stop; device fault stopping both; three underruns within five seconds faulting the run; missing clock leaving Running with ClockHealth::Unavailable.

~~~cpp
TEST(Engine_StartFailureTearsDownPreparedPeer)
{
    FakeEndpointSessionFactory factory;
    factory.Session(SpeakerRole::Rear).prepareResult =
        SessionResult::Failure(41, SpeakerRole::Rear, L"rear failed");
    EngineController engine(factory);
    const SessionResult result = engine.Start(ValidRunConfiguration(), {});
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(engine.Status().state, PlaybackState::Faulted);
    EXPECT_EQ(factory.Session(SpeakerRole::Front).stopCalls, 1u);
    EXPECT_EQ(factory.Session(SpeakerRole::Rear).stopCalls, 1u);
}
~~~

- [ ] **Step 2: Run the lifecycle tests to verify missing interfaces**

Expected: EndpointSession.h and EngineController.h are absent.

- [ ] **Step 3: Define the injected endpoint boundary**

~~~cpp
struct SessionResult
{
    bool ok = true;
    EngineFault fault{};
    static SessionResult Success() noexcept;
    static SessionResult Failure(std::uint32_t code, SpeakerRole role,
                                 std::wstring message);
};

class IEndpointSession
{
public:
    virtual ~IEndpointSession() = default;
    virtual SessionResult Prepare(const EndpointRoute&, TestPattern,
                                  std::stop_token) = 0;
    virtual SessionResult Prime() = 0;
    virtual SessionResult ArmStart(std::uint64_t startQpc100ns) = 0;
    virtual void SetDelayMs(std::uint32_t) noexcept = 0;
    virtual void SetCorrectionPpm(double) noexcept = 0;
    virtual EndpointTelemetry Snapshot() noexcept = 0;
    virtual void Stop() noexcept = 0;
};

class IEndpointSessionFactory
{
public:
    virtual ~IEndpointSessionFactory() = default;
    virtual std::unique_ptr<IEndpointSession> Create(SpeakerRole role) = 0;
};
~~~

- [ ] **Step 4: Implement the single-owner state machine**

EngineController validates exactly two distinct non-empty endpoint IDs and exactly one reference. It creates sessions by role, prepares and primes both, selects a common start timestamp 1,000,000 100 ns units (100 ms) in the future, then arms both. Every failure calls Stop on every created session, records the exact fault, and ends Faulted.

~~~cpp
std::uint64_t QpcNow100ns() noexcept
{
    LARGE_INTEGER counter{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&counter);
    QueryPerformanceFrequency(&frequency);
    return static_cast<std::uint64_t>(
        static_cast<long double>(counter.QuadPart) * 10'000'000.0L /
        static_cast<long double>(frequency.QuadPart));
}
~~~

Expose this exact control surface:

~~~cpp
class EngineController
{
public:
    explicit EngineController(IEndpointSessionFactory& factory);
    SessionResult Start(const RunConfiguration& configuration,
                        std::stop_token stopToken);
    void Stop() noexcept;
    void SetDelayMs(SpeakerRole role, std::uint32_t delayMs) noexcept;
    void Tick(std::uint64_t qpc100ns) noexcept;
    [[nodiscard]] EngineStatus Status() const;
};
~~~

Tick snapshots both sessions. A nonzero faultCode faults both. Track underrun timestamps in a fixed std::array of three QPC values and fault when three new underruns occur within five seconds. Feed the reference/follower clocks to ClockSynchronizer; publish its correction only to the follower. Stop transitions through Stopping, stops both, clears sessions, and ends Stopped unless preserving a Faulted reason.

- [ ] **Step 5: Add the non-UI coordinator**

AudioEngineCoordinator owns one std::jthread, a mutex/condition-variable command queue used only by UI/control code, an atomic shared_ptr<const EngineStatus> snapshot, and an atomic cancellation flag. Public methods PostStart, PostStop, PostDelay, and Status return without waiting on endpoint preparation. PostStop sets cancellation immediately so an in-progress Prepare sees its stop_token. The worker serializes commands and calls EngineController::Tick every 100 ms while running.

~~~cpp
class AudioEngineCoordinator
{
public:
    AudioEngineCoordinator();
    explicit AudioEngineCoordinator(std::unique_ptr<IEndpointSessionFactory> factory);
    ~AudioEngineCoordinator();
    void PostStart(RunConfiguration configuration);
    void PostStop() noexcept;
    void PostDelay(SpeakerRole role, std::uint32_t delayMs) noexcept;
    [[nodiscard]] std::shared_ptr<const EngineStatus> Status() const noexcept;
};
~~~

Add static_assert(std::atomic<double>::is_always_lock_free) beside the pipeline's live correction value for the required x64 target.

- [ ] **Step 6: Test cancellation and non-blocking posts**

Use a FakeEndpointSession whose Prepare waits on stop_token. Assert PostStart returns within 10 ms, PostStop causes preparation to exit, both fake sessions receive Stop, and coordinator destruction joins its worker.

- [ ] **Step 7: Run all test configurations**

Build and run Debug and Release tests. Expected: all state, fault, cancellation, and DSP tests pass.

- [ ] **Step 8: Commit the engine control layer**

~~~powershell
git add src/audio/EndpointSession.h src/audio/EngineController.* src/audio/AudioEngineCoordinator.* tests/audio/FakeEndpointSession.h tests/audio/EngineControllerTests.cpp tests/audio/AudioEngineCoordinatorTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: coordinate two endpoint sessions safely"
~~~

---

### Task 9: Event-driven shared-mode WASAPI session

**Files:**
- Create: src/audio/WasapiEndpointSession.h
- Create: src/audio/WasapiEndpointSession.cpp
- Create: src/audio/WasapiBackend.h
- Create: src/audio/WasapiBackend.cpp
- Create: tests/audio/TelemetryQueueTests.cpp
- Create: tests/audio/WasapiEndpointSessionTests.cpp
- Modify: src/audio/EndpointSession.h
- Modify: src/audio/AudioEngineCoordinator.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: IEndpointSession, EndpointPipeline, EndpointRoute, and EngineController.
- Produces: TelemetryQueue, IWasapiBackend/WindowsWasapiBackend, WasapiEndpointSession, and WasapiEndpointSessionFactory.

- [ ] **Step 1: Test lock-free telemetry publication**

Add a fixed-capacity TelemetryQueue value type to EndpointSession.h. Test that Push followed by Pop preserves every field, FIFO order is stable, Push returns false instead of overwriting unread data when full, and 100,000 single-writer/single-reader iterations preserve a role/sample-rate pair.

- [ ] **Step 2: Run tests and confirm TelemetryQueue is missing**

Expected: compilation fails on the missing type.

- [ ] **Step 3: Implement the SPSC telemetry queue**

Use std::array<EndpointTelemetry, 64>, an atomic write index, and an atomic read index. Push is called only by the render worker: it loads read with acquire ordering, returns false when advancing write would make the queue full, writes one otherwise-unused slot, then publishes write with release ordering. Pop is called only by the coordinator: it loads write with acquire ordering, returns false when empty, copies the readable slot, then publishes read with release ordering. Add static_assert(std::is_trivially_copyable_v<EndpointTelemetry>). WasapiEndpointSession::Snapshot drains all available items into a coordinator-owned cached value and returns the newest; the render worker never overwrites unread storage or blocks when the queue is full.

~~~cpp
class TelemetryQueue
{
public:
    bool Push(const EndpointTelemetry& value) noexcept
    {
        const auto write = write_.load(std::memory_order_relaxed);
        const auto next = (write + 1) % values_.size();
        if (next == read_.load(std::memory_order_acquire)) return false;
        values_[write] = value;
        write_.store(next, std::memory_order_release);
        return true;
    }
    bool Pop(EndpointTelemetry& value) noexcept
    {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        value = values_[read];
        read_.store((read + 1) % values_.size(), std::memory_order_release);
        return true;
    }
private:
    std::array<EndpointTelemetry, 64> values_{};
    std::atomic<std::size_t> write_{0};
    std::atomic<std::size_t> read_{0};
};
~~~

- [ ] **Step 4: Declare the production session**

WasapiEndpointSession implements every IEndpointSession method. Its constructor receives only SpeakerRole. Prepare launches one std::jthread and waits on a preparation completion event from the non-UI coordinator thread. The worker initializes COM as COINIT_MULTITHREADED, resolves the selected IMMDevice by stable ID, activates IAudioClient and IAudioRenderClient, obtains IAudioClock when available, and owns those interfaces until worker exit.

WasapiEndpointSessionFactory::Create returns make_unique<WasapiEndpointSession>(role).

Move direct COM calls behind this worker-owned boundary so lifecycle faults can be injected without hardware:

~~~cpp
enum class SessionPhase
{
    WorkerStart, ActivateDevice, DiscoverFormat, AllocateBuffers,
    InitializeClient, Prime, FirstRender, StableRender, Stop
};

struct BackendBuffer
{
    std::span<std::byte> bytes;
    std::uint32_t frames = 0;
};

enum class BackendWaitResult { BufferReady, Timeout };

struct BackendResult
{
    bool ok = true;
    std::uint32_t faultCode = 0;
};

class IWasapiBackend
{
public:
    virtual ~IWasapiBackend() = default;
    virtual BackendResult ActivateDevice(const std::wstring& endpointId,
                                         std::stop_token stopToken) = 0;
    virtual BackendResult DiscoverFormat() = 0;
    virtual EndpointMixFormat MixFormat() const noexcept = 0;
    virtual std::uint32_t BufferFrames() const noexcept = 0;
    virtual double BufferDurationMs() const noexcept = 0;
    virtual BackendResult InitializeSharedMode() = 0;
    virtual BackendResult PrimeSilence() = 0;
    virtual BackendResult StartAt(std::uint64_t qpcTarget100ns) = 0;
    virtual BackendWaitResult WaitForRender(
        std::chrono::milliseconds timeout) = 0;
    virtual BackendResult BeginRender(BackendBuffer& buffer) = 0;
    virtual BackendResult EndRender(std::uint32_t frames, bool silent) = 0;
    virtual ClockSnapshot ReadClock() noexcept = 0;
    virtual void Stop() noexcept = 0;
};

using PhaseHook = void (*)(SessionPhase phase, void* context);

class WasapiEndpointSession final : public IEndpointSession
{
public:
    explicit WasapiEndpointSession(SpeakerRole role);
    WasapiEndpointSession(SpeakerRole role,
                          std::unique_ptr<IWasapiBackend> backend,
                          PhaseHook hook,
                          void* hookContext);
    ~WasapiEndpointSession() override;
    SessionResult Prepare(const EndpointRoute&, TestPattern,
                          std::stop_token) override;
    SessionResult Prime() override;
    SessionResult ArmStart(std::uint64_t startQpc100ns) override;
    void SetDelayMs(std::uint32_t) noexcept override;
    void SetCorrectionPpm(double) noexcept override;
    EndpointTelemetry Snapshot() noexcept override;
    void Stop() noexcept override;
};

class WasapiEndpointSessionFactory final : public IEndpointSessionFactory
{
public:
    std::unique_ptr<IEndpointSession> Create(SpeakerRole role) override;
};
~~~

The production constructor creates WindowsWasapiBackend and uses a null PhaseHook. A test constructor accepts unique_ptr<IWasapiBackend>, PhaseHook, and hook context. Invoke the hook immediately before each named phase, then call the matching backend method; test hooks may throw and the worker boundary must convert that into a fault.

- [ ] **Step 5: Inject every lifecycle fault**

Create FakeWasapiBackend with configurable results and a blocking ActivateDevice that honors stop_token. Add table-driven tests for WorkerStart, ActivateDevice, DiscoverFormat, AllocateBuffers, InitializeClient, Prime, FirstRender, StableRender, and Stop. For each phase assert the worker publishes the configured numeric fault, calls backend Stop exactly once, and joins. Add a stop-during-ActivateDevice test and a device-invalidated-during-StableRender test; both must return without use-after-release, and the latter must be visible to EngineController so it stops the peer session.

~~~cpp
struct InjectedSessionFailure
{
    std::uint32_t faultCode;
};
struct FaultHookContext
{
    SessionPhase phase;
    std::uint32_t faultCode;
};
void ThrowAtPhase(SessionPhase phase, void* rawContext)
{
    const auto& context = *static_cast<FaultHookContext*>(rawContext);
    if (phase == context.phase) throw InjectedSessionFailure{context.faultCode};
}

TEST(WasapiSession_ConvertsWorkerStartupExceptionToFault)
{
    FaultHookContext context{SessionPhase::WorkerStart, 0xE001u};
    WasapiEndpointSession session(SpeakerRole::Front,
        std::make_unique<FakeWasapiBackend>(), &ThrowAtPhase, &context);
    const EndpointRoute route{SpeakerRole::Front, L"fake-front", 0, false};
    const SessionResult result = session.Prepare(
        route, TestPattern::PairedClicks, {});
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(session.Snapshot().faultCode, context.faultCode);
}
~~~

- [ ] **Step 6: Parse and validate the shared mix format**

Add a private ParseMixFormat(const WAVEFORMATEX&) function that accepts IEEE float and extensible IEEE float at 32 bits, plus PCM/extensible PCM at 16, 24, or 32 bits. Reject zero sample rate/channels, inconsistent block alignment, and every other encoding before either session starts.

~~~cpp
const bool extensible = format.wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
    format.cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
const GUID subformat = extensible
    ? reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format).SubFormat
    : GUID_NULL;
const bool isFloat = format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
    (extensible && IsEqualGUID(subformat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT));
const bool isPcm = format.wFormatTag == WAVE_FORMAT_PCM ||
    (extensible && IsEqualGUID(subformat, KSDATAFORMAT_SUBTYPE_PCM));
~~~

- [ ] **Step 7: Initialize event-driven shared mode**

On the worker call IAudioClient::Initialize with AUDCLNT_SHAREMODE_SHARED and flags AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST, zero periodicity, and the endpoint mix format. Create an auto-reset render event, call SetEventHandle, obtain buffer size, allocate EndpointPipeline scratch during Prepare, and publish sample rate, channels, and buffer duration.

Prime obtains the full render buffer and releases it with AUDCLNT_BUFFERFLAGS_SILENT. ArmStart stores the common QPC target and signals the worker. The worker waits with a waitable timer until the target, calls IAudioClient::Start, then waits on render or stop events.

~~~cpp
ThrowIfFailed(audioClient_->Initialize(
    AUDCLNT_SHAREMODE_SHARED,
    AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
    0, 0, mixFormat_, nullptr));
ThrowIfFailed(audioClient_->SetEventHandle(renderEvent_.get()));
ThrowIfFailed(audioClient_->GetBufferSize(&bufferFrames_));
~~~

- [ ] **Step 8: Implement the allocation-free render loop**

For each render event:

1. Call GetCurrentPadding.
2. Calculate availableFrames = bufferFrames - padding.
3. Treat padding == 0 after the first completed render as an underrun observation.
4. Call IAudioRenderClient::GetBuffer.
5. Render exactly availableFrames through EndpointPipeline into the returned byte span so the logical timeline advances.
6. On a detected underrun, release that completed buffer with AUDCLNT_BUFFERFLAGS_SILENT; otherwise release without the silent flag. A single underrun therefore produces silence but does not leave the endpoint timeline farther behind.
7. Read IAudioClock::GetPosition plus GetFrequency when available and publish ClockSnapshot.

Store HRESULT values as uint32 faultCode. Treat AUDCLNT_E_DEVICE_INVALIDATED and AUDCLNT_E_RESOURCES_INVALIDATED as terminal. Catch all exceptions at the worker boundary, publish a fixed numeric code, stop the client, release COM on the worker, and call CoUninitialize.

~~~cpp
while (workerState_ != WorkerState::Stopped)
{
    if (stopRequested_.load(std::memory_order_acquire) &&
        workerState_ == WorkerState::Running)
    {
        workerState_ = WorkerState::Fading;
        fadeFramesRemaining_ = totalFadeFrames_;
    }
    const BackendWaitResult wait = backend_->WaitForRender(bufferDuration_);
    if (wait == BackendWaitResult::Timeout)
    {
        if (workerState_ == WorkerState::Fading) workerState_ = WorkerState::Stopped;
        continue;
    }
    BackendBuffer buffer{};
    const BackendResult begin = backend_->BeginRender(buffer);
    if (!begin.ok) { PublishFault(begin.faultCode); break; }
    const bool underrun = runningOnce_ && latestPaddingFrames_ == 0;
    const bool rendered = pipeline_.Render(
        buffer.bytes, buffer.frames, currentGain_, targetGain_);
    const BackendResult end = backend_->EndRender(
        buffer.frames, underrun || !rendered);
    if (!end.ok) { PublishFault(end.faultCode); break; }
    PublishTelemetry(backend_->ReadClock(), underrun);
    runningOnce_ = true;
}
~~~

- [ ] **Step 9: Implement a coordinated 10 ms stop fade**

When stop is requested, set an atomic fadeFramesRemaining to sampleRate / 100 and wake the worker into a Fading state rather than exiting its event loop. For each subsequent buffer, calculate startGain and endGain from fadeFramesRemaining, pass them to EndpointPipeline::Render, and subtract the rendered frame count. Exit the render loop after the fade reaches zero. If the endpoint no longer signals, wait at most one buffer duration, then stop immediately. IAudioClient::Stop and COM release occur on the owning worker; worker join and handle closure occur on the control path. Stop is idempotent at every preparation stage.

~~~cpp
const std::uint32_t rendered = std::min(buffer.frames, fadeFramesRemaining_);
const float startGain = static_cast<float>(fadeFramesRemaining_) / totalFadeFrames_;
fadeFramesRemaining_ -= rendered;
const float endGain = static_cast<float>(fadeFramesRemaining_) / totalFadeFrames_;
pipeline_.Render(buffer.bytes, buffer.frames, startGain, endGain);
if (fadeFramesRemaining_ == 0) state_ = WorkerState::Stopped;
~~~

- [ ] **Step 10: Wire the production factory and run non-hardware verification**

Construct WasapiEndpointSessionFactory inside AudioEngineCoordinator's production constructor. Build the app and all tests in Debug and Release. Do not start playback automatically.

Expected: both projects compile with warning level 4, all dependency-free tests pass, and launching the app still enumerates endpoints without producing audio.

- [ ] **Step 11: Commit the WASAPI session**

~~~powershell
git add src/audio/WasapiEndpointSession.* src/audio/WasapiBackend.* src/audio/EndpointSession.h src/audio/AudioEngineCoordinator.cpp tests/audio/TelemetryQueueTests.cpp tests/audio/WasapiEndpointSessionTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: render test audio through shared WASAPI"
~~~

---

### Task 10: Persist pattern and integrate playback controls

**Files:**
- Modify: src/RouterSettings.h
- Modify: src/RouterSettings.cpp
- Create: tests/RouterSettingsTests.cpp
- Modify: src/AppWindow.h
- Modify: src/AppWindow.cpp
- Modify: SoundStageRouter.vcxproj
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: AudioEngineCoordinator, EngineStatus, RunConfiguration, and TestPattern.
- Produces: persisted lastPattern and a Win32 control surface for Start, Stop, delay updates, and status polling.

- [ ] **Step 1: Add failing pattern persistence codec tests**

Extract pure functions TestPatternToString(TestPattern) and TestPatternFromString(wstring_view) into RouterSettings.h/.cpp. Test all four round trips and verify an unknown value returns PairedClicks. Add TestPattern lastPattern = PairedClicks and bool loadAdjustedValues = false to RouterSettings.

~~~cpp
TEST(RouterSettings_TestPatternsRoundTrip)
{
    for (const TestPattern pattern : {
             TestPattern::PairedClicks, TestPattern::AlternatingClicks,
             TestPattern::FrontTone, TestPattern::RearTone})
    {
        EXPECT_EQ(TestPatternFromString(TestPatternToString(pattern)), pattern);
    }
    EXPECT_EQ(TestPatternFromString(L"unknown"), TestPattern::PairedClicks);
}
~~~

- [ ] **Step 2: Run tests to verify the codec is absent**

Expected: compilation fails because RouterSettings has no lastPattern.

- [ ] **Step 3: Persist the last pattern**

Read Routing/TestPattern with default PairedClicks and write the canonical values PairedClicks, AlternatingClicks, FrontTone, and RearTone. Parse each delay string with wcstol, set errno to zero first, require a non-empty value, require the end pointer to reach the string terminator, and reject ERANGE before ClampDelayMs. Set loadAdjustedValues when either delay is malformed/out of range or the pattern string is unknown. Do not persist loadAdjustedValues, runtime clock estimates, health, or underruns. Add RouterSettings.cpp to the test project and link shell32.lib and ole32.lib.

~~~cpp
std::optional<int> ParseDelay(std::wstring_view text)
{
    if (text.empty()) return std::nullopt;
    std::wstring owned(text);
    wchar_t* end = nullptr;
    errno = 0;
    const long value = std::wcstol(owned.c_str(), &end, 10);
    if (errno == ERANGE || end == owned.c_str() || *end != L'\0' ||
        value < 0 || value > MaximumDelayMs) return std::nullopt;
    return static_cast<int>(value);
}
~~~

- [ ] **Step 4: Add control members and command IDs**

Add patternCombo, startButton, stopButton, frontStatus, rearStatus, syncStatus, and a unique_ptr<AudioEngineCoordinator> to AppWindow. Add command IDs for pattern/start/stop and a 250 ms timer ID. Create the four pattern combo entries in enum order. When a saved endpoint ID is not in the active enumeration, add a combo entry labelled Saved Front output unavailable or Saved Rear output unavailable with item data -1, retain the ID in RouterSettings, and select that entry so the missing assignment is visible instead of discarded. Show Settings contained invalid values; supported defaults were applied when loadAdjustedValues is true.

~~~cpp
HWND patternCombo_ = nullptr;
HWND startButton_ = nullptr;
HWND stopButton_ = nullptr;
HWND frontStatus_ = nullptr;
HWND rearStatus_ = nullptr;
HWND syncStatus_ = nullptr;
std::unique_ptr<audio::AudioEngineCoordinator> coordinator_;

constexpr int PatternComboId = 1008;
constexpr int StartButtonId = 1009;
constexpr int StopButtonId = 1010;
constexpr UINT_PTR StatusTimerId = 1;
constexpr UINT StatusTimerPeriodMs = 250;
~~~

- [ ] **Step 5: Validate and post a start command**

Implement BuildRunConfiguration() to reject either unavailable synthetic combo entry, require two selected, distinct active endpoint indices, and create this route vector:

~~~cpp
configuration.routes = {
    {SpeakerRole::Front, endpoints_[frontIndex].id,
     ClampDelayMs(ReadDelay(frontDelay_)), false},
    {SpeakerRole::Rear, endpoints_[rearIndex].id,
     ClampDelayMs(ReadDelay(rearDelay_)), true}
};
~~~

StartTest saves settings, disables refresh and endpoint/pattern selection, enables Stop, and calls coordinator_->PostStart without waiting.

- [ ] **Step 6: Forward live delay edits**

Handle EN_CHANGE for FrontDelayId and RearDelayId only while the coordinator reports Running. Clamp the edit value and call PostDelay for the matching role. Do not restart streams.

~~~cpp
if (HIWORD(wParam) == EN_CHANGE &&
    coordinator_->Status()->state == audio::PlaybackState::Running)
{
    if (LOWORD(wParam) == FrontDelayId)
        coordinator_->PostDelay(audio::SpeakerRole::Front,
                                audio::ClampDelayMs(ReadDelay(frontDelay_)));
    if (LOWORD(wParam) == RearDelayId)
        coordinator_->PostDelay(audio::SpeakerRole::Rear,
                                audio::ClampDelayMs(ReadDelay(rearDelay_)));
}
~~~

- [ ] **Step 7: Poll and render immutable status**

Start the UI timer in WM_CREATE. On WM_TIMER read coordinator_->Status and update each status label with prepared/running state, mix rate/channels, buffer ms, manual delay, underrun count, reference/follower, clock health, relative ppm, correction ppm, and last fault. On Stopped or Faulted, re-enable selection and Start. Never wait for audio threads in WM_TIMER.

~~~cpp
case WM_TIMER:
    if (wParam == StatusTimerId)
    {
        const auto status = coordinator_->Status();
        RenderEngineStatus(*status);
        const bool selectable =
            status->state == audio::PlaybackState::Stopped ||
            status->state == audio::PlaybackState::Faulted;
        EnableWindow(frontCombo_, selectable);
        EnableWindow(rearCombo_, selectable);
        EnableWindow(patternCombo_, selectable);
        EnableWindow(refreshButton_, selectable);
        EnableWindow(startButton_, selectable);
        EnableWindow(stopButton_, !selectable);
        return 0;
    }
    break;
~~~

- [ ] **Step 8: Stop safely from buttons and window shutdown**

StopButton posts PostStop. WM_DESTROY first kills the UI timer, calls coordinator_->PostStop, destroys the coordinator so its control worker joins, then posts quit. The AppWindow destructor must not hold HWND-dependent state while the coordinator is still running.

~~~cpp
case WM_DESTROY:
    KillTimer(window_, StatusTimerId);
    if (coordinator_)
    {
        coordinator_->PostStop();
        coordinator_.reset();
    }
    PostQuitMessage(0);
    return 0;
~~~

- [ ] **Step 9: Build and manually smoke the UI without audio**

Launch SoundStageRouter.exe, verify the four patterns appear, select the same endpoint for both roles and confirm Start is rejected, select two endpoints and confirm controls change state after Start/Stop, and verify closing during Preparing exits cleanly. Keep endpoint volume low for the first audible run.

- [ ] **Step 10: Run the automated suite and commit**

Run Debug and Release tests, then build Debug and Release app targets. Expected: every command exits 0.

~~~powershell
git add src/RouterSettings.* src/AppWindow.* tests/RouterSettingsTests.cpp SoundStageRouter.vcxproj SoundStageRouter.Tests.vcxproj
git commit -m "feat: expose synchronized test playback controls"
~~~

---

### Task 11: Fault behavior, documentation, and full software verification

**Files:**
- Create: tests/audio/WasapiFaultClassificationTests.cpp
- Modify: src/audio/WasapiEndpointSession.cpp
- Modify: README.md
- Modify: docs/superpowers/specs/2026-08-08-synchronized-test-playback-design.md
- Modify: SoundStageRouter.Tests.vcxproj

**Interfaces:**
- Consumes: production fault codes and all public playback behavior.
- Produces: named fault classification, repeatable build/test commands, and an implementation-status record.

- [ ] **Step 1: Add fault classification tests**

Extract ClassifyWasapiFailure(HRESULT, SpeakerRole) into a noexcept helper. Verify AUDCLNT_E_DEVICE_INVALIDATED and AUDCLNT_E_RESOURCES_INVALIDATED return terminal device faults naming the role; an unavailable IAudioClock returns degraded clock health with no render fault; three underruns in five seconds are classified by EngineController, not the session.

~~~cpp
TEST(WasapiFault_DeviceInvalidationIsTerminal)
{
    const EngineFault fault = ClassifyWasapiFailure(
        AUDCLNT_E_DEVICE_INVALIDATED, SpeakerRole::Rear);
    EXPECT_EQ(fault.role, SpeakerRole::Rear);
    EXPECT_TRUE(fault.code != 0);
}
~~~

- [ ] **Step 2: Run the new tests and verify the helper is missing**

Expected: compile failure on ClassifyWasapiFailure.

- [ ] **Step 3: Implement named fault messages off the render thread**

The render worker publishes only fault code and role. EngineController or UI control code maps known codes to fixed messages: Front output disconnected, Rear output disconnected, Endpoint resources invalidated, Unsupported endpoint format, Render deadline repeatedly missed, and Endpoint clock unavailable. Unknown HRESULT text includes its hexadecimal code and never formats on the render worker.

~~~cpp
std::wstring FormatFault(const EngineFault& fault)
{
    switch (fault.code)
    {
    case DeviceInvalidatedCode:
        return fault.role == SpeakerRole::Front
            ? L"Front output disconnected" : L"Rear output disconnected";
    case ResourcesInvalidatedCode: return L"Endpoint resources invalidated";
    case UnsupportedFormatCode: return L"Unsupported endpoint format";
    case RepeatedUnderrunCode: return L"Render deadline repeatedly missed";
    case ClockUnavailableCode: return L"Endpoint clock unavailable";
    default:
        std::wostringstream message;
        message << L"Audio failure 0x" << std::hex << fault.code;
        return message.str();
    }
}
~~~

- [ ] **Step 4: Update README with exact boundaries and commands**

Document that this milestone generates test audio only, uses Front/Rear shared-mode endpoints, defaults Rear as reference, and requires manual acoustic delay. Include the exact MSBuild and test-executable commands from this plan. Link the approved design, this plan, and the separate acoustic analyzer plan. State that a virtual 5.1/7.1 endpoint, system capture, and automatic microphone calibration remain future work.

- [ ] **Step 5: Run fresh full verification**

~~~powershell
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Debug -p:Platform=x64
.\build\tests\Debug\SoundStageRouter.Tests.exe
msbuild SoundStageRouter.sln -t:Rebuild -p:Configuration=Release -p:Platform=x64
.\build\tests\Release\SoundStageRouter.Tests.exe
git diff --check
~~~

Expected: both rebuilds exit 0, both test runs report zero failures, and git diff --check prints no errors.

- [ ] **Step 6: Perform the two-device smoke test**

With Realtek selected as Front and Bluetooth headrest as Rear, verify FrontTone and RearTone placement, AlternatingClicks order, manual PairedClicks alignment, live 1–20 ms delay edits without a pop or replayed click, and Bluetooth disconnection stopping both streams. This is a smoke gate; the objective 30-minute measurement belongs to the acoustic analyzer plan.

- [ ] **Step 7: Mark the spec implementation status and commit**

Change the spec status only after Steps 5 and 6 pass. Record the tested hardware endpoint names and date beneath a Hardware verification note.

~~~powershell
git add README.md docs/superpowers/specs/2026-08-08-synchronized-test-playback-design.md src/audio/WasapiEndpointSession.cpp tests/audio/WasapiFaultClassificationTests.cpp SoundStageRouter.Tests.vcxproj
git commit -m "docs: verify synchronized test playback milestone"
~~~
