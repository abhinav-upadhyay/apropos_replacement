#ifndef __UTIL_H
#define __UTIL_H
int
easprintf(char **__restrict ret, const char *fmt, ...);

void *
ecalloc(size_t n, size_t s);

void *
emalloc(size_t n);


void *
erealloc(void *p, size_t n);

char *
estrdup(const char *s);
#endif
