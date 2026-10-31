// c++: a small C++ compiler that runs inside our OS.
//
// It reads one source file and writes a real program (an ELF file) containing x86
// machine code, with no assembler or linker. This is what it understands:
//   types:      int, char, bool, string, void, const, and arrays
//   functions:  with parameters, return values and recursion
//   control:    if, else, while, do, for, break, continue, return, and ?:
//   input and output: cout, cin, endl, getline
//   library:    abs, min, max, rand, to_string, stoi, exit, s.length(), s.size()
//
// How it works:
//   1. The lexer turns the text into tokens, like "int", "x" and "=".
//   2. The parser follows the grammar, and writes machine code as it goes.
//   3. Every expression leaves its answer in the EAX register.
//   4. It reads the file 3 times, so a function can be used before it's written:
//      pass 1 learns return types, pass 2 learns addresses, pass 3 is the real one.
//
// Parts of the C++ standard library are written in C++ themselves. They are in
// `runtime_library` at the bottom of this file, and are compiled in every time.

#include "libc.h"

#define MAX_SOURCE_SIZE  65536
#define MAX_CODE_SIZE    262144
#define MAX_DATA_SIZE    65536
#define MAX_NAME_LENGTH  32
#define MAX_TEXT_LENGTH  512
#define MAX_VARIABLES    512
#define MAX_FUNCTIONS    256
#define MAX_PARAMETERS   16
#define MAX_LOOPS        32
#define MAX_JUMPS        128

// Where the parts of the finished program go in memory.
#define CODE_ADDRESS 0x08049000u
#define DATA_ADDRESS 0x09000000u
#define BSS_ADDRESS  0x09800000u
#define PAGE         4096

#define ELF_HEADER_SIZE 52
#define ELF_SEGMENT_SIZE 32
#define ELF_SEGMENTS 3

#define LAST_PASS 3

enum TokenKind {
    TOKEN_END, TOKEN_NUMBER, TOKEN_TEXT, TOKEN_LETTER, TOKEN_NAME,

    // Words
    TOKEN_INT, TOKEN_CHAR, TOKEN_BOOL, TOKEN_STRING, TOKEN_VOID, TOKEN_CONST,
    TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_DO, TOKEN_FOR, TOKEN_RETURN,
    TOKEN_BREAK, TOKEN_CONTINUE, TOKEN_TRUE, TOKEN_FALSE, TOKEN_COUT, TOKEN_CIN,
    TOKEN_ENDL, TOKEN_GETLINE, TOKEN_USING, TOKEN_NAMESPACE,

    // Symbols
    TOKEN_LEFT_ROUND, TOKEN_RIGHT_ROUND, TOKEN_LEFT_CURLY, TOKEN_RIGHT_CURLY,
    TOKEN_LEFT_SQUARE, TOKEN_RIGHT_SQUARE, TOKEN_SEMICOLON, TOKEN_COMMA, TOKEN_DOT,
    TOKEN_QUESTION, TOKEN_COLON,
    TOKEN_ASSIGN, TOKEN_PLUS_ASSIGN, TOKEN_MINUS_ASSIGN, TOKEN_TIMES_ASSIGN,
    TOKEN_DIVIDE_ASSIGN, TOKEN_MOD_ASSIGN, TOKEN_PLUS_PLUS, TOKEN_MINUS_MINUS,
    TOKEN_PLUS, TOKEN_MINUS, TOKEN_TIMES, TOKEN_DIVIDE, TOKEN_MOD,
    TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS, TOKEN_LESS_EQUAL, TOKEN_GREATER,
    TOKEN_GREATER_EQUAL, TOKEN_AND, TOKEN_OR, TOKEN_NOT, TOKEN_OUTPUT, TOKEN_INPUT,
};

enum ValueType { TYPE_VOID_VALUE, TYPE_INT_VALUE, TYPE_BOOL_VALUE, TYPE_CHAR_VALUE, TYPE_STRING_VALUE };

struct KeyWord {
    const char *text;
    TokenKind kind;
};

static const KeyWord key_words[] = {
    { "int", TOKEN_INT }, { "char", TOKEN_CHAR }, { "bool", TOKEN_BOOL },
    { "string", TOKEN_STRING }, { "void", TOKEN_VOID }, { "const", TOKEN_CONST },
    { "if", TOKEN_IF }, { "else", TOKEN_ELSE }, { "while", TOKEN_WHILE },
    { "do", TOKEN_DO }, { "for", TOKEN_FOR }, { "return", TOKEN_RETURN },
    { "break", TOKEN_BREAK }, { "continue", TOKEN_CONTINUE }, { "true", TOKEN_TRUE },
    { "false", TOKEN_FALSE }, { "cout", TOKEN_COUT }, { "cin", TOKEN_CIN },
    { "endl", TOKEN_ENDL }, { "getline", TOKEN_GETLINE }, { "using", TOKEN_USING },
    { "namespace", TOKEN_NAMESPACE },
};

#define KEY_WORD_COUNT (int)(sizeof(key_words) / sizeof(key_words[0]))

struct Variable {
    char name[MAX_NAME_LENGTH];
    ValueType type;
    bool is_array;
    bool is_global;
    bool is_array_parameter;    // holds the address of an array from the caller
    bool is_constant;
    int constant_value;
    int location;               // stack offset, or address for a global
};

struct Function {
    char name[MAX_NAME_LENGTH];
    ValueType return_type;
    int parameter_count;
    uint32_t address;
    int defined_in_pass;
};

struct Loop {
    int break_jumps[MAX_JUMPS];
    int break_count;
    int continue_jumps[MAX_JUMPS];
    int continue_count;
};

// The input
static char source[MAX_SOURCE_SIZE];
static const char *source_position;
static const char *file_name;
static int line_number;
static bool reading_runtime;

// The current token
static TokenKind token;
static int token_number;
static char token_text[MAX_TEXT_LENGTH];
static int token_text_length;

// The output
static uint8_t code[MAX_CODE_SIZE];
static uint32_t code_size;
static uint8_t data[MAX_DATA_SIZE];
static uint32_t data_size;
static uint32_t bss_size;

// What the compiler knows
static Variable globals[MAX_VARIABLES];
static int global_count;
static Variable locals[MAX_VARIABLES];
static int local_count;
static int next_local_offset;
static int biggest_local_offset;
static Function functions[MAX_FUNCTIONS];
static int function_count;
static Loop loops[MAX_LOOPS];
static int loop_count;
static int pass;

extern const char *const runtime_library;

static int parse_expression();
static int parse_unary();
static void parse_statement();

static void compile_error(const char *message)
{
    if (reading_runtime)
        fprintf(STDERR, "c++: error in the built-in library, line %d: %s\n", line_number, message);
    else
        fprintf(STDERR, "%s:%d: error: %s\n", file_name, line_number, message);
    exit(1);
}

// ---------- Lexer: turns the text into tokens ----------

static bool is_letter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

static void skip_spaces_and_comments()
{
    for (;;) {
        char c = *source_position;

        if (c == '\n')
            line_number++;

        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            source_position++;
        } else if (c == '/' && source_position[1] == '/') {
            while (*source_position != '\0' && *source_position != '\n')
                source_position++;
        } else if (c == '/' && source_position[1] == '*') {
            source_position += 2;
            while (*source_position != '\0' &&
                   !(source_position[0] == '*' && source_position[1] == '/')) {
                if (*source_position == '\n')
                    line_number++;
                source_position++;
            }
            if (*source_position != '\0')
                source_position += 2;
        } else if (c == '#') {
            // Lines like #include have nothing to do here. Everything is built in.
            while (*source_position != '\0' && *source_position != '\n')
                source_position++;
        } else {
            return;
        }
    }
}

