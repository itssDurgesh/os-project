# Codebase and System Call Guide

This guide explains how the code is organised, what each file does, which system calls exist, and how files call each other.

**Code version:** commit `9e9ae97` on `main` of the private repo `itssDurgesh/os-project`.
**Size:** 149 files and about 17,000 lines. The code is C++ and NASM assembly, for 32-bit x86, and it runs in QEMU.

## Contents

1. [The big picture](#1-the-big-picture)
2. [Folder map](#2-folder-map)
3. [What each file does](#3-what-each-file-does)
4. [System calls](#4-system-calls)
5. [How files call each other](#5-how-files-call-each-other)
6. [Step-by-step traces](#6-step-by-step-traces)
7. [Where the code differs from the PRD](#7-where-the-code-differs-from-the-prd)

---

## 1. The big picture

```text
USER SPACE (ring 3): cannot touch hardware or kernel memory
  Programs     sh, ls, cat, edit, c++, login, init ...      user/bin/*.cpp
  C library    fork(), read(), printf(), malloc() ...       user/libc.cpp
  Start code   calls main(), then exit()                    user/crt0.asm
------------------------- int 0x80 -----------------------------------
KERNEL (ring 0): full control
  System call layer   table of 49 handlers                  kernel/syscall.cpp
  Process             PCB, scheduler, fork, exec, signals   process/
  Memory              frames, paging, swap, slab            memory/
  File Handling       VFS, ext2, devfs, tmpfs, procfs       fs/
  Storage             partitions, buffer cache, swap slots  storage/
  Resource            locks, drivers, Banker's              resource/
  HAL                 GDT, IDT, PIC, timer, PCI, serial     hal/
------------------------------------------------------------------------
HARDWARE (emulated by QEMU)
  x86 CPU, 128 MB RAM, ATA disk, PS/2 keyboard and mouse, VGA text screen
```

**Two simple rules explain most of the design:**

1. **User programs can enter the kernel in only one way:** `int 0x80`, a system call. A CPU interrupt or fault can also move a program into the kernel, but the program doesn't choose where it lands.
2. **The kernel is not preempted.** Kernel code stops only when it chooses to sleep. A timer tick only sets a flag, and the actual switch happens when the process is about to go back to user mode.

---

## 2. Folder map

Each folder belongs to one teammate.

| Folder | Owner | Files | Lines | What's inside |
| --- | --- | --- | --- | --- |
| `boot/` | Integration | 2 | 82 | The first code that runs, and the GRUB settings |
| `hal/` | Integration | 9 | 523 | GDT, TSS, IDT, PIC, timer, real-time clock, PCI, serial port |
| `kernel/` | Integration | 6 | 825 | `kmain`, `kprintf`, panic, syscall table, boot tests |
| `include/` | Shared | 14 | 1,032 | Header files that connect the subsystems |
| `process/` | Process Management | 6 | 1,453 | PCB, scheduler, `fork`/`exec`, signals, message queues |
| `memory/` | Memory Management | 8 | 1,301 | Frames, paging, slab, `kmalloc`, swap logic, shared memory |
| `resource/` | Resource Management | 7 | 1,096 | Disk, keyboard, mouse and screen drivers, locks, Banker's |
| `storage/` | Storage Hierarchy | 2 | 246 | Partition table, buffer cache, swap slots |
| `fs/` | File Handling | 8 | 3,017 | VFS, ext2, devfs, tmpfs, procfs, open files, pipes |
| `user/` | Integration | 50 | 6,592 | C library and 44 programs, including the shell and C++ compiler |
| `tests/` | Everyone | 18 | 648 | `run.sh` and the commands it types into the OS |
| `tools/` | Integration | 3 | 120 | Scripts that build the disk image |
| `package/` | Integration | 2 | 43 | `run.bat` and the README for the Windows ZIP |
| `rootfs/` | Shared | 7 | 322 | Files copied onto the OS disk, including `/examples` |

The top folder also has the `Makefile` (build rules), `linker.ld` (kernel memory layout), `README.md`, `SETUP.md` and `requirements.txt`.

---

## 3. What each file does

### 3.1 Boot and build

| File | Role | Main parts |
| --- | --- | --- |
| `boot/boot.asm` | First kernel code. Turns on a temporary page map, moves to `0xC0000000`, and calls C++ | `_start`, `higher_half` |
| `boot/grub.cfg` | Tells GRUB to load `/boot/kernel.elf` with Multiboot | menu entry |
| `linker.ld` | Places `.boot` at 1 MB and the rest of the kernel at `0xC0000000` + 1 MB | sections, `_kernel_end` |
| `user/user.ld` | Places user programs at `0x08048000` | sections |
| `Makefile` | Builds everything | `all`, `run`, `test`, `dist`, `clean` |
| `tools/mkimage.sh` | Builds `os.img`: MBR, GRUB, 3 partitions | `make_ext2` |
| `tools/mkusers.py` | Writes `/etc/passwd` and `/etc/shadow` | salted SHA-256 |
| `tools/mkbigfile.py` | Writes the 5 MB test file `/etc/big.bin` | used by the `disk` test |
| `package/run.bat` | Starts QEMU on Windows | not tested on Windows yet |

### 3.2 Hardware layer (`hal/`, `include/hal.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/hal.h` | Port I/O, interrupts on/off, the `Registers` struct | `inb`, `outb`, `insw`, `outsw`, `turn_off_interrupts`, `restore_interrupts`, `read_cr2`, `write_cr3` |
| `hal/gdt.cpp` | Kernel and user segments, and the TSS | `gdt_init`, `tss_set_kernel_stack` |
| `hal/cpu.asm` | Helpers that C++ can't write | `gdt_load`, `enter_user_mode` |
| `hal/idt.cpp` | Interrupt table and the common interrupt function | `idt_init`, `set_interrupt_handler`, `handle_interrupt` |
| `hal/isr.asm` | 256 small entry stubs, plus saving and restoring the CPU state | `common_interrupt_code`, `return_from_interrupt` |
| `hal/pic.cpp` | Moves IRQs to `0x20`-`0x2F`, and controls masks and EOI | `pic_init`, `pic_unmask`, `pic_eoi` |
| `hal/pit.cpp` | The 100 Hz timer | `timer_init`, `get_ticks`, `timer_interrupt` |
| `hal/pci.cpp` | Finds devices on the PCI bus | `pci_read`, `pci_write`, `pci_find_device` |
| `hal/rtc.cpp` | Reads the date and time from the real-time clock chip | `rtc_read` |
| `hal/serial.cpp` | COM1 port. Kernel output also goes here, and this is `boot.log` | `serial_init`, `serial_putc`, `serial_enable_input`, `qemu_exit` |

### 3.3 Kernel core (`kernel/`)

| File | Role | Main functions |
| --- | --- | --- |
| `kernel/main.cpp` | Boot order | `kmain`, `kernel_thread`, `run_global_constructors` |
| `kernel/syscall.cpp` | System call table | `syscall_init`, `syscall_interrupt` |
| `kernel/kprintf.cpp` | Kernel `printf`, to serial and screen | `kprintf`, `kvprintf` |
| `kernel/panic.cpp` | Stops the system on a fatal error | `panic`, `panic_with_registers` |
| `kernel/string.cpp` | The kernel's own string functions | `memcpy`, `memset`, `strcmp`, `copy_string` |
| `kernel/tests.cpp` | 8 boot-time self tests | `tests_init`, `tests_run` |

### 3.4 Process Management (`process/`, `include/process.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/process.h` | The PCB (`struct Process`) and `class WaitQueue` | |
| `process/sched.cpp` | Priority Round-Robin, aging, quotas, kernel threads, wait queues | `schedule`, `scheduler_tick`, `switch_process_if_needed`, `pick_next`, `make_ready`, `age_ready_queues`, `thread_create`, `thread_exit`, `thread_sleep`, `create_process`, `delete_process`, `WaitQueue::sleep`, `WaitQueue::wake_one`, `WaitQueue::wake_all` |
| `process/switch.asm` | The context switch | `switch_stacks` |
| `process/process.cpp` | ELF loader, and exec, fork, exit, wait | `process_exec`, `process_fork`, `process_exit`, `process_wait`, `process_fault`, `process_start_first` |
| `process/sys_process.cpp` | Process system calls | `sys_fork`, `sys_exec`, `sys_wait`, `sys_pipe`, and others |
| `process/signal.cpp` | Signals | `send_signal`, `deliver_signals`, `start_handler`, `sys_kill`, `sys_sigaction`, `sys_sigreturn` |
| `process/message_queue.cpp` | Message queues | `sys_msgget`, `sys_msgsnd`, `sys_msgrcv` |

### 3.5 Memory Management (`memory/`, `include/memory.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/memory.h` | Page flags, `MemoryArea`, and all memory functions | |
| `memory/frame.cpp` | Bitmap and frame table for physical RAM | `frame_init`, `alloc_frame`, `alloc_frames`, `free_frame`, `add_frame_user`, `frame_get_owner`, `frame_set_owner` |
| `memory/paging.cpp` | Two-level page tables and the page fault handler | `paging_init`, `paging_map`, `paging_unmap`, `paging_lookup`, `paging_switch`, `page_fault` |
| `memory/address_space.cpp` | One address space per process, and safe user-pointer checks | `make_page_directory`, `copy_page_directory`, `delete_page_directory`, `write_to_page_directory`, `user_range_valid`, `copy_string_from_user` |
| `memory/virtual_memory.cpp` | Demand paging, Clock, swapper thread | `handle_page_fault`, `load_page`, `get_user_frame`, `evict_one_page`, `swapper_thread`, `add_memory_area`, `sys_meminfo` |
| `memory/slab.cpp` | Same-size kernel objects | `slab_cache_init`, `slab_alloc`, `slab_free` |
| `memory/kmalloc.cpp` | General kernel allocator and C++ `new`/`delete` | `kmalloc`, `kfree`, `kmalloc_zeroed` |
| `memory/shared_memory.cpp` | Shared memory segments | `sys_shmget`, `sys_shmat`, `sys_shmdt` |
| `memory/sys_memory.cpp` | Heap and `mmap` system calls | `sys_brk`, `sys_sbrk`, `sys_mmap`, `sys_munmap` |

### 3.6 Resource Management (`resource/`, `include/sync.h`, `include/input.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/sync.h` | Lock classes | `Spinlock`, `Mutex`, `Semaphore`, `CondVar` |
| `resource/sync.cpp` | Lock code | `Mutex::lock`, `Mutex::unlock`, `Semaphore::wait`, `Semaphore::post`, `CondVar::wait` |
| `resource/user_semaphore.cpp` | Semaphores for user programs | `sys_sem_open`, `sys_sem_wait`, `sys_sem_post`, `sys_sem_close` |
| `resource/banker.cpp` | Banker's Algorithm | `system_is_safe`, `try_to_grant`, `sys_res_declare`, `sys_res_request`, `sys_res_release`, `banker_process_exit` |
| `resource/ata.cpp` | Disk driver, DMA with PIO fallback | `ata_init`, `ata_read`, `ata_write`, `dma_transfer`, `pio_read`, `pio_write`, `ata_interrupt` |
| `resource/keyboard.cpp` | PS/2 keyboard and the key queue | `keyboard_init`, `keyboard_interrupt`, `input_put_char`, `input_get_char` |
| `resource/mouse.cpp` | PS/2 mouse | `mouse_init`, `mouse_interrupt`, `mouse_get_event` |
| `resource/vga.cpp` | 80x25 text screen and escape codes | `vga_init`, `vga_putc` |

### 3.7 Storage Hierarchy (`storage/`, `include/storage.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/storage.h` | `Buffer`, partition IDs, block and swap functions | |
| `storage/block.cpp` | MBR reading and the 128-block buffer cache | `storage_init`, `block_get`, `block_put`, `block_write`, `find_buffer`, `find_reusable_buffer`, `read_partition_table` |
| `storage/swap.cpp` | Swap slots, with one bit per slot | `swap_init`, `swap_alloc_slot`, `swap_free_slot`, `swap_read`, `swap_write` |

### 3.8 File Handling (`fs/`, `include/vfs.h`)

| File | Role | Main functions |
| --- | --- | --- |
| `include/vfs.h` | Base classes `Inode` and `File` | |
| `fs/vfs.cpp` | Path walking, mount table, permission check | `walk`, `vfs_lookup`, `vfs_create_path`, `vfs_check_permission`, `vfs_mount`, `follow_mounts` |
| `fs/file.cpp` | Global open-file objects | `file_open`, `file_close`, `file_read`, `file_write`, `file_seek`, class `InodeFile` |
| `fs/sys_file.cpp` | File system calls | `sys_open`, `sys_read`, `sys_write`, `sys_stat`, `sys_chdir`, `sys_dup2`, and others |
| `fs/ext2.cpp` | The real disk filesystem | `ext2_mount`, `map_block`, `read_data`, `write_data`, `alloc_block`, `find_entry`, `add_entry`, class `Ext2Inode` |
| `fs/devfs.cpp` | `/dev`: console, input, null, mouse, with line editing and history | `devfs_mount`, `read_line`, `console_set_echo`, classes `ConsoleDevice`, `RawInputDevice`, `NullDevice`, `MouseDevice` |
| `fs/tmpfs.cpp` | `/tmp`, stored in RAM | `tmpfs_mount`, class `TmpInode` |
| `fs/procfs.cpp` | `/proc`: meminfo, uptime, processes, date, diskinfo | `procfs_mount`, `write_meminfo`, `write_processes`, `write_date`, `write_diskinfo` |
| `fs/pipe.cpp` | Pipes | `pipe_create`, classes `PipeReader`, `PipeWriter` |

### 3.9 Shared headers

| File | Role |
| --- | --- |
| `include/abi.h` | The contract between kernel and programs: syscall numbers, flags, structs |
| `include/syscall.h` | Declares every `sys_*` handler |
| `include/console.h` | `kprintf`, `panic`, VGA functions |
| `include/errno.h` | Error numbers such as `ENOENT` and `EACCES` |
| `include/kstring.h` | Declares the kernel string functions |
| `include/multiboot.h` | The structures GRUB passes to us |
| `include/tests.h` | Boot test functions |

### 3.10 User space (`user/`)

| File | Role |
| --- | --- |
| `user/crt0.asm` | Program start: calls `main`, then `exit`. Also holds `signal_return_stub` |
| `user/libc.cpp`, `user/libc.h` | System call wrappers, strings, paths, `printf`, `malloc`, `free` |
| `user/users.cpp` | Reads `/etc/passwd`, and checks passwords against `/etc/shadow` |
| `user/sha256.cpp` | SHA-256 hash |

**Programs in `user/bin/` (44):**

| Group | Programs |
| --- | --- |
| Start-up | `init` (PID 1), `login`, `sh` (shell) |
| Files | `ls`, `cat`, `cp`, `mv`, `rm`, `touch`, `mkdir`, `rmdir`, `head`, `tail`, `grep`, `sort`, `wc`, `find`, `chmod` |
| System | `ps`, `top`, `free`, `df`, `kill`, `sleep`, `whoami`, `pwd`, `echo`, `clear`, `date`, `uname` |
| Tools | `edit` (text editor), `c++` (compiler), `run` (compile and start) |
| Hardware checks | `keytest`, `mousetest` |
| Tests | `forktest`, `fstest`, `swaptest`, `sigtest`, `ipctest`, `banktest`, `quotatest`, `permtest`, `tmptest` |

`cd`, `help`, `history` and `exit` are not programs. The shell runs them itself, because `cd` must change the shell's own folder and `history` reads the shell's own list.

**How the shell in `user/bin/sh.cpp` reads a line:**

| Function | What it does |
| --- | --- |
| `next_token` | Finds the next word or operator: `\|`, `<`, `>`, `>>`, `&`, `;`, `&&` |
| `read_word` | Reads one word, removes quotes, and expands `~` |
| `add_matching_files` | Expands wildcards such as `*.txt` |
| `parse_pipeline` | Builds the commands joined by `\|` |
| `run_line` | Runs each part of the line, and applies `;` and `&&` |
| `run_builtin` | Runs `cd`, `help`, `history` and `exit` |
| `run_pipeline` | Forks each command and connects the pipes |

---

## 4. System calls

### 4.1 How one system call travels

Example: a program calls `read(fd, buffer, 100)`.

| Step | Where | What happens |
| --- | --- | --- |
| 1 | `user/libc.cpp`, `read` | Calls `syscall3(SYS_READ, fd, buffer, 100)` |
| 2 | `user/libc.cpp`, `syscall3` | Puts 16 in `EAX`, the arguments in `EBX`, `ECX`, `EDX`, and runs `int $0x80` |
| 3 | CPU | Finds vector `0x80` in the IDT. `idt_init` marks it `GATE_USER`, so ring 3 may use it. The CPU moves to ring 0, takes the kernel stack from the TSS, and pushes the user's `EIP`, `CS`, `EFLAGS`, `ESP` and `SS` |
| 4 | `hal/isr.asm` | The stub pushes a dummy error code and the vector number. `common_interrupt_code` saves the registers and calls `handle_interrupt` |
| 5 | `hal/idt.cpp`, `handle_interrupt` | Calls `handlers[0x80]`, which is `syscall_interrupt` |
| 6 | `kernel/syscall.cpp`, `syscall_interrupt` | Turns interrupts back on, checks the number, and calls `syscall_table[16]`. An unknown number returns `-ENOSYS` |
| 7 | `fs/sys_file.cpp`, `sys_read` | Checks the FD and the user's pointer, then copies data through a kernel buffer |
| 8 | `hal/idt.cpp`, `handle_interrupt` | On the way out: `switch_process_if_needed` and `deliver_signals` |
| 9 | `hal/isr.asm`, `return_from_interrupt` | Restores the registers and runs `iret`. The result is in `EAX` |

**Argument registers:** `EBX`, `ECX`, `EDX`, `ESI`, `EDI`. **Errors** come back as negative numbers, such as `-ENOENT` (-2).

### 4.2 All 49 system calls

The numbers are in `include/abi.h`, and the table is filled in `syscall_init` in `kernel/syscall.cpp`.

**Process Management**

| # | Name | Handler file | Main work done by | Used by |
| --- | --- | --- | --- | --- |
| 1 | `fork` | `process/sys_process.cpp` | `process_fork` | `sh`, `init`, `run`, tests |
| 2 | `exec` | `process/sys_process.cpp` | `process_exec` | `sh`, `init`, `login`, `run` |
| 3 | `exit` | `process/sys_process.cpp` | `process_exit` | every program, through `crt0.asm` |
| 4 | `wait` | `process/sys_process.cpp` | `process_wait` | `sh`, `init`, `run`, tests |
| 5 | `getpid` | `process/sys_process.cpp` | reads the PCB | `sigtest` |
| 6 | `kill` | `process/signal.cpp` | `send_signal` | `kill`, tests |
| 7 | `sleep` | `process/sys_process.cpp` | `thread_sleep` (argument is in milliseconds) | `sleep`, `top`, `login`, tests |
| 8 | `sigaction` | `process/signal.cpp` | stores the handler in the PCB | `init`, `sh`, `edit`, `sigtest` |
| 9 | `sigreturn` | `process/signal.cpp` | restores saved registers | `signal_return_stub` in `crt0.asm` |
| 27 | `pipe` | `process/sys_process.cpp` | `pipe_create` in `fs/pipe.cpp` | `sh`, `sigtest` |
| 28 | `msgget` | `process/message_queue.cpp` | finds or makes a queue | `ipctest` |
| 29 | `msgsnd` | `process/message_queue.cpp` | adds a message, may sleep | `ipctest` |
| 30 | `msgrcv` | `process/message_queue.cpp` | takes a message, may sleep | `ipctest` |
| 38 | `getuid` | `process/sys_process.cpp` | reads the PCB | `sh`, `whoami`, `run` |
| 39 | `getgid` | `process/sys_process.cpp` | reads the PCB | no program uses it yet |
| 43 | `setuid` | `process/sys_process.cpp` | root only | `login`, tests |
| 44 | `setgid` | `process/sys_process.cpp` | root only | `login`, `permtest` |
| 46 | `procinfo` | `process/sys_process.cpp` | walks the process list | `ps`, `top`, `quotatest` |
| 48 | `set_quota` | `process/sys_process.cpp` | sets `cpu_quota`, root only | `quotatest` |

**Memory Management**

| # | Name | Handler file | Main work done by | Used by |
| --- | --- | --- | --- | --- |
| 10 | `mmap` | `memory/sys_memory.cpp` | `add_memory_area` (zero-filled memory only, not files) | `swaptest` |
| 11 | `munmap` | `memory/sys_memory.cpp` | `unmap_user_page` | `swaptest` |
| 12 | `brk` | `memory/sys_memory.cpp` | `set_heap_end` | no C library wrapper yet |
| 13 | `sbrk` | `memory/sys_memory.cpp` | `set_heap_end` | `malloc`, `swaptest`, strings in compiled C++ |
| 31 | `shmget` | `memory/shared_memory.cpp` | allocates frames | `ipctest`, `banktest` |
| 32 | `shmat` | `memory/shared_memory.cpp` | `paging_map` with `PTE_SHARED` | `ipctest`, `banktest` |
| 33 | `shmdt` | `memory/shared_memory.cpp` | `detach_slot` | `ipctest` |
| 49 | `meminfo` | `memory/virtual_memory.cpp` | `get_memory_info` | `free`, `top`, `swaptest` |

**File Handling**

| # | Name | Handler file | Main work done by | Used by |
| --- | --- | --- | --- | --- |
| 14 | `open` | `fs/sys_file.cpp` | `file_open` | nearly every program |
| 15 | `close` | `fs/sys_file.cpp` | `file_close` | nearly every program |
| 16 | `read` | `fs/sys_file.cpp` | `file_read` | `sh`, `cat`, `login`, and others |
| 17 | `write` | `fs/sys_file.cpp` | `file_write` | everything, through `printf` |
| 18 | `seek` | `fs/sys_file.cpp` | `file_seek` | `fstest` |
| 19 | `stat` | `fs/sys_file.cpp` | `vfs_lookup` | `ls`, `cp`, `mv`, `chmod`, `find`, `is_folder` in libc, tests |
| 20 | `unlink` | `fs/sys_file.cpp` | `vfs_unlink` | `rm`, `mv` |
| 21 | `mkdir` | `fs/sys_file.cpp` | `vfs_mkdir` | `mkdir`, `cp -r` |
| 22 | `rmdir` | `fs/sys_file.cpp` | `vfs_rmdir` | `rmdir`, `rm -r` |
| 23 | `readdir` | `fs/sys_file.cpp` | `Inode::readdir` | `ls`, `find`, `cp -r`, `rm -r`, `*` in `sh` |
| 24 | `chdir` | `fs/sys_file.cpp` | `vfs_lookup` | `cd` in `sh`, `login` |
| 25 | `getcwd` | `fs/sys_file.cpp` | reads `cwd_path` | `sh`, `pwd` |
| 26 | `dup2` | `fs/sys_file.cpp` | copies an FD | `sh`, for pipes and redirection |
| 45 | `set_echo` | `fs/sys_file.cpp` | `console_set_echo`, root only | `login`, hides the password |
| 47 | `chmod` | `fs/sys_file.cpp` | `Inode::sync` | `chmod` |

**Resource Management**

| # | Name | Handler file | Main work done by | Used by |
| --- | --- | --- | --- | --- |
| 34 | `sem_open` | `resource/user_semaphore.cpp` | finds or makes a semaphore | `ipctest` |
| 35 | `sem_wait` | `resource/user_semaphore.cpp` | sleeps while the count is 0 | `ipctest` |
| 36 | `sem_post` | `resource/user_semaphore.cpp` | adds 1 and wakes one process | `ipctest` |
| 37 | `sem_close` | `resource/user_semaphore.cpp` | frees it when the last user closes | `ipctest` |
| 40 | `res_declare` | `resource/banker.cpp` | stores the maximum claim | `banktest` |
| 41 | `res_request` | `resource/banker.cpp` | `try_to_grant`, `system_is_safe` | `banktest` |
| 42 | `res_release` | `resource/banker.cpp` | gives back and wakes waiters | `banktest` |

### 4.3 Calls that different commands use

| Command you type | Main system calls behind it |
| --- | --- |
| Any command in the shell | `fork`, `exec`, `wait` |
| `ls` | `open`, `readdir`, `close` |
| `ls -l` | the same, plus `stat` |
| `cat file` | `open`, `read`, `write`, `close` |
| `cp a b` | `open` (twice), `read`, `write`, `close` |
| `mv a b` | what `cp` uses, plus `unlink` |
| `mkdir x` | `mkdir` |
| `cd x` | `chdir` |
| `pwd` | `getcwd` |
| `chmod 644 f` | `chmod` |
| `ps` | `procinfo` |
| `free` | `meminfo` |
| `kill 5 9` | `kill` |
| `sleep 2` | `sleep` |
| `a \| b` | `pipe`, `dup2`, `fork` (twice) |
| `a > file` | `open`, `dup2` |

---

## 5. How files call each other

Each row says: this function in this file calls that function in that file, and why.

### 5.1 Boot chain

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| GRUB | `_start` | `boot/boot.asm` | Starts the kernel |
| `_start` / `higher_half` | `kmain` | `kernel/main.cpp` | Moves from assembly to C++ |
| `kmain` | `serial_init` | `hal/serial.cpp` | Gets log output working first |
| `kmain` | `vga_init` | `resource/vga.cpp` | Screen output |
| `kmain` | `gdt_init`, then `gdt_load` | `hal/gdt.cpp`, `hal/cpu.asm` | Segments and TSS |
| `kmain` | `idt_init` | `hal/idt.cpp` | Interrupt table |
| `kmain` | `pic_init`, `timer_init` | `hal/pic.cpp`, `hal/pit.cpp` | Device interrupts and ticks |
| `kmain` | `frame_init`, `paging_init`, `kmalloc_init` | `memory/*.cpp` | Memory core |
| `kmain` | `scheduler_init`, `thread_create`, `idle_loop` | `process/sched.cpp` | Starts scheduling. The boot code becomes the idle process |
| `kernel_thread` | `keyboard_init`, `mouse_init`, `ata_init` | `resource/*.cpp` | Drivers |
| `kernel_thread` | `storage_init` | `storage/block.cpp` | Partition table and cache |
| `kernel_thread` | `ext2_mount`, `devfs_mount`, `tmpfs_mount`, `procfs_mount` | `fs/*.cpp` | Filesystems |
| `kernel_thread` | `virtual_memory_init` | `memory/virtual_memory.cpp` | Swap and the swapper thread |
| `kernel_thread` | `syscall_init` | `kernel/syscall.cpp` | System call table |
| `kernel_thread` | `process_start_first("/sbin/init")` | `process/process.cpp` | First program, in ring 3 |

### 5.2 How the subsystems depend on each other

**Interrupts and HAL into the rest of the kernel**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `common_interrupt_code` | `handle_interrupt` | `hal/idt.cpp` | Every interrupt arrives here |
| `handle_interrupt` | `pic_eoi` | `hal/pic.cpp` | Tells the PIC the IRQ was received |
| `handle_interrupt` | `switch_process_if_needed` | `process/sched.cpp` | Switches only on the way back to user mode |
| `handle_interrupt` | `deliver_signals` | `process/signal.cpp` | Runs pending signals |
| `handle_interrupt` | `process_fault` | `process/process.cpp` | A bad program ends itself |
| `handle_interrupt` | `panic_with_registers` | `kernel/panic.cpp` | A fault in the kernel is fatal |
| `timer_interrupt` | `scheduler_tick` | `process/sched.cpp` | Runs every 10 ms |
| `page_fault` | `handle_page_fault` | `memory/virtual_memory.cpp` | Loads the missing page |

**Process Management uses Memory**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `process_fork` | `copy_page_directory`, `copy_memory_areas` | `memory/address_space.cpp`, `memory/virtual_memory.cpp` | The child gets its own copy of memory |
| `process_fork` | `shared_memory_copy_to_child` | `memory/shared_memory.cpp` | Shared segments stay shared |
| `process_exec` | `make_page_directory`, `add_memory_area` | `memory/` | A fresh address space |
| `process_exec` | `map_new_user_page`, `write_to_page_directory` | `memory/` | Builds the first stack pages |
| `process_exit` | `release_memory_areas`, `delete_page_directory` | `memory/` | Frees everything |
| `create_process` | `slab_alloc` | `memory/slab.cpp` | Allocates the PCB |
| `kernel_stack_alloc` | `alloc_frames` | `memory/frame.cpp` | 8 KB kernel stack. If there's no memory, `fork` fails instead of the OS stopping |
| `schedule` | `paging_switch` | `memory/paging.cpp` | Loads the next process's page map |

**Process Management uses File Handling**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `process_exec` | `vfs_lookup`, `vfs_check_permission` | `fs/vfs.cpp` | Finds the program and checks the execute bit |
| `read_program_header` | `Inode::read` | `fs/ext2.cpp`, through the VFS | Reads the ELF header |
| `process_fork` | `hold_file`, `hold_inode` | `fs/` | The child shares open files |
| `process_exit` | `file_close`, `release_inode` | `fs/` | Closes everything |
| `sys_pipe` | `pipe_create` | `fs/pipe.cpp` | Creates the two ends |

**Memory uses Storage and File Handling**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `evict_one_page` | `swap_alloc_slot`, `swap_write` | `storage/swap.cpp` | Writes a dirty page to disk |
| `handle_page_fault` | `swap_read`, `swap_free_slot` | `storage/swap.cpp` | Brings a page back |
| `delete_page_directory` | `swap_free_slot` | `storage/swap.cpp` | Frees slots when a process dies |
| `load_page` | `Inode::read` | `fs/ext2.cpp`, through the VFS | Loads a program page from its file |
| `add_memory_area` | `hold_inode` | `fs/vfs.cpp` | Keeps the file alive while it's mapped |

**File Handling uses Storage, Memory and Resource**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `sys_open`, `sys_read`, others | `copy_string_from_user`, `user_range_valid` | `memory/address_space.cpp` | Bad user pointers fail safely |
| `read_data`, `write_data`, many others | `block_get`, `block_put`, `block_write` | `storage/block.cpp` | All ext2 disk access goes through the cache |
| `Ext2Inode` functions | `Mutex::lock`, `Mutex::unlock` | `resource/sync.cpp` | One lock for the filesystem |
| `read_line`, `RawInputDevice::read` | `input_get_char` | `resource/keyboard.cpp` | Gets keys |
| `MouseDevice::read` | `mouse_get_event` | `resource/mouse.cpp` | Gets mouse events |
| `ConsoleDevice::write` | `kprintf` | `kernel/kprintf.cpp` | Program output reaches the screen and serial |
| `ProcFile::read` | `kmalloc` | `memory/kmalloc.cpp` | Gets a page for the text |
| `write_meminfo` | `get_memory_info` | `memory/virtual_memory.cpp` | Gets the numbers for `/proc/meminfo` |
| `write_date` | `rtc_read` | `hal/rtc.cpp` | Gets the time for `/proc/date` |
| `write_diskinfo` | `ext2_get_usage` | `fs/ext2.cpp` | Gets the numbers for `/proc/diskinfo` |

**Storage uses Resource and Memory**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `block_get` | `ata_read` | `resource/ata.cpp` | Cache miss |
| `block_write` | `ata_write` | `resource/ata.cpp` | Write-through |
| `swap_read`, `swap_write` | `ata_read`, `ata_write` | `resource/ata.cpp` | Swap goes straight to the disk driver |
| `storage_init` | `alloc_frames` | `memory/frame.cpp` | 128 blocks of cache memory |

**Resource uses HAL and Process**

| Caller | Calls | In file | Why |
| --- | --- | --- | --- |
| `ata_init` | `set_interrupt_handler`, `pic_unmask` | `hal/idt.cpp`, `hal/pic.cpp` | Listens for IRQ 14 |
| `find_dma_controller` | `pci_find_device`, `pci_read`, `pci_write` | `hal/pci.cpp` | Finds the DMA controller |
| `wait_for_irq` | `WaitQueue::sleep` | `process/sched.cpp` | The process sleeps until the disk finishes |
| `ata_interrupt` | `WaitQueue::wake_all` | `process/sched.cpp` | Wakes the sleeper |
| `input_put_char` | `send_signal_to_all_programs` | `process/signal.cpp` | Ctrl+C sends `SIGINT` |
| `sys_res_request` | `WaitQueue::sleep` | `process/sched.cpp` | An unsafe request waits |
| `banker_process_exit` | called from `process_exit` | `process/process.cpp` | Returns resources when a process ends |

### 5.3 Who uses each kernel lock

| Lock | File | Protects |
| --- | --- | --- |
| `pager_lock` (Mutex) | `memory/virtual_memory.cpp` | Moving one page in or out at a time |
| `fs.lock` (Mutex) | `fs/ext2.cpp` | The whole ext2 filesystem |
| `ata_lock` (Mutex) | `resource/ata.cpp` | One disk command at a time |

When a page loads from a file, these locks are taken in this order: `pager_lock`, then `fs.lock`, then `ata_lock`. It's always the same order, so they can't deadlock each other.

`Spinlock`, the kernel `Semaphore` and `CondVar` are built and tested in `kernel/tests.cpp`, but no subsystem uses them yet. Interrupts-off sections do the job of a spinlock in the scheduler and drivers.

---

## 6. Step-by-step traces

### 6.1 Power on to the login prompt

1. QEMU's BIOS loads GRUB from the MBR of `os.img`.
2. GRUB reads `/boot/kernel.elf` from partition 1 and jumps to `_start`.
3. `_start` turns on 4 MB pages and paging, and jumps to `higher_half` at `0xC0000000`.
4. `kmain` runs: serial, VGA, GDT, IDT, PIC, timer, frames, paging, `kmalloc`, scheduler.
5. `thread_create` makes the kernel thread, which is PID 1. `idle_loop` switches to it straight away, and `thread_start` turns interrupts on.
6. `kernel_thread` starts the drivers, reads the partition table, and mounts `/`, `/dev`, `/tmp` and `/proc`.
7. `virtual_memory_init` starts the swapper thread, and `syscall_init` fills the table.
8. `process_start_first` turns PID 1 into `/sbin/init`, in ring 3, through `enter_user_mode`.
9. `init` opens `/dev/console` three times (FDs 0, 1, 2), forks, and execs `/bin/login`.
10. `login` checks the password, calls `setgid`, `setuid` and `chdir`, and execs `/bin/sh`.

### 6.2 Typing `ls` in the shell

1. Each key raises IRQ 1. `keyboard_interrupt` calls `input_put_char`, which queues the key and wakes the reader.
2. The shell is blocked in `read(STDIN, ...)`. `read_line` in `fs/devfs.cpp` collects keys until Enter.
3. `parse_pipeline` in `sh.cpp` reads the words, and `run_pipeline` calls `fork`.
4. `process_fork` checks the 64-process limit, makes a new PCB, copies memory and shares FDs. The child's `EAX` is 0.
5. The child calls `exec("/bin/ls")`. `process_exec` reads the ELF header, records the memory areas and builds a new stack.
6. `ls` starts, and its first instruction causes a page fault. `handle_page_fault` loads that page from the file.
7. `ls` calls `open`, `readdir` and `write`, then returns from `main`.
8. `crt0.asm` calls `exit`. `process_exit` frees everything, and the PCB stays as a zombie.
9. The shell wakes in `process_wait`, reads the status, and `delete_process` frees the PCB.

### 6.3 A timer tick switches processes

1. Every 10 ms the timer raises IRQ 0, and `handle_interrupt` calls `timer_interrupt`.
2. `scheduler_tick` adds to `cpu_ticks`, wakes sleepers, and runs aging every 100 ms.
3. `quantum_left` starts at 2. When it reaches 0, `need_resched` is set.
4. If the tick came from user mode, `switch_process_if_needed` calls `schedule`.
5. `schedule` puts the old process back on its ready queue, and `pick_next` chooses the next one.
6. It sets the TSS kernel stack, calls `paging_switch`, and then `switch_stacks` in `process/switch.asm`.
7. The new process continues from wherever it last stopped.

### 6.4 `cat /etc/hello.txt` reads from the disk

1. `sys_open` copies the path into the kernel, and `file_open` calls `vfs_lookup`.
2. `walk` checks each folder's execute permission and calls `Ext2Inode::lookup`.
3. `find_entry` searches the folder, and `get_inode` gives the file's inode.
4. `vfs_check_permission` checks read permission. An `InodeFile` is created and gets an FD.
5. `sys_read` calls `file_read`, then `InodeFile::read`, then `Ext2Inode::read`.
6. `read_data` calls `map_block` to turn the file position into a disk block.
7. `block_get` looks in the cache. On a miss, it calls `ata_read`.
8. `dma_transfer` starts the disk, and `wait_for_irq` puts the process to sleep.
9. IRQ 14 runs `ata_interrupt`, which wakes the process. The block stays in the cache.
10. `sys_read` copies the data to the user. `write` to FD 1 goes through `ConsoleDevice::write`, `kprintf` and `vga_putc`.

### 6.5 RAM fills up

1. `get_user_frame` sees RAM is at least 80% used, and wakes the swapper.
2. `swapper_thread` calls `evict_one_page` in a loop until RAM is below 70%.
3. Clock: a page with `PTE_ACCESSED` set has the bit cleared and gets a second chance.
4. A dirty page gets a swap slot. Its entry becomes the slot number plus `PTE_SWAPPED`, and `swap_write` saves it.
5. A clean page is dropped, because it can be read again from its file, or it was all zeros.
6. Later, touching that page faults. `handle_page_fault` sees `PTE_SWAPPED` and calls `swap_read`.

### 6.6 `ls | wc` uses a pipe

1. `run_pipeline` calls `pipe(fds)`. `sys_pipe` calls `pipe_create`, which makes a `PipeReader` and a `PipeWriter`.
2. The first child runs `dup2(fds[1], STDOUT)`, closes the pipe FDs, and execs `ls`.
3. The second child runs `dup2(previous_read, STDIN)` and execs `wc`.
4. The shell closes its own copies. If it didn't, `wc` would never see end of file.
5. A reader sleeps on an empty pipe, and a writer sleeps on a full one (4,096 bytes).
6. When `ls` exits, `PipeWriter::close` wakes `wc`, and `read` returns 0.

### 6.7 Ctrl+C stops a program

1. `keyboard_interrupt` turns Ctrl+C into character 3.
2. `input_put_char` calls `send_signal_to_all_programs(SIGINT)`.
3. `send_signal` sets a bit in `pending_signals`, unless the signal is ignored. `init` ignores `SIGINT`, and its children inherit that.
4. `wake_process_for_signal` wakes the process if it is sleeping.
5. On return to user mode, `deliver_signals` calls `process_exit(130)`, which is 128 + 2.
6. The shell survives because it is ignoring `SIGINT`. It called `default_signal(SIGINT)` only in the child.

### 6.8 Banker's delays an unsafe request

1. `banktest` declares a maximum of 6 pipes, and takes 4 of the 8.
2. A child declares a maximum of 6 and asks for 3.
3. `try_to_grant` pretends to grant it, and `system_is_safe` looks for a finishing order.
4. No order exists, so the grant is undone and the child sleeps in `waiting_for_resources`.
5. The parent releases its 4 resources, and `sys_res_release` wakes the child.
6. The child tries again, and now the request is safe.

---

## 7. Where the code differs from the PRD

Know these before the viva, so nobody is caught out.

| PRD says | Code does | What to say |
| --- | --- | --- |
| Block layer has `block_read` and `block_write` | `block_get`, `block_put` and `block_write` | `block_get` both reads and reserves the buffer, so one function name covers it |
| File Handling and swap both use the block layer | Swap calls `ata_read` and `ata_write` directly | Swap needs no cache, because a page is written once and read once |
| ISR handles the event and then sends EOI | `pic_eoi` runs first, before the handler | A handler can lead to a process switch, and the EOI must not wait for that process to run again |
| `cachetest`, `schedtest` and `locktest` | No programs with these names. The checks live in the `disk`, `sched` and `aging` boot tests, and in `ipctest` | See [01-Feature-Coverage.md](01-Feature-Coverage.md) |
| Banker's covers pipes, queues and shared memory | `res_request` is a separate call. `pipe()`, `msgget()` and `shmget()` don't call it | Banker's is demonstrated by `banktest`. It is not enforced on real pipes |
| Spinlock busy-waits | `Spinlock` turns interrupts off, and panics if already locked | On one CPU, spinning could never end, because nothing else could run to unlock it |
| Inode stores timestamps | Stored, but as seconds since boot | ext2 isn't connected to the real-time clock. `rtc_read` exists, but only `/proc/date` uses it, for the `date` command |
| Kernel locks taken in a documented order | The order exists but is not written down in one place | See section 5.3 of this guide |
