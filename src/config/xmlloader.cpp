#include "xmlloader.h"
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
        campofficialpromote.loadXml("");
        return true;
    }

}  // namespace xmlconfigs