// Reads one character from inside quotes, turning \n into a real new line.
static char read_quoted_character()
{
    char c = *source_position;

    source_position++;
    if (c != '\\')
        return c;

    c = *source_position;
    source_position++;

    if (c == 'n')
        return '\n';
    if (c == 't')
        return '\t';
    if (c == 'r')
        return '\r';
    if (c == '0')
        return '\0';
    return c;                   // this covers \\ and \" and \'
}

static void read_name()
{
    int length = 0;

    while (is_letter(*source_position) || is_digit(*source_position)) {
        if (length < MAX_NAME_LENGTH - 1) {
            token_text[length] = *source_position;
            length++;
        }
        source_position++;
    }
    token_text[length] = '\0';

    // std::cout is the same as cout here.
    if (strcmp(token_text, "std") == 0 && source_position[0] == ':' &&
        source_position[1] == ':') {
        source_position += 2;
        read_name();
        return;
    }

    token = TOKEN_NAME;
    for (int i = 0; i < KEY_WORD_COUNT; i++) {
        if (strcmp(token_text, key_words[i].text) == 0) {
            token = key_words[i].kind;
            return;
        }
    }
}

static void read_number()
{
    token = TOKEN_NUMBER;
    token_number = 0;

    while (is_digit(*source_position)) {
        token_number = token_number * 10 + (*source_position - '0');
        source_position++;
    }
}

static void read_text()
{
    token = TOKEN_TEXT;
    token_text_length = 0;
    source_position++;

    while (*source_position != '"') {
        if (*source_position == '\0' || *source_position == '\n')
            compile_error("this text has no closing \"");
        if (token_text_length >= MAX_TEXT_LENGTH - 1)
            compile_error("this text is too long");

        token_text[token_text_length] = read_quoted_character();
        token_text_length++;
    }
    token_text[token_text_length] = '\0';
    source_position++;
}

static void read_letter()
{
    token = TOKEN_LETTER;
    source_position++;
    token_number = read_quoted_character();

    if (*source_position != '\'')
        compile_error("a character needs a closing '");
    source_position++;
}

// Checks if the next characters are this symbol, and moves past them if so.
static bool symbol(const char *text, TokenKind kind)
{
    size_t length = strlen(text);

    if (strncmp(source_position, text, length) != 0)
        return false;

    source_position += length;
    token = kind;
    return true;
}

static void next_token()
{
    skip_spaces_and_comments();

    char c = *source_position;

    if (c == '\0') {
        token = TOKEN_END;
    } else if (is_letter(c)) {
        read_name();
    } else if (is_digit(c)) {
        read_number();
    } else if (c == '"') {
        read_text();
    } else if (c == '\'') {
        read_letter();
    } else if (symbol("<<", TOKEN_OUTPUT) || symbol(">>", TOKEN_INPUT) ||
               symbol("==", TOKEN_EQUAL) || symbol("!=", TOKEN_NOT_EQUAL) ||
               symbol("<=", TOKEN_LESS_EQUAL) || symbol(">=", TOKEN_GREATER_EQUAL) ||
               symbol("&&", TOKEN_AND) || symbol("||", TOKEN_OR) ||
               symbol("++", TOKEN_PLUS_PLUS) || symbol("--", TOKEN_MINUS_MINUS) ||
               symbol("+=", TOKEN_PLUS_ASSIGN) || symbol("-=", TOKEN_MINUS_ASSIGN) ||
               symbol("*=", TOKEN_TIMES_ASSIGN) || symbol("/=", TOKEN_DIVIDE_ASSIGN) ||
               symbol("%=", TOKEN_MOD_ASSIGN) || symbol("(", TOKEN_LEFT_ROUND) ||
               symbol(")", TOKEN_RIGHT_ROUND) || symbol("{", TOKEN_LEFT_CURLY) ||
               symbol("}", TOKEN_RIGHT_CURLY) || symbol("[", TOKEN_LEFT_SQUARE) ||
               symbol("]", TOKEN_RIGHT_SQUARE) || symbol(";", TOKEN_SEMICOLON) ||
               symbol(",", TOKEN_COMMA) || symbol(".", TOKEN_DOT) ||
               symbol("?", TOKEN_QUESTION) || symbol(":", TOKEN_COLON) ||
               symbol("=", TOKEN_ASSIGN) || symbol("+", TOKEN_PLUS) ||
               symbol("-", TOKEN_MINUS) || symbol("*", TOKEN_TIMES) ||
               symbol("/", TOKEN_DIVIDE) || symbol("%", TOKEN_MOD) ||
               symbol("<", TOKEN_LESS) || symbol(">", TOKEN_GREATER) ||
               symbol("!", TOKEN_NOT)) {
        return;
    } else {
        compile_error("I don't understand this character");
    }
}

static bool accept(TokenKind kind)
{
    if (token == kind) {
        next_token();
        return true;
    }
    return false;
}

static void expect(TokenKind kind, const char *message)
{
    if (token != kind)
        compile_error(message);
    next_token();
}

// ---------- Machine code: each function writes one x86 instruction ----------

static void emit_byte(uint8_t value)
{
    if (code_size >= MAX_CODE_SIZE)
        compile_error("the program is too big");

    code[code_size] = value;
    code_size++;
}

static void emit_bytes(uint8_t first, uint8_t second)
{
    emit_byte(first);
    emit_byte(second);
}

static void emit_bytes(uint8_t first, uint8_t second, uint8_t third)
{
    emit_byte(first);
    emit_byte(second);
    emit_byte(third);
}

static void emit_number(uint32_t value)
{
    emit_byte(value & 0xFF);
    emit_byte((value >> 8) & 0xFF);
    emit_byte((value >> 16) & 0xFF);
    emit_byte((value >> 24) & 0xFF);
}

static void write_number_at(uint32_t position, uint32_t value)
{
    code[position] = value & 0xFF;
    code[position + 1] = (value >> 8) & 0xFF;
    code[position + 2] = (value >> 16) & 0xFF;
    code[position + 3] = (value >> 24) & 0xFF;
}

static uint32_t here()
{
    return CODE_ADDRESS + code_size;
}

static void emit_move_number_to_eax(uint32_t value) { emit_byte(0xB8); emit_number(value); }
static void emit_push_eax() { emit_byte(0x50); }
static void emit_pop_eax() { emit_byte(0x58); }
static void emit_pop_ecx() { emit_byte(0x59); }
static void emit_move_eax_to_ecx() { emit_bytes(0x89, 0xC1); }
static void emit_store_eax_at_ecx() { emit_bytes(0x89, 0x01); }
static void emit_store_al_at_ecx() { emit_bytes(0x88, 0x01); }
static void emit_load_eax_from_eax() { emit_bytes(0x8B, 0x00); }
static void emit_load_byte_from_eax() { emit_bytes(0x0F, 0xB6, 0x00); }
static void emit_test_eax() { emit_bytes(0x85, 0xC0); }
static void emit_clear_eax() { emit_bytes(0x31, 0xC0); }

static void emit_add_to_stack_pointer(int bytes)
{
    emit_bytes(0x81, 0xC4);
    emit_number(bytes);
}

// Writes a jump that doesn't know where it's going yet. `patch_jump` fills it in.
static int emit_jump()
{
    emit_byte(0xE9);
    emit_number(0);
    return (int)code_size - 4;
}

static int emit_jump_if_zero()
{
    emit_bytes(0x0F, 0x84);
    emit_number(0);
    return (int)code_size - 4;
}

static void patch_jump_to(int position, uint32_t address)
{
    write_number_at(position, address - (CODE_ADDRESS + position + 4));
}

static void patch_jump(int position)
{
    patch_jump_to(position, here());
}

static void emit_jump_to(uint32_t address)
{
    emit_byte(0xE9);
    emit_number(address - (here() + 4));
}

