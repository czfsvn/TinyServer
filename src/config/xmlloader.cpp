#include "xmlloader.h"
#include "config.h"
#include "xmlfiles/CampofficialPromote.h"

xml::CampofficialPromote campofficialpromote;
namespace xmlconfigs
{
    bool tiny_server_loadConfig()
    {
        return true;
    }

    bool gateway_loadConfig()
    {
        campofficialpromote.loadXml("test_official.xml");
        return true;
    }
}  // namespace xmlconfigs
