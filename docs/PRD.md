# OS Project — Product Requirements Document

Oct 30, 2026 · @Anshul singhal

## Overview

This project builds a custom operating system from scratch, designed and specified by a 6-member team where each member owns one core subsystem. A single developer will implement the entire codebase. The OS must correctly manage hardware resources, handle user processes, store and retrieve data, and expose a stable interface that ties all subsystems together.

**Project goals:**

1. Produce a bootable, functional OS with all six subsystems operational.
2. Keep each subsystem's design self-contained so that one developer can implement them sequentially.
3. Deliver a codebase that is readable, documented, and integration-ready from the first compile.
4. Make sure every teammate can explain every part, not only their own.

**Platform baseline:** 32-bit x86, single core, written in C++, running inside the QEMU emulator. The OS is never booted directly on a laptop (see Running on Team Laptops).

## Team Structure & Roles

Six members each specify one subsystem. One developer codes the entire project.

| # | Member Role | Responsibility in This PRD |
| --- | --- | --- |
| 1 | Storage Hierarchy | Define storage tiers, caching strategy, and disk layout |
| 2 | Process Management | Define process lifecycle, scheduling, and IPC |
| 3 | Memory Management | Define address space, paging, and allocation strategy |
| 4 | Resource Management | Define CPU, I/O, and device arbitration rules |
| 5 | File Handling | Define filesystem structure, file ops, and permissions |
| 6 | Integration | Define interfaces between all five subsystems above |

**The sole developer** reads this PRD in full and implements all six subsystems sequentially, treating each section's spec as the contract for that module.

**Owners and buddies:** Every piece has one owner. Each part also has one buddy, the teammate whose part touches it most, who must be able to explain it.

| # | Role | Owns | Explained by buddy |
| --- | --- | --- | --- |
| 1 | Storage Hierarchy | Disk layout, partition table, block layer, buffer cache, swap device | File Handling |
| 2 | Process Management | PCB, scheduler, context switch, `fork/exec/wait`, ELF loader, signals, pipes, message queues | Resource Management |
| 3 | Memory Management | Frames and frame table, paging, slab, page faults, Clock, shared memory | Storage Hierarchy |
| 4 | Resource Management | Sync primitives, IRQ table, keyboard, ATA and VGA drivers, quotas, Banker's | Integration |
| 5 | File Handling | VFS, ext2, devfs, FD and open-file tables, permissions | Process Management |
| 6 | Integration | Boot, GDT/TSS, HAL, panic, syscall table, init order, libc, shell, test programs | Memory Management |

**How we learn each other's parts:**

- Each subsystem section ends with a one-page explainer in the same format.
- Shared Walkthroughs follows one real action through several parts. Everyone should learn these.
- In teach-back sessions, each member teaches their part to their buddy. The buddy explains it back and also runs that part's demo.
- Before submission, hold a mock viva where anyone can be asked about any part.
- Each subsystem's public header (`include/*.h`) has a short comment on every function, and the buddy reviews each change to that part.

## Storage Hierarchy

The OS uses a four-tier storage model: CPU registers → L1/L2/L3 cache → main memory (RAM) → persistent storage (disk). Each tier has strict access-time and size constraints that the developer must respect in implementation.

| Tier | Medium | Managed by | Access time (approx.) |
| --- | --- | --- | --- |
| 1 | CPU Registers | Hardware / compiler | < 1 ns |
| 2 | CPU Cache (L1–L3) | Hardware (transparent) | 1–10 ns |
| 3 | Main Memory (RAM) | Memory Manager subsystem | 50–100 ns |
| 4 | Persistent Storage (disk) | File Handling + Storage subsystem | 0.1–10 ms |

**Design requirements:**

- Bootloader must locate the OS kernel on disk and load it into RAM before control is handed to the kernel. The plan is to use GRUB through Multiboot (see Open Questions for the Instructor).
- The OS must define a contiguous kernel memory region (reserved, never paged out).
- Disk layout must include: MBR / boot sector, kernel image partition, swap partition, and user data partition.
- A disk block size of 4 KB is the baseline; the File Handling subsystem builds on top of this. Block numbers count from the start of each partition, so the MBR at disk sector 0 and the filesystem's block 0 never clash.
- The OS targets a single core. On x86, hardware keeps the CPU caches coherent, so no OS code is needed for this.
- Block layer: a small interface (`block_read`, `block_write`) over the disk driver. File Handling and swap both use it.
- Disk buffer cache: recently used disk blocks are kept in RAM. This is the cache the OS controls, because Tiers 1 and 2 are managed by hardware. Policy: write-through, so every write reaches the disk straight away. It is slower, but simple to explain and loses less data in a crash.
- Swap space on disk acts as Tier 3 overflow; the Memory Manager starts evicting pages when RAM usage exceeds 80% and stops when usage falls to 70%. The gap prevents constant swapping around a single limit.

**Deliverable from this member:** A written storage layout diagram (block sizes, partition table, address ranges) and a cache policy decision (write-through or write-back) with justification.

### Explainer: Storage Hierarchy

**What problem it solves:** Fast memory is small, and big storage is slow. This part decides where data lives, so the computer feels fast and nothing is lost when the power goes off.

**Real-life comparison:** Your desk, your bookshelf and a storeroom. What you're using stays on the desk, and everything else is further away but there is more room.

**Main data structures:**

- Partition table: where each disk area starts and ends.
- Buffer cache entry: block number, block data, and whether it's in use.
- Swap map: one bit per swap slot, free or used.

**Step-by-step example: reading the same block twice**

