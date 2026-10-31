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
            if [ "$command" = "CTRL-C" ]; then
                printf '\003'
            elif [ "$command" = "CTRL-S" ]; then
                printf '\023'
            elif [ "$command" = "CTRL-Q" ]; then
                printf '\021'
            elif [ "$command" = "UP" ]; then
                printf '\033[A'
            elif [ "$command" = "LEFT" ]; then
                printf '\033[D'
            elif [ "$command" = "HOME" ]; then
                printf '\033[H'
            elif [ "$command" = "DELETE" ]; then
                printf '\033[3~'
            elif [ "$command" = "BACKSPACE" ]; then
                printf '\177'
            elif [ "$command" = "ENTER" ]; then
                printf '\n'
            elif [ "${command#TYPE:}" != "$command" ]; then
                printf '%s' "${command#TYPE:}"
            elif [ "$command" = "DOWN" ]; then
                printf '\033[B'
            elif [ "$command" = "RIGHT" ]; then
                printf '\033[C'
            elif [ "$command" = "NO-ENTER-X" ]; then
                printf 'X'
            else
                printf '%s\n' "$command"
            fi
            sleep "${SHELL_DELAY:-0.4}"
        done < "$script"
        sleep "${SHELL_WAIT:-1}"
        kill "$(cat build/qemu.pid)"    # QEMU never stops by itself
    } | timeout 120 qemu-system-i386 "${QEMU_ARGS[@]}" $snapshot \
        -pidfile build/qemu.pid \
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
    SHELL_DELAY=4 shell_test filesystem tests/filesystem-commands.txt \
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
        for key in r o o t ret r o o t ret; do
            echo "sendkey $key"
            sleep 0.1
        done
        sleep 1
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

# Wrong password, a normal user being refused, then logging out and back in.
login_test() {
    local log=build/test-login.log

    SHELL_DELAY=0.6 shell_test login tests/login-commands.txt "login-done"
    report "login-wrong-password" "$log" "Login incorrect"
    report "login-as-student" "$log" "^student$"
    report "login-refused" "$log" "/etc/shadow: permission denied"
    report "login-root-again" "$log" "^root$"
    if grep -q "root:" "$log"; then
        echo "FAIL  password file leaked to a normal user"
        failed=$((failed + 1))
    else
        echo "PASS  shadow file stayed hidden"
        passed=$((passed + 1))
    fi
}

# Moves the mouse through QEMU, which sends real PS/2 packets to IRQ 12.
mouse_test() {
    local log=build/test-mouse.log

    rm -f "$log"
    {
        sleep 3
        for key in r o o t ret r o o t ret; do
            echo "sendkey $key"
            sleep 0.1
        done
        sleep 1
        for key in m o u s e t e s t ret; do
            echo "sendkey $key"
            sleep 0.1
        done
        sleep 1
        echo "mouse_move 10 5"
        sleep 0.5
        echo "mouse_move -7 3"
        sleep 1
        echo quit
    } | timeout 30 qemu-system-i386 "${QEMU_ARGS[@]}" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" -serial "file:$log" \
        -monitor stdio > /dev/null 2>&1
    # QEMU's y axis points the opposite way to PS/2, so 5 arrives as -5.
    report "mouse-first-move" "$log" "mouse moved x=10 y=-5 buttons=0"
    report "mouse-second-move" "$log" "mouse moved x=-7 y=-3 buttons=0"
}

# The editor, the new commands, history and background programs.
tools_test() {
    local log=build/test-tools.log

    SHELL_DELAY=0.7 shell_test tools tests/tools-commands.txt "tools-done"
    report "serial-backspace-and-left" "$log" "^abXc$"
    report "serial-home-and-delete" "$log" "^delete-ok$"
    report "edit-saves-file" "$log" "^second line$"
    report "edit-arrow-key" "$log" "^secoXnd"
    report "cp-and-mv" "$log" "^Hello from the OS Project disk"
    report "grep" "$log" "^nice line$"
    report "background-started" "$log" "started. process"
    report "background-finished" "$log" "finished. process"
    if [ "$(grep -c '^history-check$' "$log")" -eq 2 ]; then
        echo "PASS  up arrow repeats a command"
        passed=$((passed + 1))
    else
        echo "FAIL  up arrow repeats a command"
        failed=$((failed + 1))
    fi
}

