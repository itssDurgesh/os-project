// System call handlers. Arguments arrive in EBX, ECX, EDX, ESI and EDI.

#ifndef SYSCALL_H
#define SYSCALL_H

#include "abi.h"
#include "hal.h"

typedef int (*syscall_handler_t)(Registers *r);

void syscall_init();

// Process Management: process/sys_process.c
int sys_fork(Registers *r);
int sys_exec(Registers *r);
int sys_exit(Registers *r);
int sys_wait(Registers *r);
int sys_getpid(Registers *r);
int sys_sleep(Registers *r);
int sys_getuid(Registers *r);
int sys_getgid(Registers *r);
int sys_pipe(Registers *r);
int sys_setuid(Registers *r);
int sys_setgid(Registers *r);
int sys_procinfo(Registers *r);

// Memory Management: memory/sys_memory.c
int sys_brk(Registers *r);
int sys_sbrk(Registers *r);

// File Handling: fs/sys_file.c
int sys_open(Registers *r);
int sys_close(Registers *r);
int sys_read(Registers *r);
int sys_write(Registers *r);
int sys_seek(Registers *r);
int sys_stat(Registers *r);
int sys_unlink(Registers *r);
int sys_mkdir(Registers *r);
int sys_rmdir(Registers *r);
int sys_readdir(Registers *r);
int sys_chdir(Registers *r);
int sys_getcwd(Registers *r);
int sys_dup2(Registers *r);
int sys_chmod(Registers *r);
int sys_set_echo(Registers *r);

#endif
