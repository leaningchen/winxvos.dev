/*===========================================================================
 * user/sh.c — 简单命令行 shell
 *
 * 功能：
 *   - 解析命令行参数
 *   - 支持管道 |
 *   - 支持重定向 < >
 *   - 支持后台 & (简单实现)
 *   - 内建命令：cd, exit
 *
 * 参考：xv6-public sh.c，适配 64 位
 *===========================================================================*/

#include "user.h"

#define MAXCMD    128
#define MAXARGS   16
#define MAXPATH   64

/* 命令类型 */
#define EXEC_CMD  1
#define REDIR_CMD 2
#define PIPE_CMD  3

struct cmd {
    int type;
};

struct execcmd {
    int   type;
    char *argv[MAXARGS];
    char *eargv[MAXARGS];   /* 参数结束指针 */
};

struct redircmd {
    int         type;
    struct cmd *cmd;
    char       *file;
    char       *efile;
    int         mode;
    int         fd;
};

struct pipecmd {
    int         type;
    struct cmd *left;
    struct cmd *right;
};

/* 前向声明 */
static struct cmd *parsecmd(char *);
static void        runcmd(struct cmd *) __attribute__((noreturn));

/*---------------------------------------------------------------------------
 * 简单内存分配（从静态缓冲区）
 *---------------------------------------------------------------------------*/
static char cmdbuf[8192];
static int  cmdbuf_used = 0;

static void *
sh_malloc(int n)
{
    /* 4 字节对齐 */
    n = (n + 3) & ~3;
    if (cmdbuf_used + n > (int)sizeof(cmdbuf)) {
        printf(2, "sh: out of cmd memory\n");
        exit();
    }
    void *p = cmdbuf + cmdbuf_used;
    cmdbuf_used += n;
    return p;
}

static void
sh_reset(void)
{
    cmdbuf_used = 0;
}

/*---------------------------------------------------------------------------
 * 工具函数
 *---------------------------------------------------------------------------*/
static int
fork1(void)
{
    int pid = fork();
    if (pid == -1) {
        printf(2, "sh: fork failed\n");
        exit();
    }
    return pid;
}

/*---------------------------------------------------------------------------
 * runcmd — 执行命令（在子进程中调用）
 *---------------------------------------------------------------------------*/
static void
runcmd(struct cmd *cmd)
{
    int p[2];
    struct execcmd *ecmd;
    struct redircmd *rcmd;
    struct pipecmd  *pcmd;

    if (cmd == 0)
        exit();

    switch (cmd->type) {
    case EXEC_CMD:
        ecmd = (struct execcmd *)cmd;
        if (ecmd->argv[0] == 0)
            exit();
        exec(ecmd->argv[0], ecmd->argv);
        printf(2, "sh: exec %s failed\n", ecmd->argv[0]);
        exit();
        break;

    case REDIR_CMD:
        rcmd = (struct redircmd *)cmd;
        close(rcmd->fd);
        if (open(rcmd->file, rcmd->mode) < 0) {
            printf(2, "sh: open %s failed\n", rcmd->file);
            exit();
        }
        runcmd(rcmd->cmd);
        break;

    case PIPE_CMD:
        pcmd = (struct pipecmd *)cmd;
        if (pipe(p) < 0) {
            printf(2, "sh: pipe failed\n");
            exit();
        }
        if (fork1() == 0) {
            close(1);
            dup(p[1]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->left);
        }
        if (fork1() == 0) {
            close(0);
            dup(p[0]);
            close(p[0]);
            close(p[1]);
            runcmd(pcmd->right);
        }
        close(p[0]);
        close(p[1]);
        wait();
        wait();
        exit();
        break;
    }
    exit();
}

/*---------------------------------------------------------------------------
 * 解析器工具
 *---------------------------------------------------------------------------*/
static char whitespace[] = " \t\r\n\v";
static char symbols[]    = "<|>&;()";

static int
gettoken(char **ps, char *es, char **q, char **eq)
{
    char *s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    if (q) *q = s;
    int ret = *s;
    switch (*s) {
    case 0:
        break;
    case '|':
    case '(':
    case ')':
    case ';':
    case '&':
    case '<':
        s++;
        break;
    case '>':
        s++;
        if (*s == '>') { ret = '+'; s++; }
        break;
    default:
        ret = 'a';
        while (s < es && !strchr(whitespace, *s) && !strchr(symbols, *s))
            s++;
        break;
    }
    if (eq) *eq = s;
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return ret;
}

static int
peek(char **ps, char *es, char *toks)
{
    char *s = *ps;
    while (s < es && strchr(whitespace, *s))
        s++;
    *ps = s;
    return *s && strchr(toks, *s);
}

static struct cmd *parseline(char **, char *);
static struct cmd *parsepipe(char **, char *);
static struct cmd *parseexec(char **, char *);
static struct cmd *parseredir(struct cmd *, char **, char *);
static struct cmd *nulterminate(struct cmd *);

static struct cmd *
parsecmd(char *s)
{
    char *es = s + strlen(s);
    struct cmd *cmd = parseline(&s, es);
    peek(&s, es, "");
    if (s != es) {
        printf(2, "sh: leftover: %s\n", s);
        exit();
    }
    nulterminate(cmd);
    return cmd;
}

