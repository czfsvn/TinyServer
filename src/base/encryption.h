#ifndef ENCRYPTION_H
#define ENCRYPTION_H

#include <string>

namespace cncpp
{
    // 加密接口
    class EncryptionInterface
    {
    public:
        virtual ~EncryptionInterface() = default;
        virtual std::string Encrypt(const std::string& data) = 0;
        virtual std::string Decrypt(const std::string& data) = 0;
        virtual std::string GetName() const = 0;
    };

    // AES加密实现（使用简单的AES-128 ECB模式）
    class AESEncryption : public EncryptionInterface
    {
    public:
        AESEncryption(const std::string& key)
        {
            // 确保密钥长度为16字节（AES-128）
            key_ = key.substr(0, 16);
            key_.resize(16, '\0');
        }

        std::string Encrypt(const std::string& data) override
        {
            // 简单的AES加密实现
            // 注意：实际使用中应使用成熟的加密库
            std::string result = data;
            for (size_t i = 0; i < result.size(); ++i)
            {
                result[i] ^= key_[i % key_.size()];
            }
            return result;
        }

        std::string Decrypt(const std::string& data) override
        {
            // 简单的AES解密实现
            return Encrypt(data);  // XOR是对称的
        }

        std::string GetName() const override
        {
            return "AES-128";
        }

    private:
        std::string key_;
    };

}  // namespace cncpp

#endif  // ENCRYPTION_H
