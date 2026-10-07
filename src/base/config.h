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

    // 连接配置
    class MysqlConfig : public ConfigModule
    {
    public:
        MysqlConfig() : port(3306), charset("utf8mb4")
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            host.ReadFromTree(tree, "mysql", "host");
            user.ReadFromTree(tree, "mysql", "user");
            password.ReadFromTree(tree, "mysql", "password");
            database.ReadFromTree(tree, "mysql", "database");
            port.ReadFromTree(tree, "mysql", "port");
            charset.ReadFromTree(tree, "mysql", "charset");
        }

    public:
        ConfigItem<std::string> host;
        ConfigItem<std::string> user;
        ConfigItem<std::string> password;
        ConfigItem<std::string> database;
        ConfigItem<uint32_t>    port;
        ConfigItem<std::string> charset;
    };

    class RedisConfig : public ConfigModule
    {
    public:
        RedisConfig() : port(6379), dbindex(0)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            host.ReadFromTree(tree, "redis", "host");
            port.ReadFromTree(tree, "redis", "port");
            dbindex.ReadFromTree(tree, "redis", "dbindex");
        }

    public:
        ConfigItem<std::string> host;
        ConfigItem<uint32_t>    port;
        ConfigItem<uint32_t>    dbindex;
    };

    class MainConfig : public ConfigModule
    {
    public:
        MainConfig()
            : daemon(false), app_name("network_app"), max_messages_per_task_per_tick(0), max_messages_per_tick(0),
              receive_queue_capacity(256), send_queue_capacity(1024)
        {
        }

        void ReadFromTree(const pt::ptree& tree) override
        {
            daemon.ReadFromTree(tree, "main", "daemon");
            app_name.ReadFromTree(tree, "main", "app_name");
            async_thread_count.ReadFromTree(tree, "main", "async_thread_count");
            main_loop_interval_ms.ReadFromTree(tree, "main", "main_loop_interval_ms");
            asio_pool_size.ReadFromTree(tree, "main", "asio_pool_size");
            log_yml_path.ReadFromTree(tree, "main", "log_yml_path");
            config_dir.ReadFromTree(tree, "main", "config_dir");
            max_messages_per_task_per_tick.ReadFromTree(tree, "main", "max_messages_per_task_per_tick");
            max_messages_per_tick.ReadFromTree(tree, "main", "max_messages_per_tick");
            receive_queue_capacity.ReadFromTree(tree, "main", "receive_queue_capacity");
            send_queue_capacity.ReadFromTree(tree, "main", "send_queue_capacity");
        }

    public:
        ConfigItem<bool>        daemon;
        ConfigItem<std::string> app_name;
        ConfigItem<uint16_t>    async_thread_count;
        ConfigItem<uint16_t>    main_loop_interval_ms;
        ConfigItem<uint16_t>    asio_pool_size;
        ConfigItem<std::string> log_yml_path;
        ConfigItem<std::string> config_dir;
        // 每拍每个 task 最多处理多少条入站消息。0 = 不限（抽干，见 ADR-0004）。
        //
        // 抽干让单拍耗时没有上界：连接多 + 突发流量时一拍就会跑爆，主循环定时器
        // 随即落后，而 ADR-0001 规定时间轮不做补拍，于是时间轮整体走慢且再也追不回来。
        // 配成正数即给单拍耗时设上界，超出的消息留到下一拍。
        //
        // 代价不只是延迟上升：接收队列有入队侧上限（见 receive_queue_capacity），
        // 限量降低了消费速率，持续跟不上时队列会填满，此后 io 线程直接丢弃新消息
        // 并计数告警（见 network.cpp 的 enqueue）。也就是说这是拿"突发时丢消息"
        // 换"定时器不落后"，不是免费的。单连接消费能力上限约为
        // 本值 × (1000 / main_loop_interval_ms) 条/秒，配的时候照这个算：
        // 低于持续入队速率就会一直丢。
        ConfigItem<uint32_t>    max_messages_per_task_per_tick;
        // 每拍所有 task 合计最多处理多少条入站消息。0 = 不限。
        //
        // 每 task 的上限挡不住连接数这个维度：1 万连接 × 每连接 64 条 = 64 万条/拍，
        // 照样把一拍跑爆。这个值是整拍的硬上界，与轮转配套保证连接之间公平。
        ConfigItem<uint32_t>    max_messages_per_tick;
        // 每条连接的**接收**队列上限（条数）。到上限后 io 线程丢弃新帧并计数告警。
        //
        // 底层队列本身是无界的（moodycamel::ConcurrentQueue），这个上限是我们自己
        // 在入队侧加的闸门（见 my_concurrent_queue.h 与 ADR-0010）。它同时锁住内存
        // 高水位：moodycamel 的 block 只回收进内部 free list、从不归还系统，不设
        // 闸的话涨过一次就永久占着。
        //
        // 只在启动时读取，运行中不可改（项目没有配置热更新通道，signal_handler.cpp
        // 的 SIGHUP 还是个空 TODO）。见 ADR-0010 里"为什么不做运行时修改接口"。
        ConfigItem<uint32_t>    receive_queue_capacity;
        // 每条连接的**发送**队列上限（条数）。到上限后丢弃待发帧并计数告警。
        //
        // 这个值比接收侧大：发送侧会出现瞬时冲高（一次全服广播、一拍内对同一连接
        // 产生多条响应），等一拍就发完了，取小了会在广播风暴时误弃载。
        //
        // 它同时修掉一个既存隐患：send_queue_ 早先是无界的，而对端不读数据时
        // async_write 悬着、is_sending_ 一直为 true，后续 send() 只入队不 kick，
        // 队列会一直涨 —— 一个不读数据的客户端能让服务器内存无上限增长。
        ConfigItem<uint32_t>    send_queue_capacity;
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
                encryption_config_.ReadFromTree(tree);
                main_config_.ReadFromTree(tree);
                gateway_config_.ReadFromTree(tree);
                tinyserver_config_.ReadFromTree(tree);
                client_config_.ReadFromTree(tree);
                dataserver_config_.ReadFromTree(tree);
                mysql_config_.ReadFromTree(tree);
                redis_config_.ReadFromTree(tree);

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

        // 获取 Mysql 配置
        const MysqlConfig& GetMysqlConfig() const
        {
            return mysql_config_.GetConfig();
        }

        // 获取 Redis 配置
        const RedisConfig& GetRedisConfig() const
        {
            return redis_config_.GetConfig();
        }

    private:
        std::string                     config_file_;
        ConfigManager<EncryptionConfig> encryption_config_;
        ConfigManager<MainConfig>       main_config_;
        ConfigManager<GatewayConfig>    gateway_config_;
        ConfigManager<TinyServerConfig> tinyserver_config_;
        ConfigManager<ClientConfig>     client_config_;
        ConfigManager<DataServerConfig> dataserver_config_;
        ConfigManager<MysqlConfig>      mysql_config_;
        ConfigManager<RedisConfig>      redis_config_;
    };

}  // namespace cncpp

#define sConfig cncpp::Config::getMe()
#define sLoggerConfig cncpp::Config::getMe().GetLoggerConfig()
#define sEncryptionConfig cncpp::Config::getMe().GetEncryptionConfig()
#define sMainConfig cncpp::Config::getMe().GetMainConfig()
#define sGatewayConfig cncpp::Config::getMe().GetGatewayConfig()
#define sTinyServerConfig cncpp::Config::getMe().GetTinyServerConfig()
#define sClientConfig cncpp::Config::getMe().GetClientConfig()
#define sDataServerConfig cncpp::Config::getMe().GetDataServerConfig()
#define sMysqlConfig cncpp::Config::getMe().GetMysqlConfig()
#define sRedisConfig cncpp::Config::getMe().GetRedisConfig()

#endif  // CONFIG_H
