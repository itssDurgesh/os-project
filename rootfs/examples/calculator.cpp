// Reads input with cin. Try typing:   6 * 7

#include <iostream>
using namespace std;

int main()
{
    int first, second;
    char operation;

    cout << "Enter a sum like 6 * 7: ";
    cin >> first >> operation >> second;

    if (operation == '+')
        cout << "Answer: " << first + second << endl;
    else if (operation == '-')
        cout << "Answer: " << first - second << endl;
    else if (operation == '*')
        cout << "Answer: " << first * second << endl;
    else if (operation == '/' && second != 0)
        cout << "Answer: " << first / second << endl;
    else
        cout << "I can't work that out" << endl;

    return 0;
}
