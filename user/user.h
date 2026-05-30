#ifndef __USER_H__
#define __USER_H__

/*===========================================================================
 * user/user.h — 用户空间系统调用和库函数声明
 *
 * 与内核 include/syscall.h 中的编号对应。
 *===========================================================================*/

typedef unsigned int   uint;
typedef unsigned short ushort;
typedef unsigned char  uchar;
typedef unsigned long  uint64_t;
typedef unsigned int   uint32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef long           int64_t;
typedef int            int32_t;
typedef unsigned long  size_t;

/* stat 结构（与 include/fs.h 中的 struct stat 相同）*/
struct stat {
    short  type;
    int    dev;
    uint   ino;
    short  nlink;
    uint64_t size;
};

/* 文件类型 */
#define T_DIR   1
#define T_FILE  2
#define T_DEV   3

/* open 标志 */
#define O_RDONLY  0x000
#define O_WRONLY  0x001
#define O_RDWR    0x002
#define O_CREATE  0x200
#define O_TRUNC   0x400

/*---------------------------------------------------------------------------
 * 系统调用声明
 *---------------------------------------------------------------------------*/
int   fork(void);
int   exit(void) __attribute__((noreturn));
int   wait(void);
int   pipe(int*);
int   write(int, const void*, int);
int   read(int, void*, int);
int   close(int);
int   kill(int);
int   exec(char*, char**);
int   open(const char*, int);
int   mknod(const char*, int, int);
int   unlink(const char*);
int   fstat(int, struct stat*);
int   link(const char*, const char*);
int   mkdir(const char*);
int   chdir(const char*);
int   dup(int);
int   getpid(void);
int   sleep(int);
int   clone(uint64_t fn, uint64_t stack, uint64_t stacksz, uint64_t arg);
void* sbrk(int);

/*---------------------------------------------------------------------------
 * 用户态库函数
 *---------------------------------------------------------------------------*/
void  puts(const char*);
char* gets(char*, int);
int   strlen(const char*);
void* memset(void*, int, uint);
void* memmove(void*, const void*, uint);
int   memcmp(const void*, const void*, uint);
int   strcmp(const char*, const char*);
int   strncmp(const char*, const char*, uint);
char* strcpy(char*, const char*);
char* strncpy(char*, const char*, uint);
char* strchr(const char*, char);
int   atoi(const char*);
void  itoa(int, char*, int);

/* printf（简化版，仅支持 %d %s %c %p）*/
void  printf(int fd, const char *fmt, ...);

#endif /* __USER_H__ */