1. File Handling asks the block layer for block 500.
2. The buffer cache has no copy, so the disk driver reads it and the process sleeps.
3. The disk interrupt wakes the process, and the copy stays in the cache.
4. The next read of block 500 comes from RAM and never touches the disk.

**Calls:** disk driver and locks (Resource Management).
**Called by:** File Handling, and Memory Management for swap.

**Likely questions:**

1. *Why have a buffer cache?* Disk is thousands of times slower than RAM, so avoiding repeat reads saves a lot of time.
2. *Write-through or write-back?* Write-through writes to disk at once. Write-back waits, which is faster but loses more in a crash. We chose write-through.
3. *Why is swap a separate partition?* Swap needs no folders or file names, just fixed-size slots, so it's simpler and faster.
4. *Why 4 KB blocks?* They match the 4 KB page size, so one page fits exactly in one swap slot.
5. *Who keeps CPU caches in sync?* The hardware. Our OS runs on one core, and x86 handles coherency itself.

## Process Management

The OS treats a process as the unit of execution: a program loaded into memory with its own address space, resources, and scheduling state. Threads are lightweight processes sharing the same address space. For now, threads exist only inside the kernel; user-level threads are a stretch goal, so no syscall creates them.

**Process lifecycle states:**

1. **New** — process is being created (PCB allocated, memory assigned).
2. **Ready** — process is in the run queue, waiting for CPU time.
3. **Running** — process is currently executing on a CPU core.
4. **Waiting** — process is blocked on I/O or an event; not consuming CPU.
5. **Terminated** — process has exited; resources are being freed. The PCB stays as a zombie, holding the exit status, until the parent collects it with `wait()`. Orphaned processes are adopted by PID 1.

**Process Control Block (PCB) must store:** Process ID (PID), parent PID, UID, GID, CPU register snapshot, kernel stack pointer, memory map pointer, open file table pointer, current directory, scheduling priority, state, exit status, pending signals, and CPU time used.

**Scheduling algorithm:** Round-Robin with priority levels (0 = highest, 3 = lowest). Each priority level has its own ready queue. The scheduler picks from the highest non-empty queue and gives each process a fixed time quantum (default: 20 ms). A process that uses its full quantum goes to the back of its queue. A running process that blocks moves to Waiting; when unblocked it returns to its priority's ready queue.

**Preventing starvation (aging):** A process left in a ready queue for more than 1 second moves up one priority level. It returns to its original level after it next runs.

**Kernel preemption rule:** Code running in the kernel is not preempted; it gives up the CPU only when it blocks voluntarily. A process that is due to be switched out is switched when it next returns to user mode. Each process has its own kernel stack. This rule removes a whole class of race conditions.

**Inter-Process Communication (IPC):**

- Pipes: unidirectional byte streams between two processes.
- Message queues: kernel-managed FIFO queues with typed messages.
- Shared memory: two or more processes map the same physical pages (coordinate via semaphores).
- Signals: asynchronous notifications (e.g., SIGTERM, SIGKILL).

**System calls exposed:** `fork()`, `exec()`, `exit()`, `wait()`, `getpid()`, `kill()`, `sleep()`, `sigaction()`, `sigreturn()`, `getuid()`, `getgid()`, `pipe()`, `msgget()`, `msgsnd()`, `msgrcv()`.

**Deliverable from this member:** State-transition diagram for process lifecycle, PCB field list, and scheduling queue design with priority definitions.

### Explainer: Process Management

**What problem it solves:** Many programs want to run, but there is only one CPU. This part gives each program a turn and saves exactly where it stopped.

**Real-life comparison:** A clinic with four token queues. Urgent queues go first, each patient gets a fixed time slot, and each patient's file records where the doctor left off.

**Main data structures:**

- PCB: everything the OS knows about one process.
- Four ready queues: one per priority level.
- Wait queues: processes sleeping until an event happens.

**Step-by-step example: the shell runs `ls`**

1. The shell calls `fork()`, which makes a child copy of it.
2. The child calls `exec()`, which replaces the copy with the `ls` program.
3. The shell calls `wait()` and moves to Waiting.
4. `ls` finishes and calls `exit()`, and its PCB stays as a zombie.
5. The shell wakes, reads the exit status, and the PCB is freed.

**Calls:** Memory Management for address spaces, File Handling for `exec` and file tables, Resource Management for the timer and locks.
**Called by:** the syscall layer and the timer interrupt.

**Likely questions:**

1. *Process or thread?* A process has its own memory. Threads share one process's memory.
2. *Why keep a zombie?* The parent still needs to read the exit status.
3. *What stops a low-priority process waiting forever?* Aging moves it up a level after 1 second.
4. *What does a context switch do?* It saves one process's registers and kernel stack pointer, then loads another's and switches to its page directory.
5. *`fork` or `exec`?* `fork` makes a copy of the process. `exec` replaces the program running in it.

## Memory Management

Every process runs in its own virtual address space. The Memory Manager maps virtual pages to physical frames and handles the boundary between RAM and swap.

**Address space layout (per process):**

| Region | Virtual address range | Description |
| --- | --- | --- |
| Kernel space | Top 1 GB (e.g., 0xC0000000–0xFFFFFFFF) | Shared across all processes; ring 0 only |
| Stack | Just below kernel space, grows down | Local variables, return addresses |
| Heap | Above BSS, grows up | Dynamic allocations via `malloc` / `free` |
| BSS | Above data segment | Uninitialized globals |
| Data | Above text | Initialized globals |
| Text | Starts at 0x08048000 (x86 default) | Executable code; read-only |

