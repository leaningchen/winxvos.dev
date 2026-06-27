#ifndef __CTYPE_H__
#define __CTYPE_H__

/*===========================================================================
 * ctype.h — 字符分类与转换函数
 *
 * 提供标准 C <ctype.h> 的常用子集，用于在裸机内核中判断字符类别
 * 和进行大小写转换，不依赖 locale（仅处理 ASCII 范围 0~127）。
 *
 * 所有函数参数类型为 int（与标准 C 一致），接受 unsigned char 值
 * 或 EOF(-1)；输入超出 0~127 时行为未定义。
 *===========================================================================*/

#include <types.h>

/*
 * isdigit — 判断是否为十进制数字字符
 * 返回 true:  '0'~'9'
 */
bool isdigit(int c);

/*
 * isalpha — 判断是否为英文字母
 * 返回 true:  'a'~'z' 或 'A'~'Z'
 */
bool isalpha(int c);

/*
 * isalnum — 判断是否为字母或数字
 * 等价于 isalpha(c) || isdigit(c)
 */
bool isalnum(int c);

/*
 * isupper — 判断是否为大写字母
 * 返回 true:  'A'~'Z'
 */
bool isupper(int c);

/*
 * islower — 判断是否为小写字母
 * 返回 true:  'a'~'z'
 */
bool islower(int c);

/*
 * isspace — 判断是否为空白字符
 * 返回 true:  空格(' ')、水平制表('\t')、换行('\n')、
 *             回车('\r')、换页('\f')、垂直制表('\v')
 */
bool isspace(int c);

/*
 * isprint — 判断是否为可打印字符（含空格）
 * 返回 true:  0x20(' ') ~ 0x7E('~')
 */
bool isprint(int c);

/*
 * toupper — 小写字母转大写
 * 非小写字母原样返回。
 * 示例: toupper('a') == 'A'，toupper('3') == '3'
 */
int  toupper(int c);

/*
 * tolower — 大写字母转小写
 * 非大写字母原样返回。
 * 示例: tolower('A') == 'a'，tolower('3') == '3'
 */
int  tolower(int c);

#endif /* __CTYPE_H__ */
