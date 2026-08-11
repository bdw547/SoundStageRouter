#include "../TestHarness.h"
#include "../../src/audio/LoopbackCapture.h"

#include <array>
#include <atomic>
#include <thread>

using namespace soundstage::audio;

namespace
{
    class FakeCaptureBackend final : public ILoopbackCaptureBackend
    {
    public:
        BackendResult DiscoverVirtualEndpoint(std::stop_token) override
        {
            return discoverResult;
        }
        CaptureFormat Format() const noexcept override { return format; }
        BackendResult InitializeSharedLoopback() override { return {}; }
        BackendResult Start() override { return {}; }
        BackendWaitResult WaitForCapture(
            std::chrono::milliseconds) override
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return BackendWaitResult::BufferReady;
        }
        BackendResult GetPacket(CapturePacket& value) override
        {
            if (packetsGiven >=
                packetsAvailable.load(std::memory_order_acquire))
            {
                value = {};
            }
            else
            {
                ++packetsGiven;
                value.frames = 1;
                value.silent = silent;
                value.masterGain = masterGain;
                if (!silent)
                {
                    value.samples = std::span(
                        samples.data(), format.channels);
                }
            }
            return {};
        }
        BackendResult ReleasePacket(std::uint32_t) override
        {
            released.fetch_add(1, std::memory_order_release);
            return {};
        }
        void Stop() noexcept override {}

        CaptureFormat format{48000, 6, 32, 24, 0x3F, true};
        BackendResult discoverResult{};
        std::array<float, 8> samples{
            0.2f, 0.3f, 0, 0, 0.4f, 0.5f, 0, 0};
        std::atomic<unsigned> packetsAvailable{1};
        std::atomic<unsigned> released{0};
        bool silent = false;
        unsigned packetsGiven = 0;
        float masterGain = 1.0f;
    };

    void WaitForRelease(
        const FakeCaptureBackend& backend, const unsigned count)
    {
        for (int wait = 0;
             wait < 100 &&
             backend.released.load(std::memory_order_acquire) < count;
             ++wait)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

TEST(LoopbackCapture_ValidatesExactVirtualFormat)
{
    EXPECT_TRUE(IsVirtualCaptureFormat({48000, 6, 32, 24, 0x3F, true}));
    EXPECT_TRUE(IsVirtualCaptureFormat(
        {48000, 8, 32, 32, 0x063F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({44100, 6, 32, 24, 0x3F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({48000, 2, 32, 8, 3, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({48000, 6, 32, 24, 0x3F, false}));
    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {48000, 8, 32, 32, 0x003F, true}));

    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {48000, 8, 24, 32, 0x063F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {48000, 8, 32, 24, 0x063F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {48000, 6, 32, 24, 0x063F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {44100, 8, 32, 32, 0x063F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat(
        {48000, 8, 32, 32, 0x063F, false}));
}

TEST(LoopbackCapture_DecoderPreservesEverySevenPointOneChannelInOrder)
{
    for (std::size_t channel = 0; channel < 8; ++channel)
    {
        std::array<float, 8> samples{};
        samples[channel] = 0.5f;
        const SurroundFrame decoded = DecodeVirtualSurroundFrame(
            VirtualSurroundFormat::SevenPointOne, samples, 0.5f);
        const std::array<float, 8> actual{
            decoded.frontLeft, decoded.frontRight,
            decoded.frontCenter, decoded.lfe,
            decoded.backLeft, decoded.backRight,
            decoded.sideLeft, decoded.sideRight};

        for (std::size_t output = 0; output < actual.size(); ++output)
        {
            EXPECT_NEAR(actual[output], output == channel ? 0.25 : 0.0,
                        1e-7);
        }
    }
}

TEST(LoopbackCapture_DecoderLeavesSideSilentForFivePointOne)
{
    const std::array<float, 8> samples{
        1, 2, 3, 4, 5, 6, 100, 200};
    const SurroundFrame decoded = DecodeVirtualSurroundFrame(
        VirtualSurroundFormat::FivePointOne, samples, 1.0f);

    EXPECT_NEAR(decoded.frontLeft, 1.0, 1e-7);
    EXPECT_NEAR(decoded.frontRight, 2.0, 1e-7);
    EXPECT_NEAR(decoded.frontCenter, 3.0, 1e-7);
    EXPECT_NEAR(decoded.lfe, 4.0, 1e-7);
    EXPECT_NEAR(decoded.backLeft, 5.0, 1e-7);
    EXPECT_NEAR(decoded.backRight, 6.0, 1e-7);
    EXPECT_NEAR(decoded.sideLeft, 0.0, 1e-7);
    EXPECT_NEAR(decoded.sideRight, 0.0, 1e-7);
}

TEST(LoopbackCapture_DecodesSevenPointOneAndPublishesLiveLevels)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* observed = backend.get();
    observed->format = {48000, 8, 32, 32, 0x063F, true};
    observed->samples = {
        0, 0, 0, 0, 0.25f, -0.25f, 0.5f, -0.5f};
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);

    EXPECT_TRUE(capture.Prepare(ring, RearFillMode::Off, {}).ok);
    EXPECT_EQ(capture.Snapshot().surroundFormat,
              VirtualSurroundFormat::SevenPointOne);
    EXPECT_TRUE(capture.Start().ok);
    WaitForRelease(*observed, 1);

    RoleFrame frame;
    std::uint64_t sequence = 99;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_NEAR(frame.rear.left, 0.75, 1e-6);
    EXPECT_NEAR(frame.rear.right, -0.75, 1e-6);

    capture.SetSurroundMixLevels({0.0f, 0.5f});
    observed->packetsAvailable.store(2, std::memory_order_release);
    WaitForRelease(*observed, 2);
    capture.Stop();

    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_NEAR(frame.rear.left, 0.25, 1e-6);
    EXPECT_NEAR(frame.rear.right, -0.25, 1e-6);
}

TEST(LoopbackCapture_DecodesFivePointOneWithoutReadingSideChannels)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* observed = backend.get();
    observed->samples = {
        0, 0, 0, 0, 0.25f, -0.25f, 0.5f, -0.5f};
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);

    capture.SetSurroundMixLevels({0.0f, 1.0f});
    EXPECT_TRUE(capture.Prepare(ring, RearFillMode::Off, {}).ok);
    EXPECT_EQ(capture.Snapshot().surroundFormat,
              VirtualSurroundFormat::FivePointOne);
    EXPECT_TRUE(capture.Start().ok);
    WaitForRelease(*observed, 1);
    capture.Stop();

    RoleFrame frame;
    std::uint64_t sequence = 99;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_NEAR(frame.rear.left, 0.0, 1e-7);
    EXPECT_NEAR(frame.rear.right, 0.0, 1e-7);
}

TEST(LoopbackCapture_ConvertsSilentPacketToAlignedZeroFrame)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* observed = backend.get();
    observed->silent = true;
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);
    EXPECT_TRUE(capture.Prepare(ring, RearFillMode::Duplicate, {}).ok);
    EXPECT_TRUE(capture.Start().ok);
    WaitForRelease(*observed, 1);
    capture.Stop();
    RoleFrame frame;
    std::uint64_t sequence = 99;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_EQ(sequence, 0u);
    EXPECT_NEAR(frame.front.left, 0.0, 1e-7);
    EXPECT_NEAR(frame.rear.right, 0.0, 1e-7);
    EXPECT_EQ(capture.Snapshot().silentFrameCount, 1u);
}

TEST(LoopbackCapture_RejectsWrongInjectedFormat)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    backend->format = {48000, 8, 32, 32, 0x003F, true};
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);
    const SessionResult result =
        capture.Prepare(ring, RearFillMode::Off, {});
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(result.fault.code, VirtualEndpointFormatCode);
    EXPECT_EQ(result.fault.message,
              std::wstring(
                  L"Set SoundStage Router Surround to 5.1 or 7.1 at 48 kHz"));
}

