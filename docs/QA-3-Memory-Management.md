# Viva Q&A: Memory Management

**Role:** Member 3, Memory Management
**You own:** frames and frame table, paging, slab, page faults, Clock, shared memory
**Your code:** `memory/frame.cpp`, `memory/paging.cpp`, `memory/address_space.cpp`, `memory/virtual_memory.cpp`, `memory/slab.cpp`, `memory/kmalloc.cpp`, `memory/shared_memory.cpp`, `memory/sys_memory.cpp`, `include/memory.h`
**Your buddy:** Storage Hierarchy explains your part. You explain Integration.

## Your 30-second summary

> Every program believes it has its own memory. Page tables translate its virtual addresses to physical RAM, 4 KB at a time. Pages load only when first used, which is demand paging. When RAM passes 80% full, a background thread uses the Clock algorithm to move pages to swap until RAM is back to 70%. The kernel uses a slab allocator for its own objects. Bad memory access only ends the program that did it.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| Virtual memory | Each program gets a private address space | Programs could read or destroy each other's data |
| Paging (4 KB) | Memory needn't be contiguous, so there's no external fragmentation | RAM would break into unusable gaps |
| Two-level page tables | Only the parts in use need tables | A flat table would cost 4 MB per process |
| Bitmap for frames | One bit per frame is tiny and simple | Finding free RAM would need a larger structure |
| Frame table | Records users and the owner of each frame | Shared memory and Clock wouldn't know who uses a frame |
| Demand paging | Programs start fast, and unused pages never use RAM | Every program would load fully at start |
| Clock replacement | Close to LRU, but needs only one bit per page | True LRU would need a timestamp on every access |
| 80% / 70% limits | The gap stops constant swapping around one number | The system could swap on and off all the time |
| Slab allocator | The kernel makes many same-size objects, and slots are reused quickly | More wasted memory and slower allocation |
| Kernel mapped in every process | System calls don't need a page table change | Every system call would flush the TLB |
| `user_range_valid` | A bad pointer must fail the call, not crash the kernel | Any program could crash the OS |

## Numbers to remember

| Item | Value | Where |
| --- | --- | --- |
| Page and frame size | 4,096 bytes | `PAGE_SIZE` |
| Address split | 10 + 10 + 12 bits | `PD_INDEX`, `PT_INDEX` |
| Kernel starts at | `0xC0000000` (top 1 GB) | `KERNEL_VMA` |
| Program code starts at | `0x08048000` | `user/user.ld` |
| Stack | just below `0xC0000000`, up to 1 MB | `process/process.cpp` |
| Shared memory starts at | `0x40000000` | `SHARED_MEMORY_START` |
| `mmap` area | `0x50000000` to `0xB0000000` | `memory/sys_memory.cpp` |
| Frames with 128 MB of RAM | 32,736 | `memory` boot test |
| Largest RAM used | 768 MB | `MAX_PHYS_MEMORY` |
| Reserved for the kernel | 256 frames (1 MB) | `RESERVED_FRAMES` |
| Memory areas per process | 16 | `MAX_MEMORY_AREAS` |
| Shared segments | 8 in total, 16 pages each, 4 per process | `memory/shared_memory.cpp` |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Memory use | `free` | RAM used and free, swap, page faults, evicted pages |
| The same as a file | `cat /proc/meminfo` | `MemTotal: 130944 KB`, `PageFaults: ...` |
| Demand paging | Run `free` twice | The page fault count goes up, because `free` itself loaded pages |
| Swap and Clock | `swaptest` | `after 110 MB: swap used 18360 KB, 32760 pages evicted, 56337 page faults`, then `TEST-PASS swaptest` (about a minute; numbers vary a little) |
| Separate memory | `forktest` | Passes a check that a child's change isn't seen by its parent |
| Shared memory | `ipctest` | `shared counter: 12000` |

---

## A. Basic concepts

**1. What is memory management?**
Giving each program the memory it needs, keeping programs apart, and using RAM well.

