# Team Tasks and PPT Plan

This file gives all 6 members their tasks, their slides, and why we use each thing. Each demo below was run on the real `os-demo.zip` image, and the expected output comes from those runs.

## Who does what

Write each member's name in the second column.

| # | Role | Member name | Owns | Code folder | Buddy who also explains it | Q&A file |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | Storage Hierarchy | | Disk layout, buffer cache, swap | `storage/` | File Handling | [QA-1](QA-1-Storage-Hierarchy.md) |
| 2 | Process Management | | PCB, scheduler, `fork`/`exec`, signals, IPC | `process/` | Resource Management | [QA-2](QA-2-Process-Management.md) |
| 3 | Memory Management | | Frames, paging, slab, Clock, shared memory | `memory/` | Storage Hierarchy | [QA-3](QA-3-Memory-Management.md) |
| 4 | Resource Management | | Locks, drivers, IRQs, Banker's, quotas | `resource/` | Integration | [QA-4](QA-4-Resource-Management.md) |
| 5 | File Handling | | VFS, ext2, devfs, permissions | `fs/` | Process Management | [QA-5](QA-5-File-Handling.md) |
| 6 | Integration | | Boot, HAL, syscalls, libc, shell, tests | `boot/`, `hal/`, `kernel/`, `user/` | Memory Management | [QA-6](QA-6-Integration.md) |

## Tasks for every member

1. Read your section below and your Q&A file.
2. Read [03-Codebase-and-System-Calls.md](03-Codebase-and-System-Calls.md), sections 1, 4 and 6.
3. Make your slides from your slide plan.
4. Run your demo yourself until you can do it without notes.
5. Teach your part to your buddy. Your buddy explains it back and runs your demo.
6. Learn your buddy's part, because anyone can be asked about any part.
7. Read [01-Feature-Coverage.md](01-Feature-Coverage.md), section 3, so a failing command doesn't surprise you.
8. Fill in your row of the laptop check-off table in the PRD.
9. Join the mock viva.

## Suggested presentation order

Integration goes first, because boot explains the whole system. Each person gets about 3 to 4 minutes.

| Order | Speaker | Slides |
| --- | --- | --- |
| 1 | Anyone | Shared opening: slides S1 to S4 |
| 2 | Integration | 6 slides |
| 3 | Process Management | 6 slides |
| 4 | Memory Management | 6 slides |
| 5 | Storage Hierarchy | 5 slides |
| 6 | File Handling | 6 slides |
| 7 | Resource Management | 6 slides |
| 8 | Anyone | Live demo, then shared closing: slides S5 to S8 |

## Shared slides

Agree on one person to make these.

| Slide | Title | What to put on it |
| --- | --- | --- |
| S1 | Title | Project name, member names, course, date |
| S2 | What we built | A 32-bit x86 OS in C++, about 17,000 lines, 49 system calls, running in QEMU |
| S3 | Architecture | The layer diagram from section 1 of the codebase guide |
| S4 | Team roles | The table of 6 roles, and how each role connects to the others |
| S5 | Test results | Clean build with 0 warnings, 110 of 110 checks passed, `e2fsck` clean |
| S6 | One command through the whole OS | The `cat notes.txt` walkthrough from the PRD, with each step's owner |
| S7 | Limits and future work | From each member's "be honest" list: no copy-on-write, no user threads, Round-Robin only, no network |
| S8 | Conclusion | What we learned, then questions |

**Tip for S7:** saying your limits first looks stronger than having the professor find them.

---

## Member 1: Storage Hierarchy

**In one line:** decides where data lives, so the computer is fast and nothing is lost.
**Real-life comparison:** your desk, your bookshelf and a storeroom.
**Code:** `storage/block.cpp`, `storage/swap.cpp`, `tools/mkimage.sh`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | The problem | Fast memory is small, big storage is slow | Pyramid: registers, cache, RAM, disk, with access times |
| 2 | Disk layout | MBR, GRUB, 16 MB kernel, 32 MB swap, 64 MB data | Bar showing the partitions |
| 3 | Buffer cache | 128 blocks of 4 KB, LRU, hit and miss | Flowchart of `block_get` |
| 4 | Write-through | Each write reaches the disk at once, with the trade-off against write-back | Two-column comparison |
| 5 | Swap | 8,192 slots, a bitmap, starts at 80%, stops at 70% | Slots with a bit under each |

