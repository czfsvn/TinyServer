#pragma once

// #include <spdlog/fmt/bundled/core.h>
// #include <spdlog/fmt/bundled/format.h>
// #include <spdlog/fmt/bundled/ranges.h>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <iostream>
#include <vector>

namespace cncpp
{

    std::string& replace_all(std::string& src, const std::string& old_str, const std::string& new_str);
    void to_upper(std::string& src);
    void to_lower(std::string& src);
    std::string formatStr(const char* format, ...);

    bool start_with(const std::string& str, const std::string& prefix);

    // this will filter any empty result, so the result vector has no empty string
    std::vector<std::string> split_filter_empty(const std::string& str, char sep);

    /*  example
    const std::list<std::string>& result = strSplit<std::list<std::string>>(line, " ");
    */
    template <typename Cont>
    inline Cont strSplit(const std::string& src, const std::string& seps)
    {
        Cont cont = {};
        std::string s;
        for (std::string::const_iterator i = src.begin(); i != src.end(); i++)
        {
            if (seps.find(*i) != std::string::npos)
            {
                if (s.length())
                    cont.emplace_back(s);
                s = "";
            }
            else
            {
                s += *i;
            }
        }

        if (!s.empty())
        {
            cont.emplace_back(s);
        }

        return cont;
    }

    /*
    // for only some sep characters
    example
    const std::list<std::string>&   res1 = str_split<std::list>(line, ",;-");
    const std::vector<std::string>& res2 = str_split<std::vector>(line, " ");
    const std::deque<std::string>&  res3 = str_split<std::deque>(line, " ");
    */
    template <template <typename T, typename = std::allocator<T>> class Cont>
    inline Cont<std::string> str_split(const std::string& src, const std::string& seps)
    {
        Cont<std::string> cont = {};
        std::string s;
        for (std::string::const_iterator i = src.begin(); i != src.end(); i++)
        {
            if (seps.find(*i) != std::string::npos)
            {
                if (s.length())
                    cont.emplace_back(s);
                s = "";
            }
            else
            {
                s += *i;
            }
        }

        if (!s.empty())
        {
            cont.emplace_back(s);
        }

        return cont;
    }

    std::vector<std::string> split(const std::string& str, char sep);

    // for only one sep character
    template <template <typename T, typename = std::allocator<T>> class Cont>
    inline Cont<std::string> split(const std::string& str, char sep)
    {
        Cont<std::string> cont = {};
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

    std::string fmtTime(const uint32_t now);

    template <typename... Args>
    std::string format(const char* fmt, Args&&... args)
    {
        try
        {
            return fmt::format(fmt, args...);
        } catch (const std::exception& e)
        {
            return "";
        }
    }

    template <typename... Args>
    void print(const char* fmt, Args&&... args)
    {
        try
        {
            fmt::print(fmt, args...);
        } catch (const std::exception& e)
        {
            std::cout << "e:" << e.what() << std::endl;
        }
    }

}  // namespace cncpp