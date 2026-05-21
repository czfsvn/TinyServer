#include "log_reader.h"
#include <ctime>
#include <fstream>
#include <regex>
#include <stdexcept>
#include "logger.h"

namespace cncpp
{
    namespace tools
    {

        // 时间戳转换工具函数
        uint64_t convertTimestampToInt(const std::string& timestamp_str)
        {
            // 格式: YYMMDD-HH:MM:SS (例如: 260420-01:24:53)，共15个字符

            // 先去除首尾空白字符（可能包含换行符、空格等）
            std::string ts    = timestamp_str;
            auto        start = ts.find_first_not_of(" \t\n\r");
            auto        end   = ts.find_last_not_of(" \t\n\r");

            if (start == std::string::npos || end == std::string::npos)
            {
                LOG_WARN("Empty timestamp string");
                return 0;
            }

            ts = ts.substr(start, end - start + 1);

            // 验证长度：YYMMDD-HH:MM:SS = 15个字符
            if (ts.length() != 15)
            {
                LOG_WARN("Invalid timestamp format (length={}): '{}'", ts.length(), ts);
                return 0;
            }

            try
            {
                int year   = 2000 + std::stoi(ts.substr(0, 2));  // YY -> 20YY
                int month  = std::stoi(ts.substr(2, 2));
                int day    = std::stoi(ts.substr(4, 2));
                int hour   = std::stoi(ts.substr(7, 2));
                int minute = std::stoi(ts.substr(10, 2));
                int second = std::stoi(ts.substr(13, 2));

                std::tm tm{};
                tm.tm_year = year - 1900;  // years since 1900
                tm.tm_mon  = month - 1;    // months since January [0-11]
                tm.tm_mday = day;          // day of the month [1-31]
                tm.tm_hour = hour;         // hours [0-23]
                tm.tm_min  = minute;       // minutes [0-59]
                tm.tm_sec  = second;       // seconds [0-60]

                // Convert to Unix timestamp (seconds since epoch)
                time_t t = std::mktime(&tm);
                if (t == -1)
                {
                    LOG_WARN("Failed to convert timestamp to time_t: '{}'", ts);
                    return 0;
                }

                return static_cast<uint64_t>(t);
            }
            catch (const std::exception& e)
            {
                LOG_WARN("Error parsing timestamp '{}': {}", ts, e.what());
                return 0;
            }
        }

        bool LogReader::readSilverTradeLog(const std::string& file_path, std::vector<SilverTradeRecord>& records)
        {
            std::ifstream file(file_path);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open file: {}", file_path);
                return false;
            }

            std::string line;
            while (std::getline(file, line))
            {
                // 跳过无效行
                if (line.empty() || line.find("[交易]") == std::string::npos)
                {
                    continue;
                }

                SilverTradeRecord record;
                if (parseSilverTradeLine(line, record))
                {
                    records.push_back(record);
                }
            }

            file.close();
            LOG_INFO("Read {} silver trade records from {}", records.size(), file_path);
            return true;
        }

        bool LogReader::readItemTradeLog(const std::string& file_path, std::vector<ItemTradeRecord>& records)
        {
            std::ifstream file(file_path);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open file: {}", file_path);
                return false;
            }

            std::string line;
            while (std::getline(file, line))
            {
                // 跳过无效行
                if (line.empty() || line.find("[交易:玩家<------>玩家]") == std::string::npos)
                {
                    continue;
                }

                ItemTradeRecord record;
                if (parseItemTradeLine(line, record))
                {
                    records.push_back(record);
                }
            }

            file.close();
            LOG_INFO("Read {} item trade records from {}", records.size(), file_path);
            return true;
        }

        bool LogReader::parseHeader(const std::string& line, std::string& timestamp, int& ws_id)
        {
            // 格式: 260420-01:24:53 WS[22] TRACE: ...
            std::regex  header_regex(R"(^(\d{6}-\d{2}:\d{2}:\d{2})\s+WS\[(\d+)\])");
            std::smatch match;

            if (std::regex_search(line, match, header_regex))
            {
                timestamp = match[1].str();
                ws_id     = std::stoi(match[2].str());
                return true;
            }

            return false;
        }

        bool LogReader::parseSilverTradeLine(const std::string& line, SilverTradeRecord& record)
        {
            // 格式: 260420-01:24:53 WS[22] TRACE: [交易] 在路上m剑客,2081350719->在路上ffx宝贝,2221350574, 交易银子, 100000
            std::regex trade_regex(
                R"(^(\d{6}-\d{2}:\d{2}:\d{2})\s+WS\[(\d+)\]\s+TRACE:\s+\[交易\]\s+([^,]+),(\d+)->([^,]+),(\d+),\s+交易银子,\s+(\d+))");
            std::smatch match;

            if (std::regex_search(line, match, trade_regex))
            {
                record.timestamp     = match[1].str();
                record.timestamp_int = convertTimestampToInt(record.timestamp);  // 转换为整型时间戳
                record.ws_id         = std::stoi(match[2].str());
                record.sender_name   = match[3].str();
                record.sender_id     = std::stoull(match[4].str());
                record.receiver_name = match[5].str();
                record.receiver_id   = std::stoull(match[6].str());
                record.amount        = std::stoull(match[7].str());
                return true;
            }

            return false;
        }

        bool LogReader::parseItemTradeLine(const std::string& line, ItemTradeRecord& record)
        {
            // 格式: 260420-00:27:32 WS[22] TRACE: [交易:玩家<------>玩家] 大雄aay [yes]  交易 2个图鉴碎片-幸运青灵 给 大雄aai茶 [yes]  成功
            std::regex item_regex(
                R"(^(\d{6}-\d{2}:\d{2}:\d{2})\s+WS\[(\d+)\]\s+TRACE:\s+\[交易:玩家<------>玩家\]\s+([^\[]+)\s+\[([^\]]+)\]\s+交易\s+(\d+)个([^\s]+)\s+给\s+([^\[]+)\s+\[([^\]]+)\]\s+(\S+))");
            std::smatch match;

            if (std::regex_search(line, match, item_regex))
            {
                record.timestamp          = match[1].str();
                record.timestamp_int      = convertTimestampToInt(record.timestamp);  // 转换为整型时间戳
                record.ws_id              = std::stoi(match[2].str());
                record.sender_name        = match[3].str();
                record.sender_confirmed   = (match[4].str() == "yes");
                record.item_count         = std::stoi(match[5].str());
                record.item_name          = match[6].str();
                record.receiver_name      = match[7].str();
                record.receiver_confirmed = (match[8].str() == "yes");
                record.success            = (match[9].str() == "成功");
                return true;
            }

            return false;
        }

        void LogReader::calculateSilverStats(const std::vector<SilverTradeRecord>& records,
                                             std::map<std::string, uint64_t>&      total_sent,
                                             std::map<std::string, uint64_t>&      total_received)
        {
            for (const auto& record : records)
            {
                total_sent[record.sender_name] += record.amount;
                total_received[record.receiver_name] += record.amount;
            }
        }

        void LogReader::calculateItemStats(const std::vector<ItemTradeRecord>&                records,
                                           std::map<std::string, std::map<std::string, int>>& item_counts)
        {
            for (const auto& record : records)
            {
                if (record.success)
                {
                    item_counts[record.receiver_name][record.item_name] += record.item_count;
                }
            }
        }

    }  // namespace tools
}  // namespace cncpp
