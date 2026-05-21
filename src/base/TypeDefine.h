#pragma once

#include <sstream>

namespace cncpp
{
    template <typename T>
    std::string toStr(const T& t)
    {
        return to_string(t);
    }

    template <typename T>
    T cast(const std::string& src)
    {
        T                 t;
        std::stringstream stream;
        stream << src;
        stream >> t;
        return t;
    }

}  // namespace cncpp
