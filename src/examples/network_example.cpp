#include <boost/asio/signal_set.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "asynctask.h"
#include "compression.h"
#include "config.h"
#include "encryption.h"
#include "io_context_pool.h"
#include "session_manager.h"
#include "utils.h"

using namespace cncpp;
namespace po = boost::program_options;

// 处理单个消息
void ProcessMessage(const NetworkMessage& message, const std::string& session_info)
{
    std::cout << "[" << session_info << "] Received message: " << message.body_ << std::endl;
}

// 会话管理器
SessionManager<Session> session_manager;
Config                  config;

void finalize()
{
    sIOContextPool.stop();
    sIOContextPool.waitForStop();
    sIOContextPool.cleanup();
    sLogger.shutdown();
}

// 优雅关闭函数
void GracefulShutdown()
{
    std::cout << "Starting graceful shutdown..." << std::endl;
    // 停止会话管理器
    session_manager.closeAllSessions();

    // 停止异步任务管理器
    sAsyncTaskManager.stop();

    sIOContextPool.cleanup();
    // 关闭日志系统
    // sLogger.shutdown();

    std::cout << "Graceful shutdown completed" << std::endl;
}

// 统一处理所有会话的消息队列
void ProcessAllSessionQueues()
{
    if (!sIOContextPool.isShutdownRequested())
    {
        // 使用会话管理器处理所有会话的消息队列
        session_manager.ProcessAllSessionQueues<NetworkMessage>(ProcessMessage);

        sAsyncTaskManager.processTasksDone();

        // 短暂休眠，避免CPU占用过高
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    std::cout << "ProcessAllSessionQueues completed." << std::endl;
}

void ServerExample(int port)
{
    std::cout << "=== Server Example ===" << std::endl;

    // 使用AES加密和Zlib压缩
    auto encryption  = std::make_shared<cncpp::AESEncryption>(sEncryptionConfig.encryptionKey());
    auto compression = std::make_shared<cncpp::ZlibCompression>();

    auto created_cb = [encryption, compression](boost::asio::ip::tcp::socket&& sock) {
        LOG_INFO("New connection from: {}:{}", sock.remote_endpoint().address().to_string(),
                 sock.remote_endpoint().port());

        auto session = std::make_shared<Session>(std::move(sock));
        session->Start();
        // 设置加密和压缩
        session->setEncryption(encryption);
        session->setCompression(compression);

        // 为会话添加消息处理任务
        std::string session_info = session->getRemoteEndpoint().address().to_string() + ":"
                                   + std::to_string(session->getRemoteEndpoint().port());
        // 将会话添加到会话管理器
        session_manager.AddSession(session, session_info);

        // 发送欢迎消息
        session->Send("Welcome to the server!", 1);
    };

    auto acceptor = sIOContextPool.CreateAcceptor(port, created_cb);
    acceptor->Start();

    if (!sAsyncTaskManager.Start())
    {
        LOG_ERROR("Failed to start async task manager");
        return;
    }

    LOG_INFO("Server started on port {}...", port);
    LOG_INFO("Press Enter to stop the server...", "");

    // 启动一个线程来统一处理所有会话的消息队列
    //std::thread message_process_thread(ProcessAllSessionQueues);

    // std::cin.get();
    sIOContextPool.setTimerCallback([]() {
        ProcessAllSessionQueues();
    }, 1000);

    sIOContextPool.Run();

    // ProcessAllSessionQueues();

    // 停止接收新连接
    acceptor->Stop();

    std::cout << "Step 5: Sleeping 2s..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::cout << "Step 6: Stopping IOContextPool Cleanup..." << std::endl;
    // sIOContextPool.Cleanup();

    std::cout << "Server stopped." << std::endl;
}

void ClientExample(const std::string& host, int port)
{
    LOG_INFO("=== Client Example ===");
    // 使用AES加密和Zlib压缩
    auto encryption  = std::make_shared<cncpp::AESEncryption>(sEncryptionConfig.encryptionKey());
    auto compression = std::make_shared<cncpp::ZlibCompression>();

    std::shared_ptr<Session> session = nullptr;

    auto connector = sIOContextPool.CreateConnector();
    connector->Connect(host, port, [&session, encryption, compression](boost::asio::ip::tcp::socket&& sock) {
        session = std::make_shared<Session>(std::move(sock));
        session->Start();
        LOG_INFO("Connected to server successfully!");

        // 设置加密和压缩
        session->SetEncryption(encryption);
        session->SetCompression(compression);

        // 为会话添加消息处理任务
        std::string session_info = session->GetRemoteEndpoint().address().to_string() + ":"
                                   + std::to_string(session->GetRemoteEndpoint().port());
        // 将会话添加到全局会话列表，由统一的线程处理消息队列
        session_manager.AddSession(session, session_info);
    }, [](const std::string& error) {
        LOG_ERROR("Connection failed: " + error);
    });

    // 启动一个线程来统一处理所有会话的消息队列
    std::thread message_process_thread(ProcessAllSessionQueues);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    if (session)
    {
        LOG_INFO("Sending messages...");
        for (int i = 0; i < 5; ++i)
        {
            std::string message = "Hello from client, message " + std::to_string(i + 1);
            session->Send(message, i + 100);  // 消息ID从100开始
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    LOG_INFO("Press Enter to disconnect...");
    // std::cin.get();

    if (session)
    {
        session->Close();
    }

    // 清空会话列表
    session_manager.CloseAllSessions();

    // 等待消息处理线程结束
    message_process_thread.join();

    LOG_INFO("Client stopped.");
}

int main(int argc, char* argv[])
{
    // 读取配置文件

    if (!sConfig.ReadFromCommandLine(argc, argv))
    {
        std::cerr << "Failed to load configuration file: " << sConfig.GetConfigFile() << std::endl;
        return 1;
    }

    // 从配置文件中读取参数，命令行参数优先
    std::string mode        = sConfig.GetNetworkConfig().mode();
    bool        daemon_mode = sConfig.GetMainConfig().daemon();
    int         port        = sConfig.GetNetworkConfig().port();
    std::string host        = sConfig.GetNetworkConfig().host();

    // 初始化日志记录器
    sLoggerConfig.app_name.set(sConfig.GetMainConfig().app_name());
    if (daemon_mode)
    {
        sLoggerConfig.enable_console.set(false);
    }

    if (!sLogger.init())
    {
        std::cerr << "Failed to initialize logger\n";
        return 1;
    }

    // 初始化 IO 上下文池（会自动初始化信号处理）
    if (!sIOContextPool.Initialize())
    {
        LOG_ERROR("Failed to initialize IOContext pool");
        return 1;
    }

    // 启用 core dump 生成
    sIOContextPool.enableCoreDump();

    // 设置 core dump 文件路径（可选）
    std::string core_dump_path = "./coredumps";
    sIOContextPool.setCoreDumpPath(core_dump_path);

#ifndef _WIN32
    // 创建 core dump 目录（如果不存在）
    struct stat st;
    if (stat(core_dump_path.c_str(), &st) != 0)
    {
        if (mkdir(core_dump_path.c_str(), 0755) != 0)
        {
            LOG_WARN("Failed to create core dump directory: " + core_dump_path);
        }
        else
        {
            LOG_INFO("Created core dump directory: " + core_dump_path);
        }
    }
#endif
    sIOContextPool.setGracefulShutdownCallback(GracefulShutdown);

    // 自定义信号处理
    sIOContextPool.setCustomSignalHandler(SIGUSR1, []() {
        // 自定义逻辑
    });

    // 如果是daemon模式，进行后台运行
    if (daemon_mode)
    {
        std::cout << "Running as daemon...\n";
        // 在Windows上，我们使用CreateProcess创建后台进程
        // 在Linux上，可以使用fork()
#ifdef _WIN32
        // Windows实现
        STARTUPINFO         si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb          = sizeof(si);
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        std::string cmd_line = std::string(argv[0]) + " --config " + sConfig.GetConfigFile();
        if (mode != "")
        {
            cmd_line += " --mode " + mode;
        }
        if (port != 0)
        {
            cmd_line += " --port " + std::to_string(port);
        }
        if (host != "")
        {
            cmd_line += " --host " + host;
        }

        if (!CreateProcess(NULL, const_cast<char*>(cmd_line.c_str()), NULL, NULL, FALSE, CREATE_NEW_PROCESS_GROUP, NULL,
                           NULL, &si, &pi))
        {
            std::cerr << "Failed to create daemon process: " << std::to_string(GetLastError()) << std::endl;
            return 1;
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        std::cout << "Daemon process created successfully\n";
        return 0;
#else
        // Linux/Unix实现
        if (daemon(1, 1) != 0)
        {
            std::cerr << "Failed to daemonize process\n";
            return 1;
        }
#endif
    }

    DeferFunctor finalizer([]() {
        sIOContextPool.Cleanup();
        sLogger.shutdown();
    });

    if (mode == "server")
    {
        ServerExample(port);
    }
    else if (mode == "client")
    {
        ClientExample(host, port);
    }
    else
    {
        LOG_ERROR("Invalid mode. Use 'server' or 'client'.");
        return 1;
    }

    return 0;
}