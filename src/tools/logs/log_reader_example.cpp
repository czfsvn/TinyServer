#include <iomanip>
#include <iostream>
#include <map>
#include <vector>
#include "log_reader.h"
#include "logger.h"

using namespace cncpp::tools;

void printSilverStats(const std::vector<SilverTradeRecord>& records)
{
    std::map<std::string, uint64_t> total_sent;
    std::map<std::string, uint64_t> total_received;

    LogReader reader;
    reader.calculateSilverStats(records, total_sent, total_received);

    std::cout << "\n=== 银子交易统计 ===" << std::endl;
    std::cout << "交易总笔数: " << records.size() << std::endl;

    // 输出发送榜前10
    std::cout << "\n发送银子榜:" << std::endl;
    int count = 0;
    for (const auto& pair : total_sent)
    {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
        if (++count >= 10)
            break;
    }

    // 输出接收榜前10
    std::cout << "\n接收银子榜:" << std::endl;
    count = 0;
    for (const auto& pair : total_received)
    {
        std::cout << "  " << pair.first << ": " << pair.second << std::endl;
        if (++count >= 10)
            break;
    }
}

void printItemStats(const std::vector<ItemTradeRecord>& records)
{
    std::map<std::string, std::map<std::string, int>> item_counts;

    LogReader reader;
    reader.calculateItemStats(records, item_counts);

    std::cout << "\n=== 物品交易统计 ===" << std::endl;
    std::cout << "交易总笔数: " << records.size() << std::endl;

    // 输出每个玩家获得的物品
    std::cout << "\n玩家物品获得统计:" << std::endl;
    for (const auto& player_pair : item_counts)
    {
        std::cout << "  " << player_pair.first << ":" << std::endl;
        for (const auto& item_pair : player_pair.second)
        {
            std::cout << "    " << item_pair.second << "个 " << item_pair.first << std::endl;
        }
    }
}

// 关联查询：根据银子交易记录查找对应的物品交易记录
void findMatchingItemTrades(const std::vector<SilverTradeRecord>& silver_records,
                            const std::vector<ItemTradeRecord>&   item_records)
{
    std::cout << "\n=== 银子与物品交易关联查询 ===" << std::endl;
    std::cout << "关联条件: " << std::endl;
    std::cout << "  1. ItemTradeRecord.timestamp_int == SilverTradeRecord.timestamp_int 或" << std::endl;
    std::cout << "     ItemTradeRecord.timestamp_int == SilverTradeRecord.timestamp_int - 1" << std::endl;
    std::cout << "  2. ws_id 相等" << std::endl;
    std::cout << "  3. SilverTradeRecord.sender_name == ItemTradeRecord.receiver_name" << std::endl;
    std::cout << "  4. SilverTradeRecord.receiver_name == ItemTradeRecord.sender_name" << std::endl;
    std::cout << "\n关联结果:" << std::endl;
    std::cout << "=" << std::string(110, '=') << "=" << std::endl;
    std::cout
        << "| 匹配状态 | 时间戳字符串 | 时间戳(秒) |  WS  | 银子买家(物品卖家)       | 银子卖家(物品买家)       | "
           "银子金额 | 物品数量 | 物品名称               |"
        << std::endl;
    std::cout << "=" << std::string(110, '=') << "=" << std::endl;

    int full_match_count    = 0;
    int partial_match_count = 0;
    for (const auto& silver : silver_records)
    {
        for (const auto& item : item_records)
        {
            // 时间戳匹配：相等或相差1秒
            bool timestamp_match
                = (item.timestamp_int == silver.timestamp_int) || (item.timestamp_int == silver.timestamp_int - 1);
            // ws_id 匹配
            bool ws_id_match = (item.ws_id == silver.ws_id);
            // 玩家名称交叉匹配
            bool name_match = (silver.sender_name == item.receiver_name) && (silver.receiver_name == item.sender_name);

            // 时间戳和ws_id匹配时就打印
            if (timestamp_match && ws_id_match)
            {
                std::string match_status = name_match ? "[完全匹配]" : "[部分匹配]";

                // 使用日志方式输出关联结果
                LOG_INFO(
                    "[关联查询] {} | 时间戳: {}({}) | WS: {} | 银子买家: {} | 银子卖家: {} | 银子金额: {} | 物品数量: "
                    "{} | 物品名称: {}",
                    match_status, silver.timestamp, silver.timestamp_int, silver.ws_id, silver.sender_name,
                    silver.receiver_name, silver.amount, item.item_count, item.item_name);

                if (name_match)
                    full_match_count++;
                else
                    partial_match_count++;
            }
        }
    }

    std::cout << "=" << std::string(110, '=') << "=" << std::endl;
    std::cout << "共找到 " << full_match_count << " 条完全匹配记录，" << partial_match_count
              << " 条部分匹配记录（时间戳和WS匹配但玩家名称不匹配）" << std::endl;
}

namespace trade
{
    void main(const std::string& zonestr, const std::string& silver_log_path, const std::string& item_log_path)
    {
        LogReader reader;

        // 读取银子交易日志
        std::vector<SilverTradeRecord> silver_records;
        if (!reader.readSilverTradeLog(silver_log_path, silver_records))
        {
            LOG_ERROR("读取银子交易日志失败");
            return;
        }
        std::cout << "成功读取" << zonestr << "银子交易日志: " << silver_records.size() << " 条记录" << std::endl;
        LOG_INFO("成功读取{}银子交易日志: {} 条记录", zonestr, silver_records.size());
        //printSilverStats(silver_records);

        // 读取物品交易日志
        std::vector<ItemTradeRecord> item_records;
        if (!reader.readItemTradeLog(item_log_path, item_records))
        {
            LOG_ERROR("读取物品交易日志失败");
            return;
        }
        std::cout << "\n成功读取" << zonestr << "物品交易日志: " << item_records.size() << " 条记录" << std::endl;
        LOG_INFO("成功读取{}物品交易日志: {} 条记录", zonestr, item_records.size());
        // printItemStats(item_records);

        // 关联查询
        LOG_INFO("{}开始关联查询...", zonestr);
        findMatchingItemTrades(silver_records, item_records);

        LOG_INFO("{}Log reader example finished", zonestr);
    }
}  // namespace trade

int main()
{
    const std::string path = "/workspace/myserver/src/tools/logs";
    // 初始化 logger
    if (!sLogger.init())
    {
        std::cerr << "Logger initialization failed!" << std::endl;
        return 1;
    }
    LOG_INFO("Log reader example started");

    trade::main("Asia/Shanghai", path + "/802-silver.txt", path + "/802-user.txt");

    return 0;
}
