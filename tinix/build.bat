cd kernel
make 2>build.log
cd ../
cp kernel/kernelz boot/
cp kernel/system.map ./
mv kernel/build.log ./
cd boot
make
cd ../
cp boot/oskernel.elf.gz ./oskernel
cp boot/boot.map ./loader.map
perl map2idc.pl > system.idc
pause