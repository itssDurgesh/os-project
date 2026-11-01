# OS Project

A small operating system written in C++ for 32-bit x86. It boots through GRUB,
has login, processes, virtual memory with swap, a real ext2 filesystem, pipes,
signals and a shell. It runs inside the QEMU emulator.

To set this up on your own laptop, see [SETUP.md](SETUP.md). The project plan, the
team's tasks and the viva preparation are in [docs](docs/README.md).

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
| `make test` | Runs all the automatic tests (takes about 6 minutes) |
| `make dist` | Makes `build/os-demo.zip` for Windows laptops |
| `make clean` | Deletes everything that was built |

## Logging in

| User | Password | Notes |
| --- | --- | --- |
| `root` | `root` | Can do anything |
| `student` | `student` | A normal user, limited by file permissions |

These are demo passwords for the assignment. Type `exit` to log out.

Type `help` in the shell to see everything.

| Area | Commands |
| --- | --- |
| Files | `ls`, `cat`, `cp`, `mv`, `rm`, `touch`, `mkdir`, `rmdir`, `head`, `tail`, `grep`, `sort`, `wc`, `find` |
| Editing | `edit file.txt` |
| C++ | `c++ file.cpp -o name`, `run file.cpp` |
| System | `ps`, `top`, `free`, `df`, `kill`, `sleep`, `whoami`, `chmod`, `date`, `uname`, `clear`, `history` |

The usual options work: `ls -la`, `kill -9 5`, `wc -l`, `head -n 5`, `tail -n 5`,
`grep -i`, `grep -v`, `grep -n`, `sort -r`, `sort -n`, `chmod +x`, `mkdir -p`,
`rm -r`, `rm -f`, `cp -r`.

The shell has pipes (`ls / | wc`), redirection (`echo hi > a.txt`, `cat < a.txt`,
`echo more >> a.txt`), background programs (`sleep 30 &`), quotes (`echo "a  b"`),
several commands on a line (`cd /tmp ; ls`, `mkdir a && cd a`), wildcards
(`ls /etc/*.txt`) and `~` for your home folder. Press the Up and
Down arrows for old commands, Left and Right to move along a line, Backspace,
Delete, Home and End to edit it, and Ctrl+C to stop a program. `keytest` shows the
code for each key, which helps if a key doesn't seem to work.

Try `cat /proc/meminfo` and `cat /proc/processes`. Files in `/tmp` live in RAM.

One program can't take the whole OS down by starting too many processes: `fork`
fails after 64, and Ctrl+C stops them all.

## Writing C++ inside the OS

```text
edit hello.cpp
run hello.cpp
```

In the editor, use the arrow keys to move, **Ctrl+S** to save and **Ctrl+Q** to quit.
`run` compiles and starts the program. To keep the program, use the compiler
yourself:

```text
c++ hello.cpp -o hello
./hello
```

There are examples in `/examples`, such as `run /examples/fibonacci.cpp`.

The compiler is `user/bin/c++.cpp`. It writes real x86 machine code, with no
assembler or linker. It understands:

- `int`, `char`, `bool`, `string`, `void`, `const`, arrays and global variables
- functions, recursion, prototypes, and arrays as parameters
- `if`, `else`, `while`, `do`, `for`, `break`, `continue`, `return`, and `? :`
- `cout`, `cin`, `endl`, `getline`
- `s.length()`, `s.size()`, `abs`, `min`, `max`, `rand`, `to_string`, `stoi`, `exit`

It's a small subset of C++: there are no classes, structs, pointers, references,
templates, `vector`, `switch` or file streams. `#include` lines are skipped,
because everything it supports is built in. Each program is compiled from one file.

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
