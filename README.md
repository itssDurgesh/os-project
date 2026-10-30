# OS Project

A small operating system written in C++ for 32-bit x86. It boots through GRUB,
has login, processes, virtual memory with swap, a real ext2 filesystem, pipes,
signals and a shell. It runs inside the QEMU emulator.

## Build and run

You need Ubuntu (WSL is fine) with these tools:

```bash
sudo apt install -y build-essential g++ gcc-multilib nasm qemu-system-x86 gdb grub-pc-bin xorriso e2fsprogs fdisk
```

Then, in this folder:

| Command | What it does |
| --- | --- |
| `make` | Builds the kernel, the programs and the disk image |
| `make run` | Starts the OS in a QEMU window |
| `make test` | Runs all the automatic tests (takes about 4 minutes) |
| `make dist` | Makes `build/os-demo.zip` for Windows laptops |
| `make clean` | Deletes everything that was built |

## Logging in

| User | Password | Notes |
| --- | --- | --- |
| `root` | `root` | Can do anything |
| `student` | `student` | A normal user, limited by file permissions |

These are demo passwords for the assignment. Type `exit` to log out.

Commands: `ls`, `ls -l`, `cat`, `echo`, `mkdir`, `rm`, `rmdir`, `wc`, `pwd`, `cd`,
`whoami`, `chmod`, `ps`, `kill`, `sleep`, `free`, and `exit`.

The shell also has pipes (`ls / | wc`) and redirection (`echo hi > a.txt`,
`cat < a.txt`, `echo more >> a.txt`). Ctrl+C stops the running program.

Changes on the disk are thrown away when QEMU closes, so each `make run` starts
clean. Remove `-snapshot` from the Makefile to keep them.

## Where everything is

Each folder belongs to one teammate from the PRD.

| Folder | Owner | What's inside |
| --- | --- | --- |
| `boot/` | Integration | The first code that runs, and the GRUB settings |
| `hal/` | Integration | GDT, interrupts, timer, serial port |
| `kernel/` | Integration | `kmain`, `kprintf`, panic, system call table, tests |
| `memory/` | Memory Management | Frames, paging, slab, `kmalloc`, virtual memory, shared memory |
| `process/` | Process Management | PCB, scheduler, `fork`/`exec`, signals, message queues |
| `resource/` | Resource Management | Keyboard, screen, disk driver, locks, Banker's Algorithm |
| `storage/` | Storage Hierarchy | Partitions, buffer cache, swap |
| `fs/` | File Handling | VFS, ext2, devfs, open files, pipes |
| `user/` | Integration | Small C library, and the programs in `user/bin/` |
| `tests/` | Everyone | The script that runs every test |

## A few words about the design

- **One CPU, and the kernel is not preempted.** Kernel code only stops when it
  chooses to sleep. This removes most race conditions.
- **Boot order.** `kernel/main.cpp` shows the 10 steps from the PRD, in order.
- **Virtual functions.** `Inode` and `File` are base classes, so the kernel can
  call `inode->read()` without caring which filesystem the file is on.
- **Deadlock rule.** The kernel never touches user memory while it holds a
  filesystem lock. See the comment at the top of `memory/virtual_memory.cpp`.
