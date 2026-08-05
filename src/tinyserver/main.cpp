#include "tinyserver.h"

int main(int argc, char* argv[])
{
    return sTinyServer.run(argc, argv) ? 0 : 1;
}