**Paging:**

- Page size: 4 KB.
- Each process has a page directory and page tables (two-level paging, because the OS is 32-bit).
- The TLB (Translation Lookaside Buffer) is hardware-managed; the OS flushes it on context switch.

**Physical frame allocator:**

- Use a bitmap over all physical memory; one bit per 4 KB frame.
- At boot, read the memory map from Multiboot and mark the kernel and reserved areas as used. The RAM size is never hard-coded.
- Frame table: one entry per frame, holding a reference count and an owner. A bitmap alone can't tell Clock or shared memory who is using a frame.
- `alloc_frame()` finds the first free bit, marks it used, and returns the frame address.
- `free_frame(addr)` lowers the frame's reference count and clears the bit only when the count reaches 0.

**Virtual memory / demand paging:**

- Pages are not loaded until first access (page fault → kernel loads page from disk).
- Page replacement algorithm: Clock (second-chance) algorithm.
- Dirty pages are written to swap before eviction.

**Kernel heap:** The kernel itself uses a slab allocator for fixed-size kernel objects (PCBs, file descriptors, inodes). `kmalloc()` handles other sizes, such as pipe buffers.

**System calls exposed:** `mmap()`, `munmap()`, `brk()`, `sbrk()`, `shmget()`, `shmat()`, `shmdt()`.

**Deliverable from this member:** Virtual address space diagram, page table structure, frame allocator pseudocode, and page-fault handler flow.

### Explainer: Memory Management

**What problem it solves:** Each program should believe it has memory to itself, must not touch anyone else's, and should be able to use more memory than the RAM installed.

**Real-life comparison:** A hotel where every guest's key says "Room 1". The front desk maps that to a real room, and when the hotel is full, some luggage goes to the storeroom (swap).

**Main data structures:**

- Page directory and page tables: the map from virtual pages to physical frames.
- Frame bitmap and frame table: which frames are free, and who uses each one.
- Slab caches: ready-made slots for kernel objects.

**Step-by-step example: a page fault**

1. A program touches a page that isn't in RAM, and the CPU raises exception 14.
2. The handler checks that the address is valid for this process.
3. It gets a free frame. If RAM is above 80% full, Clock evicts pages first.
4. It loads the page from the executable file or from swap.
5. It updates the page table, and the program retries the instruction.

**Calls:** Storage Hierarchy for swap, File Handling to load pages.
**Called by:** Process Management, the syscall layer, and the page-fault handler.

**Likely questions:**

1. *Virtual or physical address?* Programs use virtual addresses. The page tables translate them to physical addresses in RAM.
2. *What is the TLB?* A hardware cache of recent translations. It's flushed on a context switch because each process has a different map.
3. *How does Clock work?* Frames sit in a circle and a hand sweeps round. If a frame was used recently, its bit is cleared and it gets a second chance. If not, it's evicted.
4. *Why is the kernel mapped into every process?* Syscalls and interrupts can then enter the kernel without switching page tables.
5. *Why a slab allocator?* The kernel keeps creating same-sized objects, and reusing slots is fast and wastes little memory.

## Resource Management

The Resource Manager arbitrates access to CPU time, I/O devices, and hardware peripherals so that no process monopolizes shared hardware and deadlocks are prevented.

**CPU resource control:**

- CPU time is distributed by the scheduler (see Process Management). Resource Management enforces hard limits via per-process CPU quotas. A quota is the maximum number of timer ticks a process may use in each 1-second window (default: no limit). A process that reaches its quota moves to Waiting until the next window starts.
- A watchdog timer (hardware timer interrupt every 10 ms) fires to enforce time-slice expiry.
- Idle process: a background process (PID 0) that runs only when all other queues are empty.

**Device and I/O management:**

| Device class | Managed via | Access model |
| --- | --- | --- |
| Disk | DMA + interrupt-driven driver | Async: process issues request, blocks until interrupt |
| Keyboard / Mouse | IRQ-based driver | Event queue; userspace reads via `/dev/input` |
| Display | Memory-mapped framebuffer | Direct write to framebuffer address |
| Network (optional) | NIC driver + interrupt | Packet queue |

**Scope note:** The disk uses DMA, and falls back to programmed I/O (PIO) if no DMA controller is found. The mouse is read through `/dev/mouse`. The display uses VGA text output, so moving the framebuffer to Stretch needs the instructor's OK (see Open Questions for the Instructor).

**Deadlock handling:**

- Strategy: Deadlock avoidance using Banker's Algorithm for resource allocation requests.
- Banker's covers a named set of limited kernel resources: pipes, message queues, and shared memory segments.
- The total of each resource type is set in a table initialized at boot.
- Each process declares its maximum claim with `res_declare()`, then uses `res_request()` and `res_release()`.
- If a request would leave the system in an unsafe state, it is deferred and the requesting process is queued. Queued requests are checked again whenever resources are released.
- Kernel locks are not covered by Banker's. They are always taken in one fixed, documented order, which prevents deadlock between them.

**Interrupt handling:**

- The PIC must be remapped first. By default, IRQ 0 uses vector 8, which clashes with the CPU's double-fault exception.

| Source | Vector | Purpose |
| --- | --- | --- |
| CPU exceptions 0–31 | 0x00–0x1F | Faults; exception 14 is the page fault |
| IRQ 0 | 0x20 | System timer (scheduler tick) |
| IRQ 1 | 0x21 | Keyboard |
| IRQ 14/15 | 0x2E/0x2F | Primary/secondary ATA disk |
| Software interrupt | 0x80 | System calls |

