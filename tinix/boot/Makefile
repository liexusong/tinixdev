############################################################
#                  Makefile for Tinix                      #
############################################################

ENTRYPOINT      = 0x100000

CC              = gcc
LD              = ld
CFLAGS          = -I ./ -c -fleading-underscore -nostdinc -fno-builtin -fno-common \
				-W -Wall
LDFLAGS         = -x -s -Ttext $(ENTRYPOINT) -e start -M

PE_KERNEL       = oskernel.pe
ELF_KERNEL      = oskernel.elf
GZIP_KERNEL     = oskernel.elf.gz
OBJS            = boot.o loader.o console.o

$(GZIP_KERNEL) : $(ELF_KERNEL)
	gzip -f $^

$(ELF_KERNEL) : $(PE_KERNEL)
	objcopy -I pe-i386 -O elf32-i386 $^ $@

$(PE_KERNEL) : $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) > boot.map

$(OBJS) :boot.S loader.c console.c
	$(CC) $(CFLAGS) $^

clean:
	-rm $(OBJS) $(PE_KERNEL) $(GZIP_KERNEL) kernelz boot.map