### Why we use this

| Choice | Why |
| --- | --- |
| Buffer cache | Disk is thousands of times slower than RAM |
| LRU | Blocks used recently are likely to be used again |
| Write-through | Simple, and a crash loses less |
| 4 KB blocks | One page fits exactly in one block |
| Separate swap partition | Swap needs fixed slots, not files or folders |

### Your demo

| Type | You should see |
| --- | --- |
| `free` | `Swap: 0 KB used, 32768 KB total` |
| `swaptest` (takes about a minute) | `after 110 MB: swap used 18360 KB, 32760 pages evicted, 56551 page faults`, then `TEST-PASS swaptest` |
| `free` | Swap is nearly empty again, and the evicted count is high |

**Say:** "110 MB was used on a machine where the swapper starts at 80% of 128 MB, so pages had to go to disk and come back unchanged."

---

## Member 2: Process Management

**In one line:** gives each program a turn on the CPU, and saves exactly where it stopped.
**Real-life comparison:** a clinic with four token queues.
**Code:** `process/sched.cpp`, `process/process.cpp`, `process/switch.asm`, `process/signal.cpp`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | What is a process | Program vs process, and the PCB's main fields | PCB as a card |
| 2 | Process states | New, Ready, Running, Waiting, Terminated | State diagram |
| 3 | Scheduler | 4 priority queues, 20 ms time slice, aging after 1 second | Four queues feeding the CPU |
| 4 | Context switch | `schedule`, `switch_stacks`, TSS, `CR3` | Two stacks, with `ESP` moving |
| 5 | `fork`, `exec`, `wait`, `exit` | The shell runs `ls` | Parent and child timeline |
| 6 | IPC | Pipes, message queues, signals | Two processes with a pipe between them |

### Why we use this

| Choice | Why |
| --- | --- |
| Round-Robin | Everyone gets a turn |
| Priorities | Important work goes first |
| Aging | Stops starvation |
| Kernel not preempted | Removes a whole class of race bugs |
| `fork` + `exec` | The child can set up pipes before the program starts |
| Zombie state | The parent still needs the exit status |

### Your demo

| Type | You should see |
| --- | --- |
| `ps` | Columns `PID PPID UID PRI STATE CPU NAME` |
| `sleep 30 &` | `[started] process 4` (your number may differ) |
| `kill 4` | `[finished] process 4, status 143` |
| `sleep 100`, then Ctrl+C | The prompt comes back |
| `forktest` | `TEST-PASS forktest` |
| Zombie program in [QA-2](QA-2-Process-Management.md), then `ps` | A line such as `8 7 0 2 zombie 0 z` |
| Two `spin` programs in [QA-2](QA-2-Process-Management.md), then `ps` | Both have similar CPU numbers, such as `302` and `240` |

To force a process to stop, type `kill -9 4`.

---

## Member 3: Memory Management

**In one line:** gives each program its own private memory, and lets it use more than the RAM.
**Real-life comparison:** a hotel where every guest's key says "Room 1", and the front desk maps it to a real room.
**Code:** `memory/frame.cpp`, `memory/paging.cpp`, `memory/virtual_memory.cpp`, `memory/slab.cpp`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | Virtual memory | Each program has a private 4 GB space, with the top 1 GB for the kernel | Address space diagram |
| 2 | Paging | 4 KB pages, two levels, 10 + 10 + 12 bits | Address split, with an arrow to the directory, table and frame |
| 3 | Frame allocator | Bitmap and frame table, with reference counts | A row of bits |
| 4 | Page faults | Three cases: load from file, from swap, or a bad address | Flowchart of `handle_page_fault` |
| 5 | Clock replacement | The hand, the Accessed bit, second chance, 80% and 70% limits | Circle of frames |
| 6 | Kernel memory | Slab and `kmalloc` | One slab split into slots |

### Why we use this

