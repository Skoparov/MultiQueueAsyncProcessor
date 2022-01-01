#pragma once

#include <list>
#include <queue>
#include <mutex>
#include <memory>
#include <thread>
#include <atomic>
#include <cassert>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <condition_variable>

namespace multi_queue_async_processor {

template<class QueueId, class Value>
struct IConsumer
{
    virtual void Consume(const QueueId& id, Value&& value) noexcept = 0;
    virtual ~IConsumer() = default;
};

namespace detail{

template<class QueueId, class Value, class UnderlyingContainer>
class Queue
{
public:
    Queue(const QueueId& id, size_t maxSize)
        : m_id{ id }
        , m_maxSize{ maxSize }
    {
        assert(m_maxSize != 0);
    }

    template<class T>
    bool Enqueue(T&& value)
    {
        std::lock_guard l{ m_dataMutex };
        if (m_queue.size() == m_maxSize)
            return false;

        m_queue.emplace(std::forward<T>(value));
        return true;
    }

    std::optional<std::pair<Value, std::shared_ptr<IConsumer<QueueId, Value>>>>
        Dequeue()
    {
        std::lock_guard l{ m_dataMutex };
        if (m_queue.empty() || !m_consumer)
            return std::nullopt;

        auto res{ std::make_pair(std::move(m_queue.front()), m_consumer) };
        m_queue.pop();
        return res;
    }

    size_t Size() const
    {
        std::shared_lock l{ m_dataMutex };
        return m_queue.size();
    }

    bool Empty()
    {
        std::shared_lock l{ m_dataMutex };
        return m_queue.empty();
    }

    const QueueId& GetId() const noexcept
    {
        return m_id;
    }

    void ResetConsumer(std::shared_ptr<IConsumer<QueueId,Value>> consumer = {})
    {
        std::lock_guard l{ m_dataMutex };
        m_consumer = std::move(consumer);
    }

    bool HasConsumer() const
    {
        std::shared_lock l{ m_dataMutex };
        return m_consumer != nullptr;
    }

private:
    const QueueId m_id;
    const size_t m_maxSize;
    std::queue<Value, UnderlyingContainer> m_queue;
    std::shared_ptr<IConsumer<QueueId,Value>> m_consumer;

    mutable std::shared_mutex m_dataMutex;
};

template<class QueueId, class Value, class QueuesUnderlyingContainer>
class QueuesManager
{
public:
    using Queue = Queue<QueueId, Value, QueuesUnderlyingContainer>;

private:
    struct QueueData
    {
        std::shared_ptr<Queue> queue;
        typename std::list<std::shared_ptr<Queue>>::iterator dispatchQueueIt;
    };

public:
    QueuesManager(size_t maxQueueSize, size_t maxQueuesNum)
        : m_maxQueueSize{ maxQueueSize }
        , m_maxQueuesNum{ maxQueuesNum }
    {
        if (!m_maxQueueSize)
            throw std::invalid_argument{ "Max queue size is 0" };

        if (!m_maxQueuesNum)
            throw std::invalid_argument{ "Max queues num is 0" };
    }

    template<class T>
    bool Enqueue(const QueueId& id, T&& value)
    {
        std::optional<QueueData*> dataOpt{ GetOrCreateQueueData(id) };
        if (!dataOpt)
            return false;

        QueueData& data{ **dataOpt };
        if (!data.queue->Enqueue(std::forward<T>(value)))
            return false;

        if (data.queue->HasConsumer() &&
            data.dispatchQueueIt == m_queuesToDispatch.end())
        {
            AddToDispatchQueue(data);
        }

        return true;
    }

    std::optional<std::shared_ptr<Queue>> GetQueueToDispatch()
    {
        auto queue{ m_queuesToDispatch.front() };
        if (queue->Empty())
        {
            QueueData& data{ m_queuesById[queue->GetId()] };
            RemoveFromDispatchQueue(data);
            return std::nullopt;
        }

        return queue;
    }

    bool HasQueueToDispatch() const noexcept
    {
        return !m_queuesToDispatch.empty();
    }

    bool AddConsumer(
        const QueueId& id,
        std::shared_ptr<IConsumer<QueueId, Value>> consumer)
    {
        std::optional<QueueData*> dataOpt{ GetOrCreateQueueData(id) };
        if (!dataOpt)
            return false;

        QueueData& data{ **dataOpt };
        if (data.queue->HasConsumer())
            return false;

        data.queue->ResetConsumer(std::move(consumer));
        if (!data.queue->Empty())
        {
            AddToDispatchQueue(data);
        }

        return true;
    }

    void RemoveConsumer(const QueueId& id)
    {
        auto it{ m_queuesById.find(id) };
        if (it == m_queuesById.end())
            return;

        QueueData& data{ it->second };
        if (data.queue->HasConsumer())
        {
            data.queue->ResetConsumer();
            RemoveFromDispatchQueue(data);
        }

        if (data.queue->Empty())
        {
            m_queuesById.erase(it);
        }
    }

private:
    std::optional<QueueData*> GetOrCreateQueueData(const QueueId& id)
    {
        auto it{ m_queuesById.find(id) };
        if (it == m_queuesById.end())
        {
            if (m_queuesById.size() == m_maxQueuesNum)
                return std::nullopt;

            std::tie(it, std::ignore) = m_queuesById.emplace(
                id,
                QueueData
                {
                    std::make_shared<Queue>(id, m_maxQueueSize),
                    m_queuesToDispatch.end()
                });
        }

        return &it->second;
    }

