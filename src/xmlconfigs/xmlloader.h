#pragma once

namespace xmlconfigs
{
    /**
     * @brief 加载XML配置
     * @param config_path 配置文件路径
     * @return 是否成功
     */
    bool tiny_server_loadConfig();

    bool gateway_loadConfig();

}  // namespace xmlconfigs