**2. Virtual or physical address?**
Programs use virtual addresses. The CPU translates them through the page tables to physical addresses in RAM.

**3. What is a page, and what is a frame?**
A page is a 4 KB block of virtual memory. A frame is a 4 KB block of physical RAM.

**4. What is paging?**
Splitting memory into fixed pieces, so a program's pages can sit in any frames.

**5. What is a page table?**
A map from virtual pages to physical frames.

**6. What is the TLB?**
A hardware cache of recent translations, so the CPU rarely needs to walk the page tables.

**7. What is a page fault?**
CPU exception 14, raised when a program touches a page that isn't available or breaks its rules.

**8. Is a page fault always an error?**
No. Most are normal, such as the first use of a page. It's an error only if the address isn't valid for that process.

**9. What is demand paging?**
Loading a page only when it's first used.

**10. What is swapping?**
Moving pages between RAM and disk, so programs can use more memory than the RAM holds.

**11. What is the difference between internal and external fragmentation?**
Internal is unused space inside an allocated block. External is free memory split into gaps too small to use. Paging removes external fragmentation, but a page can have up to 4 KB of internal waste.

**12. Name some page replacement algorithms.**
FIFO, Optimal, LRU and Clock. We use Clock.

**13. What is Belady's anomaly?**
With FIFO, adding more frames can sometimes cause more page faults. LRU doesn't have this problem.

**14. What is thrashing?**
When the system spends more time swapping than running programs.

**15. What are stack and heap?**
The stack holds function calls and local variables, and grows down. The heap holds dynamic memory, and grows up.

**16. What is a dirty page?**
A page that changed since it was loaded, so it must be saved before removal.

**17. What is a memory leak?**
Memory that's allocated and never freed.

---

## B. Our design

**18. Draw the address space of a process.**

```text
0xFFFFFFFF  +-----------------------+
            | Kernel (ring 0 only)  |  same in every process
0xC0000000  +-----------------------+
            | Stack (grows down)    |  up to 1 MB
            +-----------------------+
            |                       |
0x50000000  | mmap area             |
0x40000000  | Shared memory         |
            | Heap (grows up)       |
            | BSS, Data             |
0x08048000  | Text (code)           |
            +-----------------------+
```

**19. How does the CPU translate an address?**
The top 10 bits pick a page directory entry, the next 10 pick a page table entry, and the last 12 are the offset inside the page.

**20. Work out `0x08049123`.**
Directory index 32 (`0x20`), table index 73 (`0x49`), offset `0x123`.

**21. Why two levels?**
A 32-bit space has about a million pages. A flat table would need 4 MB per process. With two levels, unused regions need no table at all.

**22. What flags are in a page table entry?**
Present, Writable, User, Accessed and Dirty come from the CPU's format. `PTE_SHARED` and `PTE_SWAPPED` are our own, in bits the CPU leaves free for the OS.

**23. What are the three states of an entry?**
Present (in RAM), `PTE_SWAPPED` (in swap, with the slot number in the entry), or zero (never loaded, so it comes from a `MemoryArea`).

**24. What is `MemoryArea`?**
One part of a program's space, such as code, heap or stack. It stores the start, end, flags, and which file and offset it comes from.

**25. How does the frame allocator work?**
A bitmap has one bit per frame. `alloc_frame` starts at `search_start`, takes the first clear bit, sets it, and returns the address. `search_start` saves rescanning used frames.

**26. How does it know how much RAM exists?**
`frame_init` reads the memory map GRUB gives through Multiboot. RAM size is never hard-coded.

**27. What is marked as used at boot?**
Low memory, the kernel, and the bitmap and frame table, which sit right after the kernel.

**28. Why a frame table as well as a bitmap?**
A bitmap only says used or free. `struct Frame` also stores a reference count, an owner page directory and a virtual address.

**29. Why the reference count?**
A shared frame has several users. `free_frame` only really frees it at 0.

