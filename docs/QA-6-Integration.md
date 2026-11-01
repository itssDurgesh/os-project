# Viva Q&A: Integration

**Role:** Member 6, Integration
**You own:** boot, GDT/TSS, HAL, panic, syscall table, init order, libc, shell, test programs
**Your code:** `boot/`, `hal/`, `kernel/`, `user/`, `tests/`, `tools/`, `include/abi.h`, `include/hal.h`, `include/syscall.h`, `Makefile`, `linker.ld`
**Your buddy:** Memory Management explains your part. You explain Resource Management.

## Your 30-second summary

> Five subsystems must start in the right order and talk to each other, and programs need one safe way into the kernel. GRUB loads our kernel, and `kmain` starts everything in 10 fixed steps. Programs enter the kernel only through `int 0x80`, and a table of 49 handlers sends each call to the right subsystem. The HAL hides the hardware. If something fatal happens, `panic` prints a report and stops. On top of the kernel are a small C library, a shell, a text editor and a small C++ compiler.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| GRUB and Multiboot | A tested bootloader gives us the memory map and 32-bit mode | We would write and debug our own bootloader |
| Higher-half kernel | The kernel sits at `0xC0000000`, leaving the low 3 GB for programs | Kernel and program addresses could clash |
| GDT | x86 requires segments for ring 0 and ring 3 | The CPU couldn't separate kernel and user |
| TSS | Tells the CPU which kernel stack to use from ring 3 | An interrupt from user mode would have no safe stack |
| System call table | One safe, numbered entry into the kernel | Programs would need direct kernel access |
| `int 0x80` | A simple, well-known way to change privilege | Programs could never reach the kernel |
| HAL | Hardware details stay in one place | Port numbers would spread through the whole kernel |
| Fixed boot order | Each step needs what came before it | Features would run before their dependencies exist |
| Panic | Stops the system before it can corrupt data | A broken kernel could damage the disk |
| `abi.h` | One file both kernel and programs agree on | Syscall numbers could get out of step |
| libc wrappers | Programs call `read()`, not assembly | Each program would need its own `int 0x80` code |
| QEMU | Same virtual PC on every laptop, and safe | A real boot could fail on modern hardware or harm Windows |
| Automated tests | `make test` proves each feature on every change | Fixes could silently break something else |
| Serial logging | `boot.log` survives a crash | A failed boot would give no clue |

## Numbers to remember

| Item | Value |
| --- | --- |
| Multiboot header magic / GRUB's answer | `0x1BADB002` / `0x2BADB002` |
| Kernel address | `0xC0000000` + 1 MB |
| Boot stack | 16 KB |
| GDT selectors | `0x08` kernel code, `0x10` kernel data, `0x1B` user code, `0x23` user data, `0x28` TSS |
| Syscall vector | `0x80` |
| System calls | 49 (numbers 1 to 49) |
| Boot steps | 10 |
| Commands in `/bin` | 43, plus `init` in `/sbin` |
| Automatic checks | 110 (all pass on `9e9ae97`) |
| Code size | about 17,000 lines |
| QEMU settings | `-machine pc -cpu qemu32 -m 128M` |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Boot | Start the OS | `Disk: using DMA`, `127 MB RAM, 125 MB free.`, `BOOT-OK`, the login prompt |
| Login | `root` / `root`, then `whoami` | `root` |
| Wrong password | Any wrong password | `Login incorrect` |
| Help | `help` | The list of commands |
| The shell | `ls /bin \| wc` | `43 43 244` |
| Invalid system call | See the program below | `-38`, which is `-ENOSYS` |
| Crash is contained | Divide by zero in a program | `CPU exception at ..., process ended`, and the shell carries on |
| Compiler | `run /examples/fibonacci.cpp` | `0 1 1 2 3 5 8 13 21 34` |
| Editor | `edit /tmp/hello.cpp` | Full-screen editor. Ctrl+S saves and Ctrl+Q quits |

**Invalid system call demo:**

```cpp
int main() {
    cout << __syscall(999, 0, 0, 0) << endl;
    return 0;
}
```

Save it as `/tmp/k.cpp`, then type `run /tmp/k.cpp`.

**Panic demo:** a panic is not available from the shell, on purpose. The `panic` boot test in `make test` divides by zero inside the kernel and prints `KERNEL PANIC: unhandled CPU exception`, then the saved registers and `System halted.`

---

## A. Basic concepts

**1. What is a kernel?**
The core of the OS. It runs with full rights and manages hardware, memory and processes.

**2. What is a system call?**
The way a program asks the kernel to do something it isn't allowed to do itself.

**3. User mode or kernel mode?**
User mode (ring 3) is limited. Kernel mode (ring 0) can use all instructions and memory.

**4. Why can't a program call kernel functions directly?**
The CPU blocks ring 3 from kernel memory and privileged instructions. That protects the system.

