// edit: a small full-screen text editor, like a very simple nano.
//   arrow keys   move the cursor
//   Ctrl+S       save
//   Ctrl+Q       quit
//
// The file is kept in a big table of lines. The keys come from /dev/input, because
// the normal console waits for Enter and we need every key straight away.

#include "libc.h"

#define MAX_LINES 2000
#define MAX_LINE_LENGTH 200
#define FILE_BUFFER_SIZE 4096
#define SCREEN_BUFFER_SIZE 6000

#define SCREEN_COLUMNS 80
#define TEXT_ROWS 23            // row 24 is the status bar and row 25 is the help
#define TAB_WIDTH 4

#define KEY_CTRL_Q 17
#define KEY_CTRL_S 19
#define KEY_ESCAPE 27
#define KEY_BACKSPACE '\b'
#define KEY_DELETE 127

static char lines[MAX_LINES][MAX_LINE_LENGTH];
static int line_count = 1;

static int cursor_row = 0;      // where the cursor is in the file
static int cursor_column = 0;
static int top_row = 0;         // the file line at the top of the screen
static int left_column = 0;     // how far long lines are scrolled sideways

static bool has_changes = false;
static bool quit_warning_shown = false;
static const char *file_name;
static const char *message = "";

static void handle_arrow_key(char direction);

static char screen_buffer[SCREEN_BUFFER_SIZE];
static int screen_length = 0;

// Output is collected and written in one go, so the screen doesn't flicker.
static void add_text(const char *text)
{
    while (*text != '\0' && screen_length < SCREEN_BUFFER_SIZE) {
        screen_buffer[screen_length] = *text;
        screen_length++;
        text++;
    }
}

static void add_number(int number)
{
    char digits[12];
    int count = 0;

    do {
        digits[count] = '0' + number % 10;
        count++;
        number = number / 10;
    } while (number != 0);

    while (count > 0) {
        count--;
        char one_digit[2] = { digits[count], '\0' };

        add_text(one_digit);
    }
}

static void move_screen_cursor(int row, int column)
{
    add_text("\033[");
    add_number(row);
    add_text(";");
    add_number(column);
    add_text("H");
}

static void show_screen()
{
    write(STDOUT, screen_buffer, screen_length);
    screen_length = 0;
}

// Keeps the cursor inside the part of the file shown on screen.
static void scroll_to_cursor()
{
    if (cursor_row < top_row)
        top_row = cursor_row;
    if (cursor_row >= top_row + TEXT_ROWS)
        top_row = cursor_row - TEXT_ROWS + 1;

    if (cursor_column < left_column)
        left_column = cursor_column;
    if (cursor_column >= left_column + SCREEN_COLUMNS - 1)
        left_column = cursor_column - SCREEN_COLUMNS + 2;
}

static void draw_text_rows()
{
    for (int screen_row = 0; screen_row < TEXT_ROWS; screen_row++) {
        int file_row = top_row + screen_row;

        move_screen_cursor(screen_row + 1, 1);

        if (file_row < line_count) {
            const char *text = lines[file_row];
            int length = (int)strlen(text);

            for (int i = left_column; i < length && i < left_column + SCREEN_COLUMNS - 1; i++) {
                char one_char[2] = { text[i], '\0' };

                add_text(one_char);
            }
        }
        add_text("\033[K");
    }
}

static void draw_status_bar()
{
    move_screen_cursor(TEXT_ROWS + 1, 1);
    add_text("\033[7m ");
    add_text(file_name);
    add_text(has_changes ? " (not saved)" : "");
    add_text("   line ");
    add_number(cursor_row + 1);
    add_text("/");
    add_number(line_count);
    add_text("   ");
    add_text(message);
    add_text("\033[K\033[0m");

    move_screen_cursor(TEXT_ROWS + 2, 1);
    add_text("Ctrl+S save   Ctrl+Q quit   Arrow keys move\033[K");
}

static void draw_everything()
{
    scroll_to_cursor();
    draw_text_rows();
    draw_status_bar();
    move_screen_cursor(cursor_row - top_row + 1, cursor_column - left_column + 1);
    show_screen();
}

static void load_file()
{
    static char buffer[FILE_BUFFER_SIZE];
    int fd = open(file_name, O_RDONLY);
    int length;
    int column = 0;

    if (fd < 0) {
        message = "New file";
        return;
    }

    line_count = 1;

    while ((length = read(fd, buffer, FILE_BUFFER_SIZE)) > 0) {
        for (int i = 0; i < length; i++) {
            bool line_is_full = column == MAX_LINE_LENGTH - 1;

            if (buffer[i] == '\n' || line_is_full) {
                if (line_count == MAX_LINES)
                    break;

                line_count++;
                column = 0;
                if (buffer[i] == '\n')
                    continue;
            }

            lines[line_count - 1][column] = buffer[i];
            column++;
            lines[line_count - 1][column] = '\0';
        }
    }
    close(fd);

    // A file that ends with Enter would otherwise show an extra empty line.
    if (line_count > 1 && lines[line_count - 1][0] == '\0')
        line_count--;
}

static void save_file()
{
    int fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (fd < 0) {
        message = error_text(fd);
        return;
    }

    for (int i = 0; i < line_count; i++) {
        write(fd, lines[i], strlen(lines[i]));
        write(fd, "\n", 1);
    }

    close(fd);
    has_changes = false;
    message = "Saved";
}

