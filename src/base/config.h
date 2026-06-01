#ifndef CONFIG_H
#define CONFIG_H

#include <boost/program_options.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <iostream>
#include <string>
#include "singleton.h"

namespace cncpp
{
    namespace po = boost::program_options;
    namespace pt = boost::property_tree;

    // 配置项模板类
    template <typename T>
    class ConfigItem
    {
    public:
        ConfigItem() : value_()
        {
        }

        ConfigItem(const T& default_value) : value_(default_value)
        {
        }

        void ReadFromTree(const pt::ptree& tree, const std::string& section, const std::string& key)
        {
            if (tree.count(section))
            {
                const auto& section_tree = tree.get_child(section);
                if (section_tree.count(key))
                {
                    value_ = section_tree.get<T>(key);
                }
            }
        }

        const T& get() const
        {
            return value_;
        }

        const T& operator()() const
        {
            return value_;
        }

        void set(const T& value)
        {
            value_ = value;
        }

    private:
        T value_;
    };

    // 配置模块基类
    class ConfigModule
    {
    public:
        virtual ~ConfigModule()                          = default;
        virtual void ReadFromTree(const pt::ptree& tree) = 0;
    };

    class MainConfig : public ConfigModule
    {
    public:
        MainConfig() : daemon(false), app_name("network_app")
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            daemon.ReadFromTree(tree, "main", "daemon");
            app_name.ReadFromTree(tree, "main", "app_name");
            async_thread_count.ReadFromTree(tree, "main", "async_thread_count");
            main_loop_interval_ms.ReadFromTree(tree, "main", "main_loop_interval_ms");
            asio_pool_size.ReadFromTree(tree, "main", "asio_pool_size");
            asio_timer_interval_ms.ReadFromTree(tree, "main", "asio_timer_interval_ms");
        }

