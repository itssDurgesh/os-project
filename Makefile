CC      := gcc
AS      := nasm
LD      := ld
QEMU    := qemu-system-i386

CFLAGS  := -m32 -std=gnu11 -ffreestanding -fno-pie -fno-stack-protector \
           -fcf-protection=none -fno-asynchronous-unwind-tables \
           -fno-omit-frame-pointer -mno-sse -mno-mmx -mno-80387 \
           -O2 -g -Wall -Wextra -Iinclude -MMD -MP
ASFLAGS := -f elf32 -g
LDFLAGS := -m elf_i386 -T linker.ld --build-id=none -z noexecstack

BUILD   := build
IMAGE   := $(BUILD)/os.img
KERNEL  := $(BUILD)/kernel.elf

# One folder per subsystem owner, so each teammate can find their part.
C_SRCS   := $(wildcard hal/*.c kernel/*.c memory/*.c process/*.c resource/*.c \
                       storage/*.c fs/*.c)
ASM_SRCS := $(wildcard boot/*.asm hal/*.asm process/*.asm)
OBJS     := $(C_SRCS:%.c=$(BUILD)/%.o) $(ASM_SRCS:%.asm=$(BUILD)/%.asm.o)

# Must match package/run.bat so every laptop gets the same virtual PC.
QEMU_FLAGS := -machine pc -cpu qemu32 -m 128M \
              -drive file=$(IMAGE),format=raw,if=ide -snapshot -no-reboot

.PRECIOUS: $(BUILD)/user/bin/%.o

.PHONY: all run test dist clean

all: $(IMAGE)

$(BUILD)/%.o: %.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.asm.o: %.asm
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# User programs: each file in user/bin becomes one program, linked with crt0 and
# the small C library.
USER_CFLAGS := $(filter-out -Iinclude,$(CFLAGS)) -Iuser -Iinclude
USER_LIB    := $(BUILD)/user/crt0.asm.o $(BUILD)/user/libc.o
USER_SRCS   := $(wildcard user/bin/*.c)
USER_BINS   := $(USER_SRCS:user/bin/%.c=$(BUILD)/user/bin/%)

$(BUILD)/user/crt0.asm.o: user/crt0.asm
	mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/user/%.o: user/%.c
	mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD)/user/bin/%: $(BUILD)/user/bin/%.o $(USER_LIB) user/user.ld
	$(LD) -m elf_i386 -T user/user.ld --build-id=none -z noexecstack \
	    $(USER_LIB) $< -o $@

ROOTFS  := $(BUILD)/rootfs

# The files that appear in / when the OS runs.
$(ROOTFS).stamp: $(shell find rootfs -type f 2>/dev/null) tools/mkbigfile.py $(USER_BINS)
	rm -rf $(ROOTFS)
	mkdir -p $(ROOTFS)/bin $(ROOTFS)/sbin $(ROOTFS)/dev $(ROOTFS)/home
	cp -r rootfs/. $(ROOTFS)/
	cp $(USER_BINS) $(ROOTFS)/bin/
	mv $(ROOTFS)/bin/init $(ROOTFS)/sbin/init
	python3 tools/mkbigfile.py $(ROOTFS)/etc/big.bin
	touch $@

$(IMAGE): $(KERNEL) boot/grub.cfg tools/mkimage.sh $(ROOTFS).stamp
	grub-file --is-x86-multiboot $<
	bash tools/mkimage.sh $(KERNEL) boot/grub.cfg $(ROOTFS) $@

run: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

# Boots the kernel once per test without a window.
test: $(IMAGE)
	bash tests/run.sh

dist: $(IMAGE)
	rm -rf $(BUILD)/dist $(BUILD)/os-demo.zip
	mkdir -p $(BUILD)/dist
	cp $(IMAGE) package/README.txt $(BUILD)/dist/
	sed 's/\r$$//; s/$$/\r/' package/run.bat > $(BUILD)/dist/run.bat
	cd $(BUILD)/dist && python3 -m zipfile -c ../os-demo.zip os.img run.bat README.txt

clean:
	rm -rf $(BUILD)

-include $(OBJS:.o=.d) $(USER_BINS:%=%.d) $(BUILD)/user/libc.d
