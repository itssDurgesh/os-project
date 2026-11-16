# Do We Cover What the Professor May Ask?

## Short answer

**Yes for OS concepts and for the commands people type most. There are still some gaps.**

All six subsystems work, all 53 system calls exist, and the automatic tests pass. A professor can type normal Linux commands such as `kill -9`, `ls -la`, `wc -l`, `tail`, `sort`, `find`, `stat`, `lsof`, `date`, `df` and `top`, and use quotes, `;`, `&&` and `*` in the shell. They can also watch the kernel give memory to a new process and take it back (`spawn`, `mem`, `pmap`, `kill`, `memlog on`).

The OS now runs inside the WSL Ubuntu terminal with `make run`. No window opens: QEMU still emulates the PC, but in the background. Read section 3 before any demo. The biggest risk isn't a missing command, though: **the terminal version has only been checked on the build laptop** (see section 8).

## How this was checked

Nothing here comes from the docs alone. Each item was run on 1 November 2026, on the code in commit `9e9ae97`.

| Check | Result |
| --- | --- |
| Clean build | 0 compiler warnings |
| `make test` | 120 passed, 0 failed (15 November 2026) |
| Linux `e2fsck` on the disk after writing from our OS | Clean |
| About 300 commands typed into the running OS | Results below |
| Stress tests: 240 MB program, fork bomb | Results in section 6 |
| 15 November 2026: process memory commands and running in the terminal | 110 old tests pass, plus 8 new `procmem` checks. A full session was typed into `make run` |

---

## 1. What works

| The professor may ask you to... | Type this | Result |
| --- | --- | --- |
| Log in as different users | `root`/`root`, `student`/`student` | Works |
| List files with permissions | `ls`, `ls -l /etc`, `ls -la` | Works |
| Move between folders | `cd /home`, `cd ..`, `cd ~`, `cd`, `pwd` | Works |
| Create and delete folders | `mkdir -p /tmp/a/b`, `rm -r /tmp/a` | Works |
| Create, read, copy, move, delete files | `touch`, `cat`, `cp`, `mv`, `rm` | Works |
| Copy a whole folder | `cp -r /tmp/a /tmp/b` | Works |
| Write and append to a file | `echo "saved text" > /tmp/a.txt`, then `>>` | Works |
| Type a file from the keyboard | `cat > /tmp/a.txt`, then Ctrl+D | Works |
| Edit a file | `edit /tmp/a.txt` | Works |
| Use a pipe | `cat /etc/passwd \| grep root \| wc -l` | Works |
| Run several commands | `cd /tmp ; ls`, `mkdir a && cd a` | Works |
| Use wildcards | `ls /etc/*.txt` | Works |
| Change permissions | `chmod 600 file`, `chmod +x file` | Works |
| Show a user being refused | As `student`: `cat /etc/shadow` | `permission denied` |
| List processes | `ps`, `top`, `cat /proc/processes` | Works, `ps` shows each one's memory (MEM KB) and RAM |
| Run in the background | `sleep 30 &` | Works |
| Create a process and see its memory | `spawn sleep 60` (or `sleep 60 &`) | Table: PID, code, data, heap, stack, total and RAM in KB. It keeps running, and `ps` lists it |
| See the memory of every process | `mem` | Table per process, then RAM and swap bars |
| See one process's memory map | `pmap 5` | Each part, its addresses, size, RAM and swap |
| Delete a process | `kill 5` | Table: PID, name, signal, RAM freed |
| Watch memory for every command | `memlog on`, then any command | A `[created]` table row when it starts, an `[ended]` line with RAM freed |
| End a process | `kill 4`, `kill -9 4` | Status 143, status 137 |
| Stop a program | Ctrl+C | Works |
| Show open files | `lsof`, `lsof 5` | Every descriptor: file, device or pipe |
| Show resources per process | `resources` | Files, shared memory, CPU limit, Banker's resources |
| Show a file's inode | `stat /etc/hello.txt` | Inode, size, links, mode, owner |
| Show memory and disk use | `free`, `df`, `cat /proc/meminfo` | Works |
| Turn the OS off | `shutdown` as root | Back to the Ubuntu prompt |
| Show the date and time | `date` | Shows UTC, from the hardware clock |
| Show the OS name | `uname -a` | `OS Project 1.0 i386` |
| Search files | `grep -i root /etc/passwd`, `find / -name "*.txt"` | Works |
| Show the first or last lines | `head -n 2 file`, `tail -n 2 file` | Works |
| Sort a file | `sort file`, `sort -r file`, `sort -n file` | Works |
| Count lines, words, bytes | `wc file`, `wc -l file` | Works |
| See old commands | `history`, or the Up arrow | Works |
| Compile and run a C++ program | `run /tmp/a.cpp` | Works for the supported subset |
| Use `#include <iostream>` and `using namespace std;` | Normal C++ start | Accepted |
| Use `fork` in your own program | `__syscall(1, 0, 0, 0)` | Works, see [QA-2](QA-2-Process-Management.md) |
| Show a real zombie | Program in [QA-2](QA-2-Process-Management.md) | `ps` shows `zombie` |
| Show Round-Robin sharing | Two `spin` programs, then `ps` | CPU ticks 302 and 240 after 4 seconds |
| Show a crash doesn't kill the OS | Divide by zero, or a bad address | `process ended`, and the shell carries on |
| Show a fork bomb doesn't kill the OS | Program in section 6 | `fork` fails at 64 processes |
| Call an invalid system call | `__syscall(999, 0, 0, 0)` | Returns -38 |
| Show swapping | `swaptest` | Passes, with real swap numbers |
| Show IPC | `ipctest` | `shared counter: 12000` |
| Show deadlock avoidance | `banktest` | Unsafe request waits, then is granted |
| Show CPU limits | `quotatest` | Ticks limited to about 50 to 60 |

