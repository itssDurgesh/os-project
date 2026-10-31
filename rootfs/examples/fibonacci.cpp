// Recursion: each number is the sum of the two before it.

#include <iostream>
using namespace std;

int fibonacci(int n)
{
    if (n < 2)
        return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main()
{
    for (int i = 0; i < 10; i++)
        cout << fibonacci(i) << " ";

    cout << endl;
    return 0;
}