static void emit_function_start_code(uint32_t *stack_size_position)
{
    emit_byte(0x55);            // push ebp
    emit_bytes(0x89, 0xE5);     // mov ebp, esp
    emit_bytes(0x81, 0xEC);     // sub esp, (how much is not known yet)
    *stack_size_position = code_size;
    emit_number(0);
}

static void emit_function_end_code()
{
    emit_byte(0xC9);            // leave
    emit_byte(0xC3);            // ret
}

// EAX holds the left side and ECX holds the right side.
static void emit_math(TokenKind operation)
{
    if (operation == TOKEN_PLUS) {
        emit_bytes(0x01, 0xC8);                 // add eax, ecx
    } else if (operation == TOKEN_MINUS) {
        emit_bytes(0x29, 0xC8);                 // sub eax, ecx
    } else if (operation == TOKEN_TIMES) {
        emit_bytes(0x0F, 0xAF, 0xC1);           // imul eax, ecx
    } else {
        emit_byte(0x99);                        // cdq
        emit_bytes(0xF7, 0xF9);                 // idiv ecx
        if (operation == TOKEN_MOD)
            emit_bytes(0x89, 0xD0);             // mov eax, edx
    }
}

static void emit_compare(TokenKind operation)
{
    uint8_t set_code = 0x94;

    if (operation == TOKEN_NOT_EQUAL)
        set_code = 0x95;
    else if (operation == TOKEN_LESS)
        set_code = 0x9C;
    else if (operation == TOKEN_LESS_EQUAL)
        set_code = 0x9E;
    else if (operation == TOKEN_GREATER)
        set_code = 0x9F;
    else if (operation == TOKEN_GREATER_EQUAL)
        set_code = 0x9D;

    emit_bytes(0x39, 0xC8);                     // cmp eax, ecx
    emit_bytes(0x0F, set_code, 0xC0);           // set al to 1 or 0
    emit_bytes(0x0F, 0xB6, 0xC0);               // movzx eax, al
}

// ---------- Names: variables and functions ----------

static Variable *find_variable(const char *name)
{
    for (int i = local_count - 1; i >= 0; i--) {
        if (strcmp(locals[i].name, name) == 0)
            return &locals[i];
    }
    for (int i = global_count - 1; i >= 0; i--) {
        if (strcmp(globals[i].name, name) == 0)
            return &globals[i];
    }
    return nullptr;
}

static Function *find_function(const char *name)
{
    for (int i = 0; i < function_count; i++) {
        if (strcmp(functions[i].name, name) == 0)
            return &functions[i];
    }
    return nullptr;
}

static void copy_name(char *to, const char *from)
{
    int i = 0;

    while (from[i] != '\0' && i < MAX_NAME_LENGTH - 1) {
        to[i] = from[i];
        i++;
    }
    to[i] = '\0';
}

static void emit_call_function(Function *function)
{
    emit_byte(0xE8);
    emit_number(function->address - (here() + 4));
}

// Calls a function from the built-in library, by name.
static void emit_call_named(const char *name)
{
    Function *function = find_function(name);

    if (function == nullptr) {
        if (pass == LAST_PASS)
            compile_error("the built-in library is missing a function");
        emit_byte(0xE8);
        emit_number(0);
        return;
    }
    emit_call_function(function);
}

// Reading and writing a variable

static void emit_load_variable(const Variable *variable)
{
    if (variable->is_array && !variable->is_array_parameter) {
        // The value of an array is its address.
        if (variable->is_global) {
            emit_move_number_to_eax(variable->location);
        } else {
            emit_bytes(0x8D, 0x85);             // lea eax, [ebp + offset]
            emit_number(variable->location);
        }
    } else if (variable->is_global) {
        emit_byte(0xA1);                        // mov eax, [address]
        emit_number(variable->location);
    } else {
        emit_bytes(0x8B, 0x85);                 // mov eax, [ebp + offset]
        emit_number(variable->location);
    }
}

static void emit_store_variable(const Variable *variable)
{
    if (variable->is_global) {
        emit_byte(0xA3);                        // mov [address], eax
        emit_number(variable->location);
    } else {
        emit_bytes(0x89, 0x85);                 // mov [ebp + offset], eax
        emit_number(variable->location);
    }
}

static bool is_assignment(TokenKind kind)
{
    return kind == TOKEN_ASSIGN || kind == TOKEN_PLUS_ASSIGN || kind == TOKEN_MINUS_ASSIGN ||
           kind == TOKEN_TIMES_ASSIGN || kind == TOKEN_DIVIDE_ASSIGN || kind == TOKEN_MOD_ASSIGN;
}

static TokenKind math_of_assignment(TokenKind kind)
{
    if (kind == TOKEN_PLUS_ASSIGN)
        return TOKEN_PLUS;
    if (kind == TOKEN_MINUS_ASSIGN)
        return TOKEN_MINUS;
    if (kind == TOKEN_TIMES_ASSIGN)
        return TOKEN_TIMES;
    if (kind == TOKEN_DIVIDE_ASSIGN)
        return TOKEN_DIVIDE;
    return TOKEN_MOD;
}

// Joins two strings: the left is on the stack and the right is in EAX.
static void emit_join_strings(int right_type)
{
    if (right_type == TYPE_CHAR_VALUE) {
        emit_push_eax();
        emit_call_named("__char_to_string");
        emit_add_to_stack_pointer(4);
    } else if (right_type != TYPE_STRING_VALUE) {
        compile_error("only text can be added to a string");
    }

    emit_push_eax();
    emit_call_named("__join");
    emit_add_to_stack_pointer(8);
}

// x = 5, or x += 5, for a plain variable.
static int parse_assign_to_variable(const Variable *variable)
{
    TokenKind operation = token;

    if (variable->is_constant)
        compile_error("a const variable can't be changed");

    next_token();

    if (operation == TOKEN_ASSIGN) {
        parse_expression();
    } else {
        emit_load_variable(variable);
        emit_push_eax();

        int right_type = parse_expression();

        if (variable->type == TYPE_STRING_VALUE) {
            emit_join_strings(right_type);
        } else {
            emit_move_eax_to_ecx();
            emit_pop_eax();
            emit_math(math_of_assignment(operation));
        }
    }

    emit_store_variable(variable);
    return variable->type;
}

// x++ or x--. The answer is the old value, as in real C++.
static int parse_step_variable(const Variable *variable)
{
    uint8_t step = (token == TOKEN_PLUS_PLUS) ? 0x40 : 0x48;    // inc eax or dec eax
    uint8_t undo = (token == TOKEN_PLUS_PLUS) ? 0x48 : 0x40;

    next_token();
    emit_load_variable(variable);
    emit_byte(step);
    emit_store_variable(variable);
    emit_byte(undo);
    return variable->type;
}

// a[i] and s[i]. EAX ends up holding the address of the element.
static void parse_element_address(const Variable *variable)
{
    next_token();
    parse_expression();
    expect(TOKEN_RIGHT_SQUARE, "expected ] here");

    if (variable->is_array)
        emit_bytes(0xC1, 0xE0, 0x02);           // shl eax, 2 (ints are 4 bytes)

    emit_push_eax();
    emit_load_variable(variable);
    emit_pop_ecx();
    emit_bytes(0x01, 0xC8);                     // add eax, ecx
}

static void emit_load_element(bool is_byte)
{
    if (is_byte)
        emit_load_byte_from_eax();
    else
        emit_load_eax_from_eax();
}

static void emit_store_element(bool is_byte)
{
    if (is_byte)
        emit_store_al_at_ecx();
    else
        emit_store_eax_at_ecx();
}

