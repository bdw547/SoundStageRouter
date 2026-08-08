#include "../TestHarness.h"
#include "../../src/audio/EndpointSession.h"

#include <atomic>
#include <thread>

using namespace soundstage::audio;

TEST(TelemetryQueue_PreservesFieldsAndFifoOrder)
{
    TelemetryQueue queue;
    EndpointTelemetry first;
    first.role = SpeakerRole::Front;
    first.sampleRate = 48000;
    first.channels = 2;
    first.bufferDurationMs = 12.5;
    first.delayMs = 42;
    first.underrunCount = 7;
    first.clock = {123, 48000, 999, 17, true};
    first.faultCode = 55;
    EndpointTelemetry second = first;
    second.role = SpeakerRole::Rear;
    second.sampleRate = 44100;
    EXPECT_TRUE(queue.Push(first));
    EXPECT_TRUE(queue.Push(second));

    EndpointTelemetry output;
    EXPECT_TRUE(queue.Pop(output));
    EXPECT_EQ(output.role, SpeakerRole::Front);
    EXPECT_EQ(output.sampleRate, 48000u);
    EXPECT_EQ(output.channels, static_cast<std::uint16_t>(2));
    EXPECT_NEAR(output.bufferDurationMs, 12.5, 1e-12);
    EXPECT_EQ(output.delayMs, 42u);
    EXPECT_EQ(output.underrunCount, 7ull);
    EXPECT_EQ(output.clock.devicePosition, 123ull);
    EXPECT_EQ(output.faultCode, 55u);
    EXPECT_TRUE(queue.Pop(output));
    EXPECT_EQ(output.role, SpeakerRole::Rear);
    EXPECT_EQ(output.sampleRate, 44100u);
    EXPECT_TRUE(!queue.Pop(output));
}

TEST(TelemetryQueue_DoesNotOverwriteUnreadValues)
{
    TelemetryQueue queue;
    EndpointTelemetry value;
    unsigned pushed = 0;
    while (queue.Push(value))
    {
        ++pushed;
    }
    EXPECT_EQ(pushed, 63u);
    EXPECT_TRUE(!queue.Push(value));
}

TEST(TelemetryQueue_SingleProducerSingleConsumerStress)
{
    TelemetryQueue queue;
    std::atomic<bool> failed{false};
    std::thread producer([&] {
        for (std::uint32_t index = 0; index < 100000; ++index)
        {
            EndpointTelemetry value;
            value.role = index % 2 == 0
                ? SpeakerRole::Front : SpeakerRole::Rear;
            value.sampleRate = 40000 + index;
            while (!queue.Push(value))
            {
                std::this_thread::yield();
            }
        }
    });
    for (std::uint32_t index = 0; index < 100000; ++index)
    {
        EndpointTelemetry value;
        while (!queue.Pop(value))
        {
            std::this_thread::yield();
        }
        const SpeakerRole expectedRole = index % 2 == 0
            ? SpeakerRole::Front : SpeakerRole::Rear;
        if (value.role != expectedRole ||
            value.sampleRate != 40000 + index)
        {
            failed = true;
        }
    }
    producer.join();
    EXPECT_TRUE(!failed.load());
}
