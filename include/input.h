// Keyboard and the input event queue (owner: Resource Management).

#ifndef INPUT_H
#define INPUT_H

void keyboard_init();

// Called from interrupt handlers when a key arrives.
void input_put_char(char c);

// Sleeps until a character is available. Returns false if a signal woke it.
bool input_get_char(char *result);

#endif
