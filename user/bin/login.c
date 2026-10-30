/* login: asks for a name and password, then becomes that user and starts their
 * shell. It starts as root, because only root may change identity. */

#include "libc.h"

#define FIELD_LENGTH 32
#define FAILED_LOGIN_DELAY_MS 1000

static bool read_field(char *buffer)
{
    int length = read(STDIN, buffer, FIELD_LENGTH - 1);

    if (length <= 0)
        return false;

    while (length > 0 && (buffer[length - 1] == '\n' || buffer[length - 1] == '\r'))
        length--;
    buffer[length] = '\0';
    return true;
}

int main(void)
{
    char name[FIELD_LENGTH];
    char password[FIELD_LENGTH];
    struct user user;

    for (;;) {
        char *argv[] = { user.shell, NULL };
        bool name_read;
        bool password_read;

        printf("\nOS Project login: ");
        name_read = read_field(name);

        printf("Password: ");
        set_echo(false);
        password_read = read_field(password);
        set_echo(true);
        printf("\n");

        if (!name_read || !password_read)
            continue;

        /* Check the password even for unknown names, and give the same answer,
         * so nobody can tell which names exist. */
        bool known = user_by_name(name, &user);
        bool correct = user_check_password(name, password);

        memset(password, 0, sizeof(password));

        if (!known || !correct) {
            sleep(FAILED_LOGIN_DELAY_MS);
            printf("Login incorrect\n");
            continue;
        }

        /* gid first: after setuid, this process is no longer allowed to. */
        setgid(user.gid);
        setuid(user.uid);
        chdir(user.home);

        exec(user.shell, argv);
        printf("login: can't start %s\n", user.shell);
        exit(1);
    }
}
