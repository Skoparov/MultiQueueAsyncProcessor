#define BOOST_TEST_MODULE MultiQueueAsyncProcessorTests

#include "helpers.h"
#include <boost/asio/post.hpp>
#include <boost/asio/thread_pool.hpp>

namespace multi_queue_async_processor::tests {

BOOST_AUTO_TEST_CASE(Queue_Id)
{
    Queue queue{ 1_Id, 1 };
    BOOST_REQUIRE(queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 0);
    BOOST_REQUIRE_EQUAL(queue.GetId(), 1_Id);
}

BOOST_AUTO_TEST_CASE(Queue_ConsumerLogic)
{
    Queue queue{ 1_Id, 1 /* maxQueueSize */ };
    BOOST_REQUIRE(!queue.HasConsumer());

    queue.ResetConsumer(std::make_shared<MockConsumer>());
    BOOST_REQUIRE(queue.HasConsumer());

    queue.ResetConsumer();
    BOOST_REQUIRE(!queue.HasConsumer());
}

BOOST_AUTO_TEST_CASE(Queue_EnqueueDequeue)
{
    Queue queue{ 1_Id, 2 /* maxQueueSize */ };
    auto consumerRef{ std::make_shared<MockConsumer>() };

    BOOST_REQUIRE(!queue.Dequeue());
    queue.ResetConsumer(consumerRef);
    BOOST_REQUIRE(!queue.Dequeue());

    BOOST_REQUIRE(queue.Enqueue(1));
    BOOST_REQUIRE(!queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 1);

    BOOST_REQUIRE(queue.Enqueue(2));
    BOOST_REQUIRE(!queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 2);

    BOOST_REQUIRE(!queue.Enqueue(3));
    BOOST_REQUIRE(!queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 2);

    RequireDequeueEq(queue, 1, consumerRef);
    RequireDequeueEq(queue, 2, consumerRef);
    BOOST_REQUIRE(!queue.Dequeue());
    BOOST_REQUIRE(queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 0);
}

BOOST_AUTO_TEST_CASE(QueuesManager_Create)
{
    BOOST_REQUIRE_THROW((QueuesManager{ 0, 1 }), std::invalid_argument);
    BOOST_REQUIRE_THROW((QueuesManager{ 1, 0 }), std::invalid_argument);
    BOOST_REQUIRE_NO_THROW((QueuesManager{ 1, 1 }));
}

BOOST_AUTO_TEST_CASE(QueuesManager_SetRemoveConsumer)
{
    QueuesManager manager{ 1 /* maxQueueSize */, 2 /* maxQueuesNum*/ };
    auto consumer{ std::make_shared<MockConsumer>() };

    BOOST_REQUIRE(manager.AddConsumer(1_Id, consumer));
    BOOST_REQUIRE(!manager.AddConsumer(1_Id, consumer));
    BOOST_REQUIRE(manager.AddConsumer(2_Id, consumer));
    manager.RemoveConsumer(1_Id);
    BOOST_REQUIRE(manager.AddConsumer(1_Id, consumer));

    BOOST_REQUIRE(!manager.AddConsumer(3_Id, consumer));
}

BOOST_AUTO_TEST_CASE(QueuesManager_EnqueueGetQueueToProcess)
{
    constexpr size_t maxQueueSize{ 2 };
    constexpr size_t maxQueuesNum{ 2 };
    QueuesManager manager{maxQueueSize, maxQueuesNum };
    auto consumerRef{ std::make_shared<MockConsumer>() };

    BOOST_REQUIRE(!manager.HasQueueToDispatch());

    for (Value val{ 0 }; val < maxQueueSize; ++val)
    {
        for (QueueId id{ 0 }; id < maxQueuesNum; ++id)
        {
            BOOST_REQUIRE(manager.Enqueue(id, val));
        }
    }

    for (QueueId id{ 0 }; id < maxQueuesNum; ++id)
    {
        BOOST_REQUIRE(!manager.HasQueueToDispatch());
        BOOST_REQUIRE(manager.AddConsumer(id, consumerRef));
        BOOST_REQUIRE(manager.HasQueueToDispatch());

        auto queue{ manager.GetQueueToDispatch() };
        for (Value val{ 0 }; val < maxQueueSize; ++val)
        {
            RequireDequeueEq(**queue, val, consumerRef);
        }

        BOOST_REQUIRE(manager.HasQueueToDispatch());
        queue = manager.GetQueueToDispatch();
        BOOST_REQUIRE(!queue);
    }
}

BOOST_AUTO_TEST_CASE(QueuesManager_QueueSizeLimit)
{
    QueuesManager manager{ 1 /* maxQueueSize */, 1 /* maxQueuesNum*/ };
    BOOST_REQUIRE(manager.Enqueue(1_Id, 1));
    BOOST_REQUIRE(!manager.Enqueue(1_Id, 1));
}

BOOST_AUTO_TEST_CASE(QueuesManager_QueuesNumLimit)
{
    QueuesManager manager{ 1 /* maxQueueSize */, 1 /* maxQueuesNum*/ };
    BOOST_REQUIRE(manager.Enqueue(1_Id, 1));
    BOOST_REQUIRE(!manager.Enqueue(2_Id, 1));
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_Enqueue)
{
    auto [processor, manager] =
        CreateProcessorAndMockQueueManager(1 /* threadsNum */);

    BOOST_REQUIRE(!manager->GetEnqueuedVal());

    manager->ShouldEnqueueValues(true);
    BOOST_REQUIRE(processor->Enqueue(1_Id, 1));

    BOOST_REQUIRE(manager->GetEnqueuedVal());
    auto [queueId, value] = *manager->GetEnqueuedVal();
    BOOST_REQUIRE_EQUAL(queueId, 1_Id);
    BOOST_REQUIRE_EQUAL(value, 1);

    manager->ShouldEnqueueValues(false);
    manager->GetEnqueuedVal().reset();
    BOOST_REQUIRE(!processor->Enqueue(1_Id, 1));
    BOOST_REQUIRE(!manager->GetEnqueuedVal());
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_AddRemoveConsumers)
{
    auto consumer{ std::make_shared<MockConsumer>() };
    auto [processor, manager] =
        CreateProcessorAndMockQueueManager(1 /* threadsNum */);

    manager->ShouldAddConsumers(false);
    BOOST_REQUIRE(!processor->AddConsumer(1_Id, consumer));

    manager->ShouldAddConsumers(true);
    BOOST_REQUIRE(processor->AddConsumer(1_Id, consumer));
    BOOST_REQUIRE(manager->ContainsConsumer(1_Id, consumer));

    processor->RemoveConsumer(1_Id);
    BOOST_REQUIRE(!manager->ContainsConsumer(1_Id, consumer));
}

template<class QueueId>
void TestDispatch(const QueueId& id1, const QueueId& id2, const QueueId& id3)
{
    constexpr std::chrono::milliseconds timeout{ 1000 };
    MultiQueueAsyncProcessor<QueueId, Value> processor
    {
        2, /* threadsNum */
        4, /* maxQueueSize */
        3 /* maxQueuesNum */
    };

    using Consumer = mocks::MockConsumer<QueueId, Value>;
    auto consumer1{ std::make_shared<Consumer>() };
    auto consumer2{ std::make_shared<Consumer>() };
    auto consumer3{ std::make_shared<Consumer>() };

    processor.Enqueue(id1, 1);
    processor.Enqueue(id2, 1);

    processor.AddConsumer(id1, consumer1);
    processor.AddConsumer(id2, consumer2);
    processor.AddConsumer(id3, consumer3);
    processor.RemoveConsumer(id3);

    processor.Enqueue(id1, 2);
    processor.Enqueue(id2, 2);
    processor.Enqueue(id3, 2);

    bool isConsumed1{ consumer1->WaitTillConsumed(2, timeout) };
    bool isConsumed2{ consumer2->WaitTillConsumed(2, timeout) };

    BOOST_REQUIRE(isConsumed1);
    RequireConsumedEq(consumer1->GetConsumedVals()[0], id1, Value{ 1 });
    RequireConsumedEq(consumer1->GetConsumedVals()[1], id1, Value{ 2 });

    BOOST_REQUIRE(isConsumed2);
    RequireConsumedEq(consumer2->GetConsumedVals()[0], id2, 1);
    RequireConsumedEq(consumer2->GetConsumedVals()[1], id2, 2);

    BOOST_REQUIRE(consumer3->GetConsumedVals().empty());
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_ValuesDispatch)
{
    TestDispatch<QueueId>(1_Id, 2_Id, 3_Id);
}

template<class QueueId, class Value>
void TestHeavyLoad(size_t queuesNum, size_t valsPerQueue)
{
    MultiQueueAsyncProcessor<QueueId, Value> processor
    {
        std::thread::hardware_concurrency(),
        valsPerQueue,
        queuesNum
    };

    using Consumer = mocks::MockConsumer<QueueId, Value>;
    std::vector<std::shared_ptr<Consumer>> consumers;

    std::vector<QueueId> ids;
    for (size_t id{ 0 }; id < queuesNum; ++id)
    {
        ids.push_back(GetTestKey<QueueId>(id));
        consumers.push_back(
            std::make_shared<Consumer>(false /* storeConsumed */));

        processor.AddConsumer(ids.back(), consumers.back());
    }

    boost::asio::thread_pool pool;
    const Value val{ GetTestVal<Value>() };
    std::atomic_bool allEnqueued{ true };

    auto start{ std::chrono::steady_clock::now() };
    for (size_t i{ 0 }; i < valsPerQueue && allEnqueued; ++i)
    {
        for (auto& id : ids)
        {
            boost::asio::post(pool, [&allEnqueued, &processor, id, &val]{
                allEnqueued = allEnqueued && processor.Enqueue(id, val);
            });
        }
    }

    BOOST_REQUIRE(allEnqueued);
    pool.join();

    for (auto& consumer : consumers)
    {
        consumer->WaitTillConsumed(valsPerQueue, std::chrono::seconds{ 5 });
        BOOST_REQUIRE_EQUAL(consumer->GetConsumedNum(), valsPerQueue);
    }

    auto end{ std::chrono::steady_clock::now() };
    auto msPassed{ std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start) };

