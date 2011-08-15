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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

static void zip(sqlite3_context *, int, sqlite3_value **);
static void unzip(sqlite3_context *, int, sqlite3_value **);
static int callback_html(void *, int, char **, char **);
static void rank_func(sqlite3_context *, int, sqlite3_value **);

typedef struct  {
double value;
int status;
} inverse_document_frequency;

/* weights for individual columns */
static const double col_weights[] = {
	2.0,	// NAME
	2.00,	// Name-description
	0.55,	// DESCRIPTION
	0.25,	// LIBRARY
	0.10,	//SYNOPSIS
	0.001,	//RETURN VALUES
	0.20,	//ENVIRONMENT
	0.01,	//FILES
	0.001,	//EXIT STATUS
	2.00,	//DIAGNOSTICS
	0.05	//ERRORS
};

static void
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


static void
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

/*
 *  do_query --
 *  Performs the searches for the keywords entered by the user.
 *  The 2nd param: snippet_args is an array of strings providing values for the
 *  last three parameters to the snippet function of sqlite. (Look at the docs).
 *  The 3rd param: args contains rest of the search parameters. Look at 
 *  arpopos-utils.h for the description of individual fields.
 *  
 */
int
do_query(sqlite3 *db, const char **snippet_args, query_args *args)
{

	char *sqlstr = NULL;
	char *temp = NULL;
	int rc;
	int i;
	int flag = 0;
	inverse_document_frequency idf = {0, 0};
	
	/* Register the rank function */
	rc = sqlite3_create_function(db, "rank_func", 1, SQLITE_ANY, (void *)&idf, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register the ranking function function");
	}
	
	/* We want to build a query of the form: "select x,y,z from mandb where
	 * mandb match :query [AND (section LIKE '1' OR section LIKE '2' OR...)]
	 * ORDER BY rank DESC..."
	 * NOTES: 1. The portion in square brackets is optional, it will be there 
	 * only if the user has specified an option on the command line to search in 
	 * one or more specific sections.
	 * 2. I am using LIKE operator because '=' or IN operators do not seem to be
	 * working with the compression option enabled.
	 */
	easprintf(&sqlstr, "SELECT section, name, name_desc, "
			"snippet(mandb, \"%s\", \"%s\", \"%s\" ), "
			"rank_func(matchinfo(mandb, \"pclxn\")) AS rank "
			 "FROM mandb WHERE mandb MATCH \'%s\'", snippet_args[0],
			 snippet_args[1], snippet_args[2], args->search_str);
	
	for (i = 0; i < SECMAX; i++) {
		if (args->sec_nums[i]) {
			if (flag == 0) {
				concat(&sqlstr, "AND (section LIKE", -1);
				flag = 1;
			}
			else
				concat(&sqlstr, "OR section LIKE", -1);
			concat(&sqlstr, args->sec_nums[i], strlen(args->sec_nums[i]));
		}
	}
	if (flag)
		concat(&sqlstr, ")", 1);
	concat(&sqlstr, "ORDER BY rank DESC", -1);
	
	/* If the user specified a value of nrec, then we need to fetch that many 
	*  number of rows
	*/
	if (args->nrec) {
		easprintf(&temp, "LIMIT %s OFFSET 0", args->nrec);
		concat(&sqlstr, temp, strlen(temp));
		free(temp);
	}
	
	/* Execute the query, and let the callback handle the output */
	sqlite3_exec(db, sqlstr, args->callback, args->callback_data, args->errmsg);
	if (*(args->errmsg) != NULL) {
		warnx("%s", *(args->errmsg));
		free(*(args->errmsg));
		free(sqlstr);
		return -1;
	}
	free(sqlstr);
	return 0;
}

/*
 * do_query_html --
 *  Utility function to output query result in HTML format.
 *  It internally calls do_query only, but it first passes the output to it's 
 *  own custom callback function, which builds a single HTML string representing
 *  one row of output.
 *  After that it delegates the call the actual user supplied callback function.
 *  #TODO One limit to this is that, the user supplied data for the callback 
 *  function would be lost.
 */