# Compiles C++ inside the OS and runs the programs it made.
compiler_test() {
    local log=build/test-compiler.log

    SHELL_DELAY=2 shell_test compiler tests/compiler-commands.txt "compiler-done"
    report "compiled-hello" "$log" "^Hello from my own compiler!"
    report "compiled-recursion" "$log" "^0 1 1 2 3 5 8 13 21 34 $"
    report "compiled-arrays" "$log" "^Sorted: 3 5 7 19 42 88$"
    report "compiled-cin" "$log" "Answer: 42$"
    report "compiled-strings" "$log" "Hello, Asha!$"
    report "compiled-string-length" "$log" "^Your name has 4 letters$"
    report "compiled-name-with-r" "$log" "Hello, Durgesh!$"
    report "compiled-feature-test" "$log" "^TEST-PASS compiler$"
    report "compiled-printing" "$log" "^print check: -42 c text 0$"
    report "compiler-error-message" "$log" "bad.cpp:1: error: expected a value here"
    report "compiler-unknown-name" "$log" "bad2.cpp:1: error: this variable doesn't exist"
    report "compiled-a-out" "$log" "^from a.out$"
    report "compiled-as-student" "$log" "^student compiled this$"
}

# Presses each key through QEMU's real keyboard.
press_keys() {
    for key in "$@"; do
        echo "sendkey $key"
        sleep 0.08
    done
}

# Backspace, the arrow keys, Home and Delete at the prompt, using real keys.
line_editing_test() {
    local log=build/test-line-editing.log

    rm -f "$log"
    {
        sleep 3
        press_keys r o o t ret r o o t ret
        sleep 1
        press_keys e c h o spc a b c d backspace left x ret
        sleep 0.5
        press_keys z e c h o spc r e a l k e y s home delete ret
        sleep 0.5
        press_keys e c h o spc w r o n g home right right right right right
        press_keys end backspace backspace backspace backspace backspace h i ret
        sleep 0.5
        press_keys up ret
        sleep 1
        echo quit
    } | timeout 30 qemu-system-i386 "${QEMU_ARGS[@]}" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" -serial "file:$log" \
        -monitor stdio > /dev/null 2>&1
    report "keys-backspace-then-left" "$log" "^abxc"
    report "keys-home-and-delete" "$log" "^realkeys"
    report "keys-end-and-backspace" "$log" "^hi"
}

# Everything a person would do: write a program in the editor, save it, compile
# it and run it, using only real key presses.
editor_to_program_test() {
    local log=build/test-editor.log

    rm -f "$log" build/editor.png
    {
        sleep 3
        press_keys r o o t ret r o o t ret
        sleep 1
        press_keys e d i t spc slash t m p slash p dot c p p ret
        sleep 1
        press_keys i n t spc m a i n shift-9 shift-0 spc shift-bracket_left ret
        press_keys spc spc spc spc c o u t spc shift-comma shift-comma spc 4 2
        press_keys spc shift-comma shift-comma spc e n d l semicolon ret
        press_keys shift-bracket_right
        press_keys up right right right right right right right right right right right right right backspace 7
        press_keys ctrl-s
        sleep 1
        echo "screendump build/editor.png -f png"
        sleep 1
        press_keys ctrl-q
        sleep 1
        press_keys r u n spc slash t m p slash p dot c p p ret
        sleep 3
        echo quit
    } | timeout 40 qemu-system-i386 "${QEMU_ARGS[@]}" -snapshot \
        -drive "file=$IMAGE,format=raw,if=ide" -serial "file:$log" \
        -monitor stdio > /dev/null 2>&1
    report "edit-compile-run-with-real-keys" "$log" "^47"
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
line_editing_test
editor_to_program_test
mouse_test
report "disk-uses-dma" build/test-grub.log "Disk: using DMA"
shell_test shell tests/shell-commands.txt "shell-commands-done"
SHELL_WAIT=3 shell_test forktest tests/forktest-commands.txt "TEST-PASS forktest"
login_test
tools_test
compiler_test
shell_test tmptest tests/tmptest-commands.txt "TEST-PASS tmptest"
SHELL_WAIT=70 shell_test swaptest tests/swaptest-commands.txt "TEST-PASS swaptest"
SHELL_WAIT=3 shell_test sigtest tests/sigtest-commands.txt "TEST-PASS sigtest"
shell_test ctrl-c tests/ctrl-c-commands.txt "^after-ctrl-c"
SHELL_WAIT=8 shell_test ipctest tests/ipctest-commands.txt "TEST-PASS ipctest"
SHELL_WAIT=3 shell_test banktest tests/banktest-commands.txt "TEST-PASS banktest"
SHELL_WAIT=6 shell_test quotatest tests/quotatest-commands.txt "TEST-PASS quotatest"
shell_test permtest tests/permtest-commands.txt "TEST-PASS permtest"
filesystem_test

echo
echo "$passed passed, $failed failed"
[ "$failed" -eq 0 ]
