#include "asynctask.h"

namespace cncpp
{
    // MainThreadTaskQueue 类实现
    MainThreadTaskQueue::MainThreadTaskQueue() : stop_(false)
    {
    }

    MainThreadTaskQueue::~MainThreadTaskQueue()
    {
        Stop();
    }

    // 添加任务到主线程队列
    void MainThreadTaskQueue::AddTask(std::function<void()> task)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        tasks_.push(std::move(task));
    }

    // 处理队列中的任务（非阻塞）
    void MainThreadTaskQueue::ProcessTasks()
    {
        std::queue<std::function<void()>> tasks_to_process = {};

        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_ || tasks_.empty())
            {
                return;
            }

            std::swap(tasks_to_process, tasks_);
        }

        // 在无锁状态下处理所有任务
        while (!tasks_to_process.empty())
        {
            tasks_to_process.front()();
            tasks_to_process.pop();
        }
    }

    // 停止任务队列
    void MainThreadTaskQueue::Stop()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
    }

    // AsyncTask 类实现
    template <typename T>
    AsyncTask<T>::~AsyncTask()
    {
    }

    // 设置任务完成回调
    template <typename T>
    void AsyncTask<T>::SetCallback(std::function<void(const TaskResult<T>&)> callback)
    {
        callback_ = callback;
    }

    // 设置主线程任务队列
    template <typename T>
    void AsyncTask<T>::SetMainThreadTaskQueue(std::shared_ptr<MainThreadTaskQueue> queue)
    {
        main_thread_queue_ = queue;
    }

    // 任务完成时调用
    template <typename T>
    void AsyncTask<T>::OnComplete(const TaskResult<T>& result)
    {
        if (callback_)
        {
            if (main_thread_queue_)
            {
                // 将回调放入主线程队列
                main_thread_queue_->AddTask([this, result]() {
                    callback_(result);
                });
            }
            else
            {
                // 直接执行回调
                callback_(result);
            }
        }
    }

    // AsyncTaskManager 类实现
    AsyncTaskManager::AsyncTaskManager()
    {
    }

    AsyncTaskManager::~AsyncTaskManager()
    {
        Stop();
    }

    bool AsyncTaskManager::Start()
    {
        if (stop_)
            return false;

        if (!main_thread_queue_)
        {
            main_thread_queue_ = std::make_shared<MainThreadTaskQueue>();
        }

        stop_ = false;

        const uint16_t thread_count = std::max<uint16_t>(1, sMainConfig.async_thread_count());
        for (uint16_t i = 0; i < thread_count; ++i)
        {
            threads_.emplace_back([this] {
                WorkerThread();
            });
        }

        return true;
    }

    // 停止任务管理器
    void AsyncTaskManager::Stop()
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
            condition_.notify_all();
        }

        processTasksDone();

        // 等待所有线程结束
        for (auto& thread : threads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }

        if (main_thread_queue_)
        {
            main_thread_queue_->Stop();
        }
    }

    // 工作线程函数
    void AsyncTaskManager::WorkerThread()
    {
        while (true)
        {
            std::function<void()> task;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });

                if (stop_ && tasks_.empty())
                {
                    return;
                }

                task = std::move(tasks_.front());
                tasks_.pop();
            }

            task();
        }
    }

    void AsyncTaskManager::processTasksDone()
    {
        if (main_thread_queue_)
        {
            main_thread_queue_->ProcessTasks();
        }
    }

#if 0
    // MySQLAsyncTask 类实现
    template <typename T>
    MySQLAsyncTask<T>::MySQLAsyncTask(const std::string& query) : query_(query)
    {
    }


    template <typename T>
    TaskResult<T> MySQLAsyncTask<T>::Execute()
    {
        // 这里应该实现实际的MySQL操作  
        // 例如：连接数据库、执行查询、获取结果
        // 为了演示，这里返回模拟结果
        LOG_INFO("Executing MySQL query: {}", query_);

        // 模拟数据库操作延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // 返回模拟结果
        T result;
        return TaskResult<T>(true, result, "");
    }
#endif

    // 显式实例化模板类
    template class AsyncTask<int>;
    template class AsyncTask<std::string>;
    template class AsyncTask<std::vector<int>>;
    // template class MySQLAsyncTask<int>;
    // template class MySQLAsyncTask<std::string>;
    // template class MySQLAsyncTask<std::vector<int>>;

}  // namespace cncpp
