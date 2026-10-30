CC      := gcc
AS      := nasm
LD      := ld
QEMU    := qemu-system-i386

CFLAGS  := -m32 -ffreestanding -fno-pie -fno-stack-protector -fcf-protection=none \
           -fno-asynchronous-unwind-tables -O2 -g -Wall -Wextra
ASFLAGS := -f elf32 -g
LDFLAGS := -m elf_i386 -T linker.ld --build-id=none -z noexecstack

BUILD   := build
OBJS    := $(BUILD)/boot.o $(BUILD)/main.o
IMAGE   := $(BUILD)/os.img

# Must match package/run.bat so every laptop gets the same virtual PC.
QEMU_FLAGS := -machine pc -cpu qemu32 -m 128M \
              -drive file=$(IMAGE),format=raw,if=ide -snapshot -no-reboot

.PHONY: all run test dist clean

all: $(IMAGE)

$(BUILD)/boot.o: boot/boot.asm
	mkdir -p $(BUILD)
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD)/main.o: kernel/main.c
	mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

$(IMAGE): $(BUILD)/kernel.elf boot/grub.cfg
	grub-file --is-x86-multiboot $<
	mkdir -p $(BUILD)/image/boot/grub
	cp $< $(BUILD)/image/boot/kernel.elf
	cp boot/grub.cfg $(BUILD)/image/boot/grub/grub.cfg
	grub-mkrescue -o $@ $(BUILD)/image

run: $(IMAGE)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

# Boots without a window and passes if the kernel reaches BOOT-OK.
test: $(IMAGE)
	rm -f $(BUILD)/boot.log
	-timeout 10 $(QEMU) $(QEMU_FLAGS) -display none -serial file:$(BUILD)/boot.log
	cat $(BUILD)/boot.log
	grep -q BOOT-OK $(BUILD)/boot.log
	@echo PASS

dist: $(IMAGE)
	rm -rf $(BUILD)/dist $(BUILD)/os-demo.zip
	mkdir -p $(BUILD)/dist
	cp $(IMAGE) package/README.txt $(BUILD)/dist/
	sed 's/\r$$//; s/$$/\r/' package/run.bat > $(BUILD)/dist/run.bat
	cd $(BUILD)/dist && python3 -m zipfile -c ../os-demo.zip os.img run.bat README.txt

clean:
	rm -rf $(BUILD)
