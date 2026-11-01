# Viva Q&A: File Handling

**Role:** Member 5, File Handling
**You own:** VFS, ext2, devfs, FD and open-file tables, permissions
**Your code:** `fs/vfs.cpp`, `fs/ext2.cpp`, `fs/file.cpp`, `fs/sys_file.cpp`, `fs/devfs.cpp`, `fs/tmpfs.cpp`, `fs/procfs.cpp`, `include/vfs.h`
**Your buddy:** Process Management explains your part. You explain Storage Hierarchy.

## Your 30-second summary

> A disk is just numbered blocks. This part adds file names, folders and permissions. Our main filesystem follows the real ext2 format, and the Linux tool `e2fsck` agrees it's clean. An inode stores a file's details and block pointers, and a directory maps names to inodes. The VFS lets the same `read()` work on ext2, device files, RAM files and `/proc`. Permissions are checked with Unix rwx bits against the user's UID and GID.

## Why we use this

| What we use | Why we use it | What would go wrong without it |
| --- | --- | --- |
| Filesystem | People and programs use names, not block numbers | Nobody could find their data |
| Real ext2 format | Linux tools can build and check our disk | We would need our own tools and couldn't prove correctness |
| Inode separate from the name | One file can have several names, and renames are simple | Directory entries would be huge |
| Direct + indirect pointers | Small files stay fast, and big files are still possible | Either a large inode or a tiny file limit |
| Bitmaps for blocks and inodes | Compact, and the real ext2 format | Finding free space would be slow |
| VFS | One interface over different filesystems | System calls would need code for each filesystem |
| FD table per process | Each process has its own numbering | Processes would clash over file numbers |
| Shared open-file object | Parent and child share one file position after `fork` | Output from parent and child would overwrite itself |
| Unix permissions | Simple, well known, and checked on every open | Any user could read `/etc/shadow` |
| devfs | Devices look like files | Programs would need special calls for each device |
| tmpfs | Fast scratch space in RAM | Temporary files would slow the disk |
| procfs | System information appears as ordinary text files | `/proc` data would need special commands |
| Copying through a kernel buffer | Avoids a deadlock between page faults and `fs.lock` | A page fault could wait forever for a lock held by the same process |

## Numbers to remember

| Item | Value | Where |
| --- | --- | --- |
| Block size | 4 KB | `BLOCK_SIZE` |
| Inode size | 128 bytes | `struct ext2_inode` |
| ext2 magic number | `0xEF53` | `EXT2_MAGIC` |
| Superblock position | byte 1024 of the partition | `SUPERBLOCK_OFFSET` |
| Root folder's inode | 2 | `ROOT_INODE` |
| Pointers in an inode | 12 direct, 1 indirect, 1 double-indirect | `ext2.cpp` |
| Pointers per block | 1,024 | `POINTERS_PER_BLOCK` |
| Direct blocks cover | 48 KB | 12 × 4 KB |
| Indirect covers | 4 MB more | 1,024 × 4 KB |
| Double-indirect covers | 4 GB more | 1,024 × 1,024 × 4 KB |
| Data partition | 16,384 blocks, 64 MB | `tools/mkimage.sh` |
| Open files per process | 256 | `MAX_OPEN_FILES` |
| Longest name / path | 255 / 256 bytes | `include/abi.h` |
| Mount table | 4 entries | `MAX_MOUNTS` |

## Demos you can run

