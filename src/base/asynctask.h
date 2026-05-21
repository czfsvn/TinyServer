#ifndef NETWORK_LIB_H
#define NETWORK_LIB_H

#include <boost/asio.hpp>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include "compression.h"
#include "dual_lock_free_queue.h"
#include "encryption.h"
#include "logger.h"
#include "message.h"

namespace cncpp
{
    // 错误码定义
    enum class ErrorCode
    {
        SUCCESS = 0,
        NETWORK_ERROR = 1,
        ENCRYPTION_ERROR = 2,
        COMPRESSION_ERROR = 3,
        INVALID_MESSAGE = 4,
        QUEUE_FULL = 5,
        UNKNOWN_ERROR = 999
    };

    // 直接使用 logger.h 中的日志宏

    using boost::asio::ip::tcp;

    // 任务结果结构体
    template <typename T>
    struct TaskResult
    {
        bool success_;       // 任务是否成功
        T result_;           // 任务结果
        std::string error_;  // 错误信息

        TaskResult();

        TaskResult(bool success, const T& result, const std::string& error = "");
    };

    // 主线程任务队列
    class MainThreadTaskQueue
    {
    public:
        MainThreadTaskQueue();
        ~MainThreadTaskQueue();

        // 添加任务到主线程队列
        void AddTask(std::function<void()> task);

        // 处理队列中的任务（非阻塞）
        void ProcessTasks();

        // 停止任务队列
        void Stop();

    private:
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        bool stop_;
    };

    // 异步任务接口
    template <typename T>
    class AsyncTask
    {
    public:
        virtual ~AsyncTask();

        // 执行任务
        virtual TaskResult<T> Execute() = 0;

        // 设置任务完成回调
        void SetCallback(std::function<void(const TaskResult<T>&)> callback);

        // 设置主线程任务队列
        void SetMainThreadTaskQueue(std::shared_ptr<MainThreadTaskQueue> queue);

        // 任务完成时调用
        void OnComplete(const TaskResult<T>& result);

    private:
        std::function<void(const TaskResult<T>&)> callback_;
        std::shared_ptr<MainThreadTaskQueue> main_thread_queue_ = nullptr;
    };

    // 异步任务管理器
    class AsyncTaskManager : public Singleton<AsyncTaskManager>
    {
    public:
        AsyncTaskManager();
        ~AsyncTaskManager();

        // 停止任务管理器
        bool Start();
        void Stop();

        // 添加任务
        template <typename T>
        void AddTask(std::shared_ptr<AsyncTask<T>> task)
        {
            // 设置主线程任务队列
            if (main_thread_queue_)
            {
                task->SetMainThreadTaskQueue(main_thread_queue_);
            }

            std::unique_lock<std::mutex> lock(mutex_);
            tasks_.push([task]() {
                auto result = task->Execute();
                task->OnComplete(result);
            });
            condition_.notify_one();
        }

        void processTasksDone();

    private:
        // 工作线程函数
        void WorkerThread();

        std::vector<std::thread> threads_;
        std::queue<std::function<void()>> tasks_;
        std::mutex mutex_;
        std::condition_variable condition_;
        bool stop_;
        std::shared_ptr<MainThreadTaskQueue> main_thread_queue_ = nullptr;
    };

#if 0
    // MySQL异步任务示例
    template <typename T>
    class MySQLAsyncTask : public AsyncTask<T>
    {
    public:
        MySQLAsyncTask(const std::string& query);

        TaskResult<T> Execute() override;

    private:
        std::string query_;
    };
#endif

}  // namespace cncpp

#define sAsyncTaskManager cncpp::AsyncTaskManager::getMe()

#endif  // NETWORK_LIB_H