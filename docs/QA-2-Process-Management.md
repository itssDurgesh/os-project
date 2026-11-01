# Viva Q&A: Process Management

**Role:** Member 2, Process Management
**You own:** PCB, scheduler, context switch, `fork`/`exec`/`wait`, ELF loader, signals, pipes, message queues
**Your code:** `process/sched.cpp`, `process/process.cpp`, `process/switch.asm`, `process/sys_process.cpp`, `process/signal.cpp`, `process/message_queue.cpp`, `fs/pipe.cpp`, `include/process.h`
**Your buddy:** Resource Management explains your part. You explain File Handling.

## Your 30-second summary

> Many programs want to run, but there's only one CPU. Each process has a PCB that records everything about it. The scheduler uses Round-Robin with four priority levels and a 20 ms time slice, and aging stops low-priority processes from starving. `fork` copies a process, `exec` replaces its program, `exit` ends it, and `wait` lets the parent collect it. Pipes, message queues, shared memory and signals let processes communicate.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| PCB | The OS must remember each process's state between turns | A paused process could never continue |
| Round-Robin | Everyone gets a turn, so the system feels responsive | One long program could hold the CPU |
| Priority levels | Important work can go first | All work would be treated the same |
| Aging | A waiting process slowly rises in priority | Low-priority processes could wait forever (starvation) |
| 20 ms time slice | Short enough to feel smooth, long enough to do useful work | Too short wastes time switching, too long feels laggy |
| Kernel not preempted | Kernel code can't be switched out halfway through | We would need far more locks, and more race bugs |
| One kernel stack per process | A sleeping process keeps its place in kernel code | Two processes in the kernel would overwrite each other |
| `fork` + `exec` as two steps | The child can set up pipes and redirection before the new program runs | The shell couldn't do `ls \| wc` this simply |
| Zombie state | The parent still needs the exit status | The exit status would be lost |
| Orphan adoption by PID 1 | Someone must collect every process | Zombies would stay forever |
| Signals | Lets the OS or another process interrupt a program | Ctrl+C and `kill` couldn't work |
| Pipes | A simple byte stream between two programs | Programs couldn't be chained |
| Message queues | Typed messages, picked by type | A reader couldn't choose which message it wants |

## Numbers to remember

| Item | Value | Where |
| --- | --- | --- |
| Priority levels | 4 (0 is highest, 3 is lowest) | `NUM_PRIORITIES` |
| Default priority | 2 | `DEFAULT_PRIORITY` |
| Timer tick | 10 ms, 100 Hz | `TIMER_HZ` |
| Time slice | 2 ticks, which is 20 ms | `TIME_QUANTUM_TICKS` |
| Aging limit | 100 ticks, which is 1 second | `AGING_LIMIT_TICKS` |
| Kernel stack | 8 KB per process | `KERNEL_STACK_PAGES` |
| User stack | up to 1 MB | `USER_STACK_PAGES` |
| Pipe buffer | 4,096 bytes | `PIPE_SIZE` |
| Message queues | 8 queues, 16 messages each, 256 bytes each | `message_queue.cpp` |
| PIDs | 0 idle, 1 init, 2 swapper, then 3 upward | `sched.cpp` |
| Most processes at once | 64 | `MAX_PROCESSES` |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Process list | `ps` | `init`, `swapper`, `sh`, `ps`, with state and priority |
| Live process states | `cat /proc/processes` | `7 0 running cat`, `3 0 waiting sh`, and so on |
| Background process | `sleep 30 &`, then `ps` | `[started] process N`, and `sleep` in the list |
| Stopping a process | `kill N` | Next prompt shows `[finished] process N, status 143` |
| Force kill | `kill N 9` | `status 137` |
| Ctrl+C | `sleep 100`, then press Ctrl+C | The prompt returns |
| Many children, no zombies | `forktest` | `TEST-PASS forktest` |
| Signals | `sigtest` | `TEST-PASS sigtest` |
| Message queues | `ipctest` | `TEST-PASS ipctest` |
| CPU quotas | `quotatest` | `cpu ticks without quota: 200, with quota: 50` (the second number is about 50 to 60) |
| A real zombie | See program 1 below | A line in `ps` with the state `zombie` |
| Round-Robin sharing | See program 2 below | Two `spin` lines in `ps` with similar CPU numbers |
| `fork` from your own program | See program 3 below | `I am the child`, then `I am the parent, my child was N` |

Both `kill -9 5` and the older `kill 5 9` work.