---

## 2. OS topics, compared with a usual syllabus

| Topic | Do we have it? | How to show it, or what to say |
| --- | --- | --- |
| Booting | Yes | Boot messages. See the boot trace in the codebase guide |
| User and kernel mode | Yes | `forktest`, invalid system call demo |
| System calls | Yes, 53 | Full table in the codebase guide |
| Process states | Yes | `ps`, `top`, `cat /proc/processes` |
| Process creation | Yes | `spawn sleep 60`, or the `fork` demo |
| Memory given to a process | Yes | `spawn`, `mem`, `pmap`, `memlog on` |
| Zombies and orphans | Yes | Zombie demo. Orphans are adopted by PID 1 |
| CPU scheduling | **Partly** | Round-Robin with priorities and aging only. FCFS, SJF and others aren't there |
| Changing priority | **No** | No `nice`. User programs all run at priority 2 |
| Threads | **Partly** | Kernel threads only. No user threads |
| Race conditions and locks | Yes | `ipctest` |
| Semaphores | Yes | `ipctest` |
| Deadlock avoidance | **Partly** | `banktest` runs one fixed scenario, and it doesn't control real pipes |
| IPC | Yes | Pipes, message queues, shared memory, signals |
| Paging | Yes | `free` page fault count |
| Segmentation | **Basic only** | Flat segments in the GDT. Paging does the real work |
| Demand paging | Yes | `free` page fault count |
| Page replacement | **Partly** | Clock only. FIFO and LRU aren't selectable |
| Swapping | Yes | `swaptest` |
| Copy-on-write | **No** | `fork` copies memory |
| Filesystems | Yes | ext2, devfs, tmpfs, procfs |
| Inodes and directories | Yes | `stat file` shows the inode number, links and mode |
| Open file table | Yes | `lsof` |
| File permissions | Yes | `ls -l`, `chmod`, `student` user |
| Hard and symbolic links | **No** | No `ln` |
| Disk scheduling | **No** | No FCFS, SCAN or SSTF. Requests are served one at a time |
| Interrupts and DMA | Yes | `Disk: using DMA`, `keytest` |
| Device files | Yes | `ls /dev` |
| Users and security | Yes | Login, salted SHA-256, permissions |
| Multi-core | **No** | Cut from scope |
| Networking | **No** | Cut from scope |
| Graphics | **No** | VGA text only. The framebuffer was not built |

---

## 3. What still behaves differently from Linux

| They type | What happens | Do this instead |
| --- | --- | --- |
| `cp a b folder` | `usage: cp [-r] <from> <to>` | Copy one file at a time |
| `mv folder newname` | `folders can't be moved` | `cp -r folder newname`, then `rm -r folder` |
| `mv a b folder` | `usage: mv <from> <to>` | Move one file at a time |
| `kill -KILL 5` | `kill: invalid argument` | `kill -9 5` |
| `ls /*/hello.txt` | No match, because wildcards only work in the last part of a path | `ls /etc/*.txt` |
| `a \|\| b` | `sh: syntax error` | Not supported |
| `history \| grep ls` | `no such file or directory`, because `history` is inside the shell | Type `history` on its own |
| `x=5`, `$x`, `$HOME` | Treated as plain text | The shell has no variables |
| `sh script.sh` | The shell ignores the file, and starts a new interactive shell | There are no shell scripts |
| `ls -R`, `grep -r` | `unknown option` | `find` |
| `ps aux`, `free -h` | Options are ignored, so you get the normal output | `ps`, `free` |
| `chmod u+x file` | `chmod: bad mode u+x` | `chmod +x file` changes it for everyone, or use `chmod 744 file` |

---

## 4. Commands that don't exist

| Group | Missing | Why |
| --- | --- | --- |
| Files | `ln`, `chown`, `du` | `ln` and `chown` need new system calls |
| System | `nice`, `env`, `which`, `man` | `nice` needs a new system call |
| Users | `useradd`, `passwd` | Accounts are made when the disk image is built |
| Power | `reboot` | Use `shutdown`, then `make run` again |
| Tools | `gcc`, `nano`, `vi`, `python` | Use `c++` and `edit` |

