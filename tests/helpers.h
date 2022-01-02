#pragma once

#include "mocks.h"
#include <boost/test/unit_test.hpp>

namespace multi_queue_async_processor::tests {

using QueueId = uint64_t;
using Value = uint64_t;
using QueueUnderlyingContainer = std::list<Value>;
using Queue = detail::Queue<QueueId, Value, QueueUnderlyingContainer>;
using QueuesManager
    = detail::QueuesManager<QueueId, Value, QueueUnderlyingContainer>;
using MockConsumer = mocks::MockConsumer<QueueId, Value>;
using MockQueueManager
    = mocks::MockQueuesManager<QueueId, Value, QueueUnderlyingContainer>;
using Processor
    = detail::MultiQueueAsyncProcessor<QueueId, Value, MockQueueManager>;

constexpr QueueId operator "" _Id(unsigned long long val) noexcept
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

template<class T>
T GetTestVal()
{
    if constexpr(std::is_integral_v<T>)
    {
        return {};
    }
    else if constexpr(std::is_same_v<T, std::string>)
    {
        constexpr size_t valueLen{ 10 };
        return std::string(valueLen, '.');
    }
    else
    {
        static_assert(!"Unhandled key type");
    }
}

template<class T>
T GetTestQueueId(size_t seed)
{
    if constexpr(std::is_integral_v<T>)
    {
        if (seed > std::numeric_limits<T>::max())
            throw std::invalid_argument{ "Id out of bounds" };

        return static_cast<T>(seed);
    }
    else if constexpr(std::is_same_v<T, std::string>)
    {
        constexpr size_t idFixedLen{ 10 };
        std::string id{ std::to_string(seed) };
        if (id.length() > idFixedLen)
            throw std::out_of_range{ "Id is too long" };

        size_t leadingZerosNum{ idFixedLen - id.length() };
        return std::string(leadingZerosNum, '0') + std::move(id);
    }
    else
    {
        static_assert(!"Unhandled value type");
    }
}

}// multi_queue_async_processor::tests
