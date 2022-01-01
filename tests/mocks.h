#pragma once

#include <MultiQueueAsyncProcessor.h>

namespace multi_queue_async_processor::tests::mocks{

template<class QueueId, class Value>
class MockConsumer : public IConsumer<QueueId, Value>
{
public:
    explicit MockConsumer(bool storeConsumed = true) noexcept
        : m_storeConsumed{ storeConsumed }
    {}

    void Consume(const QueueId& id, Value&& val) noexcept final
    {
        std::lock_guard l{ m_consumedValsMutex };

        ++m_consumedNum;
        if (m_storeConsumed)
        {
            m_consumedVals.emplace_back(id, val);
        }

        m_waitTillConsumed.notify_one();
    }

    template<class Rep, class Period>
    bool WaitTillConsumed(
        size_t itemsConsumed,
        std::chrono::duration<Rep, Period> timeout)
    {
        std::unique_lock l{ m_consumedValsMutex };
        return m_waitTillConsumed.wait_for(l, timeout, [&]{
            return m_consumedNum == itemsConsumed;
        });
    }

    size_t GetConsumedNum() const noexcept
    {
        return m_consumedNum;
    }

    const std::vector<std::pair<QueueId, Value>>&
        GetConsumedVals() const noexcept
    {
        return m_consumedVals;
    }

private:
    const bool m_storeConsumed;
    std::mutex m_consumedValsMutex;
    std::condition_variable m_waitTillConsumed;

    size_t m_consumedNum{ 0 };
    std::vector<std::pair<QueueId, Value>> m_consumedVals;
};

template<class QueueId, class Value, class QueuesUnderlyingContainer>
class MockQueuesManager
{
public:
    using Queue = detail::Queue<QueueId, Value, QueuesUnderlyingContainer>;

public:
    template<class T>
    bool Enqueue(const QueueId& id, T&& value)
    {
        if (!m_enqueueValues)
            return false;

        m_enqueuedVal = { id, std::move(value) };
        return true;
    }

    std::shared_ptr<Queue> GetQueueToDispatch()
    {
        return {};
    }

    bool HasQueueToDispatch() const noexcept
    {
        return false;
    }

    bool AddConsumer(
        const QueueId& id,
        std::shared_ptr<IConsumer<QueueId, Value>> consumer)
    {
        if (!m_addConsumers)
            return false;

        m_consumers[id] = std::move(consumer);
        return true;
    }

    void RemoveConsumer(const QueueId& id)
    {
        m_consumers.erase(id);
    }

    bool ContainsConsumer(
        const QueueId& id,
        const std::shared_ptr<IConsumer<QueueId, Value>>& refConsumer) const
    {
        auto it{ m_consumers.find(id) };
        return it != m_consumers.end()?
            it->second == refConsumer:
            false;
    }

    void ShouldEnqueueValues(bool enqueueValues) noexcept
    {
        m_enqueueValues = enqueueValues;
    }

    void ShouldAddConsumers(bool addConsumers) noexcept
    {
        m_addConsumers = addConsumers;
    }

    std::optional<std::pair<QueueId, Value>>& GetEnqueuedVal() noexcept
    {
        return m_enqueuedVal;
    }

private:
    bool m_enqueueValues{ true };
    bool m_addConsumers{ true };

    std::optional<std::pair<QueueId, Value>> m_enqueuedVal;
    std::unordered_map<
        QueueId,
        std::shared_ptr<IConsumer<QueueId, Value>>> m_consumers;
};

}// multi_queue_async_processor::tests::mocks
