#ifndef UTIL_H
#define UTIL_H
#ifdef __linux__

int easprintf(char ** __restrict, const char * __restrict, ...);
void *ecalloc(size_t, size_t);
void *emalloc(size_t);
void *erealloc(void *, size_t);
char *estrdup(const char *);

#else
    #include <util.h>
#endif
#endif
