// shutdown: turns the OS off and goes back to the Ubuntu terminal. Only root.

#include "libc.h"

int main()
{
    int status = shutdown();

    // Only reached if the kernel said no.
    fprintf(STDERR, "shutdown: %s (log in as root to turn the OS off)\n",
            error_text(status));
    return 1;
}
