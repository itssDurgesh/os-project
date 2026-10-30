/* System Call Interface. Programs put a number in EAX and run `int 0x80`. This
 * table sends each number to its handler, and the result goes back in EAX. */

#include <stddef.h>

#include "console.h"
#include "errno.h"
#include "hal.h"
#include "syscall.h"

static const syscall_handler_t syscall_table[SYSCALL_COUNT] = {
    [SYS_FORK] = sys_fork,
    [SYS_EXEC] = sys_exec,
    [SYS_EXIT] = sys_exit,
    [SYS_WAIT] = sys_wait,
    [SYS_GETPID] = sys_getpid,
    [SYS_SLEEP] = sys_sleep,
    [SYS_BRK] = sys_brk,
    [SYS_SBRK] = sys_sbrk,
    [SYS_OPEN] = sys_open,
    [SYS_CLOSE] = sys_close,
    [SYS_READ] = sys_read,
    [SYS_WRITE] = sys_write,
    [SYS_SEEK] = sys_seek,
    [SYS_STAT] = sys_stat,
    [SYS_UNLINK] = sys_unlink,
    [SYS_MKDIR] = sys_mkdir,
    [SYS_RMDIR] = sys_rmdir,
    [SYS_READDIR] = sys_readdir,
    [SYS_CHDIR] = sys_chdir,
    [SYS_GETCWD] = sys_getcwd,
    [SYS_DUP2] = sys_dup2,
    [SYS_PIPE] = sys_pipe,
    [SYS_GETUID] = sys_getuid,
    [SYS_GETGID] = sys_getgid,
    [SYS_SETUID] = sys_setuid,
    [SYS_SETGID] = sys_setgid,
    [SYS_SET_ECHO] = sys_set_echo,
    [SYS_PROCINFO] = sys_procinfo,
    [SYS_CHMOD] = sys_chmod,
};

static void syscall_interrupt(struct regs *r)
{
    uint32_t number = r->eax;

    /* Long system calls must not block the timer or the disk. */
    cpu_sti();

    if (number < SYSCALL_COUNT && syscall_table[number] != NULL)
        r->eax = (uint32_t)syscall_table[number](r);
    else
        r->eax = (uint32_t)-ENOSYS;
}

void syscall_init(void)
{
    interrupt_register(VECTOR_SYSCALL, syscall_interrupt);
}