    void AddToDispatchQueue(QueueData& data)
    {
        m_queuesToDispatch.push_back(data.queue);
        data.dispatchQueueIt = std::prev(m_queuesToDispatch.end());
    }

    void RemoveFromDispatchQueue(QueueData& data)
    {
        if (data.dispatchQueueIt != m_queuesToDispatch.end())
        {
            m_queuesToDispatch.erase(data.dispatchQueueIt);
            data.dispatchQueueIt = m_queuesToDispatch.end();
        }
    }

private:
    const size_t m_maxQueueSize;
    const size_t m_maxQueuesNum;
    std::list<std::shared_ptr<Queue>> m_queuesToDispatch;
    std::unordered_map<QueueId, QueueData> m_queuesById;
};

template<class QueueId, class Value, class QueuesManager>
class MultiQueueAsyncProcessor
{
public:
    MultiQueueAsyncProcessor(
        size_t threadsNum,
        std::unique_ptr<QueuesManager> queuesManager)
            : m_queuesManager{ std::move(queuesManager) }
    {
        if (!m_queuesManager)
            throw std::invalid_argument{ "Queues manager is null" };

        if (!threadsNum)
            throw std::invalid_argument{ "Threads num is 0" };

        try
        {
            StartWorkers(threadsNum);
        }
        catch (...)
        {
            StopWorkers();
            throw;
        }
    }

    ~MultiQueueAsyncProcessor()
    {
        try
        {
            StopWorkers();
        }
        catch (...)
        {
            // log exception
        }
    }

    template<class T>
    bool Enqueue(const QueueId& id, T&& value)
    {
        std::lock_guard l{ m_queueManagerMutex };
        if (m_stop || !m_queuesManager->Enqueue(id, std::forward<T>(value)))
            return false;

        m_waitingForQueuesToDispatchCv.notify_one();
        return true;
    }

    bool AddConsumer(
        const QueueId& id,
        std::shared_ptr<IConsumer<QueueId, Value>> consumer)
    {
        std::lock_guard l{ m_queueManagerMutex };
        return m_queuesManager->AddConsumer(id, std::move(consumer));
    }

    void RemoveConsumer(const QueueId& id)
    {
        std::lock_guard l{ m_queueManagerMutex };
        m_queuesManager->RemoveConsumer(id);
    }

private:
    void StartWorkers(size_t threadsNum)
    {
        for (size_t i{ 0 }; i < threadsNum; ++i)
        {
            m_workers.emplace_back(
                &MultiQueueAsyncProcessor::ProcessCycle,
                this);
        }
    }

    void StopWorkers()
    {
        m_stop = true;
        m_waitingForQueuesToDispatchCv.notify_all();

        for (auto& thread : m_workers)
        {
            thread.join();
        }

        m_workers.clear();
    }

    std::optional<std::shared_ptr<typename QueuesManager::Queue>>
        GetQueueToDispatch()
    {
        std::unique_lock l{ m_queueManagerMutex };
        m_waitingForQueuesToDispatchCv.wait(l, [&]{
            return m_queuesManager->HasQueueToDispatch() || m_stop;
        });

        if (m_stop)
            return nullptr;

        return m_queuesManager->GetQueueToDispatch();
    }

    void ProcessCycle()
    {
        while (!m_stop)
        {
            try
            {
                // Having multiple workers may lead to values being consumed
                // out of order (kind of like RabbitMq for example). If we need
                // to maintain strict consumption order, we'll have to limit the
                // number of workers to 1 or somehow shard queues by worker ids
                auto queue{ GetQueueToDispatch() };
                while (!m_stop && queue)
                {
                    auto valueAndConsumer{ (*queue)->Dequeue() };
                    if (!valueAndConsumer)
                        break;

                    auto& [value, consumer] = *valueAndConsumer;
                    consumer->Consume((*queue)->GetId(), std::move(value));
                }
            }
            catch (...)
            {
                // log exception in a thread safe manner
            }
        }
    }

private:
    std::unique_ptr<QueuesManager> m_queuesManager;

    std::atomic_bool m_stop{ false };
    std::vector<std::thread> m_workers;

    mutable std::mutex m_queueManagerMutex;
    std::condition_variable m_waitingForQueuesToDispatchCv;
};

}// detail

template<
    class QueueId,
    class Value,
    class QueuesUnderlyingContainer = std::list<Value>>
class MultiQueueAsyncProcessor
    : public detail::MultiQueueAsyncProcessor<
        QueueId,
        Value,
        detail::QueuesManager<QueueId, Value, QueuesUnderlyingContainer>>
{
    using QueuesManager
        = detail::QueuesManager<QueueId, Value, QueuesUnderlyingContainer>;

public:
    MultiQueueAsyncProcessor(
        size_t threadsNum,
        size_t maxQueueSize,
        size_t maxQueuesNum)
            : detail::MultiQueueAsyncProcessor<QueueId, Value, QueuesManager>{
                  threadsNum,
                  std::make_unique<QueuesManager>(maxQueueSize, maxQueuesNum) }
    {}
};

}// multi_queue_async_processor