| Show this | Type this | You should see |
| --- | --- | --- |
| Filesystem test | `fstest` | `TEST-PASS fstest` (it takes a few seconds, because writes go straight to disk) |
| Permissions test | `permtest` | `TEST-PASS permtest` |
| tmpfs and procfs test | `tmptest` | `TEST-PASS tmptest` |
| Permissions | `ls -l /etc` | `-rw------- root 177 shadow` |
| Refused as a normal user | Log in as `student`, then `cat /etc/shadow` | `cat: /etc/shadow: permission denied` |
| Change permissions | `touch /tmp/p.txt`, `chmod 600 /tmp/p.txt`, `ls -l /tmp` | `-rw------- root 0 p.txt` |
| Redirection | `echo saved > /tmp/a.txt`, `cat /tmp/a.txt` | `saved` |
| Appending | `echo more >> /tmp/a.txt` | The file now has 2 lines |
| Input redirection | `wc < /tmp/a.txt` | `2 2 11` |
| Pipes as files | `ls / \| wc` | Counts the root folder's entries |
| Device files | `ls /dev` | `console`, `input`, `null`, `mouse` |
| Virtual files | `cat /proc/meminfo` | Values made at the moment you read it |

---

## A. Basic concepts

**1. What is a file?**
A named collection of data on storage.

**2. What is a filesystem?**
The structure on disk that turns numbered blocks into named files and folders.

**3. What is a directory?**
A special file that holds a list of names and their inode numbers.

**4. What is an inode?**
A record of a file's type, permissions, owner, size, timestamps, link count and block pointers.

**5. Where is the file name stored?**
In the directory entry, not in the inode.

**6. What is a superblock?**
The filesystem's summary: total and free blocks, block size, inode count and more.

**7. What is a block group?**
ext2 divides the disk into groups, each with its own bitmaps and inode table. Our 64 MB partition has one.

**8. What is a file descriptor?**
A small number a process uses to refer to an open file.

**9. What are FDs 0, 1 and 2?**
Standard input, standard output and standard error.

**10. What is a hard link?**
A second name for the same inode. The `links` count records how many names it has.

**11. What is a symbolic link?**
A small file that stores a path to another file. We don't have these.

**12. What is a mount?**
Attaching a filesystem to a folder, so its files appear there.

**13. What is a VFS?**
A common layer that hides differences between filesystems.

**14. Explain `rwxr-xr--`.**
The owner can read, write and execute. The group can read and execute. Others can only read.

**15. What is `755` in binary terms?**
Each digit is 3 bits: 7 is `rwx`, 5 is `r-x`.

**16. What does `x` mean on a folder?**
Permission to search it, meaning to go through it to reach what's inside.

**17. Name some file allocation methods.**
Contiguous, linked and indexed. ext2 uses indexed allocation, through pointers in the inode.

---

## B. Our design

**18. Draw the on-disk layout.**

```text
Block 0    first 1024 bytes unused, then the superblock
Block 1    group descriptor table
...        block bitmap, inode bitmap, inode table
...        file and folder data
```

**19. How was the filesystem built?**
`tools/mkimage.sh` runs `mke2fs` with 4 KB blocks, 128-byte inodes and only the `filetype` feature.

**20. How do you know the format is correct?**
`make test` writes files from our OS, then runs Linux `e2fsck` on the disk. It finds no errors.

**21. What does `ext2_mount` do?**
Reads the superblock, checks the magic number and block size, works out the group count, reads the group table, and returns the root inode (2).

**22. How do you find an inode on disk?**
`inode_location` works out the group as `(number - 1) / inodes_per_group`, then the block and offset inside the inode table.

**23. Why `number - 1`?**
ext2 numbers inodes from 1, and bitmap positions count from 0.

**24. Explain the 15 block slots.**
Slots 0 to 11 point directly to data. Slot 12 is indirect, slot 13 is double-indirect, and slot 14 is triple-indirect, which we keep but don't use.

**25. Walk through `map_block` for block 500 of a file.**
It's past the 12 direct blocks. Subtracting 12 gives 488, which is under 1,024. So it reads the indirect block from slot 12, and takes entry 488.

**26. What is the largest file?**
12 + 1,024 + 1,024² blocks of 4 KB, about 4 GB in theory. Our partition is 64 MB.

**27. How was double-indirect tested?**
The `disk` boot test reads the 5 MB file `/etc/big.bin` and checks every byte. 5 MB goes past the roughly 4.05 MB that direct and indirect blocks cover.

