/* Pipes: a one-way byte stream through a kernel buffer. Interrupt handlers never
 * touch a pipe and the kernel isn't preempted, so no lock is needed. */

#include <stddef.h>

#include "errno.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define PIPE_SIZE 4096

struct pipe {
    uint8_t data[PIPE_SIZE];
    uint32_t head;              /* next byte to read */
    uint32_t count;             /* bytes waiting */
    uint32_t readers;
    uint32_t writers;
    struct WaitQueue can_read;
    struct WaitQueue can_write;
};

static int pipe_read(struct file *file, void *buffer, uint32_t count)
{
    struct pipe *pipe = file->pipe;
    uint8_t *output = (uint8_t *)buffer;
    uint32_t done = 0;

    while (pipe->count == 0) {
        if (pipe->writers == 0)
            return 0;           /* end of file: every write end has closed */
        pipe->can_read.sleep();
    }

    while (done < count && pipe->count > 0) {
        output[done++] = pipe->data[pipe->head];
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
        pipe->count--;
    }

    pipe->can_write.wake_all();
    return (int)done;
}

static int pipe_write(struct file *file, const void *buffer, uint32_t count)
{
    struct pipe *pipe = file->pipe;
    const uint8_t *input = (const uint8_t *)buffer;
    uint32_t done = 0;

    while (done < count) {
        if (pipe->readers == 0)
            return (done > 0) ? (int)done : -EPIPE;

        if (pipe->count == PIPE_SIZE) {
            pipe->can_read.wake_all();
            pipe->can_write.sleep();
            continue;
        }

        pipe->data[(pipe->head + pipe->count) % PIPE_SIZE] = input[done++];
        pipe->count++;
    }

    pipe->can_read.wake_all();
    return (int)done;
}

static void free_if_unused(struct pipe *pipe)
{
    if (pipe->readers == 0 && pipe->writers == 0)
        kfree(pipe);
}

static void pipe_close_read(struct file *file)
{
    struct pipe *pipe = file->pipe;

    pipe->readers--;
    pipe->can_write.wake_all();
    free_if_unused(pipe);
}

static void pipe_close_write(struct file *file)
{
    struct pipe *pipe = file->pipe;

    pipe->writers--;
    pipe->can_read.wake_all();
    free_if_unused(pipe);
}

static const struct file_ops pipe_read_ops = {
    .read = pipe_read,
    .close = pipe_close_read,
};

static const struct file_ops pipe_write_ops = {
    .write = pipe_write,
    .close = pipe_close_write,
};

int pipe_create(struct file **read_end, struct file **write_end)
{
    struct pipe *pipe = (struct pipe *)kmalloc_zeroed(sizeof(*pipe));
    struct file *reader = file_alloc();
    struct file *writer = file_alloc();

    if (pipe == NULL || reader == NULL || writer == NULL) {
        kfree(pipe);
        kfree(reader);
        kfree(writer);
        return -ENOMEM;
    }

    pipe->readers = 1;
    pipe->writers = 1;

    reader->pipe = pipe;
    reader->flags = O_RDONLY;
    reader->ops = &pipe_read_ops;

    writer->pipe = pipe;
    writer->flags = O_WRONLY;
    writer->ops = &pipe_write_ops;

    *read_end = reader;
    *write_end = writer;
    return 0;
}
