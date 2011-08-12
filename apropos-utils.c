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
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

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
void
concat(char **dst, const char *src, int srclen)
{
	int total_len, dst_len;
	assert(src != NULL);
	if (srclen == -1)
		srclen = strlen(src);

	/* if destination buffer dst is NULL, then simply strdup the source buffer */
	if (*dst == NULL) {
		*dst = estrdup(src);
		return;
	}
	else	
		dst_len = strlen(*dst);
	
	/* calculate total string length:
	*  one extra character for the nul byte 
	*  and one for the space character 
	*/	
	total_len = dst_len + srclen + 2;
	
	*dst = (char *) erealloc(*dst, total_len);
		
	/* Append a space at the end of dst */
	memcpy(*dst + dst_len, " ", 1);
	dst_len++;
	
	/* Now, copy src at the end of dst */	
	memcpy(*dst + dst_len, src, srclen + 1);
	return;
}

/* init --
 *   Prepare the database. Register the compress/uncompress functions and the
 *   stopword tokenizer.
 *	 The create_flag tells us whether the caller wants us to create the database
 *	 in case it doesn't exist or not. In case the caller doesn't want to create
 *	 the database (a non-zero value), we return. 
 *	 A return value of 1 to the caller indicates that the database does not exist
 */
int
init(sqlite3 **db, int create_flag)
{

	struct stat sb;
	const sqlite3_tokenizer_module *stopword_tokenizer_module;
	const char *sqlstr;
	int rc;
	int idx;
	int return_val = 0;
	sqlite3_stmt *stmt = NULL;

	/* If the db file does not already exists, set the return_val to 1 */
	if (!(stat(DBPATH, &sb) == 0 && S_ISREG(sb.st_mode)))
		return_val = 1;
	/* A zero value of create_flag means that caller does not want to proceed
	 * in case database file did not exist, so return.
	 */
	if (!create_flag && return_val)
		return return_val;
		
	/* Now initialize the database connection */
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, db, SQLITE_OPEN_READWRITE | 
		             SQLITE_OPEN_CREATE, NULL);
	
	if (rc != SQLITE_OK) {
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Could not open database");
	}
	
	sqlite3_extended_result_codes(*db, 1);
	
	/* Register the zip and unzip functions for FTS compression */
	rc = sqlite3_create_function(*db, "zip", 1, SQLITE_ANY, NULL, 
                             zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(*db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: compress");
	}

	rc = sqlite3_create_function(*db, "unzip", 1, SQLITE_ANY, NULL, 
		                         unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(*db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: uncompress");
	}
	
	/* Register the stopword tokenizer */
	sqlstr = "select fts3_tokenizer(:tokenizer_name, :tokenizer_ptr)";
	rc = sqlite3_prepare_v2(*db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(*db));
		sqlite3_close(*db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_name");
	rc = sqlite3_bind_text(stmt, idx, "stopword_tokenizer", -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(*db));
		sqlite3_finalize(stmt);
		sqlite3_close(*db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	sqlite3Fts3PorterTokenizerModule((const sqlite3_tokenizer_module **) 
		&stopword_tokenizer_module);

	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_ptr");
	rc = sqlite3_bind_blob(stmt, idx, &stopword_tokenizer_module, 
		sizeof(stopword_tokenizer_module), SQLITE_STATIC);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(*db));
		sqlite3_finalize(stmt);
		sqlite3_close(*db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		warnx("%s", sqlite3_errmsg(*db));
		sqlite3_finalize(stmt);
		sqlite3_close(*db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	sqlite3_finalize(stmt);
	return return_val;
}