- The Interrupt Descriptor Table (IDT) must be populated before enabling interrupts.
- ISRs (Interrupt Service Routines) save full CPU state, handle the event, send End of Interrupt (EOI) to the PIC, and return with `iret`.

**Synchronization primitives the developer must implement:** Spinlock, mutex, semaphore, and condition variable.

**System calls exposed:** `sem_open()`, `sem_wait()`, `sem_post()`, `sem_close()`, `res_declare()`, `res_request()`, `res_release()`.

**Deliverable from this member:** Device driver interface spec, IRQ table, Banker's Algorithm resource table, and synchronization primitive API.

### Explainer: Resource Management

**What problem it solves:** All programs share the same hardware. This part decides who gets what, responds when devices need attention, and stops processes getting stuck waiting on each other.

**Real-life comparison:** A traffic police officer at a busy junction, plus a careful bank. The bank only lends if everyone can still be repaid in some order.

**Main data structures:**

- IDT: tells the CPU which function handles each interrupt.
- Banker's tables: available, maximum claim, and current allocation.
- Lock structures: lock state plus a queue of waiting processes.

**Step-by-step example: a key press**

1. The keyboard raises IRQ 1.
2. The CPU uses the IDT to jump to the keyboard ISR, which saves the CPU state.
3. The ISR reads the key and puts it in the event queue.
4. It sends EOI to the PIC and returns with `iret`.
5. The process waiting for input wakes up.

**Calls:** the HAL, and the scheduler to block and wake processes.
**Called by:** every subsystem that needs a lock or a device.

**Likely questions:**

1. *Spinlock or mutex?* A spinlock keeps the CPU busy while waiting, so it suits very short waits. A mutex puts the process to sleep.
2. *Semaphore or mutex?* A semaphore is a counter that lets several through. A mutex lets one owner through.
3. *What is a safe state?* One where at least one order exists in which every process can finish.
4. *Why remap the PIC?* Its default vectors clash with CPU exceptions.
5. *Why not use Banker's for everything?* It needs maximum claims in advance and adds work to every request, so it covers three resource types and locks use a fixed order.

## File Handling

The filesystem gives processes a named, hierarchical view of persistent data. All user-visible disk access goes through the file handling layer; raw disk blocks are never exposed to user processes. Swap is the one kernel exception: it uses the block layer directly.

**Filesystem type:** A simplified filesystem that follows the real ext2 on-disk format, so Linux `mke2fs` can build the disk image and `e2fsck` can check it. Key on-disk structures:

- **Superblock** (block 0 of the partition, at byte offset 1024, as in real ext2): total blocks, free blocks, block size, inode count.
- **Block Group Descriptor Table**: location of inode bitmap, block bitmap, inode table per group.
- **Inode**: 128 bytes. Stores file type, permissions, owner UID/GID, size, timestamps, link count, and 12 direct block pointers + 1 indirect + 1 double-indirect. The real ext2 triple-indirect slot is kept but left unused.
- **Directory entry**: 4-byte inode number + 2-byte entry length + 1-byte name length + 1-byte file type + filename string.

**File operations (system calls):** `open()`, `close()`, `read()`, `write()`, `seek()`, `stat()`, `unlink()`, `mkdir()`, `rmdir()`, `readdir()`, `chdir()`, `getcwd()`, `dup2()`.

**File descriptor tables:** Open files use two levels, so that parent and child share one file position after `fork()`, and so that pipes work as file descriptors.

- Per-process table of open file descriptors (max 256 per process). Each entry points to an entry in the global open-file table.
- Global open-file table. Each entry: current offset, access mode (read / write / read-write), reference count, operations table, and a pointer to an inode or a pipe.
- FD 0 = stdin, FD 1 = stdout, FD 2 = stderr (inherited from parent).

**Permissions model:** Unix-style rwxrwxrwx (owner / group / other), checked on every `open()` call against the process's UID/GID.

**Virtual filesystem (VFS) layer:** A thin abstraction layer that lets the kernel call `vfs_read()`, `vfs_write()`, etc., regardless of the underlying filesystem type. The VFS dispatches to the correct driver (ext2, tmpfs, devfs) via a function pointer table per mounted filesystem.

**Mount table:** At boot, the root filesystem is mounted at `/`. Additional mounts (e.g., `/dev`) are registered in the global mount table. `/proc` is a stretch goal and would need its own procfs driver.

**Deliverable from this member:** On-disk inode and directory structure, VFS interface definition, system call signatures, and permission check pseudocode.

### Explainer: File Handling

**What problem it solves:** A disk is just numbered blocks. This part adds file names, folders and permissions, so people and programs can find their data.

**Real-life comparison:** A library. The catalogue card (inode) says where a book's chapters are, the shelf list (directory) maps titles to cards, and your borrowing slip (open-file entry) records your page.

**Main data structures:**

- Inode: a file's details and block pointers, but not its name.
- Directory entry: a file name and its inode number.
- FD table and open-file table: which files a process has open, and its position in each.

**Step-by-step example: `open("/home/notes.txt")`, then `read()`**

1. The VFS splits the path and starts at the root directory.
2. It searches for `home`, then for `notes.txt`, and gets its inode.
3. It checks the rwx permissions against the process's UID and GID.
4. It creates an open-file entry and returns a free file descriptor.
5. `read()` turns the offset into a block number and asks the buffer cache for that block.

