// grep: prints the lines that contain some text. -i ignores upper and lower case,
// -v prints the lines that don't match, and -n adds line numbers.

#include "libc.h"

#define BUFFER_SIZE 4096
#define LINE_LENGTH 512

static char buffer[BUFFER_SIZE];
static char line[LINE_LENGTH];
static int line_length = 0;
static int line_number = 0;

static const char *wanted;
static bool ignore_case = false;
static bool show_non_matching = false;
static bool show_line_numbers = false;

static char lower_case(char c)
{
    return (c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c;
}

static bool same_letter(char a, char b)
{
    return ignore_case ? lower_case(a) == lower_case(b) : a == b;
}

static bool starts_with_wanted(const char *text)
{
    for (const char *w = wanted; *w != '\0'; w++, text++) {
        if (*text == '\0' || !same_letter(*text, *w))
            return false;
    }
    return true;
}

static bool line_matches()
{
    for (const char *text = line; ; text++) {
        if (starts_with_wanted(text))
            return true;
        if (*text == '\0')
            return false;
    }
}

// `file_name` is nullptr unless there are several files to tell apart.
static void finish_line(const char *file_name)
{
    line[line_length] = '\0';
    line_number++;

    if (line_matches() != show_non_matching) {
        if (file_name != nullptr)
            printf("%s:", file_name);
        if (show_line_numbers)
            printf("%d:", line_number);
        printf("%s\n", line);
    }
    line_length = 0;
}

static bool search_file(int fd, const char *file_name, const char *name_for_errors)
{
    int length;

    line_number = 0;

    while ((length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        for (int i = 0; i < length; i++) {
            if (buffer[i] == '\n') {
                finish_line(file_name);
            } else if (line_length < LINE_LENGTH - 1) {
                line[line_length] = buffer[i];
                line_length++;
            }
        }
    }

    if (length < 0) {
        fprintf(STDERR, "grep: %s: %s\n", name_for_errors, error_text(length));
        return false;
    }

    if (line_length > 0)
        finish_line(file_name);
    return true;
}

int main(int argc, char *argv[])
{
    int next_argument = 1;
    bool all_worked = true;

    while (next_argument < argc && argv[next_argument][0] == '-') {
        for (const char *letter = argv[next_argument] + 1; *letter != '\0'; letter++) {
            if (*letter == 'i') {
                ignore_case = true;
            } else if (*letter == 'v') {
                show_non_matching = true;
            } else if (*letter == 'n') {
                show_line_numbers = true;
            } else {
                fprintf(STDERR, "grep: unknown option -%c\n", *letter);
                return 1;
            }
        }
        next_argument++;
    }

    if (next_argument == argc) {
        fprintf(STDERR, "usage: grep [-i] [-v] [-n] <text> [files]\n");
        return 1;
    }

    wanted = argv[next_argument];
    next_argument++;

    if (next_argument == argc)
        return search_file(STDIN, nullptr, "input") ? 0 : 1;

    int file_count = argc - next_argument;

    for (int i = next_argument; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "grep: %s: %s\n", argv[i], error_text(fd));
            all_worked = false;
            continue;
        }

        if (!search_file(fd, (file_count > 1) ? argv[i] : nullptr, argv[i]))
            all_worked = false;
        close(fd);
    }
    return all_worked ? 0 : 1;
}
