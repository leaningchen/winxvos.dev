/*===========================================================================
 * kernel/sysfile.c — 文件系统相关系统调用完整实现
 *
 * 实现：open, close, read, write, dup, fstat, link, unlink,
 *       mkdir, mknod, chdir, pipe
 *
 * 参考：xv6-public sysfile.c，适配 64 位 WinixOS
 *===========================================================================*/

#include <types.h>
#include <param.h>
#include <spinlock.h>
#include <sleeplock.h>
#include <fs.h>
#include <buf.h>
#include <file.h>
#include <proc.h>
#include <defs.h>
#include <idt.h>
#include <syscall.h>
#include <libc.h>

/*---------------------------------------------------------------------------
 * fdalloc — 在当前进程的 ofile[] 中分配一个文件描述符槽
 *---------------------------------------------------------------------------*/
static int
fdalloc(struct file *f)
{
    struct proc *p = myproc();
    for (int fd = 0; fd < NOFILE; fd++) {
        if (p->ofile[fd] == 0) {
            p->ofile[fd] = f;
            return fd;
        }
    }
    return -1;
}

/*---------------------------------------------------------------------------
 * sys_dup — dup(fd): 复制文件描述符
 *---------------------------------------------------------------------------*/
int64_t
sys_dup(struct trapframe *tf)
{
    int fd;
    if (argint(tf, 1, &fd) < 0)
        return -1;

    struct proc *p = myproc();
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return -1;

    struct file *f = filedup(p->ofile[fd]);
    int nfd = fdalloc(f);
    if (nfd < 0) {
        fileclose(f);
        return -1;
    }
    return nfd;
}

/*---------------------------------------------------------------------------
 * sys_read — read(fd, buf, n)
 *---------------------------------------------------------------------------*/
int64_t
sys_read(struct trapframe *tf)
{
    int fd, n;
    uint64_t p;

    if (argint(tf, 1, &fd) < 0 || argptr(tf, 2, &p) < 0 || argint(tf, 3, &n) < 0)
        return -1;

    struct proc *proc = myproc();
    if (fd < 0 || fd >= NOFILE || proc->ofile[fd] == 0)
        return -1;

    return fileread(proc->ofile[fd], (char *)p, n);
}

/*---------------------------------------------------------------------------
 * sys_write — write(fd, buf, n)
 *---------------------------------------------------------------------------*/
int64_t
sys_write(struct trapframe *tf)
{
    int fd, n;
    uint64_t p;

    if (argint(tf, 1, &fd) < 0 || argptr(tf, 2, &p) < 0 || argint(tf, 3, &n) < 0)
        return -1;

    struct proc *proc = myproc();
    if (fd < 0 || fd >= NOFILE || proc->ofile[fd] == 0)
        return -1;

    return filewrite(proc->ofile[fd], (char *)p, n);
}

/*---------------------------------------------------------------------------
 * sys_close — close(fd)
 *---------------------------------------------------------------------------*/
int64_t
sys_close(struct trapframe *tf)
{
    int fd;
    if (argint(tf, 1, &fd) < 0)
        return -1;

    struct proc *p = myproc();
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return -1;

    fileclose(p->ofile[fd]);
    p->ofile[fd] = 0;
    return 0;
}

/*---------------------------------------------------------------------------
 * sys_fstat — fstat(fd, stat)
 *---------------------------------------------------------------------------*/
int64_t
sys_fstat(struct trapframe *tf)
{
    int fd;
    uint64_t st;

    if (argint(tf, 1, &fd) < 0 || argptr(tf, 2, &st) < 0)
        return -1;

    struct proc *p = myproc();
    if (fd < 0 || fd >= NOFILE || p->ofile[fd] == 0)
        return -1;

    return filestat(p->ofile[fd], (struct stat *)st);
}

/*---------------------------------------------------------------------------
 * sys_link — link(old, new): 创建硬链接
 *---------------------------------------------------------------------------*/
