// chmod: changes a file's permissions. The mode is octal, such as 644, or letters
// such as +x or -w, which change that permission for everyone.

#include "libc.h"

#define ALL_READ  0444
#define ALL_WRITE 0222
#define ALL_EXEC  0111
#define BAD_MODE  (-1000)

// Works out a mode from letters like +x. Returns BAD_MODE if they make no sense,
// or an error number if the file can't be found.
static int mode_from_letters(const char *text, const char *path)
{
    struct stat info;
    int bits = 0;
    int status = stat(path, &info);

    if (status < 0)
        return status;

    for (const char *letter = text + 1; *letter != '\0'; letter++) {
        if (*letter == 'r')
            bits |= ALL_READ;
        else if (*letter == 'w')
            bits |= ALL_WRITE;
        else if (*letter == 'x')
            bits |= ALL_EXEC;
        else
            return BAD_MODE;
    }

    return (text[0] == '+') ? (info.mode | bits) : (info.mode & ~bits);
}

static int mode_from_digits(const char *text)
{
    int mode = 0;

    for (const char *digit = text; *digit != '\0'; digit++) {
        if (*digit < '0' || *digit > '7')
            return BAD_MODE;
        mode = mode * 8 + (*digit - '0');
    }
    return mode;
}

int main(int argc, char *argv[])
{
    int mode;
    int status;

    if (argc != 3) {
        fprintf(STDERR, "usage: chmod <mode> <file>\n");
        return 1;
    }

    if (argv[1][0] == '+' || argv[1][0] == '-')
        mode = mode_from_letters(argv[1], argv[2]);
    else
        mode = mode_from_digits(argv[1]);

    if (mode == BAD_MODE) {
        fprintf(STDERR, "chmod: bad mode %s\n", argv[1]);
        return 1;
    }

    if (mode < 0) {
        fprintf(STDERR, "chmod: %s: %s\n", argv[2], error_text(mode));
        return 1;
    }

    status = chmod(argv[2], mode);
    if (status < 0) {
        fprintf(STDERR, "chmod: %s: %s\n", argv[2], error_text(status));
        return 1;
    }
    return 0;
}
