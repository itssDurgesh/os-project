# Viva Q&A: Resource Management

**Role:** Member 4, Resource Management
**You own:** sync primitives, IRQ table, keyboard, ATA and VGA drivers, quotas, Banker's Algorithm
**Your code:** `resource/sync.cpp`, `resource/banker.cpp`, `resource/ata.cpp`, `resource/keyboard.cpp`, `resource/mouse.cpp`, `resource/vga.cpp`, `resource/user_semaphore.cpp`, `include/sync.h`, `include/input.h`
**Your buddy:** Integration explains your part. You explain Process Management.

## Your 30-second summary

> All programs share the same hardware. This part decides who gets what, and how the OS responds when a device needs attention. Interrupts tell the kernel when a key is pressed or the disk has finished. Drivers hide the device details. Locks stop two processes corrupting shared data. Banker's Algorithm refuses unsafe resource requests, so deadlock can't form. CPU quotas stop one process taking too much CPU.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| Interrupts | The device tells us when it needs attention | The CPU would waste time polling every device |
| PIC remap | Default IRQ vectors clash with CPU exceptions | The timer would look like a double fault |
| DMA | The disk controller copies data by itself | The CPU would copy every byte, as in PIO |
| PIO fallback | Works if no DMA controller is found | The disk wouldn't work on such a machine |
| Sleeping during disk I/O | The waiting process uses no CPU | CPU time would be wasted waiting |
| Mutex | One process at a time in a critical section, and waiters sleep | Data would be corrupted by races |
| Semaphore | A counter that can let several through, or signal between processes | Producer and consumer couldn't coordinate |
| Interrupts-off Spinlock | On one CPU, only an interrupt can break in | Spinning would never end |
| Banker's Algorithm | Refuses unsafe requests before deadlock happens | Processes could wait on each other forever |
| Fixed lock order | Prevents circular waiting between kernel locks | Two kernel paths could deadlock |
| CPU quotas | A hard limit on CPU use per second | One process could dominate |
| Key queue | The interrupt handler must be quick | Keys would be lost while a program is busy |

## Numbers to remember

| Item | Value |
| --- | --- |
| Timer | IRQ 0, vector `0x20`, 100 Hz |
| Keyboard | IRQ 1, vector `0x21`, port `0x60` |
| Serial | IRQ 4, vector `0x24` |
| Mouse | IRQ 12, vector `0x2C` |
| Disk | IRQ 14, vector `0x2E`, ports `0x1F0` to `0x1F7` |
| System calls | vector `0x80` |
| Page fault | vector 14 (`0x0E`) |
| CPU exceptions | vectors `0x00` to `0x1F` |
| Screen | 80 × 25 at physical address `0xB8000`, 2 bytes per character |
| Key queue | 256 characters |
| Banker's totals | 8 pipes, 4 message queues, 4 shared memory segments |
| User semaphores | 16 |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Semaphores and shared memory | `ipctest` | `shared counter: 12000`, then `TEST-PASS ipctest` |
| Banker's Algorithm | `banktest` | `unsafe request is waiting, as it should`, then `request granted once it was safe` |
| CPU quota | `quotatest` | `cpu ticks without quota: 200, with quota: 50` (the second number is about 50 to 60) |
| Keyboard codes | `keytest`, press keys, press `q` | The number for each key |
| Disk driver mode | Boot message | `Disk: using DMA` |
| Ctrl+C | `sleep 100`, then Ctrl+C | The prompt returns |

---

## A. Basic concepts

**1. What is an interrupt?**
A signal that stops the CPU's current work so it can handle an event, then go back.

**2. Interrupt or exception?**
An interrupt comes from a device, at any time. An exception comes from the running instruction, such as divide by zero.

**3. Polling or interrupts?**
Polling keeps asking the device. Interrupts let the device tell the CPU, which saves CPU time.

**4. What is an ISR?**
Interrupt Service Routine: the function that handles one interrupt.

**5. What is the IDT?**
Interrupt Descriptor Table: it tells the CPU which code handles each of the 256 vectors.

