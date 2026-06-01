#include "gate_user.h"

GateUser::GateUser(uint32_t user_id)
    : user_id_(user_id),
      state_(UserState::Disconnected),
      connect_time_(std::chrono::steady_clock::now()),
      last_active_time_(std::chrono::steady_clock::now())
{
}

const std::string& GateUser::getGatewayID() const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return gateway_id_;
}

void GateUser::setGatewayID(const std::string& gateway_id)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    gateway_id_ = gateway_id;
}

const std::string& GateUser::getIPAddress() const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return ip_address_;
}

void GateUser::setIPAddress(const std::string& ip_address)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    ip_address_ = ip_address;
}

std::chrono::steady_clock::time_point GateUser::getConnectTime() const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return connect_time_;
}

void GateUser::setConnectTime()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    connect_time_ = std::chrono::steady_clock::now();
    last_active_time_ = connect_time_;
}

const std::string& GateUser::getToken() const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return token_;
}

void GateUser::setToken(const std::string& token)
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    token_ = token;
}

std::chrono::steady_clock::time_point GateUser::getLastActiveTime() const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    return last_active_time_;
}

void GateUser::updateLastActiveTime()
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    last_active_time_ = std::chrono::steady_clock::now();
}

bool GateUser::isTimeout(std::chrono::seconds timeout) const
{
    std::lock_guard<std::mutex> lock(data_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_active_time_);
    return elapsed >= timeout;
}

void GateUser::setProperty(const std::string& key, const std::string& value)
{
    std::lock_guard<std::mutex> lock(props_mutex_);
    properties_[key] = value;
}

std::string GateUser::getProperty(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(props_mutex_);
    auto it = properties_.find(key);
    return (it != properties_.end()) ? it->second : "";
}

bool GateUser::hasProperty(const std::string& key) const
{
    std::lock_guard<std::mutex> lock(props_mutex_);
    return properties_.find(key) != properties_.end();
}

void GateUser::removeProperty(const std::string& key)
{
    std::lock_guard<std::mutex> lock(props_mutex_);
    properties_.erase(key);
}
