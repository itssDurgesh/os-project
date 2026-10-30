/* System call handlers. Arguments arrive in EBX, ECX, EDX, ESI and EDI. */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "abi.h"
#include "hal.h"

typedef int (*syscall_handler_t)(struct Registers *r);

void syscall_init(void);

/* Process Management: process/sys_process.c */
int sys_fork(struct Registers *r);
int sys_exec(struct Registers *r);
int sys_exit(struct Registers *r);
int sys_wait(struct Registers *r);
int sys_getpid(struct Registers *r);
int sys_sleep(struct Registers *r);
int sys_getuid(struct Registers *r);
int sys_getgid(struct Registers *r);
int sys_pipe(struct Registers *r);
int sys_setuid(struct Registers *r);
int sys_setgid(struct Registers *r);
int sys_procinfo(struct Registers *r);

/* Memory Management: memory/sys_memory.c */
int sys_brk(struct Registers *r);
int sys_sbrk(struct Registers *r);

/* File Handling: fs/sys_file.c */
int sys_open(struct Registers *r);
int sys_close(struct Registers *r);
int sys_read(struct Registers *r);
int sys_write(struct Registers *r);
int sys_seek(struct Registers *r);
int sys_stat(struct Registers *r);
int sys_unlink(struct Registers *r);
int sys_mkdir(struct Registers *r);
int sys_rmdir(struct Registers *r);
int sys_readdir(struct Registers *r);
int sys_chdir(struct Registers *r);
int sys_getcwd(struct Registers *r);
int sys_dup2(struct Registers *r);
int sys_chmod(struct Registers *r);
int sys_set_echo(struct Registers *r);

#endif
