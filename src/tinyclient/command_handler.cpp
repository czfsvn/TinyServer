#include "command_handler.h"
#include "logger.h"
#include "tinyclient.h"

CommandHandler::CommandHandler(TinyClient& client) : client_(client)
{
    // 注册内置命令
    registerCommand("help", [this](TinyClient&, const std::vector<std::string>& args) {
        if (args.size() > 0)
        {
            showHelp(args[0]);
        }
        else
        {
            listCommands();
        }
    }, "显示帮助信息，用法: help [命令名]");

    registerCommand("connect", [this](TinyClient& client, const std::vector<std::string>&) {
        LOG_INFO("尝试连接服务器...");
        client.connect();
    }, "连接到服务器");

    registerCommand("send", [this](TinyClient& client, const std::vector<std::string>& args) {
        if (args.empty())
        {
            LOG_WARN("请提供要发送的消息内容，用法: send <消息>");
            return;
        }
        std::string message;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                message += " ";
            message += args[i];
        }
        LOG_INFO("发送消息: {}", message);
        client.sendMessage(message);
    }, "发送消息到服务器，用法: send <消息内容>");

    registerCommand("auth", [this](TinyClient& client, const std::vector<std::string>& args) {
        if (args.empty())
        {
            LOG_WARN("请提供用户ID，用法: auth <用户ID>");
            return;
        }
        std::string user_id = args[0];
        LOG_INFO("发送认证消息，用户ID: {}", user_id);
        client.sendMessage(user_id);
    }, "发送认证消息，用法: auth <用户ID>");

    registerCommand("quit", [this](TinyClient& client, const std::vector<std::string>&) {
        LOG_INFO("退出客户端...");
        client.stop();
        exit(0);
    }, "退出客户端");

    registerCommand("ping", [this](TinyClient& client, const std::vector<std::string>&) {
        LOG_INFO("发送 ping 消息");
        client.sendMessage("ping");
    }, "发送 ping 消息到服务器");

    registerCommand("echo", [this](TinyClient& client, const std::vector<std::string>& args) {
        if (args.empty())
        {
            LOG_WARN("请提供要回显的内容，用法: echo <内容>");
            return;
        }
        std::string content;
        for (size_t i = 0; i < args.size(); ++i)
        {
            if (i > 0)
                content += " ";
            content += args[i];
        }
        LOG_INFO("发送 echo 消息: {}", content);
        client.sendMessage("echo " + content);
    }, "发送 echo 消息到服务器，用法: echo <内容>");

    registerCommand("status", [this](TinyClient&, const std::vector<std::string>&) {
        LOG_INFO("客户端状态检查...");
        // 这里可以添加状态检查逻辑
        LOG_INFO("状态: 运行中");
    }, "显示客户端状态");
}

void CommandHandler::registerCommand(const std::string& name, CommandCallback callback, const std::string& help)
{
    commands_[name] = std::make_pair(callback, help);
}

bool CommandHandler::executeCommand(const std::string& input)
{
    if (input.empty())
        return false;

    std::vector<std::string> args = splitCommand(input);
    if (args.empty())
        return false;

    std::string command = args[0];
    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    auto it = commands_.find(command);
    if (it != commands_.end())
    {
        std::vector<std::string> cmd_args(args.begin() + 1, args.end());
        try
        {
            it->second.first(client_, cmd_args);
            return true;
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("命令执行失败: {}", e.what());
            return false;
        }
    }

    LOG_WARN("未知命令: {}, 输入 help 查看可用命令", command);
    return false;
}

void CommandHandler::showHelp(const std::string& command) const
{
    auto it = commands_.find(command);
    if (it != commands_.end())
    {
        LOG_INFO("{}: {}", command, it->second.second);
    }
    else
    {
        LOG_WARN("未知命令: {}", command);
    }
}

void CommandHandler::listCommands() const
{
    LOG_INFO("可用命令列表:");
    for (const auto& pair : commands_)
    {
        LOG_INFO("  {} - {}", pair.first, pair.second.second);
    }
}

std::vector<std::string> CommandHandler::splitCommand(const std::string& input) const
{
    std::vector<std::string> args;
    std::string              arg;
    bool                     in_quotes = false;

    for (char c : input)
    {
        if (c == '"')
        {
            in_quotes = !in_quotes;
        }
        else if (c == ' ' && !in_quotes)
        {
            if (!arg.empty())
            {
                args.push_back(arg);
                arg.clear();
            }
        }
        else
        {
            arg += c;
        }
    }

    if (!arg.empty())
    {
        args.push_back(arg);
    }

    return args;
}