**28. What is a hole in a file?**
A block pointer that's 0. `read_data` returns zeros there.

**29. What does a directory entry look like?**
4-byte inode number, 2-byte entry length, 1-byte name length, 1-byte type, then the name, rounded up to 4 bytes.

**30. How do you find a name in a folder?**
`find_entry` reads each block and checks each entry one after another.

**31. How do you add an entry?**
`add_entry` looks for an entry with spare room and splits it. If no block has room, the folder grows by one block.

**32. How do you delete an entry?**
`remove_entry` adds the deleted entry's length to the previous entry, so it's skipped. The first entry in a block gets inode 0 instead.

**33. When is a file really deleted?**
When its link count is 0 and the last user closes it. `Ext2Inode::release` then frees its blocks and inode.

**34. Why can an open file still be read after `rm`?**
The inode stays until the reference count reaches 0. `fstest` checks this.

**35. What is `refcount` compared with `links`?**
`links` counts names on disk. `refcount` counts users in memory.

**36. Why is there an inode cache?**
`fs.cached_inodes` makes sure each file has one in-memory copy, so all users see the same size and pointers.

**37. Explain the VFS in our code.**
`Inode` and `File` are C++ base classes with virtual functions. Each filesystem makes its own class, so `inode->read()` runs the right code.

**38. Where's the "function pointer table" from the PRD?**
C++ virtual functions are that table. The compiler builds it for each class.

**39. Which filesystems do we have?**
ext2 on `/`, devfs on `/dev`, tmpfs on `/tmp`, procfs on `/proc`.

**40. Walk through `open("/etc/hello.txt")`.**
`sys_open` finds a free FD and copies the path. `file_open` calls `vfs_lookup`, and `walk` goes through each folder. Then `vfs_check_permission` runs, an `InodeFile` is created, and the FD points to it.

**41. What does `walk` check at each folder?**
That it is a folder, and that the user has execute permission on it.

**42. How do mounts work during a walk?**
After each step, `follow_mounts` checks if this inode is covered by a mount. If so, it switches to the mounted filesystem's root.

**43. How does the permission check work?**
`vfs_check_permission` gives root a pass. Otherwise it uses the owner bits, group bits or other bits, whichever applies, and compares them with what's wanted.

**44. What permissions are needed to create a file?**
Write permission on the folder. `vfs_create_path` checks this.

**45. Why two levels of file tables?**
Each process has `files`, an array of `File` pointers. The `File` holds the offset. After `fork`, both processes point to the same `File`, so they share a position.

**46. How do pipes fit in?**
`PipeReader` and `PipeWriter` are `File` classes, so `read` and `write` work on them. Their `inode` stays `nullptr`.

**47. How does redirection work?**
The shell opens the file, then `dup2(fd, STDOUT)` makes FD 1 point to it. `cat` never knows.

**48. What does `dup2` do?**
Closes the new FD if it's open, and points it at the same `File`, adding to the reference count.

**49. Why does `sys_read` use a kernel buffer?**
The filesystem holds `fs.lock` while reading. Writing straight to user memory could page fault, and loading that page could need `fs.lock`. That would deadlock.

**50. How does `O_APPEND` work?**
`InodeFile::write` moves the offset to the end of the file before each write.

**51. How does `O_TRUNC` work?**
`file_open` calls `inode->truncate()`, which frees every block.

**52. What does `seek` do on a pipe?**
Returns `-ESPIPE`, because a pipe has no position.

**53. How does `getcwd` work?**
Each process keeps `cwd_path` as text. `update_cwd_path` works out `.` and `..` when you change folder.

**54. How does `chmod` work?**
`sys_chmod` allows root or the owner, changes `mode`, and calls `inode->sync()` to save it.

**55. What are the devices in `/dev`?**
`console` is line input with editing and screen output. `input` is raw keys. `null` discards writes. `mouse` gives mouse events.

