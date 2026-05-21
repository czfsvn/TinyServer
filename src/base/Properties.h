#pragma once

#include <string.h>
#include <ext/hash_map>
#include <list>
#include <vector>
#include "StringUtil.h"

namespace cncpp
{
    class Properties
    {
    public:
        const std::string& getProperty(const std::string& key) const
        {
            const static std::string dummy = "";
            auto iter = property_map_.find(key);
            if (iter == property_map_.end())
                return dummy;

            return iter->second;
        }

        void setProperty(const std::string& key, const std::string& value)
        {
            property_map_[key] = value;
        }

        const std::string& operator[](const std::string& key) const
        {
            return getProperty(key);
        }

        const bool hasKey(const std::string& key) const
        {
            auto iter = property_map_.find(key);
            if (iter == property_map_.end())
                return false;

            return true;
        }

        void parseCmdLine(const std::string& line)
        {
            const std::list<std::string>& result = strSplit<std::list<std::string>>(line, " ");
            for (const auto& str : result)
            {
                const std::vector<std::string>& ret = str_split<std::vector>(str, "=");
                if (ret.size() == 2)
                {
                    setProperty(ret[0], ret[1]);
                }
            }
        }

        void parseCmdLine(const char* line)
        {
            parseCmdLine(std::string(line));
        }

    private:
        struct KeyHash : public std::unary_function<const std::string, size_t>
        {
            size_t operator()(const std::string& src) const
            {
                std::string str(src);
                __gnu_cxx::hash<const char*> HH;
                to_lower(str);
                return HH(str.c_str());
            }
        };

        struct KeyEqual : public std::binary_function<const std::string, const std::string, bool>
        {
            bool operator()(const std::string& s1, const std::string& s2) const
            {
                return strcasecmp(s1.c_str(), s2.c_str()) == 0;
            }
        };

        using PropHashMap = __gnu_cxx::hash_map<std::string, std::string, KeyHash, KeyEqual>;
        PropHashMap property_map_ = {};
    };
}  // namespace cncpp
