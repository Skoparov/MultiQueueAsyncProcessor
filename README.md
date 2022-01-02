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
## Performance (Synthetic Tests)

| Processor  | RAM | Runs |
| ------------- | ------------- | ------------- |
| Intel(R) Core(TM) i7-9700KF 3.60GHz, 3600 Mhz, 8 Cores  | 32 GB  | 100  |

#### MSVC 2019 64 bit /MD /O2 /Ob2 /DNDEBUG

##### Queues Number Growth
| Queue Id Type | Value Type  | Queues/Consumers  | Values per Queue |  Total Enqueues | Avg Speed (processed/sec) |  Time (ms) |
| ------------- | ------------- | ------------- | ------------- | ------------- | ------------- | ------------- |
| std::string (8 chars) | std::string (50 chars) | 5000 | 1000 | 5000000 | 1825000 | 2739 |
| std::string (8 chars) | std::string (50 chars) | 10000 | 1000 | 10000000 | 1802000 | 5548 |
| std::string (8 chars) | std::string (50 chars) | 15000 | 1000 | 15000000 | 1737000 | 8631 |
| uint64_t  | uint64_t | 5000 | 1000 | 5000000 | 2235000 | 2237 |
| uint64_t  | uint64_t | 10000 | 1000 | 10000000 | 2237000 | 4469 |
| uint64_t | uint64_t | 15000 | 1000 | 15000000 | 2243000 | 6687 |

##### Values Per Queue Number Growth
| Queue Id Type | Value Type  | Queues/Consumers  | Values per Queue |  Total Enqueues | Avg Speed (processed/sec) |  Time (ms) |
| ------------- | ------------- | ------------- | ------------- | ------------- | ------------- | ------------- |
| std::string (8 chars) | std::string (50 chars) | 100 | 50000 | 5000000 | 1749000 | 2858 |
| std::string (8 chars) | std::string (50 chars) | 100 | 100000 | 10000000 | 1698000 | 5886 |
| std::string (8 chars) | std::string (50 chars) | 100 | 150000 | 15000000 | 1702000 | 8809 |
| uint64_t  | uint64_t | 100 | 50000 | 5000000 | 2010000 | 2487 |
| uint64_t  | uint64_t | 100 | 100000 | 10000000 | 1993000 | 5017 |
| uint64_t | uint64_t | 100 | 150000 | 15000000 | 1972000 | 7606 |


## Tests

Tests require ```boost``` to work. Please run them with at least ```--log_level=test_suite``` to see perf info.

#### Custom Tests Arguments
* ```--perf_runs_avg``` sets the number of perf test runs to calc average (1 by default).

#### Example Tests Run:

```./MultiQueueAsyncProcessorTests --log_level=test_suite -- --perf_runs_avg 2```
