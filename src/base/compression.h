#ifndef COMPRESSION_H
#define COMPRESSION_H

#include <string>

namespace cncpp
{
    // 压缩接口
    class CompressionInterface
    {
    public:
        virtual ~CompressionInterface() = default;
        virtual std::string Compress(const std::string& data) = 0;
        virtual std::string Decompress(const std::string& data) = 0;
        virtual std::string GetName() const = 0;
    };

    // zlib压缩实现
    class ZlibCompression : public CompressionInterface
    {
    public:
        ZlibCompression(int level = 6) : compression_level_(level)
        {
        }

        std::string Compress(const std::string& data) override
        {
            // 简单的压缩实现
            // 注意：实际使用中应使用zlib库
            std::string result;
            char current = 0;
            int count = 0;

            for (char c : data)
            {
                if (c == current && count < 255)
                {
                    count++;
                }
                else
                {
                    if (count > 0)
                    {
                        result += current;
                        result += static_cast<char>(count);
                    }
                    current = c;
                    count = 1;
                }
            }

            if (count > 0)
            {
                result += current;
                result += static_cast<char>(count);
            }

            return result;
        }

        std::string Decompress(const std::string& data) override
        {
            // 简单的解压实现
            std::string result;
            for (size_t i = 0; i < data.size(); i += 2)
            {
                if (i + 1 < data.size())
                {
                    char c = data[i];
                    int count = static_cast<unsigned char>(data[i + 1]);
                    result.append(count, c);
                }
            }
            return result;
        }

        std::string GetName() const override
        {
            return "Zlib";
        }

    private:
        int compression_level_;
    };

}  // namespace cncpp

#endif  // COMPRESSION_H
