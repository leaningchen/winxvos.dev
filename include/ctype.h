#ifndef __CTYPE_H__
#define __CTYPE_H__

#include <types.h>

bool isdigit(int c);
bool isalpha(int c);
bool isalnum(int c);
bool isupper(int c);
bool islower(int c);
bool isspace(int c);
bool isprint(int c);
int  toupper(int c);
int  tolower(int c);

#endif /* __CTYPE_H__ */