static struct cmd *
parseline(char **ps, char *es)
{
    struct cmd *cmd = parsepipe(ps, es);
    /* 忽略 & 和 ; */
    while (peek(ps, es, "&;")) {
        int tok = gettoken(ps, es, 0, 0);
        if (tok == '&' || tok == ';') {
            /* 简单忽略（不支持后台/列表）*/
            (void)tok;
        }
    }
    return cmd;
}

static struct cmd *
parsepipe(char **ps, char *es)
{
    struct cmd *cmd = parseexec(ps, es);
    if (peek(ps, es, "|")) {
        gettoken(ps, es, 0, 0);
        struct pipecmd *p = sh_malloc(sizeof(*p));
        p->type  = PIPE_CMD;
        p->left  = cmd;
        p->right = parsepipe(ps, es);
        cmd = (struct cmd *)p;
    }
    return cmd;
}

static struct cmd *
parseredir(struct cmd *cmd, char **ps, char *es)
{
    int tok, mode, fd;
    char *q, *eq;

    while (peek(ps, es, "<>")) {
        tok = gettoken(ps, es, 0, 0);
        if (tok == '<') {
            mode = O_RDONLY;
            fd   = 0;
        } else if (tok == '>') {
            mode = O_WRONLY | O_CREATE | O_TRUNC;
            fd   = 1;
        } else {   /* '+' = >> */
            mode = O_WRONLY | O_CREATE;
            fd   = 1;
        }
        if (gettoken(ps, es, &q, &eq) != 'a') {
            printf(2, "sh: missing file in redirect\n");
            exit();
        }
        struct redircmd *r = sh_malloc(sizeof(*r));
        r->type  = REDIR_CMD;
        r->cmd   = cmd;
        r->file  = q;
        r->efile = eq;
        r->mode  = mode;
        r->fd    = fd;
        cmd = (struct cmd *)r;
    }
    return cmd;
}

static struct cmd *
parseexec(char **ps, char *es)
{
    if (peek(ps, es, "(")) {
        gettoken(ps, es, 0, 0);
        struct cmd *cmd = parseline(ps, es);
        if (!peek(ps, es, ")")) {
            printf(2, "sh: missing )\n");
            exit();
        }
        gettoken(ps, es, 0, 0);
        cmd = parseredir(cmd, ps, es);
        return cmd;
    }

    struct execcmd *cmd = sh_malloc(sizeof(*cmd));
    cmd->type = EXEC_CMD;

    struct cmd *ret = parseredir((struct cmd *)cmd, ps, es);
    int argc = 0;
    char *q, *eq;

    while (!peek(ps, es, "|)&;")) {
        int tok = gettoken(ps, es, &q, &eq);
        if (tok == 0) break;
        if (tok != 'a') {
            printf(2, "sh: syntax error\n");
            exit();
        }
        cmd->argv[argc]  = q;
        cmd->eargv[argc] = eq;
        argc++;
        if (argc >= MAXARGS) {
            printf(2, "sh: too many args\n");
            exit();
        }
        ret = parseredir(ret, ps, es);
    }
    cmd->argv[argc]  = 0;
    cmd->eargv[argc] = 0;
    return ret;
}

/*---------------------------------------------------------------------------
 * nulterminate — 将所有 eargv 指针处写入 '\0'（就地分割缓冲区）
 *---------------------------------------------------------------------------*/
static struct cmd *
nulterminate(struct cmd *cmd)
{
    struct execcmd *ecmd;
    struct redircmd *rcmd;
    struct pipecmd  *pcmd;

    if (!cmd) return 0;
    switch (cmd->type) {
    case EXEC_CMD:
        ecmd = (struct execcmd *)cmd;
        for (int i = 0; ecmd->argv[i]; i++)
            *ecmd->eargv[i] = 0;
        break;
    case REDIR_CMD:
        rcmd = (struct redircmd *)cmd;
        nulterminate(rcmd->cmd);
        *rcmd->efile = 0;
        break;
    case PIPE_CMD:
        pcmd = (struct pipecmd *)cmd;
        nulterminate(pcmd->left);
        nulterminate(pcmd->right);
        break;
    }
    return cmd;
}

/*---------------------------------------------------------------------------
 * main — shell 主循环
 *---------------------------------------------------------------------------*/
int
main(void)
{
    static char buf[MAXCMD];
    int fd;

    /* 确保标准文件描述符打开 */
    while ((fd = open("console", O_RDWR)) >= 0) {
        if (fd >= 3) {
            close(fd);
            break;
        }
    }

    while (gets(buf, sizeof(buf)) != 0) {
        /* 跳过空行 */
        if (buf[0] == 0 || buf[0] == '\n')
            continue;

        /* 内建命令：cd */
        if (strncmp(buf, "cd ", 3) == 0) {
            /* 去掉尾部换行 */
            char *p = buf + 3;
            int l = strlen(p);
            if (l > 0 && p[l-1] == '\n') p[l-1] = 0;
            if (chdir(p) < 0)
                printf(2, "sh: cannot cd %s\n", p);
            continue;
        }

        /* 内建命令：exit */
        if (strncmp(buf, "exit", 4) == 0)
            exit();

        sh_reset();
        int pid = fork1();
        if (pid == 0) {
            runcmd(parsecmd(buf));
            exit();   /* 不应到达 */
        }
        wait();
    }
    exit();
}
