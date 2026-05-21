#include "protobuf_util.h"
#include "logger.h"

namespace cncpp
{
    std::string ProtobufUtil::Serialize(const google::protobuf::Message& message)
    {
        std::string data;
        if (!message.SerializeToString(&data))
        {
            LOG_ERROR("Failed to serialize protobuf message");
            return "";
        }
        return data;
    }

    bool ProtobufUtil::Deserialize(const std::string& data, google::protobuf::Message& message)
    {
        if (!message.ParseFromString(data))
        {
            LOG_ERROR("Failed to deserialize protobuf message");
            return false;
        }
        return true;
    }

}  // namespace cncpp