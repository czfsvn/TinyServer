#include "client_service.h"
#include <algorithm>
#include <iostream>
#include "logger.h"


ClientService::ClientService()
{
}

ClientService::~ClientService()
{
    stopCommandThread();
}

bool ClientService::init(size_t client_count)
{
    client_count_ = client_count;
    return true;
}

bool ClientService::onInit()
{
    LOG_INFO("ClientService initializing...");

    if (!sTinyClientManager.init(client_count_))
    {
        LOG_ERROR("Failed to initialize ClientManager");
        return false;
    }

    LOG_INFO("ClientService initialized with {} clients", client_count_);
    return true;
}

bool ClientService::onStart()
{
    LOG_INFO("ClientService starting...");

    if (!sTinyClientManager.connectAll())
    {
        LOG_ERROR("Failed to connect clients");
        return false;
    }

    startCommandThread();

    LOG_INFO("ClientService started successfully");
    return true;
}

void ClientService::onStop()
{
    LOG_INFO("ClientService stopping...");

    sTinyClientManager.disconnectAll();

    LOG_INFO("ClientService stopped");
}

bool ClientService::onTick()
{
    static uint32_t tick_count = 0;

    if (++tick_count % 100 == 0)
    {
        sTinyClientManager.processAllMessages();

        size_t connected = sTinyClientManager.getConnectedCount();
        size_t total     = sTinyClientManager.getClientCount();

        LOG_DEBUG("ClientService tick - connected: {}/{}", connected, total);
    }

    return true;
}

TinyClientPtr ClientService::getClient(uint32_t index)
{
    return sTinyClientManager.getClient(index);
}

TinyClientPtr ClientService::getAvailableClient()
{
    return sTinyClientManager.getAvailableClient();
}

void ClientService::printHelp() const
{
    std::cout << "\nTinyClient 命令行帮助\n";
    std::cout << "------------------------\n";
    std::cout << "help      - 显示此帮助信息\n";
    std::cout << "quit/exit - 退出客户端\n";
    std::cout << "------------------------\n";
}

void ClientService::handleCommandInput()
{
    std::string input;
    std::cout << "\nTinyClient 命令行模式\n";
    std::cout << "输入 'help' 查看可用命令\n";
    std::cout << "------------------------\n";

    while (cmd_thread_running_.load())
    {
        std::cout << "> ";
        std::getline(std::cin, input);

        if (input.empty())
            continue;

        std::string lower_input = input;
        std::transform(lower_input.begin(), lower_input.end(), lower_input.begin(), ::tolower);

        if (lower_input == "help")
        {
            printHelp();
            continue;
        }

        if (lower_input == "quit" || lower_input == "exit")
        {
            stop();
            break;
        }

        auto client = getAvailableClient();
        if (client && client->getCommandHandler())
        {
            client->getCommandHandler()->executeCommand(input);
        }
        else
        {
            std::cout << "No available client to execute command\n";
        }
    }
}

void ClientService::startCommandThread()
{
    if (!cmd_thread_running_.load())
    {
        cmd_thread_running_.store(true);
        cmd_thread_ = std::thread(&ClientService::handleCommandInput, this);
        LOG_INFO("Command thread started");
    }
}

void ClientService::stopCommandThread()
{
    if (cmd_thread_running_.exchange(false))
    {
        if (cmd_thread_.joinable())
        {
            cmd_thread_.join();
        }
        LOG_INFO("Command thread stopped");
    }
}