int64_t
sys_link(struct trapframe *tf)
{
    char old[128], new_[128];

    if (argstr(tf, 1, old, sizeof(old)) < 0 ||
        argstr(tf, 2, new_, sizeof(new_)) < 0)
        return -1;

    begin_op();

    struct inode *ip = namei(old);
    if (ip == 0) {
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->type == T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    ip->nlink++;
    iupdate(ip);
    iunlock(ip);

    struct inode *dp;
    char name[DIRSIZ];
    if ((dp = nameiparent(new_, name)) == 0)
        goto bad;
    ilock(dp);
    if (dp->dev != ip->dev || dirlink(dp, name, ip->inum) < 0) {
        iunlockput(dp);
        goto bad;
    }
    iunlockput(dp);
    iput(ip);
    end_op();
    return 0;

bad:
    ilock(ip);
    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return -1;
}

/*---------------------------------------------------------------------------
 * isdirempty — 检查目录 dp 是否为空（仅含 . 和 ..）
 *---------------------------------------------------------------------------*/
static int
isdirempty(struct inode *dp)
{
    struct dirent de;
    for (uint32_t off = 2 * sizeof(de); off < dp->size; off += sizeof(de)) {
        if (readi(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
            panic("isdirempty: readi");
        if (de.inum != 0)
            return 0;
    }
    return 1;
}

/*---------------------------------------------------------------------------
 * sys_unlink — unlink(path): 删除目录项
 *---------------------------------------------------------------------------*/
int64_t
sys_unlink(struct trapframe *tf)
{
    char path[128];
    if (argstr(tf, 1, path, sizeof(path)) < 0)
        return -1;

    begin_op();

    char name[DIRSIZ];
    struct inode *dp = nameiparent(path, name);
    if (dp == 0) {
        end_op();
        return -1;
    }

    ilock(dp);

    /* 不允许删除 . 和 .. */
    if (strncmp(name, ".", DIRSIZ) == 0 || strncmp(name, "..", DIRSIZ) == 0) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    uint32_t off;
    struct inode *ip = dirlookup(dp, name, &off);
    if (ip == 0) {
        iunlockput(dp);
        end_op();
        return -1;
    }

    ilock(ip);
    if (ip->nlink < 1)
        panic("unlink: nlink < 1");
    if (ip->type == T_DIR && !isdirempty(ip)) {
        iunlockput(ip);
        iunlockput(dp);
        end_op();
        return -1;
    }

    /* 清空目录项 */
    struct dirent de;
    memset(&de, 0, sizeof(de));
    if (writei(dp, (char *)&de, off, sizeof(de)) != (int)sizeof(de))
        panic("unlink: writei");
    if (ip->type == T_DIR) {
        dp->nlink--;
        iupdate(dp);
    }
    iunlockput(dp);

    ip->nlink--;
    iupdate(ip);
    iunlockput(ip);
    end_op();
    return 0;
}

/*---------------------------------------------------------------------------
 * create — 内部辅助：在路径 path 处创建 inode（type/major/minor）
 *          调用者需在 begin_op/end_op 内调用
 *          返回已上锁的 inode
 *---------------------------------------------------------------------------*/
static struct inode *
create(char *path, int16_t type, int16_t major, int16_t minor)
{
    char name[DIRSIZ];
    struct inode *dp = nameiparent(path, name);
    if (dp == 0)
        return 0;

    ilock(dp);

    struct inode *ip = dirlookup(dp, name, 0);
    if (ip != 0) {
        iunlockput(dp);
        ilock(ip);
        if (type == T_FILE && ip->type == T_FILE)
            return ip;
        iunlockput(ip);
        return 0;
    }

    if ((ip = ialloc(dp->dev, type)) == 0)
        panic("create: ialloc");

    ilock(ip);
    ip->major  = major;
    ip->minor  = minor;
    ip->nlink  = 1;
    iupdate(ip);

    if (type == T_DIR) {
        /* 为新目录创建 . 和 .. 条目 */
        dp->nlink++;
        iupdate(dp);
        if (dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
            panic("create: dots");
    }

    if (dirlink(dp, name, ip->inum) < 0)
        panic("create: dirlink");

    iunlockput(dp);
    return ip;
}

/*---------------------------------------------------------------------------
 * sys_open — open(path, oflags)
 *---------------------------------------------------------------------------*/
int64_t
sys_open(struct trapframe *tf)
{
    char path[128];
    int oflags;

    if (argstr(tf, 1, path, sizeof(path)) < 0 || argint(tf, 2, &oflags) < 0)
        return -1;

    begin_op();

    struct inode *ip;
    if (oflags & O_CREATE) {
        ip = create(path, T_FILE, 0, 0);
        if (ip == 0) {
            end_op();
            return -1;
        }
    } else {
        if ((ip = namei(path)) == 0) {
            end_op();
            return -1;
        }
        ilock(ip);
        if (ip->type == T_DIR && oflags != O_RDONLY) {
            iunlockput(ip);
            end_op();
            return -1;
        }
    }

    struct file *f = filealloc();
    int fd = -1;
    if (f)
        fd = fdalloc(f);

    if (f == 0 || fd < 0) {
        if (f) fileclose(f);
        iunlockput(ip);
        end_op();
        return -1;
    }

    /* 若 O_TRUNC 且为普通文件，截断 */
    if ((oflags & O_TRUNC) && ip->type == T_FILE)
        itrunc(ip);

    f->type     = FD_INODE;
    f->ip       = ip;
    f->off      = 0;
    f->readable = !(oflags & O_WRONLY);
    f->writable = (oflags & O_WRONLY) || (oflags & O_RDWR);

    iunlock(ip);
    end_op();
    return fd;
}

/*---------------------------------------------------------------------------
 * sys_mkdir — mkdir(path)
 *---------------------------------------------------------------------------*/
int64_t
sys_mkdir(struct trapframe *tf)
{
    char path[128];
    if (argstr(tf, 1, path, sizeof(path)) < 0)
        return -1;

    begin_op();
    struct inode *ip = create(path, T_DIR, 0, 0);
    if (ip == 0) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

/*---------------------------------------------------------------------------
 * sys_mknod — mknod(path, major, minor)
 *---------------------------------------------------------------------------*/
int64_t
sys_mknod(struct trapframe *tf)
{
    char path[128];
    int major, minor;

    if (argstr(tf, 1, path, sizeof(path)) < 0 ||
        argint(tf, 2, &major) < 0 ||
        argint(tf, 3, &minor) < 0)
        return -1;

    begin_op();
    struct inode *ip = create(path, T_DEV, (int16_t)major, (int16_t)minor);
    if (ip == 0) {
        end_op();
        return -1;
    }
    iunlockput(ip);
    end_op();
    return 0;
}

/*---------------------------------------------------------------------------
 * sys_chdir — chdir(path)
 *---------------------------------------------------------------------------*/
int64_t
sys_chdir(struct trapframe *tf)
{
    char path[128];
    if (argstr(tf, 1, path, sizeof(path)) < 0)
        return -1;

    begin_op();
    struct inode *ip = namei(path);
    if (ip == 0) {
        end_op();
        return -1;
    }
    ilock(ip);
    if (ip->type != T_DIR) {
        iunlockput(ip);
        end_op();
        return -1;
    }
    iunlock(ip);
    end_op();

    struct proc *p = myproc();
    iput(p->cwd);
    p->cwd = ip;
    return 0;
}

/*---------------------------------------------------------------------------
 * sys_pipe — pipe(fds): 创建管道，fds[0]=读端, fds[1]=写端
 *---------------------------------------------------------------------------*/
int64_t
sys_pipe(struct trapframe *tf)
{
    uint64_t fdarray;
    if (argptr(tf, 1, &fdarray) < 0)
        return -1;

    struct file *rf, *wf;
    if (pipealloc(&rf, &wf) < 0)
        return -1;

    int fd0 = -1, fd1 = -1;
    fd0 = fdalloc(rf);
    if (fd0 < 0) {
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    fd1 = fdalloc(wf);
    if (fd1 < 0) {
        myproc()->ofile[fd0] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }

    /* 将 fd0/fd1 写回用户空间 */
    int fds[2] = { fd0, fd1 };
    if (copyout(myproc()->pgdir, fdarray, fds, 2 * sizeof(int)) < 0) {
        myproc()->ofile[fd0] = 0;
        myproc()->ofile[fd1] = 0;
        fileclose(rf);
        fileclose(wf);
        return -1;
    }
    return 0;
}
