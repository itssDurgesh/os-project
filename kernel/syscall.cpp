/* System Call Interface. Programs put a number in EAX and run `int 0x80`. This
 * table sends each number to its handler, and the result goes back in EAX. */

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "syscall.h"

static syscall_handler_t syscall_table[SYSCALL_COUNT];

static void syscall_interrupt(struct Registers *r)
{
    uint32_t number = r->eax;

    /* Long system calls must not block the timer or the disk. */
    enable_interrupts();

    if (number < SYSCALL_COUNT && syscall_table[number] != NULL)
        r->eax = (uint32_t)syscall_table[number](r);
    else
        r->eax = (uint32_t)-ENOSYS;
}

void syscall_init(void)
{
    syscall_table[SYS_FORK] = sys_fork;
    syscall_table[SYS_EXEC] = sys_exec;
    syscall_table[SYS_EXIT] = sys_exit;
    syscall_table[SYS_WAIT] = sys_wait;
    syscall_table[SYS_GETPID] = sys_getpid;
    syscall_table[SYS_SLEEP] = sys_sleep;
    syscall_table[SYS_BRK] = sys_brk;
    syscall_table[SYS_SBRK] = sys_sbrk;
    syscall_table[SYS_OPEN] = sys_open;
    syscall_table[SYS_CLOSE] = sys_close;
    syscall_table[SYS_READ] = sys_read;
    syscall_table[SYS_WRITE] = sys_write;
    syscall_table[SYS_SEEK] = sys_seek;
    syscall_table[SYS_STAT] = sys_stat;
    syscall_table[SYS_UNLINK] = sys_unlink;
    syscall_table[SYS_MKDIR] = sys_mkdir;
    syscall_table[SYS_RMDIR] = sys_rmdir;
    syscall_table[SYS_READDIR] = sys_readdir;
    syscall_table[SYS_CHDIR] = sys_chdir;
    syscall_table[SYS_GETCWD] = sys_getcwd;
    syscall_table[SYS_DUP2] = sys_dup2;
    syscall_table[SYS_PIPE] = sys_pipe;
    syscall_table[SYS_GETUID] = sys_getuid;
    syscall_table[SYS_GETGID] = sys_getgid;
    syscall_table[SYS_SETUID] = sys_setuid;
    syscall_table[SYS_SETGID] = sys_setgid;
    syscall_table[SYS_SET_ECHO] = sys_set_echo;
    syscall_table[SYS_PROCINFO] = sys_procinfo;
    syscall_table[SYS_CHMOD] = sys_chmod;
    set_interrupt_handler(VECTOR_SYSCALL, syscall_interrupt);
}