static int parse_element(const Variable *variable)
{
    if (!variable->is_array && variable->type != TYPE_STRING_VALUE)
        compile_error("only arrays and strings can use [ ]");

    bool is_byte = !variable->is_array;
    ValueType element_type = variable->is_array ? variable->type : TYPE_CHAR_VALUE;

    parse_element_address(variable);

    if (is_assignment(token)) {
        TokenKind operation = token;

        next_token();
        emit_push_eax();                        // remember where it goes

        if (operation == TOKEN_ASSIGN) {
            parse_expression();
        } else {
            emit_bytes(0x8B, 0x04, 0x24);       // mov eax, [esp]
            emit_load_element(is_byte);
            emit_push_eax();
            parse_expression();
            emit_move_eax_to_ecx();
            emit_pop_eax();
            emit_math(math_of_assignment(operation));
        }

        emit_pop_ecx();
        emit_store_element(is_byte);
        return element_type;
    }

    if (token == TOKEN_PLUS_PLUS || token == TOKEN_MINUS_MINUS) {
        uint8_t step = (token == TOKEN_PLUS_PLUS) ? 0x40 : 0x48;
        uint8_t undo = (token == TOKEN_PLUS_PLUS) ? 0x48 : 0x40;

        next_token();
        emit_move_eax_to_ecx();
        emit_bytes(0x8B, 0x01);                 // mov eax, [ecx]
        emit_byte(step);
        emit_store_element(is_byte);
        emit_byte(undo);
        return element_type;
    }

    emit_load_element(is_byte);
    return element_type;
}

static int parse_function_call(const char *name)
{
    Function *function = find_function(name);
    int argument_count = 0;

    next_token();               // the (

    if (token != TOKEN_RIGHT_ROUND) {
        for (;;) {
            parse_expression();
            emit_push_eax();
            argument_count++;
            if (!accept(TOKEN_COMMA))
                break;
        }
    }
    expect(TOKEN_RIGHT_ROUND, "expected ) after the arguments");

    if (function == nullptr) {
        if (pass == LAST_PASS)
            compile_error("this function doesn't exist");

        emit_byte(0xE8);
        emit_number(0);
        emit_add_to_stack_pointer(argument_count * 4);
        return TYPE_INT_VALUE;
    }

    if (pass == LAST_PASS && argument_count != function->parameter_count)
        compile_error("wrong number of arguments for this function");

    emit_call_function(function);
    emit_add_to_stack_pointer(argument_count * 4);
    return function->return_type;
}

// __syscall(number, a, b, c) goes straight to the kernel. The library uses it.
static int parse_system_call()
{
    next_token();
    expect(TOKEN_LEFT_ROUND, "expected (");

    for (int i = 0; i < 4; i++) {
        parse_expression();
        emit_push_eax();
        if (i < 3)
            expect(TOKEN_COMMA, "__syscall needs 4 numbers");
    }
    expect(TOKEN_RIGHT_ROUND, "expected )");

    emit_byte(0x5A);            // pop edx
    emit_pop_ecx();
    emit_byte(0x5B);            // pop ebx
    emit_pop_eax();
    emit_bytes(0xCD, 0x80);     // int 0x80
    return TYPE_INT_VALUE;
}

static int parse_name()
{
    char name[MAX_NAME_LENGTH];

    copy_name(name, token_text);

    // The library can treat an address as a string, and a string as an address.
    if (strcmp(name, "__syscall") == 0)
        return parse_system_call();

    if (strcmp(name, "__to_string") == 0 || strcmp(name, "__to_int") == 0) {
        next_token();
        expect(TOKEN_LEFT_ROUND, "expected (");
        parse_expression();
        expect(TOKEN_RIGHT_ROUND, "expected )");
        return (name[5] == 's') ? TYPE_STRING_VALUE : TYPE_INT_VALUE;
    }

    next_token();

    if (token == TOKEN_LEFT_ROUND)
        return parse_function_call(name);

    Variable *variable = find_variable(name);

    if (variable == nullptr)
        compile_error("this variable doesn't exist");

    if (variable->is_constant) {
        emit_move_number_to_eax(variable->constant_value);
        return variable->type;
    }

    if (token == TOKEN_LEFT_SQUARE)
        return parse_element(variable);

    if (is_assignment(token))
        return parse_assign_to_variable(variable);

    if (token == TOKEN_PLUS_PLUS || token == TOKEN_MINUS_MINUS)
        return parse_step_variable(variable);

    emit_load_variable(variable);

    // s.length() and s.size()
    if (token == TOKEN_DOT) {
        next_token();
        if (variable->type != TYPE_STRING_VALUE)
            compile_error("only a string can use .length()");
        if (strcmp(token_text, "length") != 0 && strcmp(token_text, "size") != 0)
            compile_error("the only string functions are .length() and .size()");

        next_token();
        expect(TOKEN_LEFT_ROUND, "expected (");
        expect(TOKEN_RIGHT_ROUND, "expected )");

        emit_push_eax();
        emit_call_named("__length");
        emit_add_to_stack_pointer(4);
        return TYPE_INT_VALUE;
    }

    return variable->type;
}

// ---------- Expressions: each one leaves its answer in EAX ----------

static bool is_type_token(TokenKind kind)
{
    return kind == TOKEN_INT || kind == TOKEN_CHAR || kind == TOKEN_BOOL ||
           kind == TOKEN_STRING || kind == TOKEN_VOID;
}

static ValueType type_of_token(TokenKind kind)
{
    if (kind == TOKEN_INT)
        return TYPE_INT_VALUE;
    if (kind == TOKEN_CHAR)
        return TYPE_CHAR_VALUE;
    if (kind == TOKEN_BOOL)
        return TYPE_BOOL_VALUE;
    if (kind == TOKEN_STRING)
        return TYPE_STRING_VALUE;
    return TYPE_VOID_VALUE;
}

// Keeps a piece of text in the data part of the program, and returns its address.
static uint32_t add_text_to_data(const char *text, int length)
{
    uint32_t address = DATA_ADDRESS + data_size;

    if (data_size + length + 1 > MAX_DATA_SIZE)
        compile_error("the program has too much text");

    memcpy(data + data_size, text, length);
    data_size += length;
    data[data_size] = 0;
    data_size++;
    return address;
}

static int parse_primary()
{
    if (token == TOKEN_NUMBER || token == TOKEN_LETTER) {
        int type = (token == TOKEN_NUMBER) ? TYPE_INT_VALUE : TYPE_CHAR_VALUE;

        emit_move_number_to_eax(token_number);
        next_token();
        return type;
    }

    if (token == TOKEN_TRUE || token == TOKEN_FALSE) {
        emit_move_number_to_eax(token == TOKEN_TRUE ? 1 : 0);
        next_token();
        return TYPE_BOOL_VALUE;
    }

    if (token == TOKEN_TEXT) {
        emit_move_number_to_eax(add_text_to_data(token_text, token_text_length));
        next_token();
        return TYPE_STRING_VALUE;
    }

    if (token == TOKEN_NAME)
        return parse_name();

    if (token == TOKEN_LEFT_ROUND) {
        next_token();

        // A cast, like (char)65. Our types are all stored the same way.
        if (is_type_token(token)) {
            int type = type_of_token(token);

            next_token();
            expect(TOKEN_RIGHT_ROUND, "expected ) after the type");
            parse_unary();
            return type;
        }

        int type = parse_expression();

        expect(TOKEN_RIGHT_ROUND, "expected ) here");
        return type;
    }

    compile_error("expected a value here");
    return TYPE_INT_VALUE;
}