static void move_line_up_to_cursor()
{
    int length = (int)strlen(lines[cursor_row]);

    if (cursor_column > length)
        cursor_column = length;
}

static void insert_character(char c)
{
    char *text = lines[cursor_row];
    int length = (int)strlen(text);

    if (length >= MAX_LINE_LENGTH - 1) {
        message = "Line is full";
        return;
    }

    for (int i = length; i >= cursor_column; i--)
        text[i + 1] = text[i];

    text[cursor_column] = c;
    cursor_column++;
    has_changes = true;
}

// Pressing Enter splits the current line in two.
static void insert_new_line()
{
    if (line_count == MAX_LINES) {
        message = "File is full";
        return;
    }

    for (int i = line_count; i > cursor_row + 1; i--)
        strcpy(lines[i], lines[i - 1]);

    strcpy(lines[cursor_row + 1], lines[cursor_row] + cursor_column);
    lines[cursor_row][cursor_column] = '\0';

    line_count++;
    cursor_row++;
    cursor_column = 0;
    has_changes = true;
}

// Backspace at the start of a line joins it onto the line above.
static void delete_character()
{
    if (cursor_column > 0) {
        char *text = lines[cursor_row];
        int length = (int)strlen(text);

        for (int i = cursor_column - 1; i < length; i++)
            text[i] = text[i + 1];

        cursor_column--;
        has_changes = true;
        return;
    }

    if (cursor_row == 0)
        return;

    int previous_length = (int)strlen(lines[cursor_row - 1]);

    if (previous_length + (int)strlen(lines[cursor_row]) >= MAX_LINE_LENGTH) {
        message = "Lines are too long to join";
        return;
    }

    strcpy(lines[cursor_row - 1] + previous_length, lines[cursor_row]);

    for (int i = cursor_row; i < line_count - 1; i++)
        strcpy(lines[i], lines[i + 1]);

    line_count--;
    cursor_row--;
    cursor_column = previous_length;
    has_changes = true;
}

static void delete_key()
{
    if (cursor_column < (int)strlen(lines[cursor_row])) {
        cursor_column++;
        delete_character();
    } else if (cursor_row < line_count - 1) {
        cursor_row++;
        cursor_column = 0;
        delete_character();
    }
}

static void handle_special_key(int input)
{
    char bracket, code;

    read(input, &bracket, 1);
    read(input, &code, 1);

    if (bracket != '[' && bracket != 'O')
        return;

    if (code >= '0' && code <= '9') {
        char tilde;

        read(input, &tilde, 1);

        if (code == '3')
            delete_key();
        else if (code == '1' || code == '7')
            cursor_column = 0;
        else if (code == '4' || code == '8')
            cursor_column = (int)strlen(lines[cursor_row]);
    } else if (code == 'H') {
        cursor_column = 0;
    } else if (code == 'F') {
        cursor_column = (int)strlen(lines[cursor_row]);
    } else {
        handle_arrow_key(code);
    }
}

static void handle_arrow_key(char direction)
{
    if (direction == 'A' && cursor_row > 0) {
        cursor_row--;
    } else if (direction == 'B' && cursor_row < line_count - 1) {
        cursor_row++;
    } else if (direction == 'C') {
        if (cursor_column < (int)strlen(lines[cursor_row])) {
            cursor_column++;
        } else if (cursor_row < line_count - 1) {
            cursor_row++;
            cursor_column = 0;
        }
    } else if (direction == 'D') {
        if (cursor_column > 0) {
            cursor_column--;
        } else if (cursor_row > 0) {
            cursor_row--;
            cursor_column = (int)strlen(lines[cursor_row]);
        }
    }

    move_line_up_to_cursor();
}

// Returns false when it's time to quit.
static bool handle_key(int input, char key)
{
    message = "";

    if (key == KEY_CTRL_Q) {
        if (has_changes && !quit_warning_shown) {
            message = "Not saved! Press Ctrl+Q again to quit anyway";
            quit_warning_shown = true;
            return true;
        }
        return false;
    }

    quit_warning_shown = false;

    if (key == KEY_CTRL_S) {
        save_file();
    } else if (key == '\n' || key == '\r') {
        insert_new_line();
    } else if (key == KEY_BACKSPACE || key == KEY_DELETE) {
        delete_character();
    } else if (key == '\t') {
        for (int i = 0; i < TAB_WIDTH; i++)
            insert_character(' ');
    } else if (key == KEY_ESCAPE) {
        handle_special_key(input);
    } else if (key >= ' ' && key <= '~') {
        insert_character(key);
    }
    return true;
}

int main(int argc, char *argv[])
{
    char key;
    int input;

    if (argc != 2) {
        fprintf(STDERR, "usage: edit <file>\n");
        return 1;
    }

    input = open("/dev/input", O_RDONLY);
    if (input < 0) {
        fprintf(STDERR, "edit: can't read the keyboard\n");
        return 1;
    }

    ignore_signal(SIGINT);      // Ctrl+C must not lose someone's work

    file_name = argv[1];
    load_file();

    printf("\033[2J");
    draw_everything();

    while (read(input, &key, 1) == 1) {
        if (!handle_key(input, key))
            break;
        draw_everything();
    }

    printf("\033[2J\033[H");
    close(input);
    return 0;
}
