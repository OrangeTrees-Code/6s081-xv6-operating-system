// user/strtok.c

#include "user/strtok.h"
// #include "user/ulib.c"

static char* next; // 保存下次扫描位置

char* strchr2(const char* s, char c);
char* strtok(char* s, const char* delim) {
    if (s)
        next = s;
    if (!next)
        return 0;

    // 跳过开头的分隔符
    char* start = next;
    while (*start && strchr2(delim, *start)) start++;
    if (*start == 0) {
        next = 0;
        return 0;
    }

    // 找到下一个分隔符
    char* end = start;
    while (*end && !strchr2(delim, *end)) end++;

    if (*end) {
        *end = 0;
        next = end + 1;
    }
    else {
        next = 0;
    }

    return start;
}

// 下面实现 strchr，因为 xv6 不用 string.h
char* strchr2(const char* s, char c) {
    while (*s) {
        if (*s == c) return (char*)s;
        s++;
    }
    return c == 0 ? (char*)s : 0;
}