    public:
        ConfigItem<bool>        daemon;
        ConfigItem<std::string> app_name;
        ConfigItem<uint16_t>    async_thread_count;
        ConfigItem<uint16_t>    main_loop_interval_ms;
        ConfigItem<uint16_t>    asio_pool_size;
        ConfigItem<uint16_t>    asio_timer_interval_ms;
    };

    // 网络配置类
    class NetworkConfig : public ConfigModule
    {
    public:
        NetworkConfig()
            : port(8080),
              host("127.0.0.1"),
              mode("server"),
              thread_count(1),
              server_host("127.0.0.1"),
              server_port(9090)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            port.ReadFromTree(tree, "network", "port");
            host.ReadFromTree(tree, "network", "host");
            mode.ReadFromTree(tree, "network", "mode");
            thread_count.ReadFromTree(tree, "network", "thread_count");
            server_host.ReadFromTree(tree, "network", "server_host");
            server_port.ReadFromTree(tree, "network", "server_port");
        }

    public:
        ConfigItem<short>       port;
        ConfigItem<std::string> host;
        ConfigItem<std::string> mode;
        ConfigItem<uint16_t>    thread_count;
        ConfigItem<std::string> server_host;  // 网关连接的服务器地址
        ConfigItem<short>       server_port;  // 网关连接的服务器端口
    };

    // 日志配置类
    class LoggerConfig : public ConfigModule
    {
    public:
        LoggerConfig()
            : log_dir("logs"),
              app_name("network_app"),
              log_file("app.log"),
              retention_days(30),
              retention_hours(72),
              max_file_size(1024 * 1024 * 100),
              enable_console(true),
              async_mode(false),
              async_queue_size(8192),
              console_level("DBG"),
              file_level("LOG_INFO"),
              pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%t%$] [%^%l%$] %v"),
              rotation_policy("DAILY")
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            log_dir.ReadFromTree(tree, "logger", "log_dir");
            app_name.ReadFromTree(tree, "logger", "app_name");
            log_file.ReadFromTree(tree, "logger", "log_file");
            retention_days.ReadFromTree(tree, "logger", "retention_days");
            retention_hours.ReadFromTree(tree, "logger", "retention_hours");
            max_file_size.ReadFromTree(tree, "logger", "max_file_size");
            enable_console.ReadFromTree(tree, "logger", "enable_console");
            async_mode.ReadFromTree(tree, "logger", "async_mode");
            async_queue_size.ReadFromTree(tree, "logger", "async_queue_size");
            console_level.ReadFromTree(tree, "logger", "console_level");
            file_level.ReadFromTree(tree, "logger", "file_level");
            pattern.ReadFromTree(tree, "logger", "pattern");
            rotation_policy.ReadFromTree(tree, "logger", "rotation_policy");
        }

    public:
        ConfigItem<std::string> log_dir;           // 日志目录
        ConfigItem<std::string> app_name;          // 应用名称
        ConfigItem<std::string> log_file;          // 日志文件名
        ConfigItem<int>         retention_days;    // 保留天数（按日）
        ConfigItem<int>         retention_hours;   // 保留小时数（按小时）
        ConfigItem<uint32_t>    max_file_size;     // 单个文件最大大小（默认100MB）
        ConfigItem<bool>        enable_console;    // 是否启用控制台日志
        ConfigItem<bool>        async_mode;        // 是否启用异步模式
        ConfigItem<uint32_t>    async_queue_size;  // 异步队列大小
        ConfigItem<std::string> console_level;     // 控制台日志级别
        ConfigItem<std::string> file_level;        // 文件日志级别
        ConfigItem<std::string> pattern;           // 日志格式
        ConfigItem<std::string> rotation_policy;   // 日志滚动策略
    };

    // 加密配置类
    class EncryptionConfig : public ConfigModule
    {
    public:
        EncryptionConfig() : encryption_key_("MySecretKey12345")
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            encryption_key_.ReadFromTree(tree, "encryption", "key");
        }

        const std::string& encryptionKey() const
        {
            return encryption_key_.get();
        }

    private:
        ConfigItem<std::string> encryption_key_;
    };

    // Gateway 配置类
    class GatewayConfig : public ConfigModule
    {
    public:
        GatewayConfig()
            : listen_port(8080),
              listen_host("0.0.0.0"),
              backend_host("127.0.0.1"),
              backend_port(9090),
              thread_count(4),
              max_connections(10000)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            listen_port.ReadFromTree(tree, "gateway", "listen_port");
            listen_host.ReadFromTree(tree, "gateway", "listen_host");
            backend_host.ReadFromTree(tree, "gateway", "backend_host");
            backend_port.ReadFromTree(tree, "gateway", "backend_port");
            thread_count.ReadFromTree(tree, "gateway", "thread_count");
            max_connections.ReadFromTree(tree, "gateway", "max_connections");
        }

    public:
        ConfigItem<short>       listen_port;      // 网关监听端口
        ConfigItem<std::string> listen_host;      // 网关监听地址
        ConfigItem<std::string> backend_host;     // 后端服务器地址
        ConfigItem<short>       backend_port;     // 后端服务器端口
        ConfigItem<uint16_t>    thread_count;     // 工作线程数
        ConfigItem<uint32_t>    max_connections;  // 最大连接数
    };

    // TinyServer 配置类
    class TinyServerConfig : public ConfigModule
    {
    public:
        TinyServerConfig()
            : listen_port(9090),
              listen_host("0.0.0.0"),
              thread_count(4),
              max_connections(10000),
              heartbeat_interval_ms(30000),
              idle_timeout_ms(60000)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            listen_port.ReadFromTree(tree, "tinyserver", "listen_port");
            listen_host.ReadFromTree(tree, "tinyserver", "listen_host");
            thread_count.ReadFromTree(tree, "tinyserver", "thread_count");
            max_connections.ReadFromTree(tree, "tinyserver", "max_connections");
            heartbeat_interval_ms.ReadFromTree(tree, "tinyserver", "heartbeat_interval_ms");
            idle_timeout_ms.ReadFromTree(tree, "tinyserver", "idle_timeout_ms");
        }

    public:
        ConfigItem<short>       listen_port;            // 服务器监听端口
        ConfigItem<std::string> listen_host;            // 服务器监听地址
        ConfigItem<uint16_t>    thread_count;           // 工作线程数
        ConfigItem<uint32_t>    max_connections;        // 最大连接数
        ConfigItem<uint32_t>    heartbeat_interval_ms;  // 心跳间隔（毫秒）
        ConfigItem<uint32_t>    idle_timeout_ms;        // 空闲超时时间（毫秒）
    };

    // Client 配置类
    class ClientConfig : public ConfigModule
    {
    public:
        ClientConfig()
            : server_host("127.0.0.1"),
              server_port(9090),
              client_count(4),
              reconnect_interval_ms(5000),
              heartbeat_interval_ms(30000)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            server_host.ReadFromTree(tree, "client", "server_host");
            server_port.ReadFromTree(tree, "client", "server_port");
            client_count.ReadFromTree(tree, "client", "client_count");
            reconnect_interval_ms.ReadFromTree(tree, "client", "reconnect_interval_ms");
            heartbeat_interval_ms.ReadFromTree(tree, "client", "heartbeat_interval_ms");
        }

    public:
        ConfigItem<std::string> server_host;            // 服务器地址
        ConfigItem<short>       server_port;            // 服务器端口
        ConfigItem<uint16_t>    client_count;           // 客户端连接数
        ConfigItem<uint32_t>    reconnect_interval_ms;  // 重连间隔（毫秒）
        ConfigItem<uint32_t>    heartbeat_interval_ms;  // 心跳间隔（毫秒）
    };

    // DataServer 配置类
    class DataServerConfig : public ConfigModule
    {
    public:
        DataServerConfig()
            : listen_port(9100),
              listen_host("0.0.0.0"),
              thread_count(2),
              db_path("./data"),
              max_cache_size(1024 * 1024 * 100)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            listen_port.ReadFromTree(tree, "dataserver", "listen_port");
            listen_host.ReadFromTree(tree, "dataserver", "listen_host");
            thread_count.ReadFromTree(tree, "dataserver", "thread_count");
            db_path.ReadFromTree(tree, "dataserver", "db_path");
            max_cache_size.ReadFromTree(tree, "dataserver", "max_cache_size");
        }

    public:
        ConfigItem<short>       listen_port;     // 数据服务器监听端口
        ConfigItem<std::string> listen_host;     // 数据服务器监听地址
        ConfigItem<uint16_t>    thread_count;    // 工作线程数
        ConfigItem<std::string> db_path;         // 数据库文件路径
        ConfigItem<uint32_t>    max_cache_size;  // 最大缓存大小（字节）
    };

    // 配置管理器模板类
    template <typename T>
    class ConfigManager
    {
    public:
        ConfigManager() : config_(T())
        {
        }

        void ReadFromTree(const pt::ptree& tree)
        {
            config_.ReadFromTree(tree);
        }

        const T& GetConfig() const
        {
            return config_;
        }
        T& GetConfig()
        {
            return config_;
        }

    private:
        T config_;
    };

    // 主配置类
    class Config : public Singleton<Config>
    {
        friend class Singleton<Config>;

    public:
        Config() = default;

        // 从命令行参数读取配置
        bool ReadFromCommandLine(int argc, char* argv[])
        {
            try
            {
                po::options_description desc("Allowed options");
                desc.add_options()("help,h", "produce help message")("config,c", po::value<std::string>(),
                                                                     "config file path")(
                    "port,p", po::value<short>(), "server port")("host,H", po::value<std::string>(), "server host")(
                    "mode,m", po::value<std::string>(), "mode: server or client");

                po::variables_map vm;
                po::store(po::parse_command_line(argc, argv, desc), vm);
                po::notify(vm);

                if (vm.count("help"))
                {
                    std::cout << desc << std::endl;
                    return false;
                }

                if (vm.count("config"))
                {
                    config_file_ = vm["config"].as<std::string>();
                    ReadFromFile(config_file_);
                }

                if (vm.count("port"))
                {
                    network_config_.GetConfig().port.set(vm["port"].as<short>());
                }

                if (vm.count("host"))
                {
                    network_config_.GetConfig().host.set(vm["host"].as<std::string>());
                }

                if (vm.count("mode"))
                {
                    network_config_.GetConfig().mode.set(vm["mode"].as<std::string>());
                }

                return true;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error reading command line arguments: " << std::string(e.what()) << std::endl;
                return false;
            }
        }

        // 从 ini 文件读取配置
        bool ReadFromFile(const std::string& file_path)
        {
            try
            {
                pt::ptree tree;
                pt::read_ini(file_path, tree);

                // 按模块读取配置
                network_config_.ReadFromTree(tree);
                logger_config_.ReadFromTree(tree);
                encryption_config_.ReadFromTree(tree);
                main_config_.ReadFromTree(tree);
                gateway_config_.ReadFromTree(tree);
                tinyserver_config_.ReadFromTree(tree);
                client_config_.ReadFromTree(tree);
                dataserver_config_.ReadFromTree(tree);

                return true;
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error reading config file: " << std::string(e.what()) << std::endl;
                //LOG_ERROR("Error reading config file: {}", std::string(e.what()));
                return false;
            }
        }

        const MainConfig& GetMainConfig() const
        {
            return main_config_.GetConfig();
        }

        // 获取网络配置
        const NetworkConfig& GetNetworkConfig() const
        {
            return network_config_.GetConfig();
        }

        const LoggerConfig& GetLoggerConfig() const
        {
            return logger_config_.GetConfig();
        }

        LoggerConfig& GetLoggerConfig()
        {
            return logger_config_.GetConfig();
            //return logger_config_;
        }

        // 获取加密配置
        const EncryptionConfig& GetEncryptionConfig() const
        {
            return encryption_config_.GetConfig();
        }

        // 获取 Gateway 配置
        const GatewayConfig& GetGatewayConfig() const
        {
            return gateway_config_.GetConfig();
        }

        // 获取 TinyServer 配置
        const TinyServerConfig& GetTinyServerConfig() const
        {
            return tinyserver_config_.GetConfig();
        }

        // 获取 Client 配置
        const ClientConfig& GetClientConfig() const
        {
            return client_config_.GetConfig();
        }

        // 获取 DataServer 配置
        const DataServerConfig& GetDataServerConfig() const
        {
            return dataserver_config_.GetConfig();
        }

        const std::string& GetConfigFile() const
        {
            return config_file_;
        }

    private:
        std::string                     config_file_;
        ConfigManager<NetworkConfig>    network_config_;
        ConfigManager<LoggerConfig>     logger_config_;
        ConfigManager<EncryptionConfig> encryption_config_;
        ConfigManager<MainConfig>       main_config_;
        ConfigManager<GatewayConfig>    gateway_config_;
        ConfigManager<TinyServerConfig> tinyserver_config_;
        ConfigManager<ClientConfig>     client_config_;
        ConfigManager<DataServerConfig> dataserver_config_;
    };

}  // namespace cncpp

#define sConfig cncpp::Config::getMe()
#define sNetworkConfig cncpp::Config::getMe().GetNetworkConfig()
#define sLoggerConfig cncpp::Config::getMe().GetLoggerConfig()
#define sEncryptionConfig cncpp::Config::getMe().GetEncryptionConfig()
#define sMainConfig cncpp::Config::getMe().GetMainConfig()
#define sGatewayConfig cncpp::Config::getMe().GetGatewayConfig()
#define sTinyServerConfig cncpp::Config::getMe().GetTinyServerConfig()
#define sClientConfig cncpp::Config::getMe().GetClientConfig()
#define sDataServerConfig cncpp::Config::getMe().GetDataServerConfig()

#endif  // CONFIG_H