int
do_query_html(sqlite3 *db, query_args *args)
{
	void *old_callback = (void *) args->callback;
	const char *snippet_args[] = {"<b>", "</b>", "..."}; 
	args->callback = &callback_html;
	args->callback_data = old_callback;
	do_query(db, snippet_args, args);
	return 0;
}

/*
 * callback_html --
 *  Callback function for do_query_html. It builds the html output and then
 *  calls the actual user supplied callback function.
 */
static int
callback_html(void *data, int ncol, char **col_values, char **col_names)
{
	char *section =  col_values[0];
	char *name = col_values[1];
	char *name_desc = col_values[2];
	char *snippet = col_values[3];
	char *html_output = NULL;
	int (*callback) (void *, int, char **, char **) = data;
	easprintf(&html_output, "<p> <b>%s(%s)</b>\t%s <br />\n%s</p>", name, section, name_desc, 
				snippet);
	(*callback)(NULL, 1, &html_output, col_names);
	free(html_output);
	return 0;
}

/*
 * rank_func --
 *  Sqlite user defined function for ranking the documents.
 *  For each phrase of the query, it computes the tf and idf and adds them over.
 *  It computes the final rank, by multiplying tf and idf together.
 *  Weight of term t for document d = (term frequency of t in d * 
 *                                      inverse document frequency of t) 
 *
 *  Term Frequency of term t in document d = Number of times t occurs in d / 
 *	                                        Number of times t appears in all 
 *											documents
 *
 *  Inverse document frequency of t = log(Total number of documents / 
 *										Number of documents in which t occurs)
 */
static void
rank_func(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	inverse_document_frequency *idf = (inverse_document_frequency *)
										sqlite3_user_data(pctx);
	double tf = 0.0;
	unsigned int *matchinfo;
	int ncol;
	int nphrase;
	int iphrase;
	int ndoc;
	int doclen = 0;
	const double k = 3.75;
	/* Check that the number of arguments passed to this function is correct. */
	assert(nval == 1);

	matchinfo = (unsigned int *) sqlite3_value_blob(apval[0]);
	nphrase = matchinfo[0];
	ncol = matchinfo[1];
	ndoc = matchinfo[2 + 3 * ncol * nphrase + ncol];
	for (iphrase = 0; iphrase < nphrase; iphrase++) {
		int icol;
		unsigned int *phraseinfo = &matchinfo[2 + ncol+ iphrase * ncol * 3];
		for(icol = 1; icol < ncol; icol++) {
			
			/* nhitcount: number of times the current phrase occurs in the current
			 *            column in the current document.
			 * nglobalhitcount: number of times current phrase occurs in the current
			 *                  column in all documents.
			 * ndocshitcount:   number of documents in which the current phrase 
			 *                  occurs in the current column at least once.
			 */
  			int nhitcount = phraseinfo[3 * icol];
			int nglobalhitcount = phraseinfo[3 * icol + 1];
			int ndocshitcount = phraseinfo[3 * icol + 2];
			doclen = matchinfo[2 + icol ];
			double weight = col_weights[icol - 1];
			if (idf->status == 0 && ndocshitcount)
				idf->value += log(((double)ndoc / ndocshitcount))* weight;

			/* Dividing the tf by document length to normalize the effect of 
			 * longer documents.
			 */
			if (nglobalhitcount > 0 && nhitcount)
				tf += (((double)nhitcount  * weight) / (nglobalhitcount * doclen));
		}
	}
	idf->status = 1;
	
	/* Final score = (tf * idf)/ ( k + tf)
	 *	Dividing by k+ tf further normalizes the weight leading to better 
	 *  results.
	 *  The value of k is experimental
	 */
	double score = (tf * idf->value/ ( k + tf)) ;
	sqlite3_result_double(pctx, score);
	return;
}
