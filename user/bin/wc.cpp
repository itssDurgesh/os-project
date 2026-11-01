// wc: counts lines, words and bytes in files or standard input. -l, -w and -c show
// only the lines, words or bytes.

#include "libc.h"

#define BUFFER_SIZE 4096

static char buffer[BUFFER_SIZE];

static bool show_lines = false;
static bool show_words = false;
static bool show_bytes = false;

static void print_number(uint32_t number, bool *first)
{
    printf("%s%u", *first ? "" : " ", number);
    *first = false;
}

// `shown_name` is nullptr unless there are several files to tell apart.
static bool count_file(int fd, const char *shown_name, const char *name_for_errors)
{
    uint32_t lines = 0, words = 0, bytes = 0;
    bool in_word = false;
    bool first = true;
    int length;

    while ((length = read(fd, buffer, BUFFER_SIZE)) > 0) {
        bytes += length;
        for (int i = 0; i < length; i++) {
            char c = buffer[i];
            bool space = c == ' ' || c == '\n' || c == '\t';

            if (c == '\n')
                lines++;
            if (!space && !in_word)
                words++;
            in_word = !space;
        }
    }

    if (length < 0) {
        fprintf(STDERR, "wc: %s: %s\n", name_for_errors, error_text(length));
        return false;
    }

    if (show_lines)
        print_number(lines, &first);
    if (show_words)
        print_number(words, &first);
    if (show_bytes)
        print_number(bytes, &first);

    if (shown_name != nullptr)
        printf(" %s", shown_name);
    printf("\n");
    return true;
}

int main(int argc, char *argv[])
{
    int first_file = 1;
    bool all_worked = true;

    while (first_file < argc && argv[first_file][0] == '-') {
        for (const char *letter = argv[first_file] + 1; *letter != '\0'; letter++) {
            if (*letter == 'l') {
                show_lines = true;
            } else if (*letter == 'w') {
                show_words = true;
            } else if (*letter == 'c') {
                show_bytes = true;
            } else {
                fprintf(STDERR, "wc: unknown option -%c\n", *letter);
                return 1;
            }
        }
        first_file++;
    }

    if (!show_lines && !show_words && !show_bytes) {
        show_lines = true;
        show_words = true;
        show_bytes = true;
    }

    if (first_file == argc)
        return count_file(STDIN, nullptr, "input") ? 0 : 1;

    int file_count = argc - first_file;

    for (int i = first_file; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "wc: %s: %s\n", argv[i], error_text(fd));
            all_worked = false;
            continue;
        }

        if (!count_file(fd, (file_count > 1) ? argv[i] : nullptr, argv[i]))
            all_worked = false;
        close(fd);
    }
    return all_worked ? 0 : 1;
}