| Choice | Why |
| --- | --- |
| Paging | Memory needn't be contiguous |
| Two-level tables | Unused regions need no table |
| Demand paging | Programs start fast, and unused pages use no RAM |
| Clock | Close to LRU, but needs only one bit |
| 80% / 70% gap | Stops constant swapping around one limit |
| Slab | The kernel makes many same-size objects |

### Your demo

| Type | You should see |
| --- | --- |
| `free` | `RAM:  2420 KB used, 128524 KB free` (numbers vary a little) |
| `free` again | The page fault count has gone up, showing demand paging |
| `cat /proc/meminfo` | `MemTotal: 130944 KB` |
| `swaptest` | `TEST-PASS swaptest` |
| `forktest` | `segmentation fault` lines, then `TEST-PASS forktest`. The faults are on purpose: they prove a bad access ends only that program |

---

## Member 4: Resource Management

**In one line:** decides who gets the hardware, and stops processes getting stuck on each other.
**Real-life comparison:** a traffic officer at a junction, plus a careful bank.
**Code:** `resource/sync.cpp`, `resource/banker.cpp`, `resource/ata.cpp`, `resource/keyboard.cpp`, `resource/vga.cpp`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | Interrupts | Why they're better than polling, and the IRQ table | Device, PIC, CPU, handler |
| 2 | A key press | IRQ 1 to the program that reads it | Five-step arrow |
| 3 | Disk driver | DMA, PIO fallback, sleeping until IRQ 14 | DMA vs PIO comparison |
| 4 | Locks | Mutex, Semaphore, CondVar, Spinlock on one CPU | When to use each one |
| 5 | Banker's Algorithm | Safe state, using the `banktest` numbers | Available, maximum and allocated tables |
| 6 | CPU quotas | Ticks per second, and the `quotatest` result | Two bars: 200 and 50 |

### Why we use this

| Choice | Why |
| --- | --- |
| Interrupts | The CPU doesn't waste time polling |
| DMA | The CPU doesn't copy every byte |
| Mutex | Waiters sleep, so they use no CPU |
| Banker's | Refuses unsafe requests before deadlock happens |
| Fixed lock order | Kernel locks can't form a cycle |
| CPU quotas | One process can't take everything |

### Your demo

| Type | You should see |
| --- | --- |
| `ipctest` | `shared counter: 12000`, which is exactly 4 × 3,000 |
| `banktest` | `unsafe request is waiting, as it should`, then `request granted once it was safe` |
| `quotatest` | `cpu ticks without quota: 200, with quota: 50` (or 60) |
| `keytest`, press `a`, press `q` | `97`, the code for `a` |

**Be ready for this:** Banker's is a separate set of calls, and it doesn't control real `pipe()` calls. Say so before you're asked.

---

## Member 5: File Handling

**In one line:** turns numbered disk blocks into named files, folders and permissions.
**Real-life comparison:** a library. The catalogue card is the inode, the shelf list is the directory, and your borrowing slip is the open-file entry.
**Code:** `fs/vfs.cpp`, `fs/ext2.cpp`, `fs/file.cpp`, `fs/sys_file.cpp`, `fs/devfs.cpp`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | ext2 on disk | Superblock, group table, bitmaps, inode table, data | Bar of the disk layout |
| 2 | Inode | Fields, and 12 direct, 1 indirect and 1 double-indirect pointers | Inode with pointer arrows |
| 3 | Directories | A name maps to an inode number. The name isn't in the inode | Table of names and numbers |
| 4 | VFS | One `read()`, four filesystems, C++ virtual functions | `read()` splitting to ext2, devfs, tmpfs, procfs |
| 5 | Opening a file | FD table, then `File`, then `Inode` | Three boxes, parent and child sharing one `File` |
| 6 | Permissions | rwx for owner, group, other, and `vfs_check_permission` | `rwxr-xr--` split into three |

### Why we use this

| Choice | Why |
| --- | --- |
| Real ext2 format | Linux `e2fsck` can check our work |
| Inode pointers | Small files are fast, and big files still work |
| VFS | The same `read()` works on every filesystem |
| Two levels of file tables | Parent and child share a file position |
| Kernel buffer in `sys_read` | Avoids a page-fault deadlock |
| tmpfs and procfs | Fast temporary files, and live system data |

