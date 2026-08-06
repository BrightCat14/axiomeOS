#include "stdio.h"
#include "syscall.h"
#include "signal.h"
#include "string.h"
#include "stdlib.h"

#define MAX_ARGS 32
#define LINE_MAX 256
#define HIST_SIZE 32

static char history[HIST_SIZE][LINE_MAX];
static int hist_count = 0;
static int hist_pos = 0;

static char line[LINE_MAX];
static int line_len = 0;

static void add_history(const char *cmd)
{
    if (cmd[0] == 0) return;
    if (hist_count > 0 && strcmp(history[hist_count - 1], cmd) == 0)
        return;
    if (hist_count < HIST_SIZE) {
        strcpy(history[hist_count], cmd);
        hist_count++;
    } else {
        for (int i = 0; i < HIST_SIZE - 1; i++)
            strcpy(history[i], history[i + 1]);
        strcpy(history[HIST_SIZE - 1], cmd);
    }
    hist_pos = hist_count;
}

static int read_line(char *buf, int max)
{
    int i = 0;
    char c;
    int esc = 0;
    
    for (;;) {
        c = getchar();
        if (c < 0) { sys_yield(); continue; }
        
        if (c == 27) { /* Escape */
            esc = 1;
            continue;
        }
        if (esc && c == '[') {
            esc = 2;
            continue;
        }
        if (esc == 2) {
            if (c == 'A') { /* Up */
                if (hist_pos > 0) {
                    hist_pos--;
                    strcpy(line, history[hist_pos]);
                    line_len = strlen(line);
                    printf("\r\033[K$ %s", line);
                }
            } else if (c == 'B') { /* Down */
                if (hist_pos < hist_count - 1) {
                    hist_pos++;
                    strcpy(line, history[hist_pos]);
                    line_len = strlen(line);
                    printf("\r\033[K$ %s", line);
                } else {
                    hist_pos = hist_count;
                    line_len = 0;
                    line[0] = 0;
                    printf("\r\033[K$ ");
                }
            }
            esc = 0;
            continue;
        }
        esc = 0;
        
        if (c == '\n' || c == '\r') {
            printf("\n");
            break;
        }
        if (c == 127 || c == 8) {
            if (i > 0) {
                i--;
                printf("\b \b");
            }
            continue;
        }
        if (c >= 32 && c < 127 && i < max - 1) {
            buf[i++] = c;
            putchar(c);
        }
    }
    buf[i] = 0;
    if (buf[0]) {
        strcpy(line, buf);
        line_len = i;
        add_history(buf);
    }
    return i;
}

static int tokenize(char *s, char **argv)
{
    int argc = 0;
    char *p = s;
    while (*p && argc < MAX_ARGS) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = 0; p++; }
    }
    return argc;
}

static void do_ps(void)
{
    struct proc_info procs[64];
    int n = sys_ps(procs, 64);
    if (n <= 0) {
        puts("ps: no processes");
        return;
    }
    const char *stname[4] = {"ready", "run", "block", "zomb"};
    puts("PID  STATE  NAME");
    for (int i = 0; i < n && i < 64; i++) {
        const char *sn = (procs[i].state >= 0 && procs[i].state < 4) ?
                         stname[procs[i].state] : "?";
        printf("%d   %s   %s\n", procs[i].pid, sn, procs[i].name);
    }
}

static void print_help(void)
{
    puts("");
    puts("axiome shell - builtins:");
    puts("  echo <text>");
    puts("  ps");
    puts("  help");
    puts("  exit");
    puts("  cd <dir>");
    puts("  pwd");
    puts("  clear");
    puts("");
    puts("External: hello, cat, ls, mkdir, rm, touch, whoami, uname");
    puts("");
}

static void do_cd(char *path)
{
    if (chdir(path) < 0)
        printf("cd: %s: no such directory\n", path);
}

static void do_pwd(void)
{
    char buf[256];
    if (getcwd(buf, sizeof(buf)) == 0)
        printf("%s\n", buf);
    else
        puts("pwd: error");
}

static void do_kill(int argc, char **argv)
{
    if (argc < 3) {
        puts("usage: kill <pid> <sig>");
        return;
    }
    int pid = atoi(argv[1]);
    int sig = atoi(argv[2]);
    if (pid <= 0 || sig <= 0 || sig >= NSIG) {
        puts("kill: invalid");
        return;
    }
    if (kill(pid, sig) < 0)
        printf("kill: cannot signal %d\n", pid);
    else
        printf("kill: signal %d sent to %d\n", sig, pid);
}

static void do_clear(void)
{
    printf("\033[2J\033[H");
}

static int run_command(char *cmd)
{
    size_t cmdlen = strlen(cmd);
    if (cmdlen == 0) return 0;
    
    char copy[LINE_MAX];
    strcpy(copy, cmd);
    char *argv[MAX_ARGS];
    int argc = tokenize(copy, argv);
    if (argc == 0) return 0;

    if (strcmp(argv[0], "exit") == 0) {
        sys_exit(0);
    } else if (strcmp(argv[0], "help") == 0) {
        print_help();
    } else if (strcmp(argv[0], "ps") == 0) {
        do_ps();
    } else if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1) putchar(' ');
            printf("%s", argv[i]);
        }
        printf("\n");
    } else if (strcmp(argv[0], "cd") == 0) {
        do_cd(argc > 1 ? argv[1] : "/");
    } else if (strcmp(argv[0], "pwd") == 0) {
        do_pwd();
    } else if (strcmp(argv[0], "kill") == 0) {
        do_kill(argc, argv);
    } else if (strcmp(argv[0], "clear") == 0) {
        do_clear();
    } else {
        long pid = sys_spawn_cmd(cmd, cmdlen);
        if (pid < 0)
            printf("sh: %s: command not found\n", argv[0]);
        else {
            int status = 0;
            sys_waitpid((int)pid, &status);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    puts("");
    puts("axiome shell v1.0 (type 'help')");
    puts("");

    char line_buf[LINE_MAX];
    for (;;) {
        printf("$ ");
        read_line(line_buf, LINE_MAX);
        run_command(line_buf);
    }
    sys_exit(0);
    return 0;
}
