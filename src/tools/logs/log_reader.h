#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace cncpp
{
    namespace tools
    {

        // 时间戳转换工具函数
        uint64_t convertTimestampToInt(const std::string& timestamp_str);

        // 260420-22:00:34 WS[28] TRACE: [交易] 雪饼是只喵,2481350039->遇见O雨蒙蒙,2961352381, 交易银子, 20000000
        // 银子交易记录
        struct SilverTradeRecord
        {
            std::string timestamp;      // 时间戳字符串 (格式: YYMMDD-HH:MM:SS)
            uint64_t    timestamp_int;  // 整型时间戳 (自Unix epoch以来的秒数)
            int         ws_id;          // WS编号
            std::string sender_name;    // 发送者名称
            uint64_t    sender_id;      // 发送者ID
            std::string receiver_name;  // 接收者名称
            uint64_t    receiver_id;    // 接收者ID
            uint64_t    amount;         // 交易金额
        };

        //
        // 物品交易记录
        struct ItemTradeRecord
        {
            std::string timestamp;           // 时间戳字符串 (格式: YYMMDD-HH:MM:SS)
            uint64_t    timestamp_int;       // 整型时间戳 (自Unix epoch以来的秒数)
            int         ws_id;               // WS编号
            std::string sender_name;         // 发送者名称
            bool        sender_confirmed;    // 发送者确认
            int         item_count;          // 物品数量
            std::string item_name;           // 物品名称
            std::string receiver_name;       // 接收者名称
            bool        receiver_confirmed;  // 接收者确认
            bool        success;             // 是否成功
        };

        // 日志读取工具类
        class LogReader
        {
        public:
            LogReader()  = default;
            ~LogReader() = default;

            // 读取银子交易日志
            bool readSilverTradeLog(const std::string& file_path, std::vector<SilverTradeRecord>& records);

            // 读取物品交易日志
            bool readItemTradeLog(const std::string& file_path, std::vector<ItemTradeRecord>& records);

            // 统计玩家银子交易总额
            void calculateSilverStats(const std::vector<SilverTradeRecord>& records,
                                      std::map<std::string, uint64_t>&      total_sent,
                                      std::map<std::string, uint64_t>&      total_received);

            // 统计玩家物品交易数量
            void calculateItemStats(const std::vector<ItemTradeRecord>&                records,
                                    std::map<std::string, std::map<std::string, int>>& item_counts);

        private:
            // 解析时间戳和WS编号
            bool parseHeader(const std::string& line, std::string& timestamp, int& ws_id);

            // 解析银子交易行
            bool parseSilverTradeLine(const std::string& line, SilverTradeRecord& record);

            // 解析物品交易行
            bool parseItemTradeLine(const std::string& line, ItemTradeRecord& record);
        };

    }  // namespace tools
}  // namespace cncpp
