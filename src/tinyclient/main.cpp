#include "client_service.h"

int main(int argc, char* argv[])
{
    return sTinyClientService.run(argc, argv) ? 0 : 1;
}