**6. What is the PIC?**
The Programmable Interrupt Controller passes device interrupts to the CPU.

**7. What is EOI?**
End of Interrupt: tells the PIC it can send more interrupts.

**8. What is a device driver?**
Code that knows how to talk to one device, and gives the OS a simple interface.

**9. What is DMA?**
Direct Memory Access: a controller copies data between device and RAM without the CPU.

**10. What is PIO?**
Programmed I/O: the CPU copies each word itself.

**11. What is a race condition?**
When the result depends on the timing of two processes using shared data.

**12. What is a critical section?**
Code that uses shared data, and must run by one process at a time.

**13. What is mutual exclusion?**
Making sure only one process is in a critical section at a time.

**14. Spinlock or mutex?**
A spinlock keeps the CPU busy while waiting. A mutex puts the process to sleep.

**15. Semaphore or mutex?**
A semaphore is a counter that may let several through. A mutex has one owner, and only that owner can unlock it.

**16. What is a condition variable?**
A way to sleep until something becomes true, used together with a mutex.

**17. What is deadlock?**
Processes waiting on each other forever.

**18. What are the four conditions for deadlock?**
Mutual exclusion, hold and wait, no preemption, and circular wait. All four must be true.

**19. Prevention, avoidance or detection?**
Prevention makes one condition impossible. Avoidance checks each request, which is what Banker's does. Detection lets deadlock happen, then recovers.

**20. What is a safe state?**
One where at least one order exists in which every process can finish.

**21. Is an unsafe state a deadlock?**
No. It only means deadlock is possible. Banker's stays out of unsafe states to be sure.

**22. What is starvation, compared with deadlock?**
Starvation is one process waiting a very long time while others move. In deadlock, none of the waiting processes can move.

**23. What is priority inversion?**
A high-priority process waits for a lock held by a low-priority one.

---

## B. Our design

**24. List your IRQ table.**

| Source | Vector | Handler | Registered in |
| --- | --- | --- | --- |
| CPU exceptions | `0x00`-`0x1F` | `handle_interrupt` (default) | `hal/idt.cpp` |
| Page fault | `0x0E` | `page_fault` | `memory/paging.cpp` |
| Timer | `0x20` | `timer_interrupt` | `hal/pit.cpp` |
| Keyboard | `0x21` | `keyboard_interrupt` | `resource/keyboard.cpp` |
| Serial | `0x24` | `serial_interrupt` | `hal/serial.cpp` |
| Mouse | `0x2C` | `mouse_interrupt` | `resource/mouse.cpp` |
| Disk | `0x2E` | `ata_interrupt` | `resource/ata.cpp` |
| System call | `0x80` | `syscall_interrupt` | `kernel/syscall.cpp` |

**25. Why remap the PIC?**
By default, IRQ 0 uses vector 8, which is the CPU's double fault. `pic_init` moves IRQs to `0x20`-`0x2F`.

**26. What happens from a key press to the handler?**
The keyboard raises IRQ 1. The PIC tells the CPU. The CPU looks up vector `0x21` and jumps to a stub in `hal/isr.asm`. `common_interrupt_code` saves the state and calls `handle_interrupt`, which calls `keyboard_interrupt`.

**27. Why 256 stubs?**
Each one pushes its own vector number, so one common function knows which interrupt arrived.

**28. Why do some stubs push a dummy 0?**
The CPU pushes an error code for some exceptions only. The dummy keeps `struct Registers` the same shape.

**29. When is EOI sent?**
At the start of `handle_interrupt`, before the handler. The handler might lead to a process switch, and EOI must not wait for that process to run again.

**30. How does the keyboard driver work?**
`keyboard_interrupt` reads a scancode from port `0x60`, tracks Shift and Ctrl, turns it into a character, and calls `input_put_char`.

**31. How are arrow keys handled?**
They arrive after a `0xE0` byte. `send_arrow_key` turns them into the same `ESC [ A` codes a terminal uses.

**32. What happens when Ctrl+C is pressed?**
It becomes character 3, and `input_put_char` calls `send_signal_to_all_programs(SIGINT)`.

