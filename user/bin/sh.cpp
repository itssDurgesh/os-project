// The shell. Runs commands, with pipes (|), redirection (<, >, >>), quotes,
// wildcards (*), and several commands on one line (; and &&).

#include "libc.h"

#define LINE_LENGTH 256
#define MAX_COMMANDS 8
#define WORD_SPACE 4096         // room for all the words of one line
#define HISTORY_SIZE 16

struct command {
    char *argv[MAX_ARGUMENTS + 1];
    int argc;
    const char *input_file;
    const char *output_file;
    bool append;
};

enum TokenKind {
    TOKEN_END,
    TOKEN_WORD,
    TOKEN_PIPE,
    TOKEN_AND,
    TOKEN_SEMICOLON,
    TOKEN_BACKGROUND,
    TOKEN_INPUT,
    TOKEN_OUTPUT,
    TOKEN_APPEND,
    TOKEN_ERROR,
};

static char line[LINE_LENGTH];
static const char *cursor;      // the next character of `line` to read
static struct command commands[MAX_COMMANDS];
static bool run_in_background;
static struct user shell_user;

// Words are copied here, because removing quotes changes them.
static char word_space[WORD_SPACE];
static int word_space_used;
static char *token_word;
static bool token_has_wildcard;

static char history[HISTORY_SIZE][LINE_LENGTH];
static int history_count = 0;

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n';
}

static bool ends_word(char c)
{
    return c == '\0' || is_space(c) || c == '|' || c == '&' || c == ';' ||
           c == '<' || c == '>';
}

// Reads one word, taking out its quotes. Inside quotes, or after a \, every
// character is plain text, so "a | b" is one word and not a pipe.
static TokenKind read_word()
{
    char *word = &word_space[word_space_used];
    int length = 0;

    if (word_space_used + LINE_LENGTH + sizeof(shell_user.home) > WORD_SPACE)
        return TOKEN_ERROR;

    token_has_wildcard = false;

    // ~ on its own, or at the start of a path, means your home folder.
    if (*cursor == '~' && (ends_word(cursor[1]) || cursor[1] == '/')) {
        strcpy(word, shell_user.home);
        length = strlen(word);
        cursor++;
    }

    while (!ends_word(*cursor)) {
        char c = *cursor;

        cursor++;
        if (c == '"' || c == '\'') {
            while (*cursor != c) {
                if (*cursor == '\0' || *cursor == '\n')
                    return TOKEN_ERROR;  // the quote was never closed

                word[length] = *cursor;
                length++;
                cursor++;
            }
            cursor++;
        } else if (c == '\\' && *cursor != '\0' && *cursor != '\n') {
            word[length] = *cursor;
            length++;
            cursor++;
        } else {
            if (c == '*' || c == '?')
                token_has_wildcard = true;

            word[length] = c;
            length++;
        }
    }

    word[length] = '\0';
    word_space_used += length + 1;
    token_word = word;
    return TOKEN_WORD;
}

static TokenKind next_token()
{
    while (is_space(*cursor))
        cursor++;

    if (*cursor == '\0')
        return TOKEN_END;

    if (cursor[0] == '&' && cursor[1] == '&') {
        cursor += 2;
        return TOKEN_AND;
    }
    if (cursor[0] == '>' && cursor[1] == '>') {
        cursor += 2;
        return TOKEN_APPEND;
    }

    char c = *cursor;

    if (!ends_word(c))
        return read_word();

    cursor++;
    if (c == '|')
        return TOKEN_PIPE;
    if (c == ';')
        return TOKEN_SEMICOLON;
    if (c == '&')
        return TOKEN_BACKGROUND;
    if (c == '<')
        return TOKEN_INPUT;
    return TOKEN_OUTPUT;
}

static bool add_argument(struct command *command, char *word)
{
    if (command->argc == MAX_ARGUMENTS)
        return false;

    command->argv[command->argc] = word;
    command->argc++;
    return true;
}

