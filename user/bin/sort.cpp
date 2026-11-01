// sort: prints the lines of files or standard input in order. -r reverses the
// order, and -n compares lines as numbers.

#include "libc.h"

#define MAX_TEXT (256 * 1024)
#define MAX_LINES 8192

static char text[MAX_TEXT];
static uint32_t text_used = 0;
static char *lines[MAX_LINES];
static int line_count = 0;

static bool reverse_order = false;
static bool by_number = false;

static bool read_all(int fd, const char *name)
{
    int length;

    while ((length = read(fd, text + text_used, MAX_TEXT - text_used - 1)) > 0) {
        text_used += length;
        if (text_used == MAX_TEXT - 1) {
            fprintf(STDERR, "sort: too much text\n");
            return false;
        }
    }

    if (length < 0) {
        fprintf(STDERR, "sort: %s: %s\n", name, error_text(length));
        return false;
    }

    // A last line with no line ending must not run into the next file.
    if (text_used > 0 && text[text_used - 1] != '\n') {
        text[text_used] = '\n';
        text_used++;
    }
    return true;
}

static bool split_into_lines()
{
    char *start = text;

    for (uint32_t i = 0; i < text_used; i++) {
        if (text[i] == '\n') {
            if (line_count == MAX_LINES) {
                fprintf(STDERR, "sort: too many lines\n");
                return false;
            }

            text[i] = '\0';
            lines[line_count] = start;
            line_count++;
            start = text + i + 1;
        }
    }
    return true;
}

static bool comes_after(const char *a, const char *b)
{
    int difference = by_number ? atoi(a) - atoi(b) : strcmp(a, b);

    return reverse_order ? difference < 0 : difference > 0;
}

// Insertion sort: each line slides back until it reaches its place.
static void sort_lines()
{
    for (int i = 1; i < line_count; i++) {
        char *moving = lines[i];
        int place = i;

        while (place > 0 && comes_after(lines[place - 1], moving)) {
            lines[place] = lines[place - 1];
            place--;
        }
        lines[place] = moving;
    }
}

int main(int argc, char *argv[])
{
    int first_file = 1;

    while (first_file < argc && argv[first_file][0] == '-') {
        for (const char *letter = argv[first_file] + 1; *letter != '\0'; letter++) {
            if (*letter == 'r') {
                reverse_order = true;
            } else if (*letter == 'n') {
                by_number = true;
            } else {
                fprintf(STDERR, "sort: unknown option -%c\n", *letter);
                return 1;
            }
        }
        first_file++;
    }

    if (first_file == argc && !read_all(STDIN, "input"))
        return 1;

    for (int i = first_file; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "sort: %s: %s\n", argv[i], error_text(fd));
            return 1;
        }

        bool worked = read_all(fd, argv[i]);

        close(fd);
        if (!worked)
            return 1;
    }

    if (!split_into_lines())
        return 1;

    sort_lines();

    for (int i = 0; i < line_count; i++)
        printf("%s\n", lines[i]);
    return 0;
}
