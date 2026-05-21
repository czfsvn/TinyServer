#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class TinyClient;

// 命令处理器类
class CommandHandler
{
public:
    using CommandCallback = std::function<void(TinyClient& client, const std::vector<std::string>& args)>;

    CommandHandler(TinyClient& client);
    ~CommandHandler() = default;

    // 禁止拷贝和移动
    CommandHandler(const CommandHandler&)            = delete;
    CommandHandler& operator=(const CommandHandler&) = delete;
    CommandHandler(CommandHandler&&)                 = delete;
    CommandHandler& operator=(CommandHandler&&)      = delete;

    // 注册命令
    void registerCommand(const std::string& name, CommandCallback callback, const std::string& help);

    // 解析并执行命令
    bool executeCommand(const std::string& input);

    // 显示帮助信息
    void showHelp(const std::string& command = "") const;

    // 显示所有可用命令
    void listCommands() const;

private:
    // 分割命令行参数
    std::vector<std::string> splitCommand(const std::string& input) const;

private:
    TinyClient&                                                    client_;
    std::map<std::string, std::pair<CommandCallback, std::string>> commands_;  // 命令名 -> (回调, 帮助信息)
};

using CommandHandlerPtr = std::shared_ptr<CommandHandler>;
