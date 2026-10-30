/* System call handlers. Arguments arrive in EBX, ECX, EDX, ESI and EDI. */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "abi.h"
#include "hal.h"

typedef int (*syscall_handler_t)(struct regs *r);

void syscall_init(void);

/* Process Management: process/sys_process.c */
int sys_fork(struct regs *r);
int sys_exec(struct regs *r);
int sys_exit(struct regs *r);
int sys_wait(struct regs *r);
int sys_getpid(struct regs *r);
int sys_sleep(struct regs *r);
int sys_getuid(struct regs *r);
int sys_getgid(struct regs *r);
int sys_pipe(struct regs *r);
int sys_setuid(struct regs *r);
int sys_setgid(struct regs *r);
int sys_procinfo(struct regs *r);

/* Memory Management: memory/sys_memory.c */
int sys_brk(struct regs *r);
int sys_sbrk(struct regs *r);

/* File Handling: fs/sys_file.c */
int sys_open(struct regs *r);
int sys_close(struct regs *r);
int sys_read(struct regs *r);
int sys_write(struct regs *r);
int sys_seek(struct regs *r);
int sys_stat(struct regs *r);
int sys_unlink(struct regs *r);
int sys_mkdir(struct regs *r);
int sys_rmdir(struct regs *r);
int sys_readdir(struct regs *r);
int sys_chdir(struct regs *r);
int sys_getcwd(struct regs *r);
int sys_dup2(struct regs *r);
int sys_chmod(struct regs *r);
int sys_set_echo(struct regs *r);

#endif