**33. How does a program read a key?**
`input_get_char` takes one from the queue. If it's empty, the process sleeps until a key arrives.

**34. Why is the queue needed?**
A handler must be short and can't sleep. It stores the key, and the program takes it later.

**35. How does the screen work?**
Memory at `0xB8000` is the screen, with 2 bytes per character: the letter and its colour. `vga_putc` writes there.

**36. What are escape codes?**
Sequences like `ESC [ 2 J` that clear the screen or move the cursor. Programs use the same ones as a Linux terminal, so the editor also works over the serial port.

**37. How does the mouse driver work?**
Each move sends 3 bytes, one interrupt each. `mouse_interrupt` collects them, and `add_event` makes one `MouseEvent`. Programs read them from `/dev/mouse`.

**38. How does the disk driver find DMA?**
`find_dma_controller` looks for a PCI device of class 1, subclass 1, turns on bus mastering, and reads the ports from BAR4.

**39. Walk through a DMA read.**
`dma_transfer` fills `dma_table` with the buffer's physical address and size, sets the direction, sends `COMMAND_READ_DMA`, and starts the controller. The process sleeps in `wait_for_irq` until IRQ 14.

**40. Why must a DMA address be physical?**
The controller works on the bus, without paging. So `virt_to_phys` is used. The buffer must be in kernel memory, where that's a simple subtraction.

**41. What is the difference between `pio_read` and `dma_transfer` in interrupts?**
PIO gets an interrupt for each sector. DMA gets one when everything is done.

**42. Why is there an `ata_lock`?**
The disk can do only one command at a time.

**43. What does the flush command do?**
`COMMAND_FLUSH` makes the drive save its own cache to the disk, for real write-through.

**44. How is your Spinlock made?**
`Spinlock::lock` turns interrupts off and marks it locked. It panics if already locked.

**45. Why does it not actually spin?**
On one CPU with interrupts off, nothing else can run to unlock it. Spinning would hang forever, so a second lock is a bug.

**46. How is your Mutex made?**
`Mutex::lock` sleeps in `waiting_list` while it's locked, then records the owner. `Mutex::unlock` checks the owner and wakes one waiter.

**47. Why `while` and not `if` in `Mutex::lock`?**
After waking, another process may have taken the lock first, so it must check again.

**48. How is your Semaphore made?**
`wait` sleeps while the count is 0, then subtracts 1. `post` adds 1 and wakes one waiter.

**49. How is your CondVar made?**
`CondVar::wait` releases the mutex and sleeps with interrupts off, so a signal can't be missed. It relocks the mutex after waking.

**50. Why turn interrupts off inside these locks?**
Wake-ups come from interrupt handlers too. Interrupts off makes checking and sleeping one safe step.

**51. Why two kinds of semaphore?**
`Semaphore` is for the kernel. `UserSemaphore` in `resource/user_semaphore.cpp` is for programs, and a signal can wake it, so Ctrl+C can stop a waiting program.

**52. Which kernel locks are in use?**
`pager_lock`, `fs.lock` and `ata_lock`. All three are mutexes.

**53. What is the kernel's lock order?**
`pager_lock`, then `fs.lock`, then `ata_lock`, which is the order they're taken when a page loads from a file.

**54. Explain Banker's data.**
`available` is a global array. Each PCB has `banker_maximum` and `banker_allocated`. What a process still needs is maximum minus allocated.

**55. Walk through `system_is_safe`.**
It copies `available`. It repeatedly finds a process that could finish with what's free, pretends it finished, and adds back what it holds. If every process can finish, the state is safe.

**56. Walk through `sys_res_request`.**
It checks the process declared first, and isn't asking for more than its maximum. `try_to_grant` pretends to give the resources and tests safety. If unsafe, it undoes that, and the process sleeps until someone releases.

**57. Explain the `banktest` numbers.**
There are 8 pipes. The parent declares 6 and holds 4. The child declares 6 and asks for 3, leaving 1 free. The parent could need 2 more and the child 3 more, and neither fits in 1, so it's unsafe. When the parent releases its 4, the request is safe and is granted.

