// Checks every feature of the compiler, and prints TEST-PASS if they all work.

#include <iostream>
#include <string>
using namespace std;

const int LIMIT = 5;
int counter = 10;
int global_list[4];
string global_text = "global";
bool global_flag = true;

int later_function(int a, int b);

void add_to_counter(int amount)
{
    counter += amount;
}

int factorial(int n)
{
    return (n <= 1) ? 1 : n * factorial(n - 1);
}

int sum_array(int values[], int count)
{
    int total = 0;
    for (int i = 0; i < count; i++)
        total += values[i];
    return total;
}

void fill_array(int values[], int count)
{
    for (int i = 0; i < count; i++)
        values[i] = i * i;
}

bool is_even(int n)
{
    return n % 2 == 0;
}

string repeat_letter(char letter, int times)
{
    string result = "";
    for (int i = 0; i < times; i++)
        result += letter;
    return result;
}

int failures = 0;

void check(bool passed, int number)
{
    if (!passed) {
        cout << "TEST-FAIL number " << number << endl;
        failures++;
    }
}

int main()
{
    // Arithmetic and precedence
    check(2 + 3 * 4 == 14, 1);
    check((2 + 3) * 4 == 20, 2);
    check(17 / 5 == 3, 3);
    check(17 % 5 == 2, 4);
    check(-7 + 2 == -5, 5);
    check(-17 / 5 == -3, 6);

    // Comparison and logic, including skipping the right side
    check(3 < 4 && 4 <= 4 && 5 > 4 && 5 >= 5, 7);
    check(3 != 4, 8);
    check(!(3 == 4), 9);
    check(true || false, 10);
    check(!(false && true), 11);
    int skipped = 0;
    check(false && (skipped = 1) == 1 ? false : true, 12);
    check(skipped == 0, 13);
    check(true || (skipped = 1) == 1, 14);
    check(skipped == 0, 15);

    // Assignment forms
    int x = 5, y;
    y = x + 1;
    check(y == 6, 16);
    x += 3;
    x -= 1;
    x *= 2;
    x /= 7;
    check(x == 2, 17);
    x = 17;
    x %= 5;
    check(x == 2, 18);

    int step = 3;
    check(step++ == 3, 19);
    check(step == 4, 20);
    check(++step == 5, 21);
    check(step-- == 5, 22);
    check(--step == 3, 23);

    // Loops, break and continue
    int total = 0;
    for (int i = 0; i < 10; i++) {
        if (i == 2)
            continue;
        if (i == 5)
            break;
        total += i;
    }
    check(total == 0 + 1 + 3 + 4, 24);

    int count = 0;
    while (count < 3)
        count++;
    check(count == 3, 25);

    count = 0;
    do {
        count++;
        if (count == 2)
            continue;
    } while (count < 4);
    check(count == 4, 26);

    int inner_total = 0;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            if (j == 1)
                break;
            inner_total++;
        }
    }
    check(inner_total == 3, 27);

    // A name in an inner block hides the outer one, and then comes back.
    int shadow = 1;
    {
        int shadow = 2;
        check(shadow == 2, 28);
    }
    check(shadow == 1, 29);

    // Functions
    check(factorial(6) == 720, 30);
    check(later_function(3, 4) == 25, 31);
    add_to_counter(5);
    check(counter == 15, 32);
    check(is_even(4) && !is_even(5), 33);

    // Arrays, including a const size and starting values
    int values[LIMIT] = { 1, 2, 3, 4, 5 };
    check(sum_array(values, LIMIT) == 15, 34);
    fill_array(values, LIMIT);
    check(values[4] == 16, 35);
    values[1] += 10;
    check(values[1] == 11, 36);
    values[1]++;
    check(values[1] == 12, 37);
    global_list[2] = 9;
    check(global_list[2] == 9 && global_list[3] == 0, 38);

    // Characters
    char letter = 'A';
    check(letter + 1 == 'B', 39);
    check((char)66 == 'B', 40);
    check('\n' == 10, 41);
    check('\r' == 13 && '\t' == 9 && '\\' == 92, 59);

    // Strings
    string text = "abc";
    check(text.length() == 3, 42);
    check(text.size() == 3, 43);
    check(text[1] == 'b', 44);
    check(text == "abc", 45);
    check(text != "abd", 46);
    string joined = text + "-" + "def";
    check(joined == "abc-def", 47);
    joined += "!";
    check(joined.length() == 8, 48);
    check(global_text == "global", 49);
    check(repeat_letter('z', 3) == "zzz", 50);
    check(to_string(-120) == "-120", 51);
    check(to_string(0) == "0", 52);
    check(stoi("-45") == -45, 53);
    text[0] = 'X';
    check(text == "Xbc", 54);

    // Library
    check(abs(-3) == 3, 55);
    check(min(2, 3) == 2 && max(2, 3) == 3, 56);
    check(rand() >= 0, 57);
    check(global_flag, 58);

    // Printing
    cout << "print check: " << -42 << " " << 'c' << " " << "text" << " " << 0 << endl;

    if (failures == 0)
        cout << "TEST-PASS compiler" << endl;
    return 0;
}

int later_function(int a, int b)
{
    return a * a + b * b;
}