**Calls:** Storage Hierarchy for blocks, Memory Management for buffers.
**Called by:** the syscall layer, Process Management for `exec`, and Memory Management to load pages.

**Likely questions:**

1. *What is an inode?* A record of a file's details and where its blocks are.
2. *Where is the file name stored?* In the directory entry, not in the inode.
3. *Why two levels of file tables?* After `fork()`, parent and child must share one file position.
4. *What is the largest file?* 12 direct + 1,024 indirect + 1,024 × 1,024 double-indirect blocks of 4 KB each, about 4 GB.
5. *Why have a VFS?* The same `read()` works on ext2 and devfs, because each mounted filesystem supplies its own function table.

## Integration

Integration is the sixth role: it owns the System Call Interface (SCI) and coordinates every other subsystem. Every user-space syscall passes through the SCI, which dispatches to the correct kernel handler.

&#91;embedded content: OS architecture · 5 layers · 6 subsystems\]

*Note: the architecture diagram above was lost when this document was exported and needs restoring from the original.*

All five subsystems register their internal interfaces with the Integration layer at kernel boot time. Some subsystems start in two stages, because each needs parts of the others. The sole developer must initialise them in this order to avoid dependency cycles:

| Step | Stage | What starts |
| --- | --- | --- |
| 1 | Boot | GDT, TSS, serial console, panic handler |
| 2 | HAL | IDT, CPU exceptions, PIC remap, timer (interrupts still off) |
| 3 | Memory core | Frame allocator, paging, slab and `kmalloc` |
| 4 | Sync primitives | Spinlock, mutex, semaphore, condition variable |
| 5 | Process core | PCB, scheduler, idle process, then interrupts turn on |
| 6 | Device drivers | Keyboard, ATA disk |
| 7 | Storage | Partition table, buffer cache, swap device |
| 8 | File Handling | VFS, ext2, devfs, mount `/` |
| 9 | Memory late stage | Swap and demand paging turn on |
| 10 | SCI | System call table, then `exec` the first user program in ring 3 |

Spinlocks need no setup, so Memory can use them from step 3. Mutexes, semaphores and condition variables can block a process, so they are only used from step 5, once the scheduler is running.

**Integration responsibilities:**

- Define the system call table: syscall number maps to a handler function pointer.
- Own the Hardware Abstraction Layer (HAL) that Resource Management and Storage Hierarchy call.
- Manage the kernel init sequence and resolve inter-subsystem dependencies.
- Expose a stable in-kernel API so each subsystem's internals can change without breaking others.
- Handle kernel panics: if any subsystem returns an unrecoverable error, Integration halts the system and prints a diagnostic.

**System call table:** A user program puts the syscall number in `EAX` and its arguments in other registers, then runs `int 0x80`. An unknown number returns an error and never crashes the kernel.

| Numbers | Owner | System calls |
| --- | --- | --- |
| 1–9 | Process Management | 1 `fork`, 2 `exec`, 3 `exit`, 4 `wait`, 5 `getpid`, 6 `kill`, 7 `sleep`, 8 `sigaction`, 9 `sigreturn` |
| 10–13 | Memory Management | 10 `mmap`, 11 `munmap`, 12 `brk`, 13 `sbrk` |
| 14–26 | File Handling | 14 `open`, 15 `close`, 16 `read`, 17 `write`, 18 `seek`, 19 `stat`, 20 `unlink`, 21 `mkdir`, 22 `rmdir`, 23 `readdir`, 24 `chdir`, 25 `getcwd`, 26 `dup2` |
| 27–30 | Process Management | 27 `pipe`, 28 `msgget`, 29 `msgsnd`, 30 `msgrcv` |
| 31–33 | Memory Management | 31 `shmget`, 32 `shmat`, 33 `shmdt` |
| 34–37 | Resource Management | 34 `sem_open`, 35 `sem_wait`, 36 `sem_post`, 37 `sem_close` |
| 38–39 | Process Management | 38 `getuid`, 39 `getgid` |
| 40–42 | Resource Management | 40 `res_declare`, 41 `res_request`, 42 `res_release` |
| 43–46 | Process Management | 43 `setuid`, 44 `setgid`, 46 `procinfo` (used by `ps`), and 45 `set_echo` from File Handling (hides the password at login) |
| 47 | File Handling | 47 `chmod` |
| 48 | Process Management | 48 `set_quota` |
| 49 | Memory Management | 49 `meminfo` (used by `free`) |

**Deliverable from this member:** System call number table, kernel init order, HAL interface definitions, and panic handler spec.

### Explainer: Integration

**What problem it solves:** Five subsystems have to start in the right order and talk to each other. User programs also need one safe way into the kernel.

**Real-life comparison:** The front desk of a college office. Every request comes to one counter with a form number and gets sent to the right department.

**Main data structures:**

- System call table: maps each number to a handler function.
- HAL interface: functions that hide direct hardware access.
- Init order: the fixed start-up sequence.

**Step-by-step example: a `read()` system call**

1. The libc wrapper puts number 16 in `EAX` and the arguments in registers.
2. It runs `int 0x80`.
3. The CPU enters ring 0 and uses the TSS to switch to the kernel stack.
4. The SCI checks the number and the user's pointers.
5. The table calls the File Handling function.
6. The result goes in `EAX`, and `iret` returns to the program.

**Calls:** all five other subsystems.
**Called by:** user programs and the boot sequence.

**Likely questions:**

