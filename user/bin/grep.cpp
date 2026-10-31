// grep: prints the lines that contain some text.

#include "libc.h"

#define BUFFER_SIZE 4096
#define LINE_LENGTH 512

static char buffer[BUFFER_SIZE];
static char line[LINE_LENGTH];
static int line_length = 0;

static bool contains(const char *text, const char *wanted)
{
    size_t wanted_length = strlen(wanted);

    for (; *text != '\0'; text++) {
        if (strncmp(text, wanted, wanted_length) == 0)
            return true;
    }
    return false;
}

static void finish_line(const char *wanted)
{
    line[line_length] = '\0';
    if (contains(line, wanted))
        printf("%s\n", line);
    line_length = 0;
}

int main(int argc, char *argv[])
{
    int fd = STDIN;
    int length;

    if (argc < 2) {
        fprintf(STDERR, "usage: grep <text> [file]\n");
        return 1;
    }

    if (argc > 2) {
        fd = open(argv[2], O_RDONLY);
        if (fd < 0) {
            fprintf(STDERR, "grep: %s: %s\n", argv[2], error_text(fd));
            return 1;
        }
    }

    while ((length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        for (int i = 0; i < length; i++) {
            if (buffer[i] == '\n') {
                finish_line(argv[1]);
            } else if (line_length < LINE_LENGTH - 1) {
                line[line_length] = buffer[i];
                line_length++;
            }
        }
    }

    if (line_length > 0)
        finish_line(argv[1]);
    return 0;
}