**5. What is a bootloader?**
A small program that loads the OS into memory and starts it.

**6. What is BIOS?**
Firmware that starts the PC and loads the first sector of the disk.

**7. What is the GDT?**
A table that defines memory segments and their privilege levels.

**8. What is the TSS?**
A structure that tells the CPU which kernel stack to use when moving from ring 3 to ring 0.

**9. What is a HAL?**
A thin layer that hides the hardware from the rest of the kernel.

**10. What is a kernel panic?**
A deliberate stop on an error the kernel can't recover from.

**11. What is a monolithic kernel?**
A kernel where all subsystems run together in ring 0. Ours is monolithic.

**12. Monolithic or microkernel?**
In a microkernel, drivers and filesystems run as user processes. Monolithic is faster and simpler, but a bug can crash everything.

**13. What is a shell?**
A program that reads commands and runs them.

**14. What does libc do?**
It gives programs normal functions like `printf`, and wraps system calls.

**15. What is ELF?**
The file format for our programs.

---

## B. Boot and start-up

**16. Walk through the boot.**
BIOS loads GRUB from the MBR. GRUB finds `/boot/kernel.elf` on partition 1, loads it, and jumps to `_start`. That turns on paging and jumps to `higher_half`, which sets the stack and calls `kmain`.

**17. How does GRUB recognise our kernel?**
The `.multiboot` section in `boot/boot.asm` holds the magic number, flags and a checksum.

**18. What state does GRUB leave the CPU in?**
32-bit protected mode with paging off, `EAX` holding `0x2BADB002`, and `EBX` holding the address of the Multiboot info.

**19. Why does `_start` use a temporary page directory?**
The kernel is linked to run at `0xC0000000`, but it's loaded near 1 MB. It needs paging on before it can reach its real addresses.

**20. Why does that directory map memory twice?**
The code is still running at the low address when paging turns on, so both addresses must work for a moment.

**21. Why 4 MB pages there?**
Four directory entries map 16 MB with no page tables needed. It's the simplest setup for a temporary map.

**22. Why call `run_global_constructors`?**
C++ global objects have constructors. A normal program's library runs them, but we have none, so `kmain` runs the list the linker made.

**23. List the 10 boot steps.**

| Step | What starts | Function(s) |
| --- | --- | --- |
| 1 | Boot | `serial_init`, `vga_init`, `gdt_init` |
| 2 | HAL | `idt_init`, `pic_init`, `timer_init` |
| 3 | Memory core | `frame_init`, `paging_init`, `kmalloc_init` |
| 4 | Sync primitives | no start-up needed |
| 5 | Process core | `scheduler_init`, `thread_create`, `idle_loop` |
| 6 | Drivers | `keyboard_init`, `mouse_init`, `ata_init` |
| 7 | Storage | `storage_init` |
| 8 | File Handling | `ext2_mount`, `vfs_mount` |
| 9 | Memory late stage | `virtual_memory_init` |
| 10 | System calls | `syscall_init`, `process_start_first` |

**24. Why does the order matter?**
Each stage needs the ones before it. Memory needs the IDT for page faults, and the disk needs the scheduler so a process can sleep.

**25. Why are steps 6 to 10 in a kernel thread?**
Disk reads sleep until an interrupt. The boot code can't sleep, because it becomes the idle process. A thread can.

**26. When do interrupts turn on?**
In `idle_loop`, through `enable_interrupts_and_wait`, after the scheduler is ready.

**27. Why does memory come in two stages?**
Basic memory is needed early. Swap and demand paging need the disk and files, which come later.

**28. Why are system calls nearly last?**
No program should enter the kernel until everything it might call is ready.

**29. What is PID 1?**
It starts as the kernel thread, and `process_start_first` turns it into `/sbin/init`.

**30. How does the CPU first reach ring 3?**
`process_exec` fills a `Registers` struct with user segments, and `enter_user_mode` jumps to `return_from_interrupt`. `iret` "returns" into the new program.

**31. Why is `iret` used?**
It's the normal way to go from ring 0 to ring 3, because it restores the code segment, stack and flags in one step.

---

## C. System calls and HAL

**32. Walk through a system call.**
The libc wrapper puts the number in `EAX` and the arguments in `EBX`, `ECX`, `EDX`, `ESI`, `EDI`, then runs `int 0x80`. The CPU enters ring 0 and uses the TSS stack. `common_interrupt_code` saves the registers, and `handle_interrupt` calls `syscall_interrupt`, which uses the table. The result goes in `EAX`, and `iret` returns.

**33. Why does `int 0x80` work from ring 3 but `int 0x20` doesn't?**
`idt_init` sets `GATE_USER` only on vector `0x80`. Other gates are ring 0 only.