Our compiler has no `fork()` function, but it has `__syscall(number, a, b, c)`, which reaches any system call. To type a program in the OS, use `edit /tmp/name.cpp`.

**Program 1: a zombie.** The child ends at once, and the parent sleeps for 15 seconds without calling `wait`.

```cpp
int main() {
    int pid = __syscall(1, 0, 0, 0);    // fork
    if (pid == 0) {
        return 0;
    }
    __syscall(7, 15000, 0, 0);          // sleep 15 seconds
    return 0;
}
```

Build it with `c++ /tmp/z.cpp -o /tmp/z`, start it with `/tmp/z &`, then type `sleep 1` and `ps`. You'll see a line like `8 7 0 2 zombie 0 z`.

**Program 2: Round-Robin.** This program never stops, so it always wants the CPU.

```cpp
int main() {
    while (true) {
    }
    return 0;
}
```

Build it with `c++ /tmp/spin.cpp -o /tmp/spin`, then type `/tmp/spin &` twice, `sleep 4` and `ps`. Our test showed `302` and `240` CPU ticks after 4 seconds, so both were getting turns. Stop them with `kill N 9`.

**Program 3: fork.**

```cpp
int main() {
    int pid = __syscall(1, 0, 0, 0);    // fork
    if (pid == 0) {
        cout << "I am the child" << endl;
        return 0;
    }
    __syscall(4, 0, 0, 0);              // wait
    cout << "I am the parent, my child was " << pid << endl;
    return 0;
}
```

Run it with `run /tmp/f.cpp`.

---

## A. Basic concepts

**1. What is a process?**
A program being run, with its own memory, open files and scheduling state.

**2. Program or process?**
A program is a file on disk. A process is that program running. One program can run as many processes.

**3. Process or thread?**
A process has its own memory. Threads share one process's memory. Our threads exist only inside the kernel.

**4. Name the process states.**
New, Ready, Running, Waiting, Terminated.

**5. Draw the state changes.**

```text
New --> Ready --> Running --> Terminated
          ^         |   |
          |         |   | waits for I/O or an event
          |  slice  |   v
          +--ended--+  Waiting
          ^             |
          +--- event ---+
```

**6. What is a PCB?**
The Process Control Block: the kernel's record of one process.

**7. What is a context switch?**
Saving one process's CPU state and loading another's.

**8. Why is a context switch a cost?**
The CPU does no useful work during it, and the TLB is flushed, so the next process starts with colder caches.

**9. What is CPU scheduling?**
Deciding which ready process runs next.

**10. Preemptive or non-preemptive?**
Preemptive can take the CPU away from a process. Non-preemptive waits for it to give up. Our user programs are preempted, but our kernel code isn't.

**11. Name some scheduling algorithms.**
FCFS, SJF, Round-Robin, Priority, and Multilevel Queue. We use Round-Robin inside each priority level.

**12. What is starvation?**
A process that never gets the CPU because others always go first.

**13. What is a zombie?**
A finished process whose PCB stays until its parent reads the exit status.

**14. What is an orphan?**
A process whose parent ended first. Ours is adopted by PID 1.

**15. What is IPC?**
Inter-Process Communication: ways for processes to share data.

**16. What are the four IPC types we have?**
Pipes, message queues, shared memory and signals. Semaphores help processes take turns.

**17. What is a signal?**
A short asynchronous notice to a process, such as `SIGTERM`.

---

## B. Our design

**18. Where is the PCB in the code?**
`struct Process` in `include/process.h`. PCBs come from a slab cache, `pcb_cache`.

**19. What does the PCB store?**
PID, parent PID, UID, GID, name, state, exit status, priority, CPU ticks, kernel stack, saved stack pointer, page directory, memory areas, heap end, file table, current directory, signal data, shared memory slots, Banker's data, and CPU quota.

**20. Where are the saved registers?**
On the process's own kernel stack. The PCB keeps `saved_esp`, which points to them.

**21. Explain `schedule()`.**
It turns interrupts off, puts the running process back on its ready queue, uses `pick_next` to take the first process from the highest non-empty queue, sets the TSS stack, changes page directory if needed, and calls `switch_stacks`.

**22. Explain `switch_stacks`.**
It pushes `ebx`, `esi`, `edi` and `ebp`, saves `ESP` in the old PCB, loads the new `ESP`, pops the same four registers, and returns. The return address comes from the new stack, so the code carries on where the new process last stopped.

**23. Why only four registers?**
The C calling rule lets a called function change the others. Registers from user mode were already saved on interrupt entry.