**30. What does `alloc_frames` do differently?**
It finds several frames in a row. The kernel stack and buffer cache need physically contiguous memory.

**31. How is the kernel mapped?**
`paging_init` maps all RAM at `0xC0000000` without `PTE_USER`. New processes copy those directory entries in `make_page_directory`.

**32. Why can't a program read kernel memory, if it's mapped?**
Kernel pages have no User flag. The CPU raises a page fault in ring 3. `forktest` writes to `0xC0100000` and gets status 139.

**33. What are `phys_to_virt` and `virt_to_phys`?**
They add or subtract `0xC0000000`. All RAM is mapped at that offset, so the kernel can reach any frame.

**34. What did boot paging look like before `paging_init`?**
`boot/boot.asm` has a temporary directory with 4 MB pages, mapping the first 16 MB at both address 0 and `0xC0000000`.

**35. Walk through a page fault.**
`page_fault` reads the address from `CR2`, turns interrupts on, and calls `handle_page_fault`. That gets a frame, takes `pager_lock`, and checks the entry. A swapped page comes from swap. A zero entry is looked up in the memory areas, and the page is loaded from its file or filled with zeros. Then the program retries the instruction.

**36. Why are interrupts turned on in the handler?**
It may read the disk, and the disk needs IRQ 14 to finish.

**37. Why does it get the frame before taking `pager_lock`?**
`get_user_frame` can sleep until the swapper frees memory, and the swapper needs `pager_lock`. Holding it there would deadlock.

**38. How is BSS handled?**
`load_page` clears the frame first, then reads only `file_size` bytes. Anything past that stays zero.

**39. What happens with a bad address?**
`handle_page_fault` returns false. From user mode, `process_fault` prints `segmentation fault` and ends only that program. From kernel mode, it's a panic.

**40. Explain Clock in our code.**
`clock_hand` moves through all frames. `frame_get_owner` skips frames that aren't simple user pages. If `PTE_ACCESSED` is set, it's cleared and the page gets a second chance. Otherwise the page is evicted.

**41. Why up to two full turns?**
The first turn may only clear the Accessed bits.

**42. What happens to a dirty page?**
It gets a swap slot, its entry becomes the slot number plus `PTE_SWAPPED`, and `swap_write` saves it.

**43. Why change the entry before writing?**
So a fault on that page waits at `pager_lock` until the write finishes. It can't read half-written data.

**44. What happens to a clean page?**
The entry becomes 0 and the frame is freed. It can be loaded again from its file, or it was only zeros.

**45. Who evicts pages?**
`swapper_thread`, a kernel thread at priority 1. It sleeps until RAM reaches 80%, then evicts until RAM is under 70%.

**46. Can shared pages be evicted?**
No. They have more than one user, so `frame_get_owner` returns false.

**47. Are kernel pages ever swapped?**
No. Only pages with an owner set by `frame_set_owner` can be evicted, and that's only user pages.

**48. What is `RESERVED_FRAMES` for?**
It keeps 1 MB free for the kernel, so a page fault can't take the last memory the kernel needs.

**49. Explain the slab allocator.**
Each slab is one frame: a 32-byte header, then equal slots. Free slots are chained inside themselves, so allocating is just taking the first one.

**50. How does `slab_free` find the slab?**
It rounds the object's address down to the page. The header is at the start of that page.

**51. How does `kmalloc` work?**
Sizes up to 2,016 bytes use one of 8 slab sizes, from 16 bytes up. Bigger requests get whole frames.

**52. How does `kfree` know which kind it has?**
Slab objects never start on a page boundary, because of the header. A page-aligned pointer must be whole frames.

**53. How does `malloc` differ from `kmalloc`?**
`malloc` runs in user space in `user/libc.cpp`, on top of `sbrk`. It uses a first-fit list. `kmalloc` is for the kernel.

**54. What do `brk` and `sbrk` do?**
They move the end of the heap. Growing does almost nothing, because pages arrive on first use. Shrinking removes pages.

