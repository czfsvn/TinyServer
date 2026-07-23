#include <algorithm>
#include <boost/lexical_cast.hpp>
#include <cctype>
#include <cstdint>
#include <functional>
#include <string>

namespace cncpp
{
    class DeferFunctor
    {
    private:
        std::function<void()> func_;

    public:
        DeferFunctor(const std::function<void()>& func) : func_(func)
        {
        }

        ~DeferFunctor()
        {
            func_();
        }
    };

    /*
        examples:
        // 数值转字符串
        std::string str = to_string(123);           // "123"
        std::string pi_str = to_string(3.14);       // "3.14"

        // 字符串转数值
        int num = to_int("456");                    // 456
        double pi = to_double("3.14159");          // 3.14159
        bool flag = to_bool("true");                // true
        uint32_t u32 = to_uint32("100");           // 100

        // 带默认值的转换（容错）
        int val = to_int("not_a_number", -1);       // -1（转换失败，返回默认值）
        bool active = to_bool("invalid", true);     // true（转换失败，返回默认值）

        // 基础转换（可能抛异常）
        int num = cast_to<int>("123");           // 123
        double pi = cast_to<double>("3.14");     // 3.14
        std::string str = cast_to<std::string>(456); // "456"

        // 带默认值的转换（安全，不抛异常）
        int val = cast_to<int>("not_a_number", -1);   // -1
        bool flag = cast_to<bool>("invalid", true);   // true

        // bool 转换支持多种格式
        bool b1 = cast_to<bool>("true");              // true
        bool b2 = cast_to<bool>("1");                 // true
        bool b3 = cast_to<bool>("yes");               // true
        bool b4 = cast_to<bool>("on");                // true
        bool b5 = cast_to<bool>("false");             // false
        bool b6 = cast_to<bool>("0");                 // false

        // 数值转字符串
        std::string num_str = cast_to<std::string>(123);   // "123"
        std::string pi_str = cast_to<std::string>(3.14);   // "3.14"
    */

    /**
     * @brief 通用类型转换（类似 boost::lexical_cast，但提供更好的错误处理）
     * @tparam To 目标类型
     * @tparam From 源类型
     * @param value 要转换的值
     * @return 转换后的值
     * @throw boost::bad_lexical_cast 如果转换失败
     */
    template <typename To, typename From>
    inline To cast_to(const From& value)
    {
        return boost::lexical_cast<To>(value);
    }

    /**
 * @brief 通用类型转换（带默认值，转换失败时返回默认值）
 * @tparam To 目标类型
 * @tparam From 源类型
 * @param value 要转换的值
 * @param default_value 转换失败时的默认值
 * @return 转换后的值，如果转换失败则返回默认值
 */
    template <typename To, typename From>
    inline To cast_to(const From& value, const To& default_value)
    {
        try
        {
            return boost::lexical_cast<To>(value);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转 bool（特化版本，支持多种格式）
     * 支持的格式：true, 1, yes, on → true
     *             false, 0, no, off → false
     * @param s 输入字符串
     * @return 转换后的 bool 值
     */
    template <>
    inline bool cast_to<bool>(const std::string& s)
    {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), ::tolower);

        if (lower_s == "true" || lower_s == "1" || lower_s == "yes" || lower_s == "on")
        {
            return true;
        }
        else if (lower_s == "false" || lower_s == "0" || lower_s == "no" || lower_s == "off")
        {
            return false;
        }
        throw boost::bad_lexical_cast(typeid(std::string), typeid(bool));
    }

    /**
     * @brief 字符串转 bool（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 bool 值
     */
    template <>
    inline bool cast_to<bool>(const std::string& s, const bool& default_value)
    {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), ::tolower);

        if (lower_s == "true" || lower_s == "1" || lower_s == "yes" || lower_s == "on")
        {
            return true;
        }
        else if (lower_s == "false" || lower_s == "0" || lower_s == "no" || lower_s == "off")
        {
            return false;
        }
        return default_value;
    }

    /**
     * @brief 将任意类型转换为字符串
     * @tparam T 输入类型
     * @param value 要转换的值
     * @return 转换后的字符串
     */
    template <typename T>
    inline std::string to_string(const T& value)
    {
        return boost::lexical_cast<std::string>(value);
    }

    /**
     * @brief 字符串转换为 int（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 int 值
     */
    inline int to_int(const std::string& s, int default_value = 0)
    {
        try
        {
            return boost::lexical_cast<int>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 long（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 long 值
     */
    inline long to_long(const std::string& s, long default_value = 0)
    {
        try
        {
            return boost::lexical_cast<long>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 float（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 float 值
     */
    inline float to_float(const std::string& s, float default_value = 0.0f)
    {
        try
        {
            return boost::lexical_cast<float>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 double（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 double 值
     */
    inline double to_double(const std::string& s, double default_value = 0.0)
    {
        try
        {
            return boost::lexical_cast<double>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 bool（支持多种格式）
     * 支持的格式：true, 1, yes, on → true
     *             false, 0, no, off → false
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 bool 值
     */
    inline bool to_bool(const std::string& s, bool default_value = false)
    {
        std::string lower_s = s;
        std::transform(lower_s.begin(), lower_s.end(), lower_s.begin(), ::tolower);

        if (lower_s == "true" || lower_s == "1" || lower_s == "yes" || lower_s == "on")
        {
            return true;
        }
        else if (lower_s == "false" || lower_s == "0" || lower_s == "no" || lower_s == "off")
        {
            return false;
        }
        return default_value;
    }

    /**
     * @brief 字符串转换为 uint32_t（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 uint32_t 值
     */
    inline uint32_t to_uint32(const std::string& s, uint32_t default_value = 0)
    {
        try
        {
            return boost::lexical_cast<uint32_t>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 uint64_t（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 uint64_t 值
     */
    inline uint64_t to_uint64(const std::string& s, uint64_t default_value = 0)
    {
        try
        {
            return boost::lexical_cast<uint64_t>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }

    /**
     * @brief 字符串转换为 int64_t（带默认值）
     * @param s 输入字符串
     * @param default_value 转换失败时的默认值
     * @return 转换后的 int64_t 值
     */
    inline int64_t to_int64(const std::string& s, int64_t default_value = 0)
    {
        try
        {
            return boost::lexical_cast<int64_t>(s);
        }
        catch (const boost::bad_lexical_cast&)
        {
            return default_value;
        }
    }
}  // namespace cncpp