#include "session_manager.h"
#if 0
namespace cncpp
{

    void SessionManager::AddSession(std::shared_ptr<Session> session, const std::string& info)
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.push_back({session, info});
    }

    void SessionManager::RemoveSession(std::shared_ptr<Session> session)
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.erase(std::remove_if(sessions_.begin(), sessions_.end(),
                                       [session](const SessionInfo& si) { return si.session == session; }),
                        sessions_.end());
    }

    void SessionManager::Clear()
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        sessions_.clear();
    }

    size_t SessionManager::GetSessionCount() const
    {
        std::unique_lock<std::mutex> lock(sessions_mutex_);
        return sessions_.size();
    }

    void SessionManager::ProcessAllSessionQueues(
        std::function<void(const NetworkMessage&, const std::string&)> message_handler)
    {
        std::vector<SessionInfo> sessions_copy;

        // 复制会话列表，避免在处理过程中被修改
        {
            std::unique_lock<std::mutex> lock(sessions_mutex_);
            sessions_copy = sessions_;
        }

        // 处理每个会话的消息队列
        for (const auto& si : sessions_copy)
        {
            NetworkMessage message;
            // 尝试从会话的接收队列中获取消息
            if (si.session && si.session->GetReceiveQueue().Pop(message))
            {
                // 调用消息处理函数
                message_handler(message, si.info);
            }
        }
    }

    void SessionManager::ForEachSession(std::function<void(const SessionInfo&)> callback) const
    {
        std::vector<SessionInfo> sessions_copy;

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

    void SessionManager::CloseAllSessions()
    {
        std::vector<SessionInfo> sessions_copy;

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
                si.session->Close();
            }
        }

        // 清空会话列表
        Clear();
    }

}  // namespace cncpp
#endif