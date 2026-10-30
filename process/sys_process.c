/* Process system calls. */

#include <stddef.h>

#include "errno.h"
#include "kstring.h"
#include "memory.h"
#include "process.h"
#include "syscall.h"
#include "vfs.h"

#define MS_PER_TICK (1000 / TIMER_HZ)

int sys_fork(struct regs *r)
{
    return process_fork(r);
}

/* Copies the path and arguments into the kernel, because exec destroys the user
 * memory they live in. */
int sys_exec(struct regs *r)
{
    const char *user_path = (const char *)r->ebx;
    char *const *user_argv = (char *const *)r->ecx;
    char *strings = kmalloc(PAGE_SIZE);
    char *path = kmalloc(MAX_PATH);
    char **argv = kmalloc((MAX_ARGUMENTS + 1) * sizeof(char *));
    uint32_t used = 0;
    int argc = 0;
    int status;

    if (strings == NULL || path == NULL || argv == NULL) {
        status = -ENOMEM;
        goto out;
    }

    status = copy_string_from_user(path, user_path, MAX_PATH);
    if (status != 0)
        goto out;

    for (;;) {
        char *argument;

        if (!user_range_valid(&user_argv[argc], sizeof(char *), false)) {
            status = -EFAULT;
            goto out;
        }

        argument = user_argv[argc];
        if (argument == NULL)
            break;

        if (argc == MAX_ARGUMENTS) {
            status = -EINVAL;
            goto out;
        }

        status = copy_string_from_user(strings + used, argument, PAGE_SIZE - used);
        if (status != 0)
            goto out;

        argv[argc++] = strings + used;
        used += strlen(strings + used) + 1;
    }
    argv[argc] = NULL;

    status = process_exec(path, argv, r);

out:
    kfree(strings);
    kfree(path);
    kfree(argv);
    return status;
}

int sys_exit(struct regs *r)
{
    process_exit((int)r->ebx);
}

int sys_wait(struct regs *r)
{
    int *user_status = (int *)r->ebx;
    int status;
    int pid;

    if (user_status != NULL && !user_range_valid(user_status, sizeof(int), true))
        return -EFAULT;

    pid = process_wait(&status);
    if (pid > 0 && user_status != NULL)
        *user_status = status;
    return pid;
}

int sys_getpid(struct regs *r)
{
    (void)r;
    return current->pid;
}

int sys_getuid(struct regs *r)
{
    (void)r;
    return current->uid;
}

int sys_getgid(struct regs *r)
{
    (void)r;
    return current->gid;
}

/* Only root can change identity. This is how login becomes the user. */
int sys_setuid(struct regs *r)
{
    if (current->uid != 0)
        return -EPERM;

    current->uid = (int)r->ebx;
    return 0;
}

int sys_setgid(struct regs *r)
{
    if (current->uid != 0)
        return -EPERM;

    current->gid = (int)r->ebx;
    return 0;
}

/* Fills in details of the process at this position in the list, for ps. */
int sys_procinfo(struct regs *r)
{
    int index = (int)r->ebx;
    struct procinfo *output = (struct procinfo *)r->ecx;

    if (!user_range_valid(output, sizeof(*output), true))
        return -EFAULT;

    for (struct pcb *process = process_list(); process != NULL;
         process = process->list_next) {
        if (index-- == 0) {
            output->pid = process->pid;
            output->parent_pid = process->parent_pid;
            output->uid = process->uid;
            output->state = process->state;
            output->priority = process->priority;
            output->cpu_ticks = process->cpu_ticks;
            strlcpy(output->name, process->name, PROCESS_NAME_LENGTH);
            return 1;
        }
    }
    return 0;
}

/* Sleeps for a number of milliseconds. */
int sys_sleep(struct regs *r)
{
    thread_sleep(r->ebx / MS_PER_TICK);
    return 0;
}

int sys_pipe(struct regs *r)
{
    int *user_fds = (int *)r->ebx;
    struct file *read_end, *write_end;
    int read_fd = -1;
    int write_fd = -1;
    int status;

    if (!user_range_valid(user_fds, 2 * sizeof(int), true))
        return -EFAULT;

    for (int fd = 0; fd < MAX_OPEN_FILES; fd++) {
        if (current->files[fd] != NULL)
            continue;

        if (read_fd < 0) {
            read_fd = fd;
        } else {
            write_fd = fd;
            break;
        }
    }
    if (write_fd < 0)
        return -EMFILE;

    status = pipe_create(&read_end, &write_end);
    if (status != 0)
        return status;

    current->files[read_fd] = read_end;
    current->files[write_fd] = write_end;
    user_fds[0] = read_fd;
    user_fds[1] = write_fd;
    return 0;
}
