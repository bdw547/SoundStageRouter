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
            if (packetGiven)
            {
                value = {};
            }
            else
            {
                packetGiven = true;
                value.frames = 1;
                value.silent = silent;
                value.masterGain = masterGain;
                if (!silent)
                {
                    value.samples = samples;
                }
            }
            return {};
        }
        BackendResult ReleasePacket(std::uint32_t) override
        {
            released.store(true);
            return {};
        }
        void Stop() noexcept override {}

        CaptureFormat format{48000, 6, 32, 24, 0x3F, true};
        BackendResult discoverResult{};
        std::array<float, 6> samples{0.2f, 0.3f, 0, 0, 0.4f, 0.5f};
        std::atomic<bool> released{false};
        bool silent = false;
        bool packetGiven = false;
        float masterGain = 1.0f;
    };
}

TEST(LoopbackCapture_ValidatesExactVirtualFormat)
{
    EXPECT_TRUE(IsVirtualCaptureFormat({48000, 6, 32, 24, 0x3F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({44100, 6, 32, 24, 0x3F, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({48000, 2, 32, 8, 3, true}));
    EXPECT_TRUE(!IsVirtualCaptureFormat({48000, 6, 32, 24, 0x3F, false}));
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
    for (int wait = 0; wait < 100 && !observed->released.load(); ++wait)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
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
    backend->format.sampleRate = 44100;
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);
    const SessionResult result =
        capture.Prepare(ring, RearFillMode::Off, {});
    EXPECT_TRUE(!result.ok);
    EXPECT_EQ(result.fault.code, VirtualEndpointFormatCode);
}

TEST(LoopbackCapture_AppliesWindowsMasterGain)
{
    auto backend = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* observed = backend.get();
    observed->masterGain = 0.25f;
    WasapiLoopbackCapture capture(std::move(backend));
    MasterFrameRingBuffer ring(8);
    EXPECT_TRUE(capture.Prepare(ring, RearFillMode::Off, {}).ok);
    EXPECT_TRUE(capture.Start().ok);
    for (int wait = 0; wait < 100 && !observed->released.load(); ++wait)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    capture.Stop();
    RoleFrame frame;
    std::uint64_t sequence = 99;
    EXPECT_TRUE(ring.Read(0, frame, sequence));
    EXPECT_NEAR(frame.front.left, 0.05, 1e-6);
    EXPECT_NEAR(frame.front.right, 0.075, 1e-6);
    EXPECT_NEAR(frame.rear.left, 0.1, 1e-6);
    EXPECT_NEAR(frame.rear.right, 0.125, 1e-6);
}

TEST(LoopbackCapture_PropagatesMissingAndDuplicateDiscovery)
{
    for (const std::uint32_t code : {
             VirtualEndpointMissingCode, VirtualEndpointDuplicateCode})
    {
        auto backend = std::make_unique<FakeCaptureBackend>();
        backend->discoverResult = {false, code};
        WasapiLoopbackCapture capture(std::move(backend));
        MasterFrameRingBuffer ring(8);
        const SessionResult result =
            capture.Prepare(ring, RearFillMode::Off, {});
        EXPECT_TRUE(!result.ok);
        EXPECT_EQ(result.fault.code, code);
    }
}
