# Viva Q&A: Storage Hierarchy

**Role:** Member 1, Storage Hierarchy
**You own:** disk layout, partition table, block layer, buffer cache, swap device
**Your code:** `storage/block.cpp`, `storage/swap.cpp`, `include/storage.h`, `tools/mkimage.sh`
**Your buddy:** File Handling explains your part. You explain Memory Management.

## Your 30-second summary

> Fast memory is small and big storage is slow, so the OS keeps data at the right level. Our disk has three partitions: 16 MB for the kernel, 32 MB for swap and 64 MB for user files. A 128-block buffer cache keeps recent disk blocks in RAM, so a repeat read never touches the disk. Writes go to disk straight away, which is called write-through. Swap gives memory somewhere to go when RAM fills up.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| Storage hierarchy | No single memory is fast, big and cheap, so we combine them | Everything would be slow, or the machine could hold very little |
| Partitions | Keep kernel, swap and user files apart | A full user disk could damage the kernel or swap |
| MBR partition table | A standard that GRUB and Linux tools understand | We would need our own format and tools |
| 4 KB block size | Matches the page size, so one page fits one block | Swapping one page would touch many pieces of disk |
| Buffer cache | Disk is far slower than RAM, and programs reread the same blocks | Every read would wait for the disk |
| LRU replacement | Recently used blocks are likely to be used again | We could throw out the blocks we need most |
| Write-through | Simple to explain, and a crash loses less | With write-back, a crash could lose blocks waiting in RAM |
| Swap partition | Lets programs use more memory than the RAM | Programs would fail as soon as RAM ran out |
| Bitmap for swap slots | One bit per slot is small and quick to search | A list of slots would use far more memory |
| Fixed-size swap slots | No folders or names needed, just a slot number | Using a filesystem would add work and locks to every swap |

## Numbers to remember

| Item | Value | Where |
| --- | --- | --- |
| Sector size | 512 bytes | `SECTOR_SIZE` |
| Block size | 4,096 bytes, which is 8 sectors | `BLOCK_SIZE` |
| First partition starts at | sector 2048 (1 MB) | `tools/mkimage.sh` |
| Kernel partition | 16 MB, ext2 | `KERNEL_SECTORS` |
| Swap partition | 32 MB, 8,192 slots | `SWAP_SECTORS` |
| Data partition | 64 MB, ext2, mounted at `/` | `DATA_SECTORS` |
| Whole image | 118,489,088 bytes (113 MB) | `os.img` |
| Buffer cache | 128 blocks, which is 512 KB | `BUFFER_COUNT` |
| Swap starts / stops | 80% RAM used / 70% RAM used | `memory/virtual_memory.cpp` |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Swap size | `free` | `Swap: ... 32768 KB total` |
| Swap being used | `swaptest`, then `free` | `after 110 MB: swap used 18360 KB, 32760 pages evicted` (numbers vary a little), ending in `TEST-PASS swaptest` |
| The same numbers as a file | `cat /proc/meminfo` | `SwapTotal: 32768 KB` |
| A file on disk | `cat /etc/hello.txt` | `Hello from the OS Project disk.` |

**Proof of the buffer cache:** the boot test named `disk` prints `disk reads on the second read: 0`. It runs in `make test` on the build machine, not from the OS shell.

---

## A. Basic concepts

**1. What is the storage hierarchy?**
Storage arranged by speed and size: CPU registers, CPU cache, RAM, then disk. Each level down is bigger and slower.

**2. Why have levels at all?**
Fast memory is expensive and small. Disk is cheap and big but slow. Keeping the data in use in fast memory makes the computer feel fast while still holding a lot.

**3. Give the rough speed of each level.**
Registers under 1 ns, cache 1 to 10 ns, RAM 50 to 100 ns, disk 0.1 to 10 ms. Disk is thousands of times slower than RAM.

**4. Who manages each level?**
The compiler and CPU manage registers. Hardware manages CPU cache. Our Memory Manager manages RAM. Our Storage and File Handling code manage the disk.

**5. What is locality of reference?**
Programs tend to reuse recent data (temporal locality) and data close to it (spatial locality). This is why caching works.