**58. What happens to resources when a process dies?**
`banker_process_exit` gives them back and wakes waiters.

**59. How do CPU quotas work?**
`cpu_quota` is the ticks allowed per second. When a process reaches it, `switch_process_if_needed` puts it in `over_quota` until the next second.

**60. What did `quotatest` show?**
Over 2 seconds, a busy process got 200 ticks with no quota. With a quota of 20 per second it got about 50 to 60, depending on the run.

**61. Why about 50 to 60 and not 40?**
The run can touch three 1-second windows, which allows up to 60. The test accepts up to 65.

---

## C. Tricky questions

**62. Does Banker's control real pipes?**
No, and it's important to say this clearly. `res_request` is a separate system call. `pipe()`, `msgget()` and `shmget()` don't call it. Banker's works correctly, and `banktest` proves it, but it does its own counting.

**63. So what stops real deadlock on pipes?**
Nothing in Banker's. A pipe deadlock is still possible if two programs wait on each other. A process can be stopped by a signal, because pipes sleep with `can_be_interrupted` set.

**64. Why not use Banker's for everything?**
It needs maximum claims in advance and adds work to each request. It also assumes the set of processes is known.

**64a. Why use Banker's at all, if it isn't practical?**
Say this: "Banker's is the classic deadlock avoidance algorithm, so we built it to show safe and unsafe states working in a real kernel. It isn't practical for a general OS, and Linux and Windows don't use it. Our kernel prevents deadlock between its locks with a fixed lock order instead."

**64b. Would connecting it to real pipes make it useful?**
Not really. Every program would have to declare its maximum needs first, and most programs can't know them. That's why we keep it as a clear demo.

**65. Are the kernel's Spinlock, Semaphore and CondVar actually used?**
They're built and pass the `sync` boot test, but only `Mutex` is used by subsystems. Short interrupts-off sections do the spinlock's job in the scheduler and drivers.

**66. Do you handle priority inversion?**
No. Our mutex has no priority inheritance.

**67. Can an interrupt handler use a Mutex?**
No. A handler can't sleep. It isn't a process that can be put on a queue.

**68. What happens if a handler sends EOI late?**
The PIC blocks that IRQ and lower ones until EOI arrives.

**69. What if the key queue is full?**
The key is dropped, because nobody is reading.

**70. Does the system use a real watchdog timer?**
No. The PRD's "watchdog" just means the 10 ms timer tick.

**71. Can a user set a quota from the shell?**
There's no shell command. `set_quota` is a system call for root, and `quotatest` uses it.

**72. Is there a network driver?**
No. It's in the Cut tier.

**73. What is special about the mouse's PS/2 setup?**
It shares port `0x60` with the keyboard, so `mouse_init` keeps interrupts off while it sets up.

**74. Why does the PRD mention a display framebuffer if we use VGA text?**
Framebuffer was moved to Stretch, and it wasn't built. It's listed as an open question for the instructor.

---

## D. Questions linking to other parts

**75. How do you use the scheduler?**
Locks and drivers call `WaitQueue::sleep` and `WaitQueue::wake_one` to block and wake processes.

**76. How does File Handling reach your drivers?**
`/dev/console` calls `input_get_char`, and output goes through `kprintf` to `vga_putc`. `/dev/mouse` calls `mouse_get_event`.

**77. How does Storage reach you?**
`block_get` and `swap_read` call `ata_read`.

**78. What does the HAL give you?**
`inb`, `outb`, `insw`, `outsw`, `set_interrupt_handler`, `pic_unmask` and `pci_find_device`.

**79. How do signals connect to you?**
`sem_wait`, keyboard reads and Banker's waits all sleep in a way a signal can wake.

---

## Be honest about these

- Banker's isn't connected to real pipes, queues or shared memory.
- `Spinlock`, kernel `Semaphore` and `CondVar` are tested but not used by subsystems.
- There's no priority inheritance.
- The key layout is US only.
- The mouse has no on-screen pointer. `mousetest` prints its movements.
- There's no framebuffer or network driver.