**55. What does `mmap` do here?**
Gives zero-filled memory from the `mmap` area. It can't map files.

**56. How does shared memory work?**
`shmget` allocates frames for a key. `shmat` maps those same frames into the process with `PTE_SHARED`. A write by one process is seen by the others straight away.

**57. Why `PTE_SHARED`?**
`copy_page_directory` must not copy these pages during `fork`, or the child's memory would stop being shared.

**58. When is a segment deleted?**
When `attach_count` falls to 0.

**59. How does `fork` copy memory?**
`copy_page_directory` walks the parent's tables, gets a new frame for each page, and copies it. If the page is in swap, it reads it with `read_swapped_page`.

**60. What does `user_range_valid` do?**
Checks that a user pointer is below the kernel and that each page is present with the right rights. It brings pages in if needed.

---

## C. Tricky questions

**61. Why do we flush the TLB on a context switch?**
The old translations belong to the previous process. Loading `CR3` in `paging_switch` flushes them.

**62. How do you update one page without a full flush?**
`invalidate_page` uses `invlpg` for just that address.

**63. Why is Clock used instead of true LRU?**
True LRU needs a record of every access. Clock only needs the hardware's Accessed bit.

**64. What happens if RAM and swap both fill up?**
We tested a 240 MB program on 128 MB of RAM and 32 MB of swap. It printed `out of memory`, then ended with a segmentation fault message. The OS stayed up, and `free` showed memory back to normal.

**65. Can a stack overflow be caught?**
Yes. The stack area is limited to 1 MB. Going past it touches an address outside every area, so the program gets a segmentation fault.

**66. Is code read-only?**
Yes, if the ELF segment has no write flag. `boot/boot.asm` also sets the WP bit in `CR0`, so even the kernel obeys read-only pages.

**67. Can code run from the stack?**
Yes. 32-bit x86 paging without PAE has no no-execute bit, so we can't block it.

**68. Why can the kernel fault on user memory safely?**
`page_fault` checks the address is below the kernel and the process has user memory. It handles the fault the same way as a user fault.

**69. What is the deadlock rule in the header of `virtual_memory.cpp`?**
The kernel never touches user memory while it holds a filesystem lock. A page fault could need that same lock to load a page. So `sys_read` copies through a kernel buffer.

**70. Do we use copy-on-write?**
No. It would make `fork` faster, but it's listed as not built.

**71. Does the `free` command show exact usage?**
It shows frames used, including the kernel and page tables. Reserved low memory counts too.

**72. Why does `free` show 127 MB-ish instead of 128 MB?**
The memory map from the BIOS reserves some areas, so usable top-of-memory is a little below 128 MB.

**73. Why is `operator new` allowed to panic?**
The kernel has no exceptions, so it can't report the failure to its caller.

**74. Can a process attach to any shared segment?**
Yes, if it knows the key. Shared memory has no permission check, which is a limitation.

---

## D. Questions linking to other parts

**75. What do you need from Storage?**
Swap slots and disk transfers: `swap_alloc_slot`, `swap_write`, `swap_read`, `swap_free_slot`.

**76. What do you need from File Handling?**
`load_page` calls `Inode::read` to fetch program pages.

**77. What do you need from Process Management?**
The swapper is a kernel thread made by `thread_create`, and it sleeps on wait queues.

**78. What does `exec` ask of you?**
A new page directory, memory areas, and the first stack pages.

**79. How does the scheduler use you?**
`schedule` calls `paging_switch` to load the next process's page map.

**80. How do you protect system calls?**
`user_range_valid` and `copy_string_from_user`, used by the handlers in `fs/`, `process/`, `resource/` and `memory/`.

---

## Be honest about these

- There's no copy-on-write, so `fork` of a big program is slow.
- `mmap` can't map files.
- Shared memory has no access control.
- There's no thrashing control, such as working sets.
- There's no protection from running code on the stack.
