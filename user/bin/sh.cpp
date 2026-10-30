// The shell. Runs commands, with pipes (|) and redirection (<, >, >>).

#include "libc.h"

#define LINE_LENGTH 256
#define MAX_COMMANDS 8

struct command {
    char *argv[MAX_ARGUMENTS + 1];
    int argc;
    const char *input_file;
    const char *output_file;
    bool append;
};

static char line[LINE_LENGTH];
static struct command commands[MAX_COMMANDS];

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

static char *next_word(char **cursor)
{
    char *word;

    while (is_space(**cursor))
        (*cursor)++;

    if (**cursor == '\0')
        return nullptr;

    word = *cursor;
    while (**cursor != '\0' && !is_space(**cursor))
        (*cursor)++;

    if (**cursor != '\0')
        *(*cursor)++ = '\0';
    return word;
}

// Splits the line into commands. Returns how many, or -1 on a syntax error.
static int parse_line()
{
    char *cursor = line;
    char *word;
    int count = 0;
    struct command *command = &commands[0];

    memset(command, 0, sizeof(*command));

    while ((word = next_word(&cursor)) != nullptr) {
        if (strcmp(word, "|") == 0) {
            if (command->argc == 0 || ++count == MAX_COMMANDS)
                return -1;
            command = &commands[count];
            memset(command, 0, sizeof(*command));
        } else if (strcmp(word, "<") == 0) {
            command->input_file = next_word(&cursor);
            if (command->input_file == nullptr)
                return -1;
        } else if (strcmp(word, ">") == 0 || strcmp(word, ">>") == 0) {
            command->append = strcmp(word, ">>") == 0;
            command->output_file = next_word(&cursor);
            if (command->output_file == nullptr)
                return -1;
        } else if (command->argc < MAX_ARGUMENTS) {
            command->argv[command->argc++] = word;
        }
    }

    if (command->argc == 0)
        return (count == 0) ? 0 : -1;
    return count + 1;
}

static void apply_redirection(const struct command *command)
{
    if (command->input_file != nullptr) {
        int fd = open(command->input_file, O_RDONLY);

        if (fd < 0) {
            fprintf(STDERR, "sh: %s: %s\n", command->input_file, error_text(fd));
            exit(1);
        }
        dup2(fd, STDIN);
        close(fd);
    }

    if (command->output_file != nullptr) {
        int flags = O_WRONLY | O_CREAT | (command->append ? O_APPEND : O_TRUNC);
        int fd = open(command->output_file, flags, 0644);

        if (fd < 0) {
            fprintf(STDERR, "sh: %s: %s\n", command->output_file, error_text(fd));
            exit(1);
        }
        dup2(fd, STDOUT);
        close(fd);
    }
}

static void run_program(const struct command *command)
{
    const char *name = command->argv[0];
    char path[MAX_PATH];
    int status;

    if (strchr(name, '/') != nullptr) {
        strcpy(path, name);
    } else {
        strcpy(path, "/bin/");
        strcpy(path + 5, name);
    }

    status = exec(path, command->argv);
    fprintf(STDERR, "sh: %s: %s\n", name, error_text(status));
    exit(127);
}

static void run_pipeline(int count)
{
    int previous_read = -1;

    for (int i = 0; i < count; i++) {
        int fds[2] = { -1, -1 };
        int pid;

        if (i < count - 1 && pipe(fds) < 0) {
            fprintf(STDERR, "sh: can't create a pipe\n");
            return;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(STDERR, "sh: can't start a process\n");
            return;
        }

        if (pid == 0) {
            if (previous_read >= 0) {
                dup2(previous_read, STDIN);
                close(previous_read);
            }
            if (fds[1] >= 0) {
                dup2(fds[1], STDOUT);
                close(fds[0]);
                close(fds[1]);
            }
            apply_redirection(&commands[i]);
            run_program(&commands[i]);
        }

        if (previous_read >= 0)
            close(previous_read);
        if (fds[1] >= 0)
            close(fds[1]);
        previous_read = fds[0];
    }

    for (int i = 0; i < count; i++)
        wait(nullptr);
}

static bool run_builtin(const struct command *command)
{
    if (strcmp(command->argv[0], "cd") == 0) {
        const char *path = (command->argc > 1) ? command->argv[1] : "/";
        int status = chdir(path);

        if (status < 0)
            fprintf(STDERR, "cd: %s: %s\n", path, error_text(status));
        return true;
    }

    if (strcmp(command->argv[0], "exit") == 0 ||
        strcmp(command->argv[0], "logout") == 0)
        exit(0);

    return false;
}

int main()
{
    struct user user;
    bool known = user_by_uid(getuid(), &user);

    printf("\nWelcome to the OS Project shell, %s.\n", known ? user.name : "user");
    printf("Try: ls -l, cat, echo, mkdir, rm, wc, pwd, cd, ps, whoami, chmod, kill,\n");
    printf("pipes like ls / | wc, and exit to log out.\n");

    for (;;) {
        char cwd[MAX_PATH];
        int length;
        int count;

        getcwd(cwd, sizeof(cwd));
        printf("%s %s ", cwd, (getuid() == 0) ? "#" : "$");

        length = read(STDIN, line, LINE_LENGTH - 1);
        if (length <= 0) {
            printf("\n");
            exit(0);
        }
        line[length] = '\0';

        count = parse_line();
        if (count < 0)
            fprintf(STDERR, "sh: syntax error\n");
        else if (count == 1 && run_builtin(&commands[0]))
            continue;
        else if (count > 0)
            run_pipeline(count);
    }
}
