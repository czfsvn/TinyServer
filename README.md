# TinyServer
using boost-asio 1.8.5 in docker, c++17

gdb bin/gateway_server -c ../src/config/config.ini
gdb bin/client -c ../src/config/config.ini
gdb bin/tinyserver -c ../src/config/config.ini
gdb bin/dataserver -c ../src/config/config.ini

