// run: compiles a C++ file and runs it straight away.

#include "libc.h"

int main(int argc, char *argv[])
{
    char output[MAX_PATH];
    int status;

    if (argc != 2) {
        fprintf(STDERR, "usage: run <file.cpp>\n");
        return 1;
    }

    // Each user gets their own file, because /tmp is shared.
    int uid = getuid();

    strcpy(output, "/tmp/program-0");
    output[13] = '0' + uid % 10;

    int pid = fork();

    if (pid == 0) {
        char *compile[] = { (char *)"/bin/c++", argv[1], (char *)"-o", output, nullptr };

        exec(compile[0], compile);
        fprintf(STDERR, "run: can't start the compiler\n");
        exit(1);
    }

    waitpid(pid, &status, false);
    if (status != 0)
        return 1;               // the compiler has already shown the error

    char *program[] = { output, nullptr };

    status = exec(output, program);
    fprintf(STDERR, "run: %s\n", error_text(status));
    return 1;
}
