// Bubble sort: an array passed to a function.

#include <iostream>
using namespace std;

const int SIZE = 6;

void sort_numbers(int numbers[], int count)
{
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - 1 - i; j++) {
            if (numbers[j] > numbers[j + 1]) {
                int temp = numbers[j];
                numbers[j] = numbers[j + 1];
                numbers[j + 1] = temp;
            }
        }
    }
}

int main()
{
    int numbers[SIZE] = { 42, 7, 19, 3, 88, 5 };

    sort_numbers(numbers, SIZE);

    cout << "Sorted:";
    for (int i = 0; i < SIZE; i++)
        cout << " " << numbers[i];
    cout << endl;

    return 0;
}