TEST(LoopbackCapture_AppliesWindowsMasterGain)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* observed = backend.get();
    observed->format = {48000, 8, 32, 32, 0x063F, true};
    observed->samples = {
        0.2f, 0.3f, 0, 0, 0.4f, 0.5f, 0.6f, 0.7f};
    observed->masterGain = 0.25f;
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);
    EXPECT_TRUE(capture.Prepare(ring, RearFillMode::Off, {}).ok);
    EXPECT_TRUE(capture.Start().ok);
    WaitForRelease(*observed, 1);
    capture.Stop();
    RoleFrame frame;
    std::uint64_t sequence = 99;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_NEAR(frame.front.left, 0.05, 1e-6);
    EXPECT_NEAR(frame.front.right, 0.075, 1e-6);
    EXPECT_NEAR(frame.rear.left, 0.25, 1e-6);
    EXPECT_NEAR(frame.rear.right, 0.3, 1e-6);
}

TEST(LoopbackCapture_PropagatesMissingAndDuplicateDiscovery)
{
    struct DiscoveryFault
    {
        std::uint32_t code;
        const wchar_t* message;
    };
    for (const DiscoveryFault expected : {
             DiscoveryFault{
                 VirtualEndpointMissingCode,
                 L"SoundStage Router Surround is not installed"},
             DiscoveryFault{
                 VirtualEndpointDuplicateCode,
                 L"Multiple SoundStage Router Surround endpoints found"}})
    {
        auto backend = std::make_unique<FakeCaptureBackend>();
        backend->discoverResult = {false, expected.code};
        WasapiLoopbackCapture capture(std::move(backend));
        MasterFrameRingBuffer ring(8);
        const SessionResult result =
            capture.Prepare(ring, RearFillMode::Off, {});
        EXPECT_TRUE(!result.ok);
        EXPECT_EQ(result.fault.code, expected.code);
        EXPECT_EQ(result.fault.message, std::wstring(expected.message));
    }
}