static int parse_unary()
{
    if (accept(TOKEN_MINUS)) {
        parse_unary();
        emit_bytes(0xF7, 0xD8);                 // neg eax
        return TYPE_INT_VALUE;
    }

    if (accept(TOKEN_PLUS))
        return parse_unary();

    // ++x and --x. The answer is the new value.
    if (token == TOKEN_PLUS_PLUS || token == TOKEN_MINUS_MINUS) {
        uint8_t step = (token == TOKEN_PLUS_PLUS) ? 0x40 : 0x48;

        next_token();
        if (token != TOKEN_NAME)
            compile_error("++ and -- need a variable");

        Variable *variable = find_variable(token_text);

        if (variable == nullptr)
            compile_error("this variable doesn't exist");

        next_token();
        emit_load_variable(variable);
        emit_byte(step);
        emit_store_variable(variable);
        return variable->type;
    }

    if (accept(TOKEN_NOT)) {
        parse_unary();
        emit_test_eax();
        emit_bytes(0x0F, 0x94, 0xC0);           // sete al
        emit_bytes(0x0F, 0xB6, 0xC0);           // movzx eax, al
        return TYPE_BOOL_VALUE;
    }

    return parse_primary();
}

// Higher numbers are done first, like * before +.
static int level_of_operator(TokenKind kind)
{
    if (kind == TOKEN_OR)
        return 1;
    if (kind == TOKEN_AND)
        return 2;
    if (kind == TOKEN_EQUAL || kind == TOKEN_NOT_EQUAL)
        return 3;
    if (kind == TOKEN_LESS || kind == TOKEN_LESS_EQUAL || kind == TOKEN_GREATER ||
        kind == TOKEN_GREATER_EQUAL)
        return 4;
    if (kind == TOKEN_PLUS || kind == TOKEN_MINUS)
        return 5;
    if (kind == TOKEN_TIMES || kind == TOKEN_DIVIDE || kind == TOKEN_MOD)
        return 6;
    return 0;
}

static int parse_binary(int lowest_level);

// a && b and a || b skip the right side if the left side already decides it.
static void parse_and_or(TokenKind operation, int level)
{
    if (operation == TOKEN_OR) {
        emit_test_eax();
        emit_bytes(0x0F, 0x95, 0xC0);           // setne al
        emit_bytes(0x0F, 0xB6, 0xC0);           // movzx eax, al
        emit_bytes(0x83, 0xF0, 0x01);           // xor eax, 1: 0 now means "true"
    }

    emit_test_eax();

    int skip = emit_jump_if_zero();

    parse_binary(level + 1);
    emit_test_eax();
    emit_bytes(0x0F, 0x95, 0xC0);
    emit_bytes(0x0F, 0xB6, 0xC0);

    int done = emit_jump();

    patch_jump(skip);
    emit_move_number_to_eax(operation == TOKEN_OR ? 1 : 0);
    patch_jump(done);
}

static int parse_binary(int lowest_level)
{
    int left_type = parse_unary();

    for (;;) {
        TokenKind operation = token;
        int level = level_of_operator(operation);

        if (level == 0 || level < lowest_level)
            return left_type;

        next_token();

        if (operation == TOKEN_AND || operation == TOKEN_OR) {
            parse_and_or(operation, level);
            left_type = TYPE_BOOL_VALUE;
            continue;
        }

        emit_push_eax();

        int right_type = parse_binary(level + 1);
        bool is_compare = level == 3 || level == 4;

        if (left_type == TYPE_STRING_VALUE && operation == TOKEN_PLUS) {
            emit_join_strings(right_type);
            left_type = TYPE_STRING_VALUE;
        } else if (left_type == TYPE_STRING_VALUE && right_type == TYPE_STRING_VALUE) {
            if (level != 3)
                compile_error("strings can only use + == and !=");

            emit_push_eax();
            emit_call_named("__same_text");
            emit_add_to_stack_pointer(8);

            if (operation == TOKEN_NOT_EQUAL)
                emit_bytes(0x83, 0xF0, 0x01);   // xor eax, 1

            left_type = TYPE_BOOL_VALUE;
        } else if (left_type == TYPE_STRING_VALUE || right_type == TYPE_STRING_VALUE) {
            compile_error("this can't be done with a string");
        } else if (is_compare) {
            emit_move_eax_to_ecx();
            emit_pop_eax();
            emit_compare(operation);
            left_type = TYPE_BOOL_VALUE;
        } else {
            emit_move_eax_to_ecx();
            emit_pop_eax();
            emit_math(operation);
            left_type = TYPE_INT_VALUE;
        }
    }
}

static int parse_expression()
{
    int type = parse_binary(1);

    // condition ? this : that
    if (accept(TOKEN_QUESTION)) {
        emit_test_eax();

        int go_to_else = emit_jump_if_zero();

        type = parse_expression();
        expect(TOKEN_COLON, "expected : here");

        int done = emit_jump();

        patch_jump(go_to_else);
        parse_expression();
        patch_jump(done);
    }
    return type;
}

// ---------- Statements ----------

static void parse_block()
{
    int saved_local_count = local_count;

    expect(TOKEN_LEFT_CURLY, "expected {");

    while (token != TOKEN_RIGHT_CURLY) {
        if (token == TOKEN_END)
            compile_error("this block has no closing }");
        parse_statement();
    }

    next_token();
    local_count = saved_local_count;    // its variables are gone now
}

static int allocate_local(int bytes)
{
    next_local_offset += bytes;
    if (next_local_offset > biggest_local_offset)
        biggest_local_offset = next_local_offset;
    return -next_local_offset;
}

static Variable *add_local(const char *name, ValueType type)
{
    if (local_count == MAX_VARIABLES)
        compile_error("too many variables");

    Variable *variable = &locals[local_count];

    local_count++;
    memset(variable, 0, sizeof(*variable));
    copy_name(variable->name, name);
    variable->type = type;
    return variable;
}

// Array sizes have to be known while compiling.
static int parse_constant_number()
{
    if (token == TOKEN_NUMBER) {
        int value = token_number;

        next_token();
        return value;
    }

    if (token == TOKEN_NAME) {
        Variable *variable = find_variable(token_text);

        if (variable != nullptr && variable->is_constant) {
            next_token();
            return variable->constant_value;
        }
    }

    compile_error("this has to be a number or a const");
    return 0;
}

// Reads { 1, 2, 3 } and stores each value in the array.
static void parse_array_values(const Variable *array, int size)
{
    int index = 0;

    expect(TOKEN_LEFT_CURLY, "expected { for the array values");

    while (token != TOKEN_RIGHT_CURLY) {
        if (index >= size)
            compile_error("too many values for this array");

        parse_expression();
        emit_push_eax();
        emit_load_variable(array);
        emit_byte(0x05);                        // add eax, index * 4
        emit_number(index * 4);
        emit_move_eax_to_ecx();
        emit_pop_eax();
        emit_store_eax_at_ecx();
        index++;

        if (!accept(TOKEN_COMMA))
            break;
    }
    expect(TOKEN_RIGHT_CURLY, "expected } after the array values");
}

static void parse_local_declaration()
{
    bool is_constant = accept(TOKEN_CONST);
    ValueType type = type_of_token(token);

    if (type == TYPE_VOID_VALUE)
        compile_error("a variable can't be void");

    next_token();

    for (;;) {
        if (token != TOKEN_NAME)
            compile_error("expected a variable name");

        Variable *variable = add_local(token_text, type);

        next_token();

        if (accept(TOKEN_LEFT_SQUARE)) {
            int size = parse_constant_number();

            expect(TOKEN_RIGHT_SQUARE, "expected ] here");

            variable->is_array = true;
            variable->location = allocate_local(size * 4);

            if (accept(TOKEN_ASSIGN))
                parse_array_values(variable, size);
        } else if (is_constant && token == TOKEN_ASSIGN && type != TYPE_STRING_VALUE) {
            next_token();
            variable->is_constant = true;
            variable->constant_value = parse_constant_number();
        } else {
            variable->location = allocate_local(4);

            if (accept(TOKEN_ASSIGN)) {
                parse_expression();
            } else if (type == TYPE_STRING_VALUE) {
                emit_move_number_to_eax(add_text_to_data("", 0));
            } else {
                emit_clear_eax();
            }
            emit_store_variable(variable);
        }

        if (!accept(TOKEN_COMMA))
            break;
    }
}

