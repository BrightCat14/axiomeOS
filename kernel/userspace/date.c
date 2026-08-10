#include "stdio.h"
#include "time.h"

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    time_t t = time(0);
    char *s = ctime(&t);
    if (s)
        printf("%s", s);
    else
        printf("date: time unavailable\n");
    return 0;
}
