#include "gate_user_manager.h"
#include "logger.h"

GateUserManager::GateUserManager()
{
}

GateUserManager::~GateUserManager()
{
}

GateUserPtr GateUserManager::addUser(uint32_t user_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    if (it != users_.end())
    {
        return it->second;
    }

    auto user       = std::make_shared<GateUser>(user_id);
    users_[user_id] = user;

    LOG_DEBUG("Added user: {}", user_id);
    return user;
}

GateUserPtr GateUserManager::getUser(uint32_t user_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    return (it != users_.end()) ? it->second : nullptr;
}

void GateUserManager::removeUser(uint32_t user_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    if (it != users_.end())
    {
        users_.erase(it);
        LOG_DEBUG("Removed user: {}", user_id);
    }
}

bool GateUserManager::hasUser(uint32_t user_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.find(user_id) != users_.end();
}

size_t GateUserManager::getUserCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

std::vector<uint32_t> GateUserManager::getAllUserIDs() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<uint32_t> ids;
    ids.reserve(users_.size());

    for (const auto& pair : users_)
    {
        ids.push_back(pair.first);
    }

    return ids;
}

std::vector<GateUserPtr> GateUserManager::getUsersByState(UserState state) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<GateUserPtr> result;

    for (const auto& pair : users_)
    {
        if (pair.second->getState() == state)
        {
            result.push_back(pair.second);
        }
    }

    return result;
}

size_t GateUserManager::getOnlineUserCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& pair : users_)
    {
        if (pair.second->isOnline())
        {
            count++;
        }
    }

    return count;
}

size_t GateUserManager::getAuthenticatedUserCount() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t count = 0;
    for (const auto& pair : users_)
    {
        if (pair.second->isAuthenticated())
        {
            count++;
        }
    }

    return count;
}

size_t GateUserManager::cleanupTimeoutUsers(std::chrono::seconds timeout)
{
    std::lock_guard<std::mutex> lock(mutex_);

    size_t removed = 0;
    auto   it      = users_.begin();

    while (it != users_.end())
    {
        if (it->second->isTimeout(timeout))
        {
            LOG_DEBUG("Cleaning up timeout user: {}", it->first);
            it = users_.erase(it);
            removed++;
        }
        else
        {
            ++it;
        }
    }

    if (removed > 0)
    {
        LOG_INFO("Cleaned up {} timeout users", removed);
    }

    return removed;
}

void GateUserManager::updateUserGatewayID(uint32_t user_id, const std::string& gateway_id)
{
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = users_.find(user_id);
    if (it != users_.end())
    {
        it->second->setGatewayID(gateway_id);
    }
}

void GateUserManager::setCacheExpirySeconds(uint32_t seconds)
{
    cache_expiry_seconds_ = seconds;
}

uint32_t GateUserManager::getCacheExpirySeconds() const
{
    return cache_expiry_seconds_;
}
