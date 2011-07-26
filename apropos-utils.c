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
	size_t i;
	char c;
	for (i = 0; i < strlen(str); i++) {
		c = tolower((unsigned char) str[i]);
		str[i] = c;
	}
	return str;
}

/*
* concat--
*  Utility function. Concatenates together: dst, a space character and src. 
* dst + " " + src 
*/
int
concat(char **dst, const char *src)
{
	int total_len, dst_len;
	int null_status = 0;	//whether *dst is NULL or not
	if (src == NULL)
		return -1;

	/* we should allow the destination string to be NULL */
	if (*dst == NULL) {
		null_status = 1;
		dst_len = 0;
	}
	else	
		dst_len = strlen(*dst);
	
	/* calculate total string length:
	*one extra character for the nul byte 
	*/	
	total_len = dst_len + strlen(src) + 1;
	
	/* if *dst was not NULL, we need to append a space at its end. So incraese
	*  the total_len accodingly
	*/
	if (null_status == 0)
		total_len++;
		
	if ((*dst = (char *) realloc(*dst, total_len)) == NULL)
		return -1;
		
	/* if *dst was not NULL initially, we need to append a space at it's end */
	if (null_status == 0) {	
		memcpy(*dst + dst_len, " ", 1);
		dst_len++;
	}
	
	memcpy(*dst + dst_len, src, strlen(src) + 1);
	
	return 0;
}