// Replaces a word like /etc/*.txt with the names that match it. With no match, the
// word is kept as it is, so the program can say the file doesn't exist. Only the
// last part of a path can have wildcards.
static bool add_matching_files(struct command *command, char *pattern)
{
    const char *name_pattern = base_name(pattern);
    int prefix_length = name_pattern - pattern;
    char folder[MAX_PATH];
    struct dirent entry;
    int found = 0;

    memcpy(folder, pattern, prefix_length);
    folder[prefix_length] = '\0';

    int fd = open((prefix_length > 0) ? folder : ".", O_RDONLY);

    while (fd >= 0 && readdir(fd, &entry) > 0) {
        if (entry.name[0] == '.' || !matches_pattern(entry.name, name_pattern))
            continue;

        int size = prefix_length + strlen(entry.name) + 1;
        char *word = &word_space[word_space_used];

        if (word_space_used + size > WORD_SPACE || !add_argument(command, word)) {
            close(fd);
            return false;
        }

        strcpy(word, folder);
        strcpy(word + prefix_length, entry.name);
        word_space_used += size;
        found++;
    }

    if (fd >= 0)
        close(fd);

    return found > 0 || add_argument(command, pattern);
}

// Reads commands joined by |, up to the token that ends them, which goes in
// `separator`. Returns how many commands, or -1 on a syntax error.
static int parse_pipeline(TokenKind *separator)
{
    int count = 0;
    struct command *command = &commands[0];

    memset(command, 0, sizeof(*command));

    for (;;) {
        TokenKind token = next_token();

        if (token == TOKEN_ERROR)
            return -1;

        if (token == TOKEN_WORD) {
            bool added = token_has_wildcard ? add_matching_files(command, token_word)
                                            : add_argument(command, token_word);

            if (!added)
                return -1;
        } else if (token == TOKEN_PIPE) {
            if (command->argc == 0 || ++count == MAX_COMMANDS)
                return -1;
            command = &commands[count];
            memset(command, 0, sizeof(*command));
        } else if (token == TOKEN_INPUT || token == TOKEN_OUTPUT || token == TOKEN_APPEND) {
            if (next_token() != TOKEN_WORD)
                return -1;

            if (token == TOKEN_INPUT) {
                command->input_file = token_word;
            } else {
                command->output_file = token_word;
                command->append = token == TOKEN_APPEND;
            }
        } else {
            *separator = token;
            run_in_background = token == TOKEN_BACKGROUND;

            if (command->argc > 0)
                return count + 1;

            // An empty line is fine, but "ls | " and "&& ls" are not.
            return (count == 0 && (token == TOKEN_END || token == TOKEN_SEMICOLON)) ? 0 : -1;
        }
    }
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

// Returns true if the last program in the pipeline succeeded.
static bool run_pipeline(int count)
{
    int pids[MAX_COMMANDS];
    int previous_read = -1;
    int status = 0;

    for (int i = 0; i < count; i++) {
        int fds[2] = { -1, -1 };
        int pid;

        if (i < count - 1 && pipe(fds) < 0) {
            fprintf(STDERR, "sh: can't create a pipe\n");
            return false;
        }

        pid = fork();
        if (pid < 0) {
            fprintf(STDERR, "sh: can't start a process: %s\n", error_text(pid));
            if (fds[0] >= 0) {
                close(fds[0]);
                close(fds[1]);
            }
            return false;
        }

        if (pid == 0) {
            if (run_in_background) {
                // It must not steal keys from you, or stop on Ctrl+C.
                int nothing = open("/dev/null", O_RDONLY);

                dup2(nothing, STDIN);
                close(nothing);
            } else {
                default_signal(SIGINT);     // Ctrl+C should stop this program
            }

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

        pids[i] = pid;

        if (previous_read >= 0)
            close(previous_read);
        if (fds[1] >= 0)
            close(fds[1]);
        previous_read = fds[0];
    }

    if (run_in_background) {
        printf("[started] process %d\n", pids[count - 1]);
        return true;
    }

    for (int i = 0; i < count; i++)
        waitpid(pids[i], &status, false);

    return status == 0;
}

// Collects background programs that have finished, without waiting for any.
static void check_finished_programs()
{
    int status;
    int pid;

    while ((pid = waitpid(0, &status, true)) > 0)
        printf("[finished] process %d, status %d\n", pid, status);
}

static void show_help()
{
    printf("Files:    ls, cat, cp, mv, rm, touch, mkdir, rmdir, edit, head, tail, grep,\n");
    printf("          sort, wc, find\n");
    printf("Folders:  cd, pwd\n");
    printf("C++:      c++ file.cpp -o name, then ./name   or   run file.cpp\n");
    printf("System:   ps, top, free, df, kill, sleep, whoami, chmod, date, uname, clear,\n");
    printf("          history\n");
    printf("Shell:    a | b, a > file, a >> file, a < file, a &, a ; b, a && b,\n");
    printf("          \"quotes\", *.txt, ~\n");
    printf("Keys:     Up and Down for old commands, Left and Right to edit,\n");
    printf("          Ctrl+C stops a program. Type exit to log out.\n");
}

static void save_to_history()
{
    char *saved = history[history_count % HISTORY_SIZE];

    strcpy(saved, line);

    int length = strlen(saved);

    if (length > 0 && saved[length - 1] == '\n')
        saved[length - 1] = '\0';

    history_count++;
}

static void show_history()
{
    int first = (history_count > HISTORY_SIZE) ? history_count - HISTORY_SIZE : 0;

    for (int i = first; i < history_count; i++)
        printf("%d  %s\n", i + 1, history[i % HISTORY_SIZE]);
}

// Commands the shell runs itself. `cd` has to be one, because a child process
// can't change the folder of its parent.
static bool run_builtin(const struct command *command, bool *worked)
{
    const char *name = command->argv[0];

    if (strcmp(name, "cd") == 0) {
        const char *path = (command->argc > 1) ? command->argv[1] : shell_user.home;
        int status = chdir(path);

        if (status < 0)
            fprintf(STDERR, "cd: %s: %s\n", path, error_text(status));

        *worked = status == 0;
        return true;
    }

    if (strcmp(name, "help") == 0) {
        show_help();
        return true;
    }

    if (strcmp(name, "history") == 0) {
        show_history();
        return true;
    }

    if (strcmp(name, "exit") == 0 || strcmp(name, "logout") == 0)
        exit(0);

    return false;
}

// Runs each part of the line in turn. After &&, the next part only runs if the
// last one worked. A ; or a background & always carries on.
static void run_line()
{
    TokenKind separator;
    bool last_worked = true;
    bool skip_next = false;

    cursor = line;
    word_space_used = 0;

    do {
        int count = parse_pipeline(&separator);

        if (count < 0) {
            fprintf(STDERR, "sh: syntax error\n");
            return;
        }

        if (count > 0 && !skip_next) {
            last_worked = true;
            if (count > 1 || run_in_background || !run_builtin(&commands[0], &last_worked))
                last_worked = run_pipeline(count);
        }

        skip_next = separator == TOKEN_AND && !last_worked;
    } while (separator != TOKEN_END);
}

int main()
{
    if (!user_by_uid(getuid(), &shell_user)) {
        strcpy(shell_user.name, "user");
        strcpy(shell_user.home, "/");
    }

    printf("\nWelcome to the OS Project shell, %s.\n", shell_user.name);
    printf("Type help to see what you can do, and exit to log out.\n");

    for (;;) {
        char cwd[MAX_PATH];
        int length;

        check_finished_programs();

        getcwd(cwd, sizeof(cwd));
        printf("%s %s ", cwd, (getuid() == 0) ? "#" : "$");

        length = read(STDIN, line, LINE_LENGTH - 1);
        if (length <= 0) {
            printf("\n");
            exit(0);
        }
        line[length] = '\0';

        if (line[0] != '\n')
            save_to_history();

        run_line();
    }
}
