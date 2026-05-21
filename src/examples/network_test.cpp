#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "asynctask.h"
#include "compression.h"
#include "encryption.h"
#include "network.h"

using namespace cncpp;

// 测试加密实现
class TestEncryption : public EncryptionInterface
{
public:
    TestEncryption(char key = 0x55) : key_(key)
    {
    }

    std::string Encrypt(const std::string& data) override
    {
        std::string result = data;
        for (char& c : result)
        {
            c ^= key_;
        }
        return result;
    }

    std::string Decrypt(const std::string& data) override
    {
        return Encrypt(data);  // XOR是对称的
    }

    std::string GetName() const override
    {
        return "TestXOR";
    }

private:
    char key_;
};

// 测试压缩实现
class TestCompression : public CompressionInterface
{
public:
    std::string Compress(const std::string& data) override
    {
        if (data.empty())
        {
            return "";
        }

        std::string result;
        char current = data[0];
        int count = 1;

        for (size_t i = 1; i < data.size(); ++i)
        {
            if (data[i] == current && count < 255)
            {
                count++;
            }
            else
            {
                result += current;
                result += static_cast<char>(count);
                current = data[i];
                count = 1;
            }
        }

        result += current;
        result += static_cast<char>(count);
        return result;
    }

    std::string Decompress(const std::string& data) override
    {
        if (data.empty())
        {
            return "";
        }

        std::string result;
        for (size_t i = 0; i < data.size(); i += 2)
        {
            if (i + 1 < data.size())
            {
                char c = data[i];
                int count = static_cast<unsigned char>(data[i + 1]);
                result.append(count, c);
            }
        }
        return result;
    }

    std::string GetName() const override
    {
        return "TestRLE";
    }
};

// 测试结果
struct TestResult
{
    bool passed;
    std::string message;
};

// 测试网络连接
TestResult TestNetworkConnection()
{
    LOG_INFO("=== Testing Network Connection ===");

    // 启动服务器
    cncpp::NetworkManager server_manager;
    server_manager.Start();

    bool server_started = false;
    std::shared_ptr<Session> server_session = nullptr;

    auto acceptor
        = server_manager.CreateAcceptor(8081, [&server_started, &server_session](boost::asio::ip::tcp::socket&& sock) {
        server_started = true;
        server_session = std::make_shared<Session>(std::move(sock));
        server_session->Start();
        LOG_INFO("Server: New connection accepted");
    });

    acceptor->Start();

    // 启动客户端
    cncpp::NetworkManager client_manager;
    client_manager.Start();

    bool client_connected = false;
    std::shared_ptr<Session> client_session = nullptr;

    auto connector = client_manager.CreateConnector();
    connector->Connect("127.0.0.1", 8081, [&client_connected, &client_session](std::shared_ptr<Session> session) {
        client_connected = true;
        client_session = session;
        LOG_INFO("Client: Connected to server");
    }, [](const std::string& error) { LOG_ERROR("Client: Connection failed: " + error); });

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(2));

    if (!server_started || !client_connected)
    {
        server_manager.Stop();
        client_manager.Stop();
        return {false, "Network connection test failed"};
    }

    // 关闭连接
    if (server_session)
    {
        server_session->Close();
    }
    if (client_session)
    {
        client_session->Close();
    }

    server_manager.Stop();
    client_manager.Stop();

    return {true, "Network connection test passed"};
}

