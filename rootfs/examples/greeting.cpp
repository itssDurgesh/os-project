// Strings: reading, joining and looking at single letters.

#include <iostream>
#include <string>
using namespace std;

int main()
{
    string name;

    cout << "What is your name? ";
    cin >> name;

    string message = "Hello, " + name + "!";

    cout << message << endl;
    cout << "Your name has " << name.length() << " letters" << endl;
    cout << "It starts with " << name[0] << endl;

    if (name == "Durgesh")
        cout << "I know you!" << endl;

    return 0;
}