static void add_loop()
{
    if (loop_count == MAX_LOOPS)
        compile_error("too many loops inside each other");

    loops[loop_count].break_count = 0;
    loops[loop_count].continue_count = 0;
    loop_count++;
}

static void finish_loop(uint32_t continue_address)
{
    Loop *loop = &loops[loop_count - 1];

    for (int i = 0; i < loop->continue_count; i++)
        patch_jump_to(loop->continue_jumps[i], continue_address);

    for (int i = 0; i < loop->break_count; i++)
        patch_jump(loop->break_jumps[i]);

    loop_count--;
}

static void parse_if()
{
    next_token();
    expect(TOKEN_LEFT_ROUND, "expected ( after if");
    parse_expression();
    expect(TOKEN_RIGHT_ROUND, "expected ) after the condition");

    emit_test_eax();

    int go_to_else = emit_jump_if_zero();

    parse_statement();

    if (accept(TOKEN_ELSE)) {
        int done = emit_jump();

        patch_jump(go_to_else);
        parse_statement();
        patch_jump(done);
    } else {
        patch_jump(go_to_else);
    }
}

static void parse_while()
{
    uint32_t start = here();

    next_token();
    expect(TOKEN_LEFT_ROUND, "expected ( after while");
    parse_expression();
    expect(TOKEN_RIGHT_ROUND, "expected ) after the condition");

    emit_test_eax();

    int leave = emit_jump_if_zero();

    add_loop();
    parse_statement();
    emit_jump_to(start);
    patch_jump(leave);
    finish_loop(start);
}

static void parse_do_while()
{
    uint32_t start = here();

    next_token();
    add_loop();
    parse_statement();

    uint32_t check = here();

    expect(TOKEN_WHILE, "expected while after the do block");
    expect(TOKEN_LEFT_ROUND, "expected ( after while");
    parse_expression();
    expect(TOKEN_RIGHT_ROUND, "expected ) after the condition");
    expect(TOKEN_SEMICOLON, "expected ; here");

    emit_test_eax();

    int leave = emit_jump_if_zero();

    emit_jump_to(start);
    patch_jump(leave);
    finish_loop(check);
}

// The step comes before the body in the text, but must run after it, so the
// code jumps over the step and back again.
static void parse_for()
{
    int saved_local_count = local_count;

    next_token();
    expect(TOKEN_LEFT_ROUND, "expected ( after for");

    if (is_type_token(token) || token == TOKEN_CONST)
        parse_local_declaration();
    else if (token != TOKEN_SEMICOLON)
        parse_expression();
    expect(TOKEN_SEMICOLON, "expected ; in the for loop");

    uint32_t check = here();

    if (token != TOKEN_SEMICOLON)
        parse_expression();
    else
        emit_move_number_to_eax(1);
    expect(TOKEN_SEMICOLON, "expected ; in the for loop");

    emit_test_eax();

    int leave = emit_jump_if_zero();
    int go_to_body = emit_jump();
    uint32_t step = here();

    if (token != TOKEN_RIGHT_ROUND)
        parse_expression();
    expect(TOKEN_RIGHT_ROUND, "expected ) after the for loop");
    emit_jump_to(check);

    patch_jump(go_to_body);
    add_loop();
    parse_statement();
    emit_jump_to(step);
    patch_jump(leave);
    finish_loop(step);

    local_count = saved_local_count;
}

static void add_loop_jump(bool is_break)
{
    if (loop_count == 0)
        compile_error("break and continue only work inside a loop");

    Loop *loop = &loops[loop_count - 1];
    int *count = is_break ? &loop->break_count : &loop->continue_count;

    if (*count == MAX_JUMPS)
        compile_error("too many break or continue statements");

    int position = emit_jump();

    if (is_break)
        loop->break_jumps[*count] = position;
    else
        loop->continue_jumps[*count] = position;

    *count = *count + 1;
}

static void emit_print(int type)
{
    emit_push_eax();

    if (type == TYPE_STRING_VALUE)
        emit_call_named("__print_string");
    else if (type == TYPE_CHAR_VALUE)
        emit_call_named("__print_char");
    else
        emit_call_named("__print_int");

    emit_add_to_stack_pointer(4);
}

static void parse_cout()
{
    next_token();

    while (accept(TOKEN_OUTPUT)) {
        if (accept(TOKEN_ENDL)) {
            emit_move_number_to_eax('\n');
            emit_print(TYPE_CHAR_VALUE);
        } else {
            emit_print(parse_binary(1));
        }
    }
}

// Calls a library function, then stores its answer in the variable or element
// named next in the source. Used by cin and getline.
static void parse_read_into(const char *int_reader, const char *string_reader,
                            const char *char_reader)
{
    if (token != TOKEN_NAME)
        compile_error("expected a variable to read into");

    Variable *variable = find_variable(token_text);

    if (variable == nullptr)
        compile_error("this variable doesn't exist");
    if (variable->is_constant)
        compile_error("a const variable can't be changed");

    next_token();

    bool is_element = token == TOKEN_LEFT_SQUARE;
    bool is_byte = is_element && !variable->is_array;
    int type = is_byte ? TYPE_CHAR_VALUE : variable->type;

    if (is_element) {
        parse_element_address(variable);
        emit_push_eax();
    }

    if (type == TYPE_STRING_VALUE)
        emit_call_named(string_reader);
    else if (type == TYPE_CHAR_VALUE)
        emit_call_named(char_reader);
    else
        emit_call_named(int_reader);

    if (is_element) {
        emit_pop_ecx();
        emit_store_element(is_byte);
    } else {
        emit_store_variable(variable);
    }
}

static void parse_cin()
{
    next_token();

    while (accept(TOKEN_INPUT))
        parse_read_into("__read_int", "__read_word", "__read_char");
}

static void parse_getline()
{
    next_token();
    expect(TOKEN_LEFT_ROUND, "expected ( after getline");
    expect(TOKEN_CIN, "getline needs cin first");
    expect(TOKEN_COMMA, "expected , after cin");
    parse_read_into("__read_line", "__read_line", "__read_line");
    expect(TOKEN_RIGHT_ROUND, "expected ) after getline");
}

static void parse_statement()
{
    if (token == TOKEN_LEFT_CURLY) {
        parse_block();
    } else if (token == TOKEN_IF) {
        parse_if();
    } else if (token == TOKEN_WHILE) {
        parse_while();
    } else if (token == TOKEN_DO) {
        parse_do_while();
    } else if (token == TOKEN_FOR) {
        parse_for();
    } else if (accept(TOKEN_SEMICOLON)) {
        // an empty statement does nothing
    } else if (is_type_token(token) || token == TOKEN_CONST) {
        parse_local_declaration();
        expect(TOKEN_SEMICOLON, "expected ; after the declaration");
    } else if (accept(TOKEN_RETURN)) {
        if (token != TOKEN_SEMICOLON)
            parse_expression();
        expect(TOKEN_SEMICOLON, "expected ; after return");
        emit_function_end_code();
    } else if (accept(TOKEN_BREAK)) {
        add_loop_jump(true);
        expect(TOKEN_SEMICOLON, "expected ; after break");
    } else if (accept(TOKEN_CONTINUE)) {
        add_loop_jump(false);
        expect(TOKEN_SEMICOLON, "expected ; after continue");
    } else if (token == TOKEN_COUT) {
        parse_cout();
        expect(TOKEN_SEMICOLON, "expected ; after cout");
    } else if (token == TOKEN_CIN) {
        parse_cin();
        expect(TOKEN_SEMICOLON, "expected ; after cin");
    } else if (token == TOKEN_GETLINE) {
        parse_getline();
        expect(TOKEN_SEMICOLON, "expected ; after getline");
    } else {
        parse_expression();
        expect(TOKEN_SEMICOLON, "expected ; at the end of this line");
    }
}

