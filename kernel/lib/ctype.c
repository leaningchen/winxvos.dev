#include <types.h>
#include <ctype.h>

bool isdigit(int c) { return c >= '0' && c <= '9'; }
bool isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool isalnum(int c) { return isalpha(c) || isdigit(c); }
bool isupper(int c) { return c >= 'A' && c <= 'Z'; }
bool islower(int c) { return c >= 'a' && c <= 'z'; }
bool isspace(int c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
bool isprint(int c) { return c >= 0x20 && c <= 0x7E; }

int toupper(int c) { return islower(c) ? c - 'a' + 'A' : c; }
int tolower(int c) { return isupper(c) ? c - 'A' + 'a' : c; }