The commands that exist are: `banktest`, `c++`, `cat`, `chmod`, `clear`, `cp`, `date`, `df`, `echo`, `edit`, `find`, `forktest`, `free`, `fstest`, `grep`, `head`, `ipctest`, `keytest`, `kill`, `login`, `ls`, `lsof`, `mem`, `mkdir`, `mousetest`, `mv`, `permtest`, `pmap`, `ps`, `pwd`, `quotatest`, `resources`, `rm`, `rmdir`, `run`, `sh`, `shutdown`, `sigtest`, `sleep`, `sort`, `spawn`, `stat`, `swaptest`, `tail`, `tmptest`, `top`, `touch`, `uname`, `wc`, `whoami`, plus the shell's own `cd`, `help`, `history`, `memlog` and `exit`.

---

## 5. What the C++ compiler can't do

**Works:** `int`, `char`, `bool`, `string`, 1D arrays, functions, recursion, `if`, `for`, `while`, `do`, `cin`, `cout`, `getline`, `#include <iostream>`, `using namespace std;`

| Tested | Result |
| --- | --- |
| `printf` | `error: this function doesn't exist` |
| `struct` or `class` | `error: expected a function or a variable here` |
| Pointers | `error: expected a variable name` |
| `vector`, `double`, `float`, `long` | `error: this variable doesn't exist` |
| `switch` | `error: expected ; at the end of this line` |
| 2D arrays | `error: expected ; after the declaration` |

Lab exercises that use structs, 2D arrays or decimals won't compile in the OS. A Banker's or scheduling simulation can still be written with 1D arrays.

---

## 6. Stress test results

| Test | Result | Good or bad? |
| --- | --- | --- |
| Divide by zero in a program | `CPU exception at ..., process ended` | Good: the OS stays up |
| Writing to a bad address | `segmentation fault at ..., process ended` | Good: the OS stays up |
| 240 MB program (RAM is 128 MB, swap 32 MB) | `out of memory`, then the program ended. Memory returned to normal | Good: the OS stays up |
| Fork bomb | `fork` fails at 64 processes. Ctrl+C stops them all, and the shell carries on | Good: the OS stays up |
| Fork bomb in the background (`/tmp/bomb &`) | The OS stays up, but background programs ignore Ctrl+C, and the shell prints `too many processes` for every command | **Weak: you have to restart the OS** (Ctrl+A then X, then `make run`) |

**Fork bomb program:**

```cpp
int main() {
    while (true) {
        __syscall(1, 0, 0, 0);
    }
    return 0;
}
```

Before this was fixed, a fork bomb halted the whole OS with `KERNEL PANIC: no memory for a kernel stack`. Now `fork` returns `-EAGAIN`, which is -11, at the limit.

---

## 7. PRD acceptance tests, mapped to what exists

The PRD names some test programs that don't exist under those names.

| PRD test | What we have | Where it runs |
| --- | --- | --- |
| `cachetest` | `disk` boot test: `disk reads on the second read: 0` | `make test` only |
| `schedtest` | `sched` and `aging` boot tests | `make test` only |
| `forktest` | `forktest` | In the OS |
| `swaptest` | `swaptest` | In the OS |
| `locktest` | Inside `ipctest` | In the OS |
| `banktest` | `banktest` | In the OS |
| `fstest` | `fstest`, `permtest` | In the OS |
| Boot on six laptops | **1 of 6 done** | See section 8 |
| Invalid system call | Inside `forktest`, and the `__syscall(999, ...)` demo | In the OS |
| Test panic | `panic` boot test | `make test` only |

`make test` needs Ubuntu and the source code, so it can't run from `os-demo.zip`.

---

## 8. Things not verified

| Item | Status |
| --- | --- |
| `make run` in the WSL Ubuntu terminal | **Passed on the build laptop** on 15 November 2026: login, `spawn`, `ps`, `mem`, `pmap`, `lsof`, `resources`, `kill`, `memlog`, `shutdown`. The other 5 laptops still need to try it |
| `run.bat` with QEMU for Windows (backup only) | **Passed on the build laptop** on 2 November 2026, with QEMU 11.1.0: login, keyboard, `free`, a pipe and the C++ compiler. The automatic tests and heavier demos such as `swaptest` have only run on QEMU 10.2.1 |
| VirtualBox | Not tested |
| Running on all 6 laptops | 1 of 6 done. The other 5 must still fill in the laptop check-off table in the PRD |
| Projector display | Not tested |

**The PRD's open questions for the instructor still have no answer:**

1. Is GRUB allowed, or must the bootloader be written by hand?
2. Is working code graded, or mainly the specs?
3. May we study xv6 and use AI help for the code?
4. May the framebuffer stay as a stretch goal?

---

## 9. What to do next

In order of risk:

| # | Task | Why |
| --- | --- | --- |
| 1 | Run `make run` in the WSL terminal on the other 5 laptops | The OS must run on everyone's laptop |
| 2 | Ask the instructor the 4 open questions | GRUB could affect marks |
| 3 | Add `nice`, which needs a new system call | Lets you show priorities from the shell |
| 4 | Add `schedtest`, `cachetest` and `locktest` as real commands | Matches the PRD's test names, and works in a live demo |