**24. What does `scheduler_tick` do?**
Every 10 ms it adds to `cpu_ticks`, wakes sleepers, resets quotas each second, runs aging every 100 ms, and counts down the time slice.

**25. Why does the tick only set `need_resched`?**
The kernel isn't preempted. The switch happens in `switch_process_if_needed`, when the process is about to return to user mode.

**26. How does aging work?**
`age_ready_queues` looks at queues 1 to 3. A process waiting 100 ticks or more moves up one level. After it next runs, it goes back to `base_priority`.

**27. Did aging work in testing?**
Yes. The `aging` boot test put a priority 3 thread beside a busy priority 1 thread. It ran after 210 ticks: priority 3 to 2 at about 100 ticks, then 2 to 1 at about 200.

**28. Is this a Multilevel Feedback Queue?**
Not exactly. MLFQ changes priority based on how much CPU a process uses. Ours has fixed levels, and aging only gives a temporary rise.

**29. What runs when nothing is ready?**
The idle process, PID 0. `idle_loop` runs `sti; hlt`, so the CPU sleeps until the next interrupt.

**30. Why doesn't `ps` show PID 0?**
Idle is a static object and isn't on the `all_processes` list.

**31. Walk through `fork`.**
`process_fork` checks the process limit, creates a PCB and kernel stack, copies the UID, memory areas and signal handlers, copies the page tables, shares open files, and builds a stack so the child returns from the system call with `EAX = 0`.

**32. Why does `fork` return twice?**
The parent returns normally with the child's PID. The child was given a copy of the parent's saved registers with `EAX` set to 0, so it wakes up at the same place with a different result.

**33. Does `fork` use copy-on-write?**
No. `copy_page_directory` copies each page. Copy-on-write is listed as not built.

**34. Walk through `exec`.**
`sys_exec` copies the path and arguments into the kernel. `process_exec` finds the file, checks the execute bit, builds a new page directory, reads the ELF header, records the memory areas, builds the new stack, and only then replaces the old program.

**35. What happens if `exec` fails?**
The old program is untouched, because the new address space is built first. `exec` returns an error, and the shell prints it and exits with 127.

**36. Does `exec` load the whole program?**
No. It records where each part is, and pages load on first use. This is demand paging.

**37. How do you check a file is a real program?**
`read_program_header` checks the magic number `0x7F 'E' 'L' 'F'`, 32-bit class, executable type and the 386 machine type. Otherwise it returns `-ENOEXEC`.

**38. How do arguments reach `main`?**
`build_user_stack` writes the strings, then the pointers, then `argc`. `crt0.asm` reads them and calls `main`.

**39. Walk through `exit`.**
`process_exit` releases Banker's resources, shared memory, memory areas, open files and the current folder. It switches to the kernel's page directory, gives its children to PID 1, tells the parent, and calls `thread_exit`, which marks it Terminated.

**40. Who frees the PCB and kernel stack?**
The parent, in `process_wait`, through `delete_process`. A process can't free the stack it's standing on.

**41. Walk through `wait`.**
`process_wait` looks through the process list for a finished child. If it finds one, it takes the exit status and frees it. If there are children but none finished, it sleeps. With no children, it returns `-ECHILD`.

**42. How does the shell run a background program?**
It forks, gives the child `/dev/null` as stdin, prints `[started] process N`, and doesn't wait. Before each prompt, `check_finished_programs` calls `waitpid` without sleeping.

**43. What does exit status 139 mean?**
A crash. `process_fault` uses it, which is 128 + 11, the Linux number for a segmentation fault.

**44. Why is status 130, 137 or 143 used?**
128 plus the signal number: `SIGINT` 2, `SIGKILL` 9, `SIGTERM` 15.

**45. How is a signal sent?**
`send_signal` sets one bit in `pending_signals`. If the target is sleeping and can be interrupted, it's woken.

**46. When does the signal actually happen?**
In `deliver_signals`, just before the process returns to user mode.

**47. How does a handler run?**
`start_handler` puts a `SignalFrame` on the user stack and changes the saved `EIP` to the handler. When the handler returns, it lands in `signal_return_stub`, which calls `sigreturn`.

**48. Which signal can't be caught?**
`SIGKILL`. `sys_sigaction` refuses it, and `send_signal` never ignores it.

**49. Can a process kill PID 1?**
No. `sys_kill` returns `-EPERM`, because orphans would have nobody to collect them.

**50. Can a normal user kill another user's process?**
No. `sys_kill` compares UIDs. Only root or the owner can.

