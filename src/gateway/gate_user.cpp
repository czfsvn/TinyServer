#include "gate_user.h"

GateUser::GateUser(uint32_t user_id)
    : user_id_(user_id),
      connect_time_(std::chrono::steady_clock::now()),
      last_active_time_(std::chrono::steady_clock::now())
{
}

bool GateUser::isTimeout(std::chrono::seconds timeout) const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_active_time_);
    return elapsed >= timeout;
}

void GateUser::setProperty(const std::string& key, const std::string& value)
{
    properties_[key] = value;
}

std::string GateUser::getProperty(const std::string& key) const
{
    auto it = properties_.find(key);
    if (it != properties_.end())
    {
        return it->second;
    }
    return "";
}

bool GateUser::hasProperty(const std::string& key) const
{
    return properties_.find(key) != properties_.end();
}

void GateUser::removeProperty(const std::string& key)
{
    properties_.erase(key);
}