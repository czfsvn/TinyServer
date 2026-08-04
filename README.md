# TinyServer
using boost-asio 1.8.5 in docker, c++17

gdb bin/gateway_server -c ../gameconfigs/config.ini

gdb bin/client -c ../gameconfigs/config.ini

gdb bin/tinyserver -c ../gameconfigs/config.ini

gdb bin/dataserver -c ../gameconfigs/config.ini