// ---------- The top level: functions and global variables ----------

static void parse_parameters(Function *function)
{
    Variable *parameters[MAX_PARAMETERS];
    int count = 0;

    if (token == TOKEN_VOID && source_position[0] == ')')
        next_token();

    while (token != TOKEN_RIGHT_ROUND) {
        accept(TOKEN_CONST);

        if (!is_type_token(token))
            compile_error("expected a parameter type");
        if (count == MAX_PARAMETERS)
            compile_error("too many parameters");

        ValueType type = type_of_token(token);

        next_token();

        // A prototype may leave out the name.
        Variable *variable = add_local((token == TOKEN_NAME) ? token_text : "", type);

        if (token == TOKEN_NAME)
            next_token();

        if (accept(TOKEN_LEFT_SQUARE)) {
            expect(TOKEN_RIGHT_SQUARE, "expected ] after the array parameter");
            variable->is_array = true;
            variable->is_array_parameter = true;
        }

        parameters[count] = variable;
        count++;

        if (!accept(TOKEN_COMMA))
            break;
    }
    expect(TOKEN_RIGHT_ROUND, "expected ) after the parameters");

    // Arguments are pushed left to right, so the last one is nearest.
    for (int i = 0; i < count; i++)
        parameters[i]->location = 8 + (count - 1 - i) * 4;

    function->parameter_count = count;
}

static void parse_function(ValueType return_type, const char *name)
{
    Function *function = find_function(name);

    if (function == nullptr) {
        if (function_count == MAX_FUNCTIONS)
            compile_error("too many functions");

        function = &functions[function_count];
        function_count++;
        memset(function, 0, sizeof(*function));
        copy_name(function->name, name);
    }

    function->return_type = return_type;

    local_count = 0;
    next_local_offset = 0;
    biggest_local_offset = 0;

    next_token();               // the (
    parse_parameters(function);

    // Just a prototype, like: int add(int a, int b);
    if (accept(TOKEN_SEMICOLON)) {
        local_count = 0;
        return;
    }

    if (function->defined_in_pass == pass)
        compile_error("this function is already defined");

    function->defined_in_pass = pass;
    function->address = here();

    uint32_t stack_size_position;

    emit_function_start_code(&stack_size_position);
    parse_block();

    // Falling off the end of a function returns 0, which main relies on.
    emit_clear_eax();
    emit_function_end_code();

    write_number_at(stack_size_position, biggest_local_offset);
    local_count = 0;
}

static void write_number_to_data(uint32_t address, uint32_t value)
{
    uint32_t position = address - DATA_ADDRESS;

    data[position] = value & 0xFF;
    data[position + 1] = (value >> 8) & 0xFF;
    data[position + 2] = (value >> 16) & 0xFF;
    data[position + 3] = (value >> 24) & 0xFF;
}

static Variable *add_global(const char *name, ValueType type)
{
    if (global_count == MAX_VARIABLES)
        compile_error("too many global variables");

    Variable *variable = &globals[global_count];

    global_count++;
    memset(variable, 0, sizeof(*variable));
    copy_name(variable->name, name);
    variable->type = type;
    variable->is_global = true;
    return variable;
}

// Reads the starting value of a global, which must be known while compiling.
static uint32_t parse_global_value()
{
    if (token == TOKEN_TEXT) {
        uint32_t address = add_text_to_data(token_text, token_text_length);

        next_token();
        return address;
    }

    if (token == TOKEN_LETTER) {
        uint32_t value = token_number;

        next_token();
        return value;
    }

    if (accept(TOKEN_TRUE))
        return 1;
    if (accept(TOKEN_FALSE))
        return 0;

    bool negative = accept(TOKEN_MINUS);
    int value = parse_constant_number();

    return (uint32_t)(negative ? -value : value);
}

// Arrays go in the BSS, which takes no space in the file because it is all zeros.
static void parse_global_variable(ValueType type, const char *name, bool is_constant)
{
    for (;;) {
        Variable *variable = add_global(name, type);

        if (accept(TOKEN_LEFT_SQUARE)) {
            int size = parse_constant_number();

            expect(TOKEN_RIGHT_SQUARE, "expected ] here");
            variable->is_array = true;
            variable->location = BSS_ADDRESS + bss_size;
            bss_size += size * 4;
        } else {
            uint32_t value = 0;

            if (accept(TOKEN_ASSIGN))
                value = parse_global_value();
            else if (type == TYPE_STRING_VALUE)
                value = add_text_to_data("", 0);

            if (is_constant && type != TYPE_STRING_VALUE) {
                variable->is_constant = true;
                variable->constant_value = (int)value;
            } else {
                uint32_t space = (data_size + 3) & ~3u;

                if (space + 4 > MAX_DATA_SIZE)
                    compile_error("too many global variables");

                data_size = space + 4;
                variable->location = DATA_ADDRESS + space;
                write_number_to_data(DATA_ADDRESS + space, value);
            }
        }

        if (!accept(TOKEN_COMMA))
            break;

        if (token != TOKEN_NAME)
            compile_error("expected a variable name");

        copy_name((char *)name, token_text);
        next_token();
    }
    expect(TOKEN_SEMICOLON, "expected ; after the global variable");
}

static void parse_program()
{
    next_token();

    while (token != TOKEN_END) {
        char name[MAX_NAME_LENGTH];

        if (accept(TOKEN_USING)) {
            expect(TOKEN_NAMESPACE, "expected namespace");
            next_token();
            expect(TOKEN_SEMICOLON, "expected ; here");
            continue;
        }

        bool is_constant = accept(TOKEN_CONST);

        if (!is_type_token(token))
            compile_error("expected a function or a variable here");

        ValueType type = type_of_token(token);

        next_token();

        if (token != TOKEN_NAME)
            compile_error("expected a name here");

        copy_name(name, token_text);
        next_token();

        if (token == TOKEN_LEFT_ROUND)
            parse_function(type, name);
        else
            parse_global_variable(type, name, is_constant);
    }
}

static void compile_text(const char *text, bool is_runtime)
{
    source_position = text;
    line_number = 1;
    reading_runtime = is_runtime;
    parse_program();
}

static void compile_everything()
{
    for (pass = 1; pass <= LAST_PASS; pass++) {
        code_size = 0;
        data_size = 0;
        bss_size = 0;
        global_count = 0;
        local_count = 0;
        loop_count = 0;

        // Where every program begins: call main, then exit with its answer.
        Function *main_function = find_function("main");

        emit_byte(0xE8);
        emit_number(main_function != nullptr ? main_function->address - (here() + 4) : 0);
        emit_bytes(0x89, 0xC3);         // mov ebx, eax
        emit_move_number_to_eax(SYS_EXIT);
        emit_bytes(0xCD, 0x80);         // int 0x80

        compile_text(runtime_library, true);
        compile_text(source, false);
    }

    if (find_function("main") == nullptr) {
        fprintf(STDERR, "%s: error: this program has no main function\n", file_name);
        exit(1);
    }
}

// ---------- Writing the finished program as an ELF file ----------

static uint32_t round_up_to_page(uint32_t value)
{
    return (value + PAGE - 1) & ~(uint32_t)(PAGE - 1);
}

static void write_32(uint8_t *to, uint32_t value)
{
    to[0] = value & 0xFF;
    to[1] = (value >> 8) & 0xFF;
    to[2] = (value >> 16) & 0xFF;
    to[3] = (value >> 24) & 0xFF;
}

static void write_16(uint8_t *to, uint32_t value)
{
    to[0] = value & 0xFF;
    to[1] = (value >> 8) & 0xFF;
}