**6. What is volatile memory?**
Memory that loses its data without power, such as RAM. Disk is non-volatile, so files survive a restart.

**7. What is a sector, and what is a block?**
A sector is the disk's smallest unit, 512 bytes here. A block is the unit our OS uses, 4 KB, which is 8 sectors.

**8. What is the MBR?**
The first sector of the disk. It holds boot code and the partition table, and ends with the signature `0xAA55`.

**9. What is a partition?**
A named part of the disk, with a start and a size, treated as a separate area.

**10. What is a buffer cache?**
RAM that holds copies of recently used disk blocks, so a repeat read doesn't need the disk.

**11. What is the difference between a cache hit and a miss?**
A hit finds the block in RAM. A miss must read it from disk, then keep a copy.

**12. Write-through or write-back?**
Write-through writes to disk at once. Write-back keeps the change in RAM and writes it later, which is faster but risks more data in a crash.

**13. What is swap?**
Disk space that holds pages moved out of RAM, so programs can use more memory than the RAM installed.

**14. Is swap the same as a file?**
No. Our swap is a raw partition of numbered slots, with no names or folders.

---

## B. Our design

**15. Draw your disk layout.**

```text
Sector 0            MBR: boot code, partition table, 0xAA55
Sectors 1-2047      GRUB's second stage
Partition 1         16 MB  ext2, holds /boot/kernel.elf
Partition 2         32 MB  swap slots
Partition 3         64 MB  ext2, mounted as /
```

**16. Which function reads the partition table?**
`read_partition_table` in `storage/block.cpp`. It reads sector 0, checks `0xAA55`, and copies the entries found at byte offset 446.

**17. What happens if the signature is wrong?**
The kernel panics with `the disk has no partition table`. It can't carry on without knowing where its data is.

**18. How is a block number turned into a disk address?**
`block_lba` does it: partition start sector + block number × 8. It panics if the block is outside the partition.

**19. Why do block numbers count from the start of each partition?**
So block 0 of the filesystem and sector 0 of the disk never clash, and a partition could move without changing the filesystem.

**20. Explain the `Buffer` struct.**
It stores the partition, block number, `valid` (data is real), `busy` (someone is using it), `last_used`, a wait queue, and a pointer to 4 KB of data.

**21. Walk through `block_get`.**
It looks for the block with `find_buffer`. On a hit it marks it busy. On a miss it picks a buffer with `find_reusable_buffer`, reads from disk with `ata_read`, and marks it valid. If someone else is using the block, it sleeps and looks again.

**22. Why does it look again after waking?**
While it slept, that buffer may have been reused for another block.

**23. What does `block_put` do?**
Marks the buffer free and wakes anyone waiting for it. The data stays, so it can still be a cache hit later.

**24. What replacement policy do you use?**
Least Recently Used. Each use stamps `last_used` with a growing counter, and the smallest stamp among non-busy buffers is reused.

**25. Why LRU and not FIFO?**
FIFO could evict a block used every second, just because it arrived first. LRU keeps the blocks in active use.

**26. Why did you choose write-through?**
It's simple, and each write is safe on disk when it returns. The cost is speed, which matters less than reliability here.

**27. How do you prove write-through?**
`block_write` calls `ata_write`, and `ata_write` sends `COMMAND_FLUSH` before returning. There is no delayed writing anywhere.

**28. Do reads get faster after a write?**
Yes. The written data stays in the buffer, so a later read is a hit.

**29. How big is the cache, and why?**
128 blocks, or 512 KB. That is enough for inodes, bitmaps and active file data, and it uses under 1% of the 128 MB of RAM.

**30. What is the swap map?**
The `slot_used` bit array in `storage/swap.cpp`, with one bit per slot. A set bit means the slot is in use.

**31. How does `swap_alloc_slot` find a slot?**
It starts after the last slot it gave out (`search_from`) and goes round until it finds a clear bit. If none is free, it returns -1.

**32. Why start from `search_from`?**
The slots just before it were recently given out, so starting there usually finds a free one sooner than rescanning from slot 0.

**33. What does `swap_free_slot` do on a bad slot?**
It panics. Freeing a slot twice means there's a bug, and carrying on could corrupt someone's memory.

