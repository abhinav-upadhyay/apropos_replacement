/*-
 * Copyright (c) 2006 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
 
#ifdef __linux__

#define _GNU_SOURCE
#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

int
easprintf(char **__restrict ret, const char *fmt, ...)
{

    int rv;
    va_list ap;
    va_start(ap, fmt);
    if ((rv = vasprintf(ret, fmt, ap)) == -1)
        err(EXIT_FAILURE, "Cannot format string");
    va_end(ap);
    return rv;
}

void *
ecalloc(size_t n, size_t s)
{
    void *ret = calloc(n, s);
    if (ret == NULL)
        err(EXIT_FAILURE, "Cannot allocate %zu bytes", n);
    return ret;
}

void *
emalloc(size_t n)
{
    void *ret = malloc(n);
    if (ret == NULL)
        err(EXIT_FAILURE, "Cannot allocate %zu bytes", n);
    return ret;
}


void *
erealloc(void *p, size_t n)
{
    void *ret = realloc(p, n);
    if (ret == NULL)
        err(EXIT_FAILURE, "Cannot re-allocate %zu bytes", n);
    return ret;
}

char *
estrdup(const char *s)
{
    char *ret = strdup(s);
    if (ret == NULL)
        err(EXIT_FAILURE, "Cannot copy string");
    return ret;
}
#endif