1. *Why can't a program call kernel functions directly?* It runs in ring 3, which the CPU blocks from kernel memory and privileged instructions. That protects the system.
2. *What is the TSS for?* It tells the CPU which kernel stack to use when moving from ring 3 to ring 0.
3. *Why does boot order matter?* Each stage needs what the earlier stages set up. Page faults need the IDT, for example.
4. *What is the HAL?* A thin layer that hides the hardware, so other parts don't touch ports directly.
5. *What does a panic do?* It prints a diagnostic and halts, because carrying on could corrupt data.

## User Space

A functional OS needs programs to run. These pieces are required:

- **ELF loader** (Process Management): `exec()` reads an ELF executable and sets up its memory.
- **Small libc** (Integration): syscall wrappers, plus `malloc` and `free`, which run in user space on top of `brk` and `sbrk`.
- **PID 1** (Integration): `/sbin/init`, the first user program. It starts login and adopts orphaned processes.
- **Login** (Integration): `/bin/login` asks for a name and password, then becomes that user and starts their shell. Passwords are stored in `/etc/shadow` as a salted SHA-256 hash, and only root can read that file. Demo accounts: `root` and `student`.
- **Shell** (Integration): runs programs, and supports pipes, redirection and Ctrl+C.
- **Basic commands** (Integration): `ls`, `cat`, `cp`, `mv`, `touch`, `echo`, `mkdir`, `rm`, `rmdir`, `head`, `grep`, `wc`, `pwd`, `whoami`, `chmod`, `ps`, `kill`, `sleep`, `free`, `clear`, and `help` in the shell.
- **Text editor** (Integration): `edit file.txt` is a full-screen editor. The arrow keys move the cursor, Ctrl+S saves and Ctrl+Q quits.
- **C++ compiler** (Integration): `c++ file.cpp -o name` compiles a small subset of C++ inside the OS and writes a real x86 ELF program. `run file.cpp` compiles and starts a program in one step. It supports `int`, `char`, `bool`, `string`, arrays, functions, recursion, `if`, loops, `cin` and `cout`. It has no classes, structs, pointers, templates or `vector`.
- **Test programs** (Integration): one for each row of Acceptance Tests.

## Shared Walkthroughs

Each step names the owner: (1) Storage, (2) Process, (3) Memory, (4) Resource, (5) File, (6) Integration.

### Power on to shell prompt

1. (6) GRUB loads the kernel. The GDT, TSS, serial console, IDT, PIC and timer are set up.
2. (3) The frame allocator, paging and slab start.
3. (4) Sync primitives become available.
4. (2) The scheduler and idle process start, and interrupts turn on.
5. (4) The keyboard and disk drivers start.
6. (1) The partition table is read, and the buffer cache and swap device start.
7. (5) ext2 is mounted at `/` and devfs at `/dev`.
8. (3) Swap and demand paging turn on.
9. (6) The syscall table is ready, PID 1 runs in ring 3, and it starts the shell.

### Typing `cat notes.txt`

1. (4) Each key press raises IRQ 1, and the driver queues the key.
2. (6) The shell's `read()` enters the kernel through `int 0x80` and gets the line.
3. (2) The shell forks, the child runs `exec("/bin/cat")`, and the shell waits.
4. (3) `cat`'s first instruction causes a page fault, and the page is loaded from the file.
5. (5) `open()` finds the inode and checks permissions.
6. (1) `read()` asks the buffer cache for the block, and a miss goes to the disk.
7. (2) `cat` sleeps until (4) IRQ 14 says the disk has finished.
8. (5) `write()` to FD 1 goes through devfs to (4) the display driver.
9. (2) `cat` exits, the shell wakes from `wait()`, and the prompt returns.

### RAM runs out

1. (3) RAM passes 80%, and Clock picks a page to evict.
2. (1) A dirty page is given a swap slot and written out through the block layer.
3. (4) The disk driver starts the write, and the process sleeps until IRQ 14.
4. (2) Another process runs in the meantime, then the sleeper wakes.
5. (3) Eviction stops at 70%. If an evicted page is used later, a page fault brings it back from swap.

### `ls | wc`

1. (6) The shell reads the line and sees the pipe.
2. (2) `pipe()` creates a kernel buffer with a read end and a write end.
3. (5) Both ends become entries in the open-file table.
4. (2) The shell forks two children.
5. (5) `dup2()` connects `ls`'s stdout to the write end and `wc`'s stdin to the read end.
6. (2) Each child calls `exec()`. A reader sleeps on an empty pipe, and a writer sleeps on a full one.
7. (5) When the last write end closes, `wc` sees end of file and prints its count.

### Banker's refuses an unsafe request

1. (2) A process starts, and (4) it declares its maximum claim with `res_declare()`.
2. (4) It calls `res_request()`, and Banker's checks whether the system would stay safe.
3. (4) The answer is no, so the request is deferred.
4. (2) The process moves to Waiting.
5. (4) Another process releases resources, and Banker's checks again.
6. (2) The request is now safe, so the process wakes with its resources.

## Glossary

