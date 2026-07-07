#include "stdio.h"
#include "syscall.h"
#include "string.h"
#include "stdlib.h"

#define MAX_ARGS 16
#define LINE_MAX 256

static int tokenize(char *s, char **argv)
{
    int argc = 0;
    char *p = s;
    while (*p && argc < MAX_ARGS)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
        {
            *p = 0;
            p++;
        }
    }
    return argc;
}

static void do_ps(void)
{
    struct proc_info procs[64];
    int n = sys_ps(procs, 64);
    const char *stname[4] = { "ready", "run", "block", "zomb" };
    printf("PID  PPID STATE NAME\n");
    for (int i = 0; i < n; i++)
    {
        const char *sn = (procs[i].state >= 0 && procs[i].state < 4)
                             ? stname[procs[i].state] : "?";
        printf("%-4d %-4d %-5s %s\n", procs[i].pid, procs[i].parent_pid, sn,
               procs[i].name);
    }
}

static void builtin_echo(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
    {
        if (i > 1)
            putchar(' ');
        printf("%s", argv[i]);
    }
    printf("\n");
}

static void print_help(void)
{
    puts("axiome sh - builtins: echo, ps, help, exit");
    puts("external programs are spawned by name: hello, cat, echo");
}

/* Run a single command line (builtin or external). */
static int run_command(char *cmd)
{
    size_t cmdlen = strlen(cmd);
    /* tokenize() mutates its buffer (inserts nulls at spaces), so tokenize a
       copy and keep the original string intact for sys_spawn_cmd. */
    char copy[LINE_MAX];
    size_t cpylen = cmdlen < LINE_MAX - 1 ? cmdlen : LINE_MAX - 1;
    for (size_t i = 0; i <= cpylen; i++)
        copy[i] = cmd[i];
    char *argv[MAX_ARGS];
    int argc = tokenize(copy, argv);
    if (argc == 0)
        return 0;

    if (strcmp(argv[0], "exit") == 0)
    {
        sys_exit(0);
    }
    else if (strcmp(argv[0], "help") == 0)
    {
        print_help();
    }
    else if (strcmp(argv[0], "ps") == 0)
    {
        do_ps();
    }
    else if (strcmp(argv[0], "echo") == 0)
    {
        builtin_echo(argc, argv);
    }
    else
    {
        long pid = sys_spawn_cmd(cmd, cmdlen);
        if (pid < 0)
            printf("sh: %s: command not found\n", argv[0]);
        else
        {
            int status = 0;
            sys_waitpid((int)pid, &status);
        }
    }
    return 0;
}

static int read_line(char *buf, int max)
{
    int i = 0;
    for (;;)
    {
        int c = getchar();
        if (c < 0)
        {
            syscall(SYS_YIELD, 0, 0, 0, 0, 0, 0);
            continue;
        }
        if (c == '\n' || c == '\r')
            break;
        if (i < max - 1)
            buf[i++] = (char)c;
    }
    buf[i] = 0;
    return i;
}

int main(int argc, char **argv)
{
    (void)argc;
    if (argc > 1 && strcmp(argv[1], "-t") == 0)
    {
        puts("[sh selftest]");
        run_command("echo hello from the axiome shell");
        run_command("ps");
        print_help();
        puts("[sh selftest] done");
        sys_exit(0);
        return 0;
    }

    puts("axiome shell (type 'help'; 'exit' to quit)");
    char line[LINE_MAX];
    for (;;)
    {
        printf("$ ");
        read_line(line, LINE_MAX);
        run_command(line);
    }
    sys_exit(0);
    return 0;
}
