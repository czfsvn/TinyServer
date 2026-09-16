#pragma once

#include <assert.h>
#include <algorithm>
#include <chrono>
#include <functional>
#include <list>
#include <stdexcept>
#include <thread>

#include "singleton.h"

namespace cncpp
{
    template <typename ConnInfoT>
    class TooOld : std::unary_function<ConnInfoT, bool>
    {
    public:
#if !defined(DOXYGEN_IGNORE)
        TooOld(unsigned int tmax) : min_age_(time(0) - tmax)
        {
        }

        bool operator()(const ConnInfoT& conn_info) const
        {
            return !conn_info.in_use && conn_info.last_used <= min_age_;
        }

#endif
    private:
        time_t min_age_;
    };

    template <typename ConnT, typename CONFIG>
    class MyConnPool : public cncpp::Singleton<MyConnPool<ConnT, CONFIG>>
    {
    public:
        using CONN = ConnT;

        struct ConnectionInfo
        {
            CONN*  conn;
            time_t last_used;
            bool   in_use;

            ConnectionInfo(CONN* c) : conn(c), last_used(time(0)), in_use(true)
            {
            }

            bool operator<(const ConnectionInfo& rhs) const
            {
                const ConnectionInfo& lhs = *this;
                return lhs.in_use == rhs.in_use ? lhs.last_used < rhs.last_used : lhs.in_use;
            }
        };

        typedef std::list<ConnectionInfo> PoolT;
        typedef typename PoolT::iterator  PoolIt;

        std::mutex mutex_;
        CONFIG     conn_cfg;

        PoolT pool_;

    public:
        MyConnPool()
        {
        }

        virtual ~MyConnPool()
        {
            clear();
            assert(empty());
        }

        void init(const CONFIG& config)
        {
            conn_cfg = config;
        }

        bool empty() const
        {
            return pool_.empty();
        }

        virtual CONN* exchange(const CONN* pc)
        {
            remove(pc);
            return grab();
        }

        /// \brief 取一条可用连接
        ///
        /// 取到的连接若已断开（isConnected() 为假），丢弃后重建并重试，最多尝试 max_grab_retry() 次；
        /// 仍失败才抛 std::runtime_error。
        ///
        /// 不变式：**grab() 成功返回 ⇒ 连接可用**。调用方拿到连接即可使用，不必再自行探活。
        virtual CONN* grab()
        {
            const unsigned int max_attempt = max_grab_retry();
            for (unsigned int attempt = 1;; ++attempt)
            {
                CONN* conn = 0;
                {
                    std::lock_guard<std::mutex> lock(mutex_);  // ensure we're not interfered with
                    remove_old_connections();
                    if (CONN* mru = find_mru())
                    {
                        conn = mru;
                    }
                    else
                    {
                        pool_.push_back(ConnectionInfo(create()));
                        conn = pool_.back().conn;
                    }
                }

                if (conn->isConnected())
                    return conn;

                // 死连接：摘掉再重试。remove() 自己加锁，故上面用块先把临界区收窄
                remove(conn);
                if (attempt >= max_attempt)
                    throw std::runtime_error("MyConnPool::grab: 无法取得可用连接");

                std::this_thread::sleep_for(std::chrono::milliseconds(grab_retry_interval_ms()));
            }
        }

        virtual void release(const CONN* pc)
        {
            std::lock_guard<std::mutex> lock(mutex_);  // ensure we're not interfered with

            for (PoolIt it = pool_.begin(); it != pool_.end(); ++it)
            {
                if (it->conn == pc)
                {
                    it->in_use    = false;
                    it->last_used = time(0);
                    break;
                }
            }
        }

        void remove(const CONN* pc)
        {
            std::lock_guard<std::mutex> lock(mutex_);  // ensure we're not interfered with

            for (PoolIt it = pool_.begin(); it != pool_.end(); ++it)
            {
                if (it->conn == pc)
                {
                    remove(it);
                    return;
                }
            }
        }

        void shrink()
        {
            clear(false);
        }

        void clear(bool all = true)
        {
            std::lock_guard<std::mutex> lock(mutex_);  // ensure we're not interfered with

            PoolIt it = pool_.begin();
            while (it != pool_.end())
            {
                if (all || !it->in_use)
                {
                    remove(it++);
                }
                else
                {
                    ++it;
                }
            }
        }

        virtual CONN* create()
        {
            return new CONN(conn_cfg);
        }

        virtual void destroy(CONN* conn)
        {
            if (conn)
                delete conn;

            conn = nullptr;
        };

        virtual unsigned int max_idle_time()
        {
            return 300;
        };

        /// \brief grab() 允许的尝试次数（含第一次）
        ///
        /// 只有「新建的连接也是死的」才会走到第二次，因此这是对端故障时的上限，
        /// 不是正常情况下的循环。
        virtual unsigned int max_grab_retry()
        {
            return 3;
        };

        /// \brief grab() 两次尝试之间的退避毫秒数
        virtual unsigned int grab_retry_interval_ms()
        {
            return 200;
        };

        size_t size() const
        {
            return pool_.size();
        }

        CONN* find_mru()
        {
            PoolIt mru = std::max_element(pool_.begin(), pool_.end());
            if (mru != pool_.end() && !mru->in_use)
            {
                mru->in_use = true;
                return mru->conn;
            }
            else
            {
                return 0;
            }
        }

        void remove(const PoolIt& it)
        {
            destroy(it->conn);
            pool_.erase(it);
        }

        void remove_old_connections()
        {
            TooOld<ConnectionInfo> too_old(max_idle_time());

            PoolIt it = pool_.begin();
            while ((it = std::find_if(it, pool_.end(), too_old)) != pool_.end())
            {
                remove(it++);
            }
        }

    private:
        uint16_t max_conn_size_ = 0;
    };

    template <typename T, typename CONFIG>
    class MyScopedConn
    {
    public:
        using CONN = T;

        explicit MyScopedConn(MyConnPool<CONN, CONFIG>* pool = &MyConnPool<CONN, CONFIG>::getMe())
            : pool_(pool), connection_(pool->grab())
        {
        }

#if __cplusplus >= 201103L
        // ScopedConnection objects cannot be copied.  We want them to be
        // tightly scoped to their use point, not put in containers or
        // passed around promiscuously.
        MyScopedConn(MyScopedConn&&)                                 = default;
        MyScopedConn(const MyScopedConn& no_copies)                  = delete;
        const MyScopedConn& operator=(const MyScopedConn& no_copies) = delete;
#endif

        /// \brief Destructor
        ///
        /// Releases the Connection back to the ConnectionPool.
        ~MyScopedConn()
        {
            pool_->release(connection_);
        }

        /// \brief Access the Connection pointer
        CONN* operator->() const
        {
            return connection_;
        }

        /// \brief Dereference
        CONN& operator*() const
        {
            return *connection_;
        }

        /// \brief Truthiness operator
        operator void*() const
        {
            return connection_;
        }

    private:
#if __cplusplus < 201103L
        // Pre C++11 alternative to no-copies ctors above.
        MyScopedConn(const MyScopedConn& no_copies);
        const MyScopedConn& operator=(const MyScopedConn& no_copies);
#endif

        MyConnPool<CONN, CONFIG>* pool_;
        CONN* const               connection_;
    };
}  // namespace cncpp
