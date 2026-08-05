#include "gateway_server.h"

int main(int argc, char* argv[])
{
    return sGatewayServer.run(argc, argv) ? 0 : 1;
}
