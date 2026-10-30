/* whoami: prints the current user's name. */

#include "libc.h"

int main(void)
{
    struct user user;

    if (user_by_uid(getuid(), &user))
        printf("%s\n", user.name);
    else
        printf("%d\n", getuid());
    return 0;
}