**34. Why doesn't swap use the buffer cache?**
A page is written once and read back once, so caching it in RAM would defeat the point of moving it out of RAM.

**35. Why does `swap_write` skip the flush?**
Swap contents only matter until the next reboot, so skipping the flush saves time.

**36. Who decides when to swap?**
Memory Management. Its swapper thread starts at 80% and stops at 70%. Storage only provides slots and disk access.

**37. How big can swap get?**
Our partition gives 8,192 slots. The code allows up to `MAX_SWAP_SLOTS`, 32,768 slots, which is 128 MB.

**38. How is the disk image built?**
`tools/mkimage.sh` makes two ext2 images with `mke2fs`, creates the partition table with `sfdisk`, copies the images in with `dd`, and installs GRUB's boot code by hand.

---

## C. Tricky questions

**39. Why not write-back, if it's faster?**
Write-back needs a background flusher and can lose more in a crash. For a teaching OS, simple and safe is the better trade.

**40. Does write-through mean the filesystem can't be corrupted?**
No. One operation may need several block writes, such as a bitmap and then an inode. A crash between them can leave a mismatch. We have no journal, so `e2fsck` would need to repair it.

**41. What happens if all 128 buffers are busy?**
`block_get` panics with `every disk buffer is in use`. A buffer is held only briefly, so this shouldn't happen normally.

**42. Who keeps CPU caches in sync?**
Hardware. We run on one core, and x86 keeps its caches coherent.

**43. What happens when swap is full?**
`evict_one_page` can still drop clean pages. If nothing can be removed, `out_of_swap` is set, and a program that needs memory ends. We tested this with a 240 MB program, which is more than RAM plus swap. It printed `out of memory` and stopped, and the OS kept running.

**44. Is the lookup fast?**
It's a straight search of 128 entries. That's slower in theory than a hash table, but it's tiny compared with a disk read, and it's easy to explain.

**45. Could two processes read the same block at once?**
No. The first marks it busy, and the second sleeps on `waiters` until `block_put`.

**46. Why does the kernel have its own partition if the kernel never mounts it?**
GRUB reads the kernel from it at boot. The running OS only needs partition 3 as `/`, and it also uses partition 2 for swap.

**47. Does the PRD's block layer match the code?**
Mostly. The PRD says `block_read` and `block_write`. The code has `block_get`, `block_put` and `block_write`, because the same call both reads a block and reserves it. Swap also goes straight to the disk driver, unlike the PRD.

**48. Why 4 KB and not 1 KB blocks?**
4 KB matches the page size, and it is ext2's largest common block size. Small files waste some space, but big files need fewer pointer blocks.

**49. Where do files in `/tmp` live?**
In RAM, in tmpfs. They don't use the disk or the buffer cache, so they disappear when the OS stops.

**50. What does the `-snapshot` QEMU flag do to your disk?**
Writes go to a temporary copy, so the image stays clean and each run starts fresh. To keep files, leave out `-snapshot`.

**50a. Do you have disk scheduling, such as SCAN or SSTF?**
No. `ata_lock` lets one request run at a time, in whatever order processes get the lock. An emulated disk has no moving head, so a scheduling algorithm would show no real benefit here.

---

## D. Questions linking to other parts

**51. What is the path from `read()` to the disk?**
`sys_read` → `Ext2Inode::read` → `read_data` → `map_block` → `block_get` → `ata_read`.

**52. How does File Handling use you?**
`fs/ext2.cpp` calls `block_get`, `block_put` and `block_write` for all its disk access.

**53. How does Memory Management use you?**
`evict_one_page` calls `swap_alloc_slot` and `swap_write`. `handle_page_fault` calls `swap_read` and `swap_free_slot`.

**54. How do you use Resource Management?**
All disk access goes through `ata_read` and `ata_write`, and those put the process to sleep until IRQ 14.

**55. What happens to the process during a cache miss?**
It sleeps in a wait queue, so other processes can use the CPU. IRQ 14 wakes it.

---

## Be honest about these

- There is no journal, so a crash during a multi-step write can leave errors.
- The cache search is a simple loop, not a hash table.
- There's no read-ahead, and no disk scheduling algorithm.
- The disk-read counter isn't shown by any shell command. The proof is in `make test`.
