#include "data_server.h"

int main(int argc, char* argv[])
{
    return sDataServer.run(argc, argv) ? 0 : 1;
}