static void write_segment(uint8_t *to, uint32_t offset, uint32_t address, uint32_t file_size,
                          uint32_t memory_size, uint32_t flags)
{
    write_32(to, 1);            // this segment gets loaded
    write_32(to + 4, offset);
    write_32(to + 8, address);
    write_32(to + 12, address);
    write_32(to + 16, file_size);
    write_32(to + 20, memory_size);
    write_32(to + 24, flags);
    write_32(to + 28, PAGE);
}

static void write_program(const char *path)
{
    static uint8_t header[PAGE];
    static const uint8_t padding[PAGE] = { 0 };
    uint32_t data_offset = PAGE + round_up_to_page(code_size);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);

    if (fd < 0) {
        fprintf(STDERR, "c++: %s: %s\n", path, error_text(fd));
        exit(1);
    }

    memset(header, 0, PAGE);
    header[0] = 0x7F;
    header[1] = 'E';
    header[2] = 'L';
    header[3] = 'F';
    header[4] = 1;              // 32-bit
    header[5] = 1;              // little-endian
    header[6] = 1;              // ELF version 1

    write_16(header + 16, 2);   // a program, not a library
    write_16(header + 18, 3);   // for x86
    write_32(header + 20, 1);
    write_32(header + 24, CODE_ADDRESS);
    write_32(header + 28, ELF_HEADER_SIZE);
    write_16(header + 40, ELF_HEADER_SIZE);
    write_16(header + 42, ELF_SEGMENT_SIZE);
    write_16(header + 44, ELF_SEGMENTS);

    uint8_t *segments = header + ELF_HEADER_SIZE;

    write_segment(segments, PAGE, CODE_ADDRESS, code_size, code_size, 5);
    write_segment(segments + ELF_SEGMENT_SIZE, data_offset, DATA_ADDRESS, data_size,
                  round_up_to_page(data_size + 1), 6);
    write_segment(segments + 2 * ELF_SEGMENT_SIZE, 0, BSS_ADDRESS, 0,
                  round_up_to_page(bss_size + 1), 6);

    write(fd, header, PAGE);
    write(fd, code, code_size);
    write(fd, padding, data_offset - PAGE - code_size);
    write(fd, data, data_size);
    close(fd);
}

static void read_source_file(const char *path)
{
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        fprintf(STDERR, "c++: %s: %s\n", path, error_text(fd));
        exit(1);
    }

    int length = read(fd, source, MAX_SOURCE_SIZE - 1);

    close(fd);

    if (length < 0 || length == MAX_SOURCE_SIZE - 1) {
        fprintf(STDERR, "c++: %s: the file is too big\n", path);
        exit(1);
    }
    source[length] = '\0';
}

int main(int argc, char *argv[])
{
    const char *input_file = nullptr;
    const char *output_file = "a.out";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[i + 1];
            i++;
        } else {
            input_file = argv[i];
        }
    }

    if (input_file == nullptr) {
        fprintf(STDERR, "usage: c++ <file.cpp> [-o program]\n");
        return 1;
    }

    file_name = input_file;
    read_source_file(input_file);
    compile_everything();
    write_program(output_file);
    return 0;
}

// The C++ library, written in the small C++ this compiler understands. A string
// here is the address of some bytes, so s[i] reads and writes single characters.
const char *const runtime_library = R"RUNTIME(
string __one_letter = "x";
int __input_address;
int __input_length;
int __input_position;
int __random_number = 12345;

int __length(string s)
{
    int count = 0;
    while (s[count] != 0)
        count = count + 1;
    return count;
}

void __print_string(string s)
{
    __syscall(17, 1, __to_int(s), __length(s));
}

string __allocate(int size)
{
    return __to_string(__syscall(13, size, 0, 0));
}

void __print_char(char c)
{
    __one_letter[0] = c;
    __syscall(17, 1, __to_int(__one_letter), 1);
}

void __print_negative(int number)
{
    if (number <= -10)
        __print_negative(number / 10);
    __print_char((char)('0' - number % 10));
}

void __print_int(int number)
{
    if (number < 0) {
        __print_char('-');
        __print_negative(number);
    } else {
        __print_negative(-number);
    }
}

int __next_char()
{
    if (__input_position >= __input_length) {
        if (__input_address == 0)
            __input_address = __to_int(__allocate(512));
        __input_length = __syscall(16, 0, __input_address, 512);
        __input_position = 0;
        if (__input_length <= 0)
            return -1;
    }

    string buffer = __to_string(__input_address);
    int c = buffer[__input_position];
    __input_position = __input_position + 1;
    return c;
}

void __put_back_char()
{
    __input_position = __input_position - 1;
}

int __is_space(int c)
{
    return c == ' ' || c == '\n' || c == '\t' || c == '\r';
}

int __skip_spaces()
{
    int c = __next_char();
    while (__is_space(c))
        c = __next_char();
    return c;
}

int __read_int()
{
    int c = __skip_spaces();
    int negative = 0;
    int number = 0;

    if (c == '-') {
        negative = 1;
        c = __next_char();
    }

    while (c >= '0' && c <= '9') {
        number = number * 10 + (c - '0');
        c = __next_char();
    }

    if (c != -1)
        __put_back_char();

    if (negative)
        return -number;
    return number;
}

char __read_char()
{
    return (char)__skip_spaces();
}

string __read_word()
{
    string word = __allocate(512);
    int length = 0;
    int c = __skip_spaces();

    while (c != -1 && !__is_space(c) && length < 511) {
        word[length] = (char)c;
        length = length + 1;
        c = __next_char();
    }

    if (c != -1)
        __put_back_char();

    word[length] = (char)0;
    return word;
}

string __read_line()
{
    string text = __allocate(512);
    int length = 0;
    int c = __next_char();

    // A leftover Enter from the last cin belongs to the line before.
    if (c == '\n')
        c = __next_char();

    while (c != -1 && c != '\n' && length < 511) {
        text[length] = (char)c;
        length = length + 1;
        c = __next_char();
    }

    text[length] = (char)0;
    return text;
}

string __join(string left, string right)
{
    int left_length = __length(left);
    int right_length = __length(right);
    string result = __allocate(left_length + right_length + 1);

    for (int i = 0; i < left_length; i++)
        result[i] = left[i];
    for (int i = 0; i < right_length; i++)
        result[left_length + i] = right[i];

    result[left_length + right_length] = (char)0;
    return result;
}

string __char_to_string(char c)
{
    string text = __allocate(2);
    text[0] = c;
    text[1] = (char)0;
    return text;
}

bool __same_text(string left, string right)
{
    int i = 0;
    while (left[i] == right[i]) {
        if (left[i] == 0)
            return true;
        i = i + 1;
    }
    return false;
}

int abs(int number)
{
    if (number < 0)
        return -number;
    return number;
}

int min(int a, int b)
{
    if (a < b)
        return a;
    return b;
}

int max(int a, int b)
{
    if (a > b)
        return a;
    return b;
}

int rand()
{
    __random_number = __random_number * 1103515 + 12345;
    return abs(__random_number / 65536) % 32768;
}

void exit(int code)
{
    __syscall(3, code, 0, 0);
}

string to_string(int number)
{
    string digits = "";
    int negative = number < 0;

    if (number == 0)
        return "0";
    if (negative)
        number = -number;

    while (number > 0) {
        digits = __char_to_string((char)('0' + number % 10)) + digits;
        number = number / 10;
    }

    if (negative)
        return "-" + digits;
    return digits;
}

int stoi(string text)
{
    int i = 0;
    int negative = 0;
    int number = 0;

    if (text[0] == '-') {
        negative = 1;
        i = 1;
    }

    while (text[i] >= '0' && text[i] <= '9') {
        number = number * 10 + (text[i] - '0');
        i = i + 1;
    }

    if (negative)
        return -number;
    return number;
}
)RUNTIME";
