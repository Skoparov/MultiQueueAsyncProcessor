#define BOOST_TEST_MODULE MultiQueueAsyncProcessorTests

#include "mocks.h"
#include <boost/asio/post.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/asio/thread_pool.hpp>

namespace multi_queue_async_processor::tests {

using QueueId = uint64_t;
using Value = uint64_t;
using QueueUnderlyingContainer = std::list<Value>;
using Queue = detail::Queue<QueueId, Value, QueueUnderlyingContainer>;
using QueuesManager
    = detail::QueuesManager<QueueId, Value, QueueUnderlyingContainer>;
using MockConsumer = mocks::MockConsumer<QueueId, Value>;
using MockQueueManager
    = mocks::MockQueueManager<QueueId, Value, QueueUnderlyingContainer>;
using Processor
    = detail::MultiQueueAsyncProcessor<QueueId, Value, MockQueueManager>;

constexpr QueueId operator "" _ID(unsigned long long val) noexcept
{
    return val;
}

void RequireDequeueEq(
    Queue& queue,
    Value valueRef,
    const std::shared_ptr<MockConsumer>& consumerRef)
{
    auto valueAndConsumer{ queue.Dequeue() };
    BOOST_REQUIRE(valueAndConsumer);

    auto& [value, consumer] = *valueAndConsumer;
    BOOST_REQUIRE_EQUAL(value, valueRef);
    BOOST_REQUIRE_EQUAL(consumer, consumerRef);
}

template<class Id, class Val>
void RequireConsumedEq(
    const std::pair<Id, Val>& consumed,
    const Id& refId,
    const std::decay_t<Val>& refVal)
{
    BOOST_REQUIRE_EQUAL(consumed.first, refId);
    BOOST_REQUIRE_EQUAL(consumed.second, refVal);
}

auto CreateProcessorAndMockQueueManager(size_t threadsNum)
{
    auto queueManager{ std::make_unique<MockQueueManager>() };
    auto dispatcherPtr{ queueManager.get() };

    return std::make_pair
    (
        std::make_unique<Processor>(threadsNum, std::move(queueManager)),
        dispatcherPtr
    );
}

BOOST_AUTO_TEST_CASE(Queue_Id)
{
    Queue queue{ 1_ID, 1 };
    BOOST_REQUIRE(queue.Empty());
    BOOST_REQUIRE_EQUAL(queue.Size(), 0);
    BOOST_REQUIRE_EQUAL(queue.GetId(), 1_ID);
}

BOOST_AUTO_TEST_CASE(Queue_ConsumerLogic)
{
    Queue queue{ 1_ID, 1 /* maxQueueSize */ };
    BOOST_REQUIRE(!queue.HasConsumer());

    queue.ResetConsumer(std::make_shared<MockConsumer>());
    BOOST_REQUIRE(queue.HasConsumer());

    queue.ResetConsumer();
    BOOST_REQUIRE(!queue.HasConsumer());
}

BOOST_AUTO_TEST_CASE(Queue_EnqueueDequeue)
{
    Queue queue{ 1_ID, 2 /* maxQueueSize */ };
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

    BOOST_REQUIRE(manager.AddConsumer(1_ID, consumer));
    BOOST_REQUIRE(!manager.AddConsumer(1_ID, consumer));
    BOOST_REQUIRE(manager.AddConsumer(2_ID, consumer));
    manager.RemoveConsumer(1_ID);
    BOOST_REQUIRE(manager.AddConsumer(1_ID, consumer));

    BOOST_REQUIRE(!manager.AddConsumer(3_ID, consumer));
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
            RequireDequeueEq(*queue, val, consumerRef);
        }

        BOOST_REQUIRE(manager.HasQueueToDispatch());
        queue = manager.GetQueueToDispatch();
        BOOST_REQUIRE(!queue->Dequeue());
    }
}

BOOST_AUTO_TEST_CASE(QueuesManager_QueueSizeLimit)
{
    QueuesManager manager{ 1 /* maxQueueSize */, 1 /* maxQueuesNum*/ };
    BOOST_REQUIRE(manager.Enqueue(1_ID, 1));
    BOOST_REQUIRE(!manager.Enqueue(1_ID, 1));
}

BOOST_AUTO_TEST_CASE(QueuesManager_QueuesNumLimit)
{
    QueuesManager manager{ 1 /* maxQueueSize */, 1 /* maxQueuesNum*/ };
    BOOST_REQUIRE(manager.Enqueue(1_ID, 1));
    BOOST_REQUIRE(!manager.Enqueue(2_ID, 1));
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_Enqueue)
{
    auto [processor, manager] =
        CreateProcessorAndMockQueueManager(1 /* threadsNum */);

    BOOST_REQUIRE(!manager->GetEnqueuedVal());

    manager->ShouldEnqueueValues(true);
    BOOST_REQUIRE(processor->Enqueue(1_ID, 1));

    BOOST_REQUIRE(manager->GetEnqueuedVal());
    auto [queueId, value] = *manager->GetEnqueuedVal();
    BOOST_REQUIRE_EQUAL(queueId, 1_ID);
    BOOST_REQUIRE_EQUAL(value, 1);

    manager->ShouldEnqueueValues(false);
    manager->GetEnqueuedVal().reset();
    BOOST_REQUIRE(!processor->Enqueue(1_ID, 1));
    BOOST_REQUIRE(!manager->GetEnqueuedVal());
}

BOOST_AUTO_TEST_CASE(MultiQueueAsyncProcessor_AddRemoveConsumers)
{
    auto consumer{ std::make_shared<MockConsumer>() };
    auto [processor, manager] =
        CreateProcessorAndMockQueueManager(1 /* threadsNum */);

    manager->ShouldAddConsumers(false);
    BOOST_REQUIRE(!processor->AddConsumer(1_ID, consumer));

    manager->ShouldAddConsumers(true);
    BOOST_REQUIRE(processor->AddConsumer(1_ID, consumer));
    BOOST_REQUIRE(manager->ContainsConsumer(1_ID, consumer));

    processor->RemoveConsumer(1_ID);
    BOOST_REQUIRE(!manager->ContainsConsumer(1_ID, consumer));
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
    TestDispatch<QueueId>(1_ID, 2_ID, 3_ID);
}

}// multi_queue_async_processor::tests
