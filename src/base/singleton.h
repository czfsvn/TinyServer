#pragma once

#include <memory>
#include <mutex>

namespace cncpp
{
    template <typename T>
    class Singleton
    {
    protected:
        Singleton() = default;
        ~Singleton() = default;

        // 禁止拷贝和赋值
        Singleton(const Singleton&) = delete;
        Singleton& operator=(const Singleton&) = delete;

        static T instance_;

    public:
        // 获取单例实例
        static T& getMe()
        {
            return instance_;
        }
    };

    // 线程安全的单例模板（使用 std::call_once）
    template <typename T>
    class ThreadSafeSingleton
    {
    private:
        static std::unique_ptr<T> instance_;
        static std::once_flag init_flag_;

    protected:
        ThreadSafeSingleton() = default;
        ~ThreadSafeSingleton() = default;

        // 禁止拷贝和赋值
        ThreadSafeSingleton(const ThreadSafeSingleton&) = delete;
        ThreadSafeSingleton& operator=(const ThreadSafeSingleton&) = delete;

    public:
        // 获取单例实例
        static T& getMe()
        {
            std::call_once(init_flag_, []() { instance_.reset(new T()); });
            return *instance_;
        }

        // 销毁单例实例（可选，用于测试或特殊场景）
        static void DestroyMe()
        {
            instance_.reset();
        }
    };

    // 初始化静态成员
    template <typename T>
    std::unique_ptr<T> ThreadSafeSingleton<T>::instance_ = nullptr;

    template <typename T>
    std::once_flag ThreadSafeSingleton<T>::init_flag_;

    template <typename T>
    T Singleton<T>::instance_;

}  // namespace cncpp