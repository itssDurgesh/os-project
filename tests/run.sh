#!/bin/bash
# Boots the kernel once per test without a window and checks its serial output.

cd "$(dirname "$0")/.." || exit 1

KERNEL=build/kernel.elf
IMAGE=build/os.img
QEMU_ARGS=(-machine pc -cpu qemu32 -m 128M -display none -no-reboot
           -device isa-debug-exit,iobase=0xf4,iosize=0x04)

passed=0
failed=0

report() {
    local name=$1 log=$2 expected=$3

    if grep -q "$expected" "$log" && ! grep -q "TEST-FAIL" "$log"; then
        echo "PASS  $name"
        passed=$((passed + 1))
    else
        echo "FAIL  $name (expected: $expected)"
        sed 's/^/      /' "$log"
        failed=$((failed + 1))
    fi
}

# Runs one kernel self test. $2 is the text that proves it worked.
kernel_test() {
    local name=$1 expected=$2
    local log=build/test-$name.log

    rm -f "$log"
    timeout 60 qemu-system-i386 "${QEMU_ARGS[@]}" -kernel "$KERNEL" \
        -append "test=$name" -serial "file:$log" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" 2>/dev/null
    report "$name" "$log" "$expected"
}

# Boots the real image through GRUB, the way a teammate would.
image_test() {
    local log=build/test-grub.log

    rm -f "$log"
    timeout 15 qemu-system-i386 "${QEMU_ARGS[@]}" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" -serial "file:$log" 2>/dev/null
    report "grub-boot" "$log" "BOOT-OK"
}

# Types commands into the serial port, the way a person would at the keyboard.
shell_test() {
    local name=$1 script=$2 expected=$3
    local image=${4:-$IMAGE}
    local snapshot=${5--snapshot}   # no colon: an empty $5 means save to disk
    local log=build/test-$name.log

    {
        sleep 3
        while IFS= read -r command; do
            printf '%s\n' "$command"
            sleep "${SHELL_DELAY:-0.4}"
        done < "$script"
        sleep 1
    } | timeout 60 qemu-system-i386 "${QEMU_ARGS[@]}" $snapshot \
        -drive "file=$image,format=raw,if=ide" -serial stdio 2>/dev/null \
        | tr -d '\r' > "$log"
    report "$name" "$log" "$expected"
}

# Writes to a copy of the disk, then lets the real Linux e2fsck judge the result.
filesystem_test() {
    local copy=build/fs-copy.img
    local data=build/fs-data.ext2
    local start=$((2048 + 16 * 2048 + 32 * 2048))

    cp "$IMAGE" "$copy"
    SHELL_DELAY=2 shell_test filesystem tests/filesystem-commands.txt \
        "TEST-PASS fstest" "$copy" ""

    dd if="$copy" of="$data" bs=512 skip=$start count=$((64 * 2048)) status=none
    if /usr/sbin/e2fsck -fn "$data" > build/test-e2fsck.log 2>&1; then
        echo "PASS  e2fsck (Linux agrees our filesystem is clean)"
        passed=$((passed + 1))
    else
        echo "FAIL  e2fsck"
        sed 's/^/      /' build/test-e2fsck.log
        failed=$((failed + 1))
    fi

    # A second boot must still see the file written by the first one.
    shell_test persistence tests/persistence-commands.txt "saved to disk" "$copy" ""
    rm -f "$copy" "$data"
}

# Presses real keys through QEMU's PS/2 keyboard, so this tests IRQ 1 and the
# scancode table, not the serial port.
keyboard_test() {
    local log=build/test-keyboard.log

    rm -f "$log"
    {
        sleep 3
        for key in e c h o spc shift-h i spc shift-1 ret; do
            echo "sendkey $key"
            sleep 0.1
        done
        sleep 1
        echo "screendump build/screen.png -f png"
        sleep 1
        echo quit
    } | timeout 30 qemu-system-i386 "${QEMU_ARGS[@]}" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" -serial "file:$log" \
        -monitor stdio > /dev/null 2>&1
    report "keyboard" "$log" "^Hi !"
}

kernel_test ticks "tick 50"
kernel_test panic "unhandled CPU exception"
kernel_test memory "TEST-PASS"
kernel_test pagefault "page fault in kernel"
kernel_test sched "TEST-PASS"
kernel_test aging "TEST-PASS"
kernel_test sync "TEST-PASS"
kernel_test disk "TEST-PASS"
image_test
keyboard_test
shell_test shell tests/shell-commands.txt "shell-commands-done"
shell_test forktest tests/forktest-commands.txt "TEST-PASS forktest"
filesystem_test

echo
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
