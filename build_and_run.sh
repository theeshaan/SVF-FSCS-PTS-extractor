cd build
make -j2
cd ..
./build/svf-example src/input.bc -extapi=../SVF/Debug-build/lib/extapi.bc -stat=false > out.txt