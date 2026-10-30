// Pipes: a one-way stream of bytes through a buffer in the kernel. Interrupt
// handlers never touch a pipe and the kernel is not preempted, so no lock is
// needed here.

#include "errno.h"
#include "memory.h"
#include "process.h"
#include "vfs.h"

#define PIPE_SIZE 4096

class Pipe {
public:
    uint8_t data[PIPE_SIZE];
    uint32_t head = 0;          // next byte to read
    uint32_t count = 0;         // bytes waiting to be read
    uint32_t readers = 1;
    uint32_t writers = 1;
    WaitQueue can_read;
    WaitQueue can_write;

    void delete_if_unused();
};

void Pipe::delete_if_unused()
{
    if (readers == 0 && writers == 0)
        delete this;
}

// The read end of a pipe.
class PipeReader : public File {
public:
    Pipe *pipe = nullptr;

    int read(void *buffer, uint32_t count) override;
    void close() override;
};

// The write end of a pipe.
class PipeWriter : public File {
public:
    Pipe *pipe = nullptr;

    int write(const void *buffer, uint32_t count) override;
    void close() override;
};

int PipeReader::read(void *buffer, uint32_t count)
{
    uint8_t *output = (uint8_t *)buffer;
    uint32_t done = 0;

    while (pipe->count == 0) {
        if (pipe->writers == 0)
            return 0;           // end of file: all the write ends have closed
        pipe->can_read.sleep(true);
        if (signal_pending())
            return -EINTR;
    }

    while (done < count && pipe->count > 0) {
        output[done] = pipe->data[pipe->head];
        done++;
        pipe->head = (pipe->head + 1) % PIPE_SIZE;
        pipe->count--;
    }

    pipe->can_write.wake_all();
    return (int)done;
}

void PipeReader::close()
{
    pipe->readers--;
    pipe->can_write.wake_all();
    pipe->delete_if_unused();
}

int PipeWriter::write(const void *buffer, uint32_t count)
{
    const uint8_t *input = (const uint8_t *)buffer;
    uint32_t done = 0;

    while (done < count) {
        if (pipe->readers == 0) {
            if (done > 0)
                return (int)done;
            return -EPIPE;
        }

        if (pipe->count == PIPE_SIZE) {
            pipe->can_read.wake_all();
            pipe->can_write.sleep(true);
            if (signal_pending()) {
                if (done > 0)
                    return (int)done;
                return -EINTR;
            }
            continue;
        }

        pipe->data[(pipe->head + pipe->count) % PIPE_SIZE] = input[done];
        done++;
        pipe->count++;
    }

    pipe->can_read.wake_all();
    return (int)done;
}

void PipeWriter::close()
{
    pipe->writers--;
    pipe->can_read.wake_all();
    pipe->delete_if_unused();
}

int pipe_create(File **read_end, File **write_end)
{
    Pipe *pipe = new Pipe();
    PipeReader *reader = new PipeReader();
    PipeWriter *writer = new PipeWriter();

    reader->pipe = pipe;
    reader->flags = O_RDONLY;

    writer->pipe = pipe;
    writer->flags = O_WRONLY;

    *read_end = reader;
    *write_end = writer;
    return 0;
}