**34. What happens with an unknown number?**
`syscall_interrupt` returns `-ENOSYS`, which is -38. We tested this with number 999.

**35. How do you stop a bad pointer crashing the kernel?**
Handlers call `user_range_valid` or `copy_string_from_user` first. A bad pointer returns `-EFAULT`.

**36. Why are interrupts turned on in `syscall_interrupt`?**
The CPU turns them off on entry. Long calls need the timer and disk.

**37. How are errors reported?**
As negative numbers in `EAX`. `error_text` in libc turns them into messages.

**38. Which file do kernel and programs share?**
`include/abi.h`, with the numbers, flags and structs.

**39. Explain `struct Registers`.**
It's the CPU state saved on the kernel stack, in the exact order `hal/isr.asm` pushes it.

**40. Why does `fork` return through `return_from_interrupt`?**
The child never entered a system call. Its stack is built to look like one, so it "returns" to user mode.

**41. What is in the HAL?**
`inb`, `outb` and friends, interrupt control, `CR2`/`CR3` access, GDT, IDT, PIC, timer, PCI and serial, in `include/hal.h` and `hal/`.

**42. Explain the GDT entries.**
Null, kernel code, kernel data, user code, user data, TSS. All segments are flat, covering 4 GB, because paging does the real protection.

**43. Why flat segments?**
x86 needs segments, but paging is simpler and stronger. Flat segments effectively switch segmentation off.

**44. Why do user selectors end in 3?**
The low 2 bits are the privilege level, so `0x1B` is entry 3 at ring 3.

**45. What does `panic` print?**
`KERNEL PANIC:`, the message, and `System halted.` `panic_with_registers` also prints `vector`, `eip`, and the registers.

**46. How does a user-program fault differ from a kernel fault?**
A user fault ends that process through `process_fault`. A kernel fault panics.

**47. Where does kernel output go?**
`kprintf` writes to both the serial port and the screen. Serial becomes `boot.log`.

---

## D. User space

**48. How does a program start?**
`_start` in `user/crt0.asm` reads `argc` and `argv`, calls `main`, then calls `exit` with the result.

**49. How does `printf` reach the screen?**
It builds text in a buffer, then calls `write(1, ...)`. That goes through `sys_write`, `ConsoleDevice::write` and `kprintf`.

**50. How does `malloc` work?**
It keeps a first-fit list of blocks, grows the heap with `sbrk`, and joins neighbouring free blocks in `free`.

**51. What does `init` do?**
Ignores `SIGINT` and `SIGTERM`, opens `/dev/console` three times for FDs 0, 1 and 2, then keeps starting `login` and collecting orphans.

**52. How does login work?**
It reads a name and password, with echo off for the password. It hashes salt + password with SHA-256 and compares it with `/etc/shadow`. On success it calls `setgid`, `setuid`, `chdir`, then execs the shell.

**53. Why `setgid` before `setuid`?**
After `setuid`, the process is no longer root and isn't allowed to change its group.

**54. Why give the same error for an unknown name and a wrong password?**
So nobody can find out which names exist.

**55. Why salt the password?**
Two users with the same password get different hashes, and pre-built tables don't work.

**56. How does the shell parse a line?**
`next_token` reads one piece at a time: a word, or an operator such as `|`, `<`, `>`, `>>`, `&`, `;` or `&&`. `read_word` removes quotes and expands `~`. `parse_pipeline` builds the commands, and `add_matching_files` expands `*`.

**56a. Why are the words copied into `word_space`?**
Removing quotes and expanding wildcards changes the text, so words can't stay inside the typed line.

**56b. How does `&&` know whether to run the next command?**
`run_pipeline` returns true if the last program's exit status was 0. `run_line` skips the next part if it wasn't.

**57. How does the shell run a pipeline?**
`run_pipeline` makes a pipe, forks each command, connects them with `dup2`, then waits for each one.

**58. Why are `cd` and `exit` built in?**
A child process can't change its parent's folder, so `cd` must run inside the shell.

**59. How does the C++ compiler work?**
`user/bin/c++.cpp` is 2,219 lines. A lexer makes tokens. A recursive-descent parser writes x86 machine code directly, leaving each result in `EAX`. It reads the file 3 times, then writes an ELF file. It needs no assembler or linker.

**60. Why 3 passes?**
So a function can be used before it's written. Pass 1 learns types, pass 2 learns addresses, pass 3 writes final code.

**61. What can the compiler handle?**
`int`, `char`, `bool`, `string`, 1D arrays, functions, recursion, `if`, loops, `cin`, `cout`, `getline`. `#include` lines are skipped, and `using namespace std;` is accepted.

**62. How does compiled code do I/O?**
A small library written in C++ is built into the compiler. It calls the kernel through `__syscall`.

