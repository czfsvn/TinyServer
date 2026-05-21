#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace cncpp
{

    // 会话信息结构体模板
    template <typename SessionType>
    struct SessionInfo
    {
        std::shared_ptr<SessionType> session;
        std::string                  info;
    };

    // 会话管理器类模板
    template <typename SessionType>
    class SessionManager
    {
    private:
        std::vector<SessionInfo<SessionType>> sessions_;
        mutable std::mutex                    sessions_mutex_;

    public:
        SessionManager()  = default;
        ~SessionManager() = default;

        // 添加会话
        void AddSession(std::shared_ptr<SessionType> session, const std::string& info = "");

        // 移除会话
        void RemoveSession(std::shared_ptr<SessionType> session);

        // 移除所有会话
        void Clear();

        // 获取会话数量
        size_t GetSessionCount() const;

        // 处理所有会话的消息队列
        template <typename MessageType>
        void ProcessAllSessionQueues(std::function<void(const MessageType&, const std::string&)> message_handler);

        // 遍历所有会话
        void ForEachSession(std::function<void(const SessionInfo<SessionType>&)> callback) const;

        // 关闭所有会话
        void CloseAllSessions();
    };

    // 模板实现
    template <typename SessionType>
    void SessionManager<SessionType>::AddSession(std::shared_ptr<SessionType> session, const std::string& info)
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.push_back({session, info});
    }

    template <typename SessionType>
    void SessionManager<SessionType>::RemoveSession(std::shared_ptr<SessionType> session)
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                       [session](const SessionInfo<SessionType>& si) {
            return si.session == session;
        }),
                        sessions_.end());
    }

    template <typename SessionType>
    void SessionManager<SessionType>::Clear()
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }

    template <typename SessionType>
    size_t SessionManager<SessionType>::GetSessionCount() const
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        return sessions_.size();
    }

    template <typename SessionType>
    template <typename MessageType>
    void SessionManager<SessionType>::ProcessAllSessionQueues(
        std::function<void(const MessageType&, const std::string&)> message_handler)
    {
        std::vector<SessionInfo<SessionType>> sessions_copy;

        // 复制会话列表，避免在处理过程中被修改
        {
            std::unique_lock<std::mutex> lock(sessions_mutex_);
            sessions_copy = sessions_;
        }

        // 处理每个会话的消息队列
        for (const auto& si : sessions_copy)
        {
            MessageType message;
            // 尝试从会话的接收队列中获取消息
            if (si.session && si.session->getReceiveQueue().pop(message))
            {
                // 调用消息处理函数
                message_handler(message, si.info);
            }
        }
    }

    template <typename SessionType>
    void SessionManager<SessionType>::ForEachSession(
        std::function<void(const SessionInfo<SessionType>&)> callback) const
    {
        std::vector<SessionInfo<SessionType>> sessions_copy;

        // 复制会话列表，避免在处理过程中被修改
        {
            std::unique_lock<std::mutex> lock(sessions_mutex_);
            sessions_copy = sessions_;
        }

        // 遍历每个会话
        for (const auto& si : sessions_copy)
        {
            callback(si);
        }
    }

    template <typename SessionType>
    void SessionManager<SessionType>::CloseAllSessions()
    {
        std::vector<SessionInfo<SessionType>> sessions_copy;

        // 复制会话列表，避免在处理过程中被修改
        {
            std::unique_lock<std::mutex> lock(sessions_mutex_);
            sessions_copy = sessions_;
        }

        // 关闭每个会话
        for (const auto& si : sessions_copy)
        {
            if (si.session)
            {
                si.session->close();
            }
        }

        // 清空会话列表
        Clear();
    }

    // 前向声明 Session 类
    class Session;

    // 模板特化：默认使用 Session 类型
    template class SessionManager<Session>;

}  // namespace cncpp