    uint64_t processedPerSec{
        (queuesNum * valsPerQueue) / msPassed.count() * 1000 };

    BOOST_TEST_MESSAGE(
        "Queues: " << queuesNum
        << ", vals per queue: " << valsPerQueue
        << ", total enqueues: " << queuesNum * valsPerQueue
        << ", avg speed: " << processedPerSec << "/sec"
        << ", time: " << msPassed.count() << " ms");
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_HeavyLoad_QueuesGrowth)
{
    constexpr size_t valsPerQueue{ 1000 };

    // 5.000.000/10.000.000/15.000.000
    auto queueNums = { 5000, 10000, 15000 };

    BOOST_TEST_MESSAGE("Strings...");
    for (size_t queuesNum : queueNums)
    {
        TestHeavyLoad<std::string, std::string>(queuesNum, valsPerQueue);
    }

    BOOST_TEST_MESSAGE("Ints...");
    for (size_t queuesNum : queueNums)
    {
        TestHeavyLoad<int, int>(queuesNum, valsPerQueue);
    }
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_HeavyLoad_ValsGrowth)
{
    constexpr size_t queuesNum{ 100 };

    // 5.000.000/10.000.000/15.000.000
    auto valsPerQueues = { 50000, 100000, 150000 };

    BOOST_TEST_MESSAGE("Strings...");
    for (size_t valsPerQueue : valsPerQueues)
    {
        TestHeavyLoad<std::string, std::string>(queuesNum, valsPerQueue);
    }

    BOOST_TEST_MESSAGE("Ints...");
    for (size_t valsPerQueue : valsPerQueues)
    {
        TestHeavyLoad<int, int>(queuesNum, valsPerQueue);
    }
}

}// multi_queue_async_processor::tests
