/*===========================================================================
 * user/init.c — 第一个用户进程（pid=1, init）
 *
 * 由内核的 userinit() 创建，负责：
 *   1. 打开 console（stdin/stdout/stderr）
 *   2. exec /sh 启动 shell
 *   3. 等待子进程（作为孤儿进程的收割者）
 *===========================================================================*/

#include "user.h"

char *argv[] = { "sh", 0 };

int
main(void)
{
    int pid, wpid;

    /* 打开控制台设备作为 stdin/stdout/stderr */
    if (open("console", O_RDWR) < 0) {
        mknod("console", 1, 1);   /* 主设备号1=CONSOLE */
        open("console", O_RDWR);
    }
    dup(0);   /* stdout = fd 1 */
    dup(0);   /* stderr = fd 2 */

    for (;;) {
        printf(1, "init: starting sh\n");
        pid = fork();
        if (pid < 0) {
            printf(1, "init: fork failed\n");
            exit();
        }
        if (pid == 0) {
            exec("/sh", argv);
            printf(1, "init: exec sh failed\n");
            exit();
        }

        /* 等待子进程，忽略非 sh 进程的退出 */
        while ((wpid = wait()) >= 0 && wpid != pid)
            printf(1, "zombie: %d\n", wpid);
    }
}
