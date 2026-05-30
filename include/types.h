#ifndef __TYPES_H__
#define __TYPES_H__

/* 基础整数类型 */
typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

/* 指针与大小类型 (64位) */
typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;
typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef int64_t             off_t;

/* 页表条目类型 (64位) */
typedef uint64_t            pte_t;
typedef uint64_t            pde_t;

/* 布尔类型 */
typedef uint8_t             bool;
#define true  1
#define false 0

/* 空指针 */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* 通用宏 */
#define MIN(a, b)    ((a) < (b) ? (a) : (b))
#define MAX(a, b)    ((a) > (b) ? (a) : (b))

/* 对齐宏 */
#define ALIGN_UP(x, a)     (((x) + (a) - 1) & ~((a) - 1))
#define ALIGN_DOWN(x, a)   ((x) & ~((a) - 1))
#define IS_ALIGNED(x, a)   (((x) & ((a) - 1)) == 0)

/* 数组元素数 */
#define NELEM(arr)  ((sizeof(arr)) / (sizeof((arr)[0])))

#endif /* __TYPES_H__ */