/*
 * @Author: czf
 * @Date: 2022-01-16 18:25:35
 * @LastEditors: czf
 * @LastEditTime: 2022-02-24 21:05:36
 * @FilePath: \cpp_project2022\src\base\Misc.h
 * @Description:
 *
 * Copyright (c) 2022 by 用户/公司名, All Rights Reserved.
 */
#ifndef __MISC_20220116_H__
#define __MISC_20220116_H__

#include <thread>

#define SAFE_SUB(x, y) ((x) > (y) ? (x - y) : 0)

#define SAFE_DELETE(x)     \
    {                      \
        if (x)             \
        {                  \
            delete (x);    \
            (x) = nullptr; \
        }                  \
    }

#define SAFE_DELETE_ARR(x) \
    {                      \
        if (x)             \
        {                  \
            delete[] (x);  \
            (x) = nullptr; \
        }                  \
    }

namespace cncpp
{
    // 秒
    void inline sleepfor_seconds(const uint32_t sec)
    {
        std::this_thread::sleep_for(std::chrono::seconds(sec));
    }
    // 毫秒
    void inline sleepfor_milliseconds(const uint32_t mill_sec)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(mill_sec));
    }
    // 微秒
    void inline sleepfor_microseconds(const uint32_t micr_sec)
    {
        std::this_thread::sleep_for(std::chrono::microseconds(micr_sec));
    }
    // 纳秒
    void inline sleepfor_nanoseconds(const uint32_t nano_sec)
    {
        std::this_thread::sleep_for(std::chrono::nanoseconds(nano_sec));
    }

    const std::thread::id inline getThreadId()
    {
        return std::this_thread::get_id();
    }

    // 支持普通指针
    template <typename T, typename... Args>
    inline typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    // 支持动态数组
    template <class T>
    inline typename std::enable_if<std::is_array<T>::value && std::extent<T>::value == 0, std::unique_ptr<T>>::type
    make_unique(size_t size)
    {
        typedef typename std::remove_extent<T>::type U;
        return std::unique_ptr<T>(new U[size]());
    }

    // 过滤掉定长数组的情况
    template <class T, class... Args>
    typename std::enable_if<std::extent<T>::value != 0, void>::type make_unique(Args&&...) = delete;

    template <class T, class R, typename... Args>
    class MyDelegate
    {
    public:
        MyDelegate(T* t, R (T::*f)(Args...)) : m_t(t), m_f(f)
        {
        }

        R operator()(Args&&... args)
        {
            return (m_t->*m_f)(std::forward<Args>(args)...);
        }

    private:
        T* m_t;
        R (T::*m_f)(Args...);
    };

    template <class T, class R, typename... Args>
    MyDelegate<T, R, Args...> CreateDelegate(T* t, R (T::*f)(Args...))
    {
        return MyDelegate<T, R, Args...>(t, f);
    }

#if 0
    struct A
    {
        void Fun(int i)
        {
            cout << i << endl;
        }
        void Fun1(int i, double j)
        {
            cout << i + j << endl;
        }
    };

    int main()
    {
        A    a;
        auto d = CreateDelegate(&a, &A::Fun);    //创建委托
        d(1);                                    //调用委托，将输出1
        auto d1 = CreateDelegate(&a, &A::Fun1);  //创建委托
        d1(1, 2.5);                              //调用委托，将输出3.5
    }

#endif

}  // namespace cncpp

#endif