| Term | Meaning |
| --- | --- |
| Boot sector / MBR | First sector of a disk; starts the boot process and holds the partition table |
| ELF | The executable file format our programs use |
| EOI | End of Interrupt; tells the PIC an interrupt has been handled |
| Frame | A 4 KB block of physical RAM |
| GDT | Global Descriptor Table; defines the kernel and user memory segments |
| HAL | Hardware Abstraction Layer; hides direct hardware access |
| IDT | Interrupt Descriptor Table; maps each interrupt to its handler |
| Inode | On-disk record of a file's details and block locations |
| IRQ | Interrupt request from a hardware device |
| ISR | Interrupt Service Routine; the function that handles an interrupt |
| Multiboot | A standard way for GRUB to load a kernel and pass it information |
| Page | A 4 KB block of virtual memory |
| Page fault | CPU exception raised when a program touches a page that isn't available |
| PCB | Process Control Block; the OS's record of one process |
| PIC | Programmable Interrupt Controller; passes device interrupts to the CPU |
| PIT | Programmable Interval Timer; creates the 10 ms tick |
| PIO | Programmed I/O; the CPU copies disk data itself instead of using DMA |
| Ring 0 / Ring 3 | CPU privilege levels: kernel code and user code |
| SCI | System Call Interface; the single entry point into the kernel |
| Slab | Allocator that reuses slots for same-sized kernel objects |
| Swap | Disk space that holds pages evicted from RAM |
| Tick | One timer interrupt, every 10 ms. The PRD's "watchdog timer" is this timer, and one 20 ms quantum is 2 ticks |
| TLB | Translation Lookaside Buffer; hardware cache of address translations |
| TSS | Task State Segment; tells the CPU which kernel stack to use |
| VFS | Virtual File System; one interface over different filesystems |
| Zombie | A finished process waiting for its parent to read its exit status |

## Scope Tiers

Rough estimate: xv6, a teaching OS, has under 10,000 lines of kernel code and does less than this PRD. This PRD is likely to need 15,000–25,000 lines, which is too much for one developer unless it's built in stages. Finish each tier before starting the next.

| Tier | Contents |
| --- | --- |
| Must have | Boot, paging, slab, preemptive scheduler, `fork/exec/wait/exit`, ELF loader, keyboard, VGA text output, ATA disk, buffer cache, VFS, ext2, devfs, pipes, shell |
| Should have | Demand paging, swap, Clock, signals, shared memory, semaphores, message queues, Banker's demo, CPU quotas |
| Stretch | DMA, mouse, tmpfs, `/proc` (all four are built); threads for user programs, framebuffer, copy-on-write (not built) |
| Cut | Multi-core, network |

## Toolchain

| Tool | Used for |
| --- | --- |
| `g++ -m32 -ffreestanding` | C++ compiler for the kernel and programs, with exceptions and RTTI turned off |
| NASM | Assembly code |
| GNU Make | Building the project |
| QEMU | Running the OS |
| GDB | Debugging the kernel through QEMU |
| GRUB tools | Making the OS bootable |
| e2fsprogs | `mke2fs` builds the filesystem and `e2fsck` checks it |

**Build machine (temporary WSL):** Only the developer needs this. MinGW `gcc` on Windows can't be used, because it builds Windows programs and not bare-metal kernels. In a terminal opened as Administrator:

```bash
wsl --install -d Ubuntu
```

Then, inside Ubuntu:

```bash
sudo apt update && sudo apt install -y build-essential gcc-multilib nasm qemu-system-x86 gdb grub-pc-bin xorriso e2fsprogs
```

Keep the code in the Ubuntu home folder, such as `~/os-project`, and push it to a private GitHub repo. To remove WSL later, push your code first, because `wsl --unregister` permanently deletes everything inside Ubuntu:

```bash
wsl --unregister Ubuntu
```

```bash
wsl --uninstall
```

**Fallback (GitHub Codespaces):** A Linux machine in the browser, with nothing installed on the laptop. One `.devcontainer` file gives everyone the same tools. It needs internet, and QEMU is easiest to use in text mode (`-nographic`). Personal accounts get a free monthly allowance, so check the current limit.

**Line endings:** Add a `.gitattributes` file with `* text=auto eol=lf` and `*.bat eol=crlf`, so Windows line endings don't break scripts and Makefiles.

## Running on Team Laptops

The OS must run on all six teammates' Windows 11 laptops.

**Run it only in QEMU, never directly on a laptop.** This OS targets old PC hardware (BIOS boot, ATA disks, legacy PIC). Modern laptops use UEFI, Secure Boot and NVMe, so it wouldn't work, and a partition mistake could overwrite Windows.

**Build once, share one image.** One machine builds `os.img`. Everyone gets the same ZIP with `os.img`, `run.bat` and a short README, and installs the same QEMU for Windows version. Teammates don't need WSL and never compile, so compiler differences can't affect them.

Starting point for `run.bat` (not tested yet):

```bat
@echo off
"C:\Program Files\qemu\qemu-system-i386.exe" -machine pc -cpu qemu32 -m 128M -drive file=os.img,format=raw,if=ide -snapshot -no-reboot -display gtk,zoom-to-fit=on -serial file:boot.log
```

**Potential issues:**