**63. How does the editor read keys one at a time?**
It reads `/dev/input`, not `/dev/console`, so it doesn't wait for Enter.

---

## E. Build, testing and laptops

**64. How is the kernel compiled?**
`g++ -m32 -ffreestanding` with no standard library, then NASM for assembly, and `ld -T linker.ld`.

**65. Why `-ffreestanding` and `-nostdlib`?**
There's no operating system under the kernel, so the normal C++ library can't be used.

**66. Why `-fno-exceptions -fno-rtti`?**
Both need runtime support that the kernel doesn't have.

**67. Why `-mno-sse -mno-80387`?**
The kernel doesn't save floating-point registers on a switch, so the compiler mustn't use them.

**68. Why does the kernel need its own `memcpy`?**
There's no C library, and GCC may generate calls to `memcpy` by itself.

**69. How does `make test` work?**
`tests/run.sh` boots QEMU once per test, types commands over the serial port, and checks the log for expected text. `sendkey` tests the real keyboard path, and `e2fsck` checks the disk.

**70. How does a test stop QEMU?**
`qemu_exit` writes to port `0xF4`, a debug-exit device added only during tests.

**71. How many tests pass?**
All 110, on a clean build of commit `9e9ae97`, with no compiler warnings. It takes about 8 minutes.

**72. Why QEMU and not a real laptop?**
Our OS targets old BIOS PCs with IDE disks. Modern laptops use UEFI and NVMe, so it wouldn't boot, and a mistake could erase Windows.

**73. Has it run on every laptop?**
Be honest here. Running through WSL with Ubuntu is tested. `run.bat` with QEMU for Windows is **not tested yet**.

**74. Why `-snapshot`?**
Changes are thrown away, so every run starts clean and a bad run can't damage the image.

**75. Why `-no-reboot`?**
A crash stops instead of looping, so `boot.log` keeps the error.

---

## F. Tricky questions

**76. The PRD says "from scratch". We use GRUB. Is that allowed?**
It's an open question for the instructor in the PRD. Get an answer before the viva.

**77. Can the OS shut itself down?**
No. There's no `shutdown` command. Type `exit`, then close the QEMU window.

**78. Does the shell support `;`, `&&`, quotes or `*`?**
Yes, plus `?` wildcards and `~`. It doesn't support `||`, variables, or wildcards in the middle of a path, such as `/*/hello.txt`.

**79. Can you run a shell script?**
No. The shell only reads from the console.

**80. Is there a `PATH`?**
No. A plain name means `/bin/name`. A name with `/` is used as it is.

**81. Can you write C with `printf` in the OS?**
No. It gives `error: this function doesn't exist`. Use `cout`.

**82. Why doesn't the compiler support `double`?**
It only handles whole-number types, and the OS doesn't save floating-point registers. The test gave `this variable doesn't exist`.

**83. Can a program in the OS call `fork`?**
Yes, through the compiler's `__syscall`. `__syscall(1, 0, 0, 0)` is `fork`, and we tested it.

**84. Can a bad program crash the whole OS?**
Bad pointers and divide by zero end only that program. Using too much memory ends that program too. A fork bomb hits the 64-process limit, and Ctrl+C stops it. We tested all of these, and the OS stayed up each time. One gap is left: a fork bomb started in the background with `&` ignores Ctrl+C, and at the limit the shell can't start `kill`, so you would have to restart QEMU.

**84a. How does `date` know the time?**
`rtc_read` in `hal/rtc.cpp` reads the CMOS real-time clock chip, which QEMU sets from the laptop's clock in UTC. File timestamps still use seconds since boot.

**85. Are demo passwords safe?**
They're for the assignment only. The method is sound, but `root`/`root` is obviously weak.

**86. Is SHA-256 good enough for passwords?**
It's a fast hash, so real systems use slow ones like bcrypt. It still means the password isn't stored in plain text.

---

## G. Questions linking to other parts

**87. How do subsystems connect to you?**
Through headers in `include/`. Each subsystem registers its interrupt with `set_interrupt_handler`, and its system calls are in the table in `syscall_init`.

**88. Why is the table in one file?**
Anyone can see every entry into the kernel in one place.

**89. What does Process Management get from you?**
The syscall route, `tss_set_kernel_stack`, and the return path where switching and signals happen.

**90. What does Memory get from you?**
The memory map from Multiboot, and vector 14.

**91. What does Resource get from you?**
Port I/O, the PIC, the IDT and PCI.

---

## Be honest about these

- `run.bat` hasn't been tested on Windows.
- The shell has no variables, `||` or scripts.
- There's no `shutdown`, `nice`, `useradd` or `passwd`.
- A background fork bomb needs a QEMU restart.
- The compiler supports a small subset of C++ only.
- The panic demo is only in `make test`, not from the shell.
- GRUB's use is still an open question for the instructor.