**56. Where's the Up arrow history?**
In `fs/devfs.cpp`. `read_line` handles editing, and `save_to_history` keeps the last 16 lines. Passwords are never saved.

**57. How does tmpfs store files?**
Each file is one block of `kmalloc` memory that doubles when needed. Each folder is a linked list of children.

**58. How does procfs work?**
Nothing is stored. `ProcFile::read` builds the text at the moment it's read, using a function such as `write_meminfo`.

**58a. Which files are in `/proc`?**
`meminfo`, `uptime`, `processes`, `date` and `diskinfo`. The `date` and `df` commands just show the last two, so they needed no new system call.

---

## C. Tricky questions

**59. Is there one global table of open files, as the PRD says?**
The idea is the same, but each `File` is a separate object, created with `new`, and not a slot in one array.

**60. Is ext2 fully supported?**
No, it's a simplified version of the format. There's one mounted filesystem, one global lock, no triple-indirect use, no journal, and real timestamps aren't stored.

**61. Why are timestamps wrong?**
ext2 isn't connected to the real-time clock. `now()` gives seconds since boot, plus 1 so the value isn't 0. The clock driver does exist: `rtc_read` in `hal/rtc.cpp` gives `/proc/date` the real time, and that's what the `date` command shows.

**62. Can I rename a file?**
Yes, with `mv`, but there's no `rename` system call, so it copies the file and deletes the original. The copy keeps the same permissions, but the file gets a new inode.

**63. Can `mv` move a folder?**
No. Without `rename`, it would have to copy everything. It checks first and prints `folders can't be moved`, so it creates nothing. Use `cp -r` and then `rm -r` instead.

**64. What does `cat /etc` do?**
It prints `cat: /etc: is a directory`. `file_read` refuses to read a folder, and programs list folders with `readdir` instead.

**65. Can a user change a file's owner?**
No. There's no `chown`, and no `ln` for links.

**66. How many groups can a user be in?**
One, the GID in the process. There are no supplementary groups.

**67. What if two processes write one file at once?**
`fs.lock` protects each write, so the data structures stay correct. The contents depend on who goes first.

**68. Is every disk operation slow here?**
Writes are slow, because each one saves the data, bitmap, group table, superblock or inode straight to disk. That's why `fstest` takes a few seconds.

**69. What stops a normal user reading `/etc/shadow`?**
Mode `600` and owner root. `permtest` and the `login` tests check this.

**70. Does a file created in `/tmp` survive a restart?**
No. tmpfs is in RAM. Files on `/` survive only if QEMU runs without `-snapshot`.

**71. Why does `ls` print `lost+found`?**
`mke2fs` creates it on every ext2 disk, for `e2fsck` to use.

**72. Why does `ls -l` show no date?**
`struct stat` has no time fields, and ext2 stores seconds since boot, not real dates.

**73. Can you mount more filesystems from the shell?**
No. There's no `mount` system call. `kernel_thread` does the mounts at boot.

---

## D. Questions linking to other parts

**74. How do you reach the disk?**
`block_get`, `block_put` and `block_write` in `storage/block.cpp`.

**75. How does `exec` use you?**
`process_exec` uses `vfs_lookup` and `vfs_check_permission`, and `read_program_header` uses `Inode::read`.

**76. How does demand paging use you?**
`load_page` calls `area->file->read()` for each page.

**77. How does `fork` affect you?**
`process_fork` calls `hold_file` for each FD and `hold_inode` for the current folder.

**78. How does your output reach the screen?**
`ConsoleDevice::write` calls `kprintf`, which calls `serial_putc` and `vga_putc`.

**79. How does login hide a password?**
It calls `set_echo(false)`. `console_set_echo` tells the console not to show or save what's typed.

---

## Be honest about these

- There's no `rename`, `link`, `symlink`, `chown` or `mount` system call.
- `mv` can't move folders.
- One global lock covers all of ext2.
- There's no journal, so a crash mid-write can leave errors.
- Timestamps are seconds since boot.
