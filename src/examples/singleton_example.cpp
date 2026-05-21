#include <iostream>
#include "singleton.h"

namespace cncpp
{

    // 示例类：使用简单单例
    class SimpleConfig : public Singleton<SimpleConfig>
    {
        friend class Singleton<SimpleConfig>;

    private:
        SimpleConfig() : port_(8080), host_("localhost")
        {
        }

    public:
        void SetPort(int port)
        {
            port_ = port;
        }
        int GetPort() const
        {
            return port_;
        }

        void SetHost(const std::string& host)
        {
            host_ = host;
        }
        std::string GetHost() const
        {
            return host_;
        }

    private:
        int port_;
        std::string host_;
    };

    // 示例类：使用线程安全单例
    class ThreadSafeLogger : public ThreadSafeSingleton<ThreadSafeLogger>
    {
        friend class ThreadSafeSingleton<ThreadSafeLogger>;

    private:
        ThreadSafeLogger() : log_level_(0)
        {
        }

    public:
        void SetLogLevel(int level)
        {
            log_level_ = level;
        }
        int GetLogLevel() const
        {
            return log_level_;
        }

        void Log(const std::string& message)
        {
            std::cout << "[LOG] " << message << std::endl;
        }

    private:
        int log_level_;
    };

}  // namespace cncpp

// 使用示例
void TestSingleton()
{
    using namespace cncpp;

    // 使用简单单例
    SimpleConfig& config = SimpleConfig::getMe();
    config.SetPort(9000);
    config.SetHost("example.com");

    std::cout << "Config: " << config.GetHost() << ":" << config.GetPort() << std::endl;

    // 使用线程安全单例
    ThreadSafeLogger& logger = ThreadSafeLogger::getMe();
    logger.SetLogLevel(1);
    logger.Log("This is a test message");

    std::cout << "Log level: " << logger.GetLogLevel() << std::endl;
}

// 自定义类的使用示例
class MyManager : public cncpp::Singleton<MyManager>
{
    friend class cncpp::Singleton<MyManager>;

private:
    MyManager() : counter_(0)
    {
    }

public:
    void Increment()
    {
        ++counter_;
    }
    int GetCounter() const
    {
        return counter_;
    }

private:
    int counter_;
};

void TestCustomSingleton()
{
    // 获取单例实例
    MyManager& manager = MyManager::getMe();

    // 使用单例
    manager.Increment();
    manager.Increment();
    manager.Increment();

    std::cout << "Counter: " << manager.GetCounter() << std::endl;

    // 获取单例指针
    MyManager* manager_ptr = &MyManager::getMe();
    std::cout << "Counter (via pointer): " << manager_ptr->GetCounter() << std::endl;
}

int main()
{
    std::cout << "=== Testing Singleton ===" << std::endl;
    TestSingleton();

    std::cout << "\n=== Testing Custom Singleton ===" << std::endl;
    TestCustomSingleton();

    return 0;
}