// 测试消息发送和接收
TestResult TestMessageSendReceive()
{
    LOG_INFO("=== Testing Message Send/Receive ===");

    cncpp::NetworkManager server_manager;
    server_manager.Start();

    std::shared_ptr<Session> server_session = nullptr;
    std::string received_message;
    std::mutex message_mutex;
    std::condition_variable message_cv;

    auto acceptor = server_manager.CreateAcceptor(
        8082, [&server_session, &received_message, &message_mutex, &message_cv](tcp::socket&& sock) {
        server_session = std::make_shared<Session>(std::move(sock));
        server_session->Start();

        // 启动消息处理线程
        std::thread([server_session, &received_message, &message_mutex, &message_cv]() {
            while (true)
            {
                NetworkMessage message;
                if (server_session->GetReceiveQueue().Pop(message))
                {
                    std::lock_guard<std::mutex> lock(message_mutex);
                    received_message = message.body_;
                    message_cv.notify_one();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }).detach();
    });

    acceptor->Start();

    cncpp::NetworkManager client_manager;
    client_manager.Start();

    std::shared_ptr<Session> client_session = nullptr;

    auto connector = client_manager.CreateConnector();
    connector->Connect("127.0.0.1", 8082, [&client_session](tcp::socket&& sock) {
        client_session = std::make_shared<Session>(std::move(sock));
        client_session->Start();
    }, [](const std::string& error) { LOG_ERROR("Client: Connection failed: " + error); });

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!client_session)
    {
        server_manager.Stop();
        client_manager.Stop();
        return {false, "Client connection failed"};
    }

    // 发送测试消息
    const std::string test_message = "Hello, Network!";
    client_session->Send(test_message);

    // 等待消息接收
    std::unique_lock<std::mutex> lock(message_mutex);
    if (message_cv.wait_for(lock, std::chrono::seconds(5), [&received_message]() { return !received_message.empty(); }))
    {
        if (received_message == test_message)
        {
            LOG_INFO("Message received correctly: {}", received_message);
        }
        else
        {
            server_manager.Stop();
            client_manager.Stop();
            return {false, "Message content mismatch"};
        }
    }
    else
    {
        server_manager.Stop();
        client_manager.Stop();
        return {false, "Message receive timeout"};
    }

    // 关闭连接
    if (server_session)
    {
        server_session->Close();
    }
    if (client_session)
    {
        client_session->Close();
    }

    server_manager.Stop();
    client_manager.Stop();

    return {true, "Message send/receive test passed"};
}

// 测试加密和压缩
TestResult TestEncryptionCompression()
{
    LOG_INFO("=== Testing Encryption and Compression ===");

    // 创建加密和压缩实例
    auto encryption = std::make_shared<TestEncryption>();
    auto compression = std::make_shared<TestCompression>();

    // 测试加密
    std::string original = "Hello, Encryption!";
    std::string encrypted = encryption->Encrypt(original);
    std::string decrypted = encryption->Decrypt(encrypted);

    if (decrypted != original)
    {
        return {false, "Encryption test failed"};
    }
    LOG_INFO("Encryption test passed");

    // 测试压缩
    std::string compressible = "AAAAABBBBBCCCCC";
    std::string compressed = compression->Compress(compressible);
    std::string decompressed = compression->Decompress(compressed);

    if (decompressed != compressible)
    {
        return {false, "Compression test failed"};
    }
    LOG_INFO("Compression test passed");

    // 测试网络传输中的加密和压缩
    cncpp::NetworkManager server_manager;
    server_manager.Start();

    std::shared_ptr<Session> server_session = nullptr;
    std::string received_message;
    std::mutex message_mutex;
    std::condition_variable message_cv;

    auto acceptor
        = server_manager.CreateAcceptor(8083, [&server_session, &received_message, &message_mutex, &message_cv,
                                               encryption, compression](std::shared_ptr<Session> session) {
        server_session = session;
        session->SetEncryption(encryption);
        session->SetCompression(compression);

        // 启动消息处理线程
        std::thread([session, &received_message, &message_mutex, &message_cv]() {
            while (true)
            {
                NetworkMessage message;
                if (session->GetReceiveQueue().Pop(message))
                {
                    std::lock_guard<std::mutex> lock(message_mutex);
                    received_message = message.body_;
                    message_cv.notify_one();
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }).detach();
    });

    acceptor->Start();
    // 启动客户端
    cncpp::NetworkManager client_manager;
    client_manager.Start();

    std::shared_ptr<Session> client_session = nullptr;

    auto connector = client_manager.CreateConnector();
    connector->Connect("127.0.0.1", 8083, [&client_session, encryption, compression](std::shared_ptr<Session> session) {
        client_session = session;
        session->SetEncryption(encryption);
        session->SetCompression(compression);
    }, [](const std::string& error) { std::cout << "Client: Connection failed: " << error << std::endl; });

    // 等待连接建立
    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (!client_session)
    {
        server_manager.Stop();
        client_manager.Stop();
        return {false, "Client connection failed"};
    }

    // 发送测试消息
    const std::string test_message = "Hello, Encryption and Compression!";
    client_session->Send(test_message);

    // 等待消息接收
    std::unique_lock<std::mutex> lock(message_mutex);
    if (message_cv.wait_for(lock, std::chrono::seconds(5), [&received_message]() { return !received_message.empty(); }))
    {
        if (received_message == test_message)
        {
            LOG_INFO("Encrypted and compressed message received correctly");
        }
        else
        {
            server_manager.Stop();
            client_manager.Stop();
            return {false, "Encrypted message content mismatch"};
        }
    }
    else
    {
        server_manager.Stop();
        client_manager.Stop();
        return {false, "Encrypted message receive timeout"};
    }

    // 关闭连接
    if (server_session)
    {
        server_session->Close();
    }
    if (client_session)
    {
        client_session->Close();
    }

    server_manager.Stop();
    client_manager.Stop();

    return {true, "Encryption and compression test passed"};
}

// 测试异步任务
TestResult TestAsyncTask()
{
    LOG_INFO("=== Testing Async Task ===");
    // 修复命名空间问题，NetworkManager需要加上cncpp::前缀
    cncpp::NetworkManager manager;
    manager.Start();

    bool task_completed = false;
    int task_result = 0;
    std::mutex task_mutex;
    std::condition_variable task_cv;
#if 0   
    // 定义任务结果类型
    typedef int TestTaskResult;

    // 创建异步任务
    class TestAsyncTask : public AsyncTask<TestTaskResult>
    {
    public:
        TestAsyncTask(int value) : value_(value)
        {
        }

        TaskResult<TestTaskResult> Execute() override
        {
            LOG_INFO("Async task executing...");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            return TaskResult<TestTaskResult>(true, value_ * 2, "");
        }

    private:
        int value_;
    };

    auto task = std::make_shared<TestAsyncTask>(42);
    task->SetCallback([&task_completed, &task_result, &task_mutex, &task_cv](const TaskResult<TestTaskResult>& result) {
        if (result.success_)
        {
            std::lock_guard<std::mutex> lock(task_mutex);
            task_result = result.result_;
            task_completed = true;
            task_cv.notify_one();
            LOG_INFO("Async task completed with result: {}", result.result_);
        }
        else
        {
            LOG_ERROR("Async task failed: {}", result.error_);
        }
    });
#endif
    // 添加任务到管理器
    // manager.GetTaskManager().AddTask(task);

    // 等待任务完成
    std::unique_lock<std::mutex> lock(task_mutex);
    if (task_cv.wait_for(lock, std::chrono::seconds(2), [&task_completed]() { return task_completed; }))
    {
        if (task_result == 84)
        {
            LOG_INFO("Async task result correct");
        }
        else
        {
            manager.Stop();
            return {false, "Async task result mismatch"};
        }
    }
    else
    {
        manager.Stop();
        return {false, "Async task timeout"};
    }

    manager.Stop();

    return {true, "Async task test passed"};
}

// 运行所有测试
int main()
{
    // 初始化日志
    LoggerConfig config;
    config.app_name.set("network_test");
    config.log_dir.set("logs");
    config.enable_console.set(true);
    if (!MyLogger::inst().init(config))
    {
        std::cerr << "Failed to initialize logger" << std::endl;
        return 1;
    }

    LOG_INFO("=== Network Library Tests ===");

    std::vector<TestResult> results;

    results.push_back(TestNetworkConnection());
    results.push_back(TestMessageSendReceive());
    results.push_back(TestEncryptionCompression());
    results.push_back(TestAsyncTask());

    LOG_INFO("=== Test Results ===");
    size_t passed = 0;
    for (size_t i = 0; i < results.size(); ++i)
    {
        LOG_INFO("Test {}: {} - {}", i + 1, (results[i].passed ? "PASSED" : "FAILED"), results[i].message);
        if (results[i].passed)
        {
            passed++;
        }
    }

    LOG_INFO("Total: {} out of {} tests passed", passed, results.size());

    return passed == results.size() ? 0 : 1;
}
