# Async Multiple Queues Processor

A generic header only library with no external dependencies capable of concurrent processing of multiple queues. Only one simultaneous consumer per queue is supported.

## Usage example
```c++
#include <MultiQueueAsyncProcessor.h>

using QueueId = uint64_t;
using Value = std::string;

struct MyConsumer : multi_queue_async_processor::IConsumer<QueueId, Value>
{
    void Consume(const QueueId&, Value&&) noexcept final{
        // Do some stuff
    }
};

void Example()
{
    multi_queue_async_processor::MultiQueueAsyncProcessor<QueueId, Value> processor
    {
        std::thread::hardware_concurrency(), /* dispatch threads number */
        3, /* maximum queue size */
        3 /* maximum number of queues */
    };

    constexpr QueueId queueId1{ 1 };
    constexpr QueueId queueId2{ 2 };
    constexpr QueueId queueId3{ 3 };
    
    auto consumer1{ std::make_shared<MyConsumer>() };
    auto consumer2{ std::make_shared<MyConsumer>() };

    bool enqueued1{ processor.Enqueue(queueId1, "Value1") };

    bool addedConsumer1{ processor.AddConsumer(queueId1, consumer1) };
    bool addedConsumer2{ processor.AddConsumer(queueId2, consumer1) };
    bool addedConsumer3{ processor.AddConsumer(queueId3, consumer2) };

    bool enqueued2{ processor.Enqueue(queueId2, "Value2") };
    processor.RemoveConsumer(queueId2);
}
```
## Usage example

Tests require ```boost``` to work. Please run them with at least ```--log_level=test_suite``` to see perf info.