**51. How does a pipe work?**
A 4 KB ring buffer in the kernel, with a read end and a write end. A reader sleeps when it's empty, and a writer sleeps when it's full.

**52. How does a reader know a pipe has ended?**
When `writers` reaches 0 and the buffer is empty, `read` returns 0, which means end of file.

**53. How do message queues differ from pipes?**
Messages keep their boundaries and have a type. `msgrcv` can ask for one type, or type 0 for the oldest of any type.

---

## C. Tricky questions

**54. Which is better, a shorter or a longer time slice?**
Shorter feels smoother but wastes more time on switching. Longer does more work but feels slower. 20 ms is a balance.

**55. Is the 20 ms slice exact?**
No. A switch waits for the next return to user mode, so a long system call can run past it.

**56. If the kernel isn't preempted, can a long system call block others?**
Yes, until it sleeps or returns. Long disk operations do sleep, so others can run.

**57. Are interrupts off during system calls?**
No. `syscall_interrupt` turns them back on, so the timer and disk keep working.

**58. Can a user change a program's priority?**
No. There's no `nice` command or system call. User programs run at priority 2, and the `swapper` thread runs at priority 1.

**59. How was priority tested, then?**
With kernel threads in the `sched` and `aging` boot tests. Those run in `make test`.

**60. Is there a limit on the number of processes?**
Yes, 64 (`MAX_PROCESSES`). Past that, `fork` returns `-EAGAIN`, which is -11. If memory runs out first, it returns `-ENOMEM`.

**60a. What happens in a fork bomb?**
It reaches 64 processes and `fork` starts failing, so the kernel keeps running. Ctrl+C stops all of them, and the shell carries on. The `forkbomb` test in `make test` checks this.

**60b. Why have a limit, if `fork` can already fail on low memory?**
Each process uses kernel memory. Without a limit, a bomb would use it all up, and then other parts of the kernel couldn't get memory either. The limit stops it long before that.

**61. Are PIDs reused?**
No. `next_pid` only goes up.

**62. Why does Ctrl+C not kill the shell?**
`init` ignores `SIGINT`, and ignored signals stay ignored across `fork` and `exec`. The shell sets it back to default only in each child.

**63. What does a writer get when nobody is reading a pipe?**
`-EPIPE`. Unlike Linux, it doesn't get a `SIGPIPE` signal.

**64. Can `sigreturn` be misused to gain kernel rights?**
No. It copies back only the normal registers, and it sets `EFLAGS` to a fixed value. It never takes `CS` or `SS` from user memory.

**65. Why is `SIGCHLD` ignored by default?**
Most programs just call `wait`, and a default action would end the parent every time a child finished.

**66. Do user programs have threads?**
No. Only kernel threads exist: `kernel` and `swapper`, plus the ones in boot tests. User threads are not built.

**67. What is the difference between `process_exit` and `thread_exit`?**
`process_exit` frees user resources and then calls `thread_exit`, which marks the PCB Terminated and switches away.

---

## D. Questions linking to other parts

**68. What does `fork` need from Memory?**
`copy_page_directory`, `copy_memory_areas` and `shared_memory_copy_to_child`.

**69. What does `exec` need from File Handling?**
`vfs_lookup`, `vfs_check_permission` and `Inode::read`.

**70. How does the timer reach the scheduler?**
IRQ 0 → `handle_interrupt` → `timer_interrupt` → `scheduler_tick`.

**71. What is the TSS's part in a switch?**
`schedule` calls `tss_set_kernel_stack`, so the next entry from ring 3 uses the new process's kernel stack.

**72. Why is the TLB flushed on a switch?**
Each process has its own page map. `paging_switch` loads `CR3`, which flushes old translations.

**73. How does a driver wake a sleeping process?**
`ata_interrupt` calls `WaitQueue::wake_all`, which calls `make_ready`.

**74. Why doesn't the file position reset after `fork`?**
Parent and child share one `File` object, which holds the offset. `process_fork` calls `hold_file` to count both users.

---

## Be honest about these

- There is no `nice` command, so priorities can't be changed from the shell.
- `fork` copies memory, because copy-on-write isn't built.
- There are no user threads.
- The scheduler has only Round-Robin with priorities. FCFS and SJF aren't selectable.
- The PRD's `schedtest` program doesn't exist. The `sched` and `aging` boot tests cover it.
- Pipes use real code in `fs/pipe.cpp`, but only `sys_pipe` is in `process/`.
