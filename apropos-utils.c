/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Abhinav Upadhyay.
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

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "sqlite3.h"

void
zip(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{	
	int nin, nout;
	long int nout2;
	const unsigned char * inbuf;
	unsigned char *outbuf;
	assert(nval == 1);
	nin = sqlite3_value_bytes(apval[0]);
	inbuf = (const unsigned char *) sqlite3_value_blob(apval[0]);
	nout = nin + 13 + (nin + 999) / 1000;
	outbuf = malloc(nout + 4);
	outbuf[0] = nin >> 24 & 0xff;
	outbuf[1] = nin >> 16 & 0xff;
	outbuf[2] = nin >> 8 & 0xff;
	outbuf[3] = nin & 0xff;
	nout2 = (long int) nout;
	compress(&outbuf[4], (unsigned long *) &nout2, inbuf, nin);
	sqlite3_result_blob(pctx, outbuf, nout2 + 4, free);
	return;
}


void
unzip(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{	
	unsigned int nin, nout, rc;
	const unsigned char * inbuf;
	unsigned char *outbuf;
	long int nout2;
	
	assert(nval == 1);
	nin = sqlite3_value_bytes(apval[0]);
	if (nin <= 4) 
		return;
	inbuf = sqlite3_value_blob(apval[0]);
	nout = (inbuf[0] << 24) + (inbuf[1] << 16) + (inbuf[2] << 8) + inbuf[3];
	outbuf = malloc(nout);
	nout2 = (long int) nout;
	rc = uncompress(outbuf, (unsigned long *) &nout2, &inbuf[4], nin);
	if (rc != Z_OK)
		free(outbuf);
	else
		sqlite3_result_blob(pctx, outbuf, nout2, free);
	return;
}

char *
lower(char *str)
{
	assert(str);
	int i = 0;
	char c;
	while (str[i] != '\0') {
		c = tolower((unsigned char) str[i]);
		str[i++] = c;
	}
	return str;
}

/*
* concat--
*  Utility function. Concatenates together: dst, a space character and src. 
* dst + " " + src 
*/
int
concat(char **dst, const char *src, int srclen)
{
	int total_len, dst_len;
	assert(src != NULL);
	if (srclen == -1)
		srclen = strlen(src);

	/* if destination buffer dst is NULL, then simply strdup the source buffer */
	if (*dst == NULL) {
		if ((*dst = strdup(src)) == NULL)
			return -1;
		else
			return 0;
	}
	else	
		dst_len = strlen(*dst);
	
	/* calculate total string length:
	*  one extra character for the nul byte 
	*  and one for the space character 
	*/	
	total_len = dst_len + srclen + 2;
	
	if ((*dst = (char *) realloc(*dst, total_len)) == NULL)
		return -1;
		
	/* Append a space at the end of dst */
	memcpy(*dst + dst_len, " ", 1);
	dst_len++;
	
	/* Now, copy src at the end of dst */	
	memcpy(*dst + dst_len, src, srclen + 1);
	return 0;
}