| Issue | What happens | Prevention |
| --- | --- | --- |
| Different QEMU versions | Behaviour differs between laptops | Everyone uses the same version, with `-machine` and `-cpu` fixed in `run.bat` |
| QEMU isn't on PATH | `run.bat` says "command not found" | Use QEMU's full path, or put a copy of the QEMU folder in the ZIP |
| Smart App Control, SmartScreen or antivirus | QEMU or `run.bat` gets blocked. Smart App Control can't simply be switched on again once it's off | Test in week 1. Unblock the ZIP in file properties. Use Codespaces if it stays blocked |
| Virtualization off, or Hyper-V conflicts | VirtualBox and WSL fail or run very slowly | QEMU's default software mode needs neither. Don't use `-accel whpx`, so all laptops behave the same |
| Timing and race bugs | Works on a fast laptop, fails on a slow one | Run the tests on all six. Don't let tests depend on real-time speed |
| Battery saver or a busy laptop | Slower, jerky demo | Plug in and close heavy apps |
| Disk image changes between runs | One bad run corrupts the OS for that person | `-snapshot` throws away changes, so every run starts clean. Saved files last until QEMU closes. Keep a master copy |
| OneDrive, spaces or non-English paths | File locks or path errors | Unzip to `C:\OSProject`, outside OneDrive |
| High-DPI laptop screens | Tiny or blurry VGA window | `zoom-to-fit=on`. Test on the projector too |
| Mouse stuck inside QEMU | Looks frozen | Put the release key (Ctrl+Alt+G) in the README |
| ARM laptops (Snapdragon) | QEMU may run slowly or fail | Check each laptop's CPU in week 1. Use Codespaces if needed |
| Kernel assumes a fixed RAM size | Works at one `-m` value only | Read the memory map from Multiboot |
| A crash gives no clue | Teammate can only say "it doesn't work" | `boot.log` captures serial output, so they can send the file. `-no-reboot` stops crash loops |
| Laptop fails on demo day | Demo lost | Keep a recorded demo video, and have the image on a second laptop |

**Laptop check-off:** Fill this in at Milestone 0, and run it again for each milestone image.

| Teammate | CPU type | QEMU version | Result |
| --- | --- | --- | --- |
| Storage Hierarchy | | | |
| Process Management | | | |
| Memory Management | | | |
| Resource Management | | | |
| File Handling | | | |
| Integration | | | |

## Acceptance Tests

Each subsystem has one demo that proves it works. The owner's buddy should be able to run it and explain the result.

| # | Subsystem | Test | Passes when |
| --- | --- | --- | --- |
| 1 | Storage Hierarchy | `cachetest` reads one block twice | The log shows only one disk read, and `e2fsck` reports a clean image |
| 2 | Process Management | `schedtest` runs priority 0 and priority 3 loops, and `forktest` runs many children | Both loops make progress, and every child is collected with no zombies left |
| 3 | Memory Management | `swaptest` uses more memory than the RAM available | All data reads back correctly, and one process can't read another's memory |
| 4 | Resource Management | `locktest` has processes share a counter through shared memory and a semaphore, and `banktest` makes an unsafe request | The counter total is exact, and the unsafe request is deferred until safe |
| 5 | File Handling | `fstest` creates, writes, reads and deletes a file larger than 48 KB | Data matches (this uses indirect blocks), and a forbidden `open()` is refused |
| 6 | Integration | Boot on all six laptops, call an invalid syscall number, and trigger a test panic | Every laptop reaches the shell, the bad syscall returns an error, and the panic prints a diagnostic |

## Milestones

Dates are left open until the deadline is confirmed. The Must have tier is complete at M6.

| Milestone | Goal | Proof | Lead |
| --- | --- | --- | --- |
| M0 | Laptop test | A tiny "hello" kernel runs on all six laptops, and each teammate sends back `boot.log` | Integration |
| M1 | Boot and interrupts | Timer ticks print to serial, and a test panic works | Integration, Resource |
| M2 | Memory core | Frames and slab objects can be allocated and freed under paging | Memory |
| M3 | Scheduling | Two kernel threads take turns under the timer | Process, Resource |
| M4 | Disk and read-only ext2 | The kernel prints a file from the disk image | Storage, File |
| M5 | User mode | The shell runs in ring 3 and runs `ls` through `fork/exec/wait` | Integration, Process |
| M6 | Writing and pipes | Files can be created, and `ls \| wc` works | File, Process |
| M7 | Virtual memory | `swaptest` passes | Memory, Storage |
| M8 | IPC and deadlock | Signals, shared memory, semaphores, message queues and `banktest` work | Process, Resource |
| M9 | Final polish | Mock viva, final run on all six laptops at least a few days before submission, and demo video recorded | Whole team |

## Implementation Status

The code is in the private repo `itssDurgesh/os-project`. `make test` boots the OS once per test and checks the result.

| Area | Status |
| --- | --- |
| Boot, GDT, interrupts, timer, panic | Done and tested |
| Frames, paging, slab, `kmalloc`, C++ `new` and `delete` | Done and tested |
| Scheduler with priorities, aging and CPU quotas | Done and tested |
| Locks: spinlock, mutex, semaphore, condition variable | Done and tested |
| Disk layout, ATA with PIO, buffer cache | Done and tested |
| ext2, VFS, devfs, open-file table, permissions | Done and tested; Linux `e2fsck` agrees the disk is clean |
| User mode, ELF loading, all 49 system calls | Done and tested |
| Login, users, `/etc/shadow`, permission checks | Done and tested |
| Pipes, signals, shared memory, semaphores, message queues | Done and tested |
| Banker's Algorithm | Done and tested |
| Demand paging, swap, Clock, `mmap` | Done and tested |
| DMA disk access, PS/2 mouse, tmpfs on `/tmp`, `/proc` | Done and tested |
| Text editor, `cp`, `mv`, `grep`, background programs, command history | Done and tested |
| Small C++ compiler that runs inside the OS | Done and tested, including a 58-check feature test |
| Framebuffer, threads for user programs, copy-on-write | Not built |
| Running through QEMU for Windows with `run.bat` | Not tested yet |

## Open Questions for the Instructor

- Is GRUB allowed, or must the bootloader be written by hand? This PRD says "from scratch".
- Is working code graded, or mainly the specs? This decides how far down the Scope Tiers to go.
- May we study xv6 and use AI help for the code?
- May the framebuffer stay as a stretch goal? The original PRD required it, and the OS uses VGA text output.