### Your demo

| Type | You should see |
| --- | --- |
| `ls -l /etc` | `-rw------- root 177 shadow` |
| `echo saved > /tmp/a.txt`, `echo more >> /tmp/a.txt`, `cat /tmp/a.txt` | `saved`, then `more` |
| `wc < /tmp/a.txt` | `2 2 11` |
| `fstest` | `TEST-PASS fstest` |
| `permtest` | `TEST-PASS permtest` |
| `exit`, log in as `student`, `cat /etc/shadow` | `cat: /etc/shadow: permission denied` |
| `df` | `ext2 65536 ...`, the disk's size and use in KB |

---

## Member 6: Integration

**In one line:** starts everything in the right order, and gives programs one safe way into the kernel.
**Real-life comparison:** the front desk of a college office.
**Code:** `boot/boot.asm`, `kernel/main.cpp`, `kernel/syscall.cpp`, `hal/`, `user/libc.cpp`, `user/bin/sh.cpp`

### Your slides

| # | Title | What to put on it | Draw this |
| --- | --- | --- | --- |
| 1 | Boot | BIOS, GRUB, `_start`, `kmain` | Timeline |
| 2 | 10-step start-up | The init order, and why it matters | Numbered list with arrows |
| 3 | Rings and the GDT | Ring 0 and ring 3, plus the TSS | Two rings, with the `int 0x80` door |
| 4 | System call path | From `read()` to `iret` | Nine-step flow from section 4.1 of the codebase guide |
| 5 | User space | libc, `init`, `login`, shell, editor, compiler | Stack of programs |
| 6 | Testing | 110 automatic checks, `make test`, `e2fsck` | Test results table |

### Why we use this

| Choice | Why |
| --- | --- |
| GRUB | A tested bootloader that gives us the memory map |
| System call table | One safe entry into the kernel |
| HAL | Hardware details stay in one place |
| Fixed boot order | Each stage needs the ones before it |
| Panic | Stops before data gets corrupted |
| QEMU | Same virtual PC on every laptop, and Windows stays safe |

### Your demo

| Type | You should see |
| --- | --- |
| Start the OS | `Disk: using DMA`, `127 MB RAM, 125 MB free.`, `BOOT-OK`, login prompt |
| Wrong password | `Login incorrect` |
| `root` / `root`, then `help` | The command list |
| `ls /bin \| wc` | `43 43 244` |
| `run /examples/fibonacci.cpp` | `0 1 1 2 3 5 8 13 21 34` |
| Invalid system call program in [QA-6](QA-6-Integration.md) | `-38` |

---

## Live demo order for the whole team

If you have one shared demo, use this order. It takes about 5 minutes without `swaptest`.

| # | Who | Type | Point to make |
| --- | --- | --- | --- |
| 1 | Integration | Start the OS, log in as `root` | Boot and login work |
| 2 | Integration | `ls /bin \| wc` | Shell, pipes, `fork`, `exec` |
| 3 | Process | `sleep 30 &`, `ps`, then `kill N` | Process states and signals |
| 4 | Memory | `free` | Page faults so far |
| 5 | File | `echo saved > /tmp/a.txt`, `cat /tmp/a.txt` | Files and redirection |
| 6 | File | `ls -l /etc` | Permissions |
| 7 | Resource | `ipctest` | Shared memory and semaphores |
| 8 | Resource | `banktest` | Deadlock avoidance |
| 9 | Integration | `run /examples/fibonacci.cpp` | Compiled inside our own OS |
| 10 | File | `exit`, log in as `student`, `cat /etc/shadow` | Security works |

## Before the presentation

| Done | Task |
| --- | --- |
| [ ] | QEMU installed on every laptop, and `run.bat` tested |
| [ ] | Instructor has answered the 4 open questions |
| [ ] | Slides tried on the projector |
| [ ] | Demo video recorded as a backup |
| [ ] | `os-demo.zip` on a second laptop |
| [ ] | Everyone has run their demo |
| [ ] | Mock viva done, with anyone asked about any part |
