rm oskernel
rm build.log
rm system.map
rm system.idc
rm loader.map
cd kernel
make clean
cd ../boot
make clean