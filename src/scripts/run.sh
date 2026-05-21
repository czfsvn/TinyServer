
if [ ! -d build ]; then
    rm -rf build 
fi

mkdir build
cd build
cmake ../src/
make
cd ..
./network_example -m1 -p 1 -p 8080 -h 127.0.0.1
./network_example -m client -p 8080 -h 127.0.0.1
