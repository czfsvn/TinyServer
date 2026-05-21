#include "stringutil.h"
#include <cstdarg>

namespace cncpp
{
    std::string& replace_all(std::string& src, const std::string& old_str, const std::string& new_str)
    {
        while (true)
        {
            std::string::size_type pos = src.find(old_str);
            if (pos == std::string::npos)
                break;

            src.replace(pos, old_str.length(), new_str);
            /*
            if ((pos = str.find(old_str)) != std::string::npos)
                str.replace(pos, old_str.length(), new_str);
            else
                break;
                */
        }

        return src;
    }

    void to_upper(std::string& src)
    {
        std::transform(src.begin(), src.end(), src.begin(), [](unsigned char c) { return std::toupper(c); });
    }

    void to_lower(std::string& src)
    {
        std::transform(src.begin(), src.end(), src.begin(), [](unsigned char c) { return std::tolower(c); });
    }

    std::string formatStr(const char* format, ...)
    {
        char str[1024] = {};
        va_list ap;
        va_start(ap, format);
        vsnprintf(str, sizeof str, format, ap);
        va_end(ap);
        return str;
    }

    std::vector<std::string> split(const std::string& str, char sep)
    {
        std::vector<std::string> cont = {};
        std::string::const_iterator start = str.begin();
        std::string::const_iterator end = str.end();
        std::string::const_iterator next = find(start, end, sep);

        while (next != end)
        {
            cont.emplace_back(start, next);
            start = next + 1;
            next = std::find(start, end, sep);
        }

        cont.emplace_back(start, next);
        return cont;
    }

    bool start_with(const std::string& str, const std::string& prefix)
    {
        size_t prefix_len = prefix.size();

        if (str.size() < prefix_len)
            return false;

        for (size_t i = 0; i < prefix_len; i++)
        {
            if (str[i] != prefix[i])
                return false;
        }

        return true;
    }

    std::vector<std::string> split_filter_empty(const std::string& str, char sep)
    {
        std::vector<std::string> res;
        std::string::const_iterator start = str.begin();
        std::string::const_iterator end = str.end();
        std::string::const_iterator next = find(start, end, sep);

        while (next != end)
        {
            if (start < next)
                res.emplace_back(start, next);

            start = next + 1;
            next = find(start, end, sep);
        }

        if (start < next)
            res.emplace_back(start, next);

        return res;
    }

    std::string fmtTime(time_t now)
    {
        if (!now)
            now = std::time(nullptr);

        return "";
        // todo: fixit
        // return fmt::format("{:%Y-%m-%d %H:%M:%S}", *std::localtime(&now));
    }
}  // namespace cncpp