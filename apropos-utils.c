/*-
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * This code was developed as part of Google's Summer of Code 2011 program.
 * Thanks to Google for sponsoring.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <vis.h>

#include "apropos-utils.h"
#include "sqlite3.h"

#define BUFLEN 1024

typedef struct orig_callback_data {
	void *data;
	int (*callback) (void *, int, char **, char **);
} orig_callback_data;

typedef struct inverse_document_frequency {
	double value;
	int status;
} inverse_document_frequency;

typedef struct set {
	char *a;
	char *b;
} set;

/* weights for individual columns */
static const double col_weights[] = {
	2.0,	// NAME
	2.00,	// Name-description
	0.55,	// DESCRIPTION
	0.10,	// LIBRARY
	0.001,	//RETURN VALUES
	0.20,	//ENVIRONMENT
	0.01,	//FILES
	0.001,	//EXIT STATUS
	2.00,	//DIAGNOSTICS
	0.05	//ERRORS
};

/*
 * lower --
 *  Converts the string str to lower case
 */
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

void
close_db(sqlite3 *db)
{
	sqlite3_close(db);
	sqlite3_shutdown();
}

/*
 * create_db --
 *  Creates the database schema.
 */
static int
create_db(sqlite3 *db)
{
	const char *sqlstr = NULL;
	char *errmsg = NULL;
	
/*------------------------ Create the tables------------------------------*/

	sqlstr = "CREATE VIRTUAL TABLE mandb USING fts4(section, name, "
				"name_desc, desc, lib, return_vals, env, files, "
				"exit_status, diagnostics, errors, compress=zip, "
				"uncompress=unzip, tokenize=simple); "	//mandb table
			"CREATE TABLE IF NOT EXISTS mandb_meta(device, inode, mtime, file UNIQUE, "
				"md5_hash UNIQUE, id  INTEGER PRIMARY KEY); "	//mandb_meta
			"CREATE TABLE IF NOT EXISTS mandb_links(link, target, section, "
				"machine); "	//mandb_links
			"CREATE VIRTUAL TABLE mandb_aux USING fts4aux(mandb)";

	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

	sqlstr = "CREATE INDEX IF NOT EXISTS index_mandb_links ON mandb_links "
			"(link); "
			"CREATE INDEX IF NOT EXISTS index_mandb_meta_dev ON mandb_meta "
			"(device, inode)";
	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	return 0;
}

/*
 * zip --
 *  User defined Sqlite function to compress the FTS table
 */
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

/*
 * unzip --
 *  User defined Sqlite function to uncompress the FTS table.
 */
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

/* init_db --
 *   Prepare the database. Register the compress/uncompress functions and the
 *   stopword tokenizer.
 *	 db_flag specifies the mode in which to open the database. 3 options are 
 *   available:
 *   	1. DB_READONLY: Open in READONLY mode. An error if db does not exist.
 *  	2. DB_READWRITE: Open in read-write mode. An error if db does not exist.
 *  	3. DB_CREATE: Open in read-write mode. It will try to create the db if
 *			it does not exist already.
 *  RETURN VALUES:
 *		The function will return NULL in case the db does not exist and DB_CREATE 
 *  	was not specified. And in case DB_CREATE was specified and yet NULL is 
 *  	returned, then there was some other error.
 *  	In normal cases the function should return a handle to the db.
 */
sqlite3 *
init_db(int db_flag)
{
	sqlite3 *db = NULL;
	struct stat sb;
	int rc;
	int create_db_flag = 0;

	/* Check if the databse exists or not */
	if (!(stat(DBPATH, &sb) == 0 && S_ISREG(sb.st_mode))) {
		/* Database does not exist, check if DB_CREATE was specified, and set
		 * flag to create the database schema
		 */
		if (db_flag == (MANDB_CREATE))
			create_db_flag = 1;
		else
		/* db does not exist and DB_CREATE was also not specified, return NULL */
			return NULL;
	}

	/* Now initialize the database connection */
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, db_flag, NULL);
	
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_shutdown();
		return NULL;
	}
	
	sqlite3_extended_result_codes(db, 1);
	
	/* Register the zip and unzip functions for FTS compression */
	rc = sqlite3_create_function(db, "zip", 1, SQLITE_ANY, NULL, 
                             zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: compress");
	}

	rc = sqlite3_create_function(db, "unzip", 1, SQLITE_ANY, NULL, 
		                         unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: uncompress");
	}
	
	if (create_db_flag) {
		if (create_db(db) < 0) {
			warnx("%s", "Could not create database schema");
			sqlite3_close(db);
			sqlite3_shutdown();
			return NULL;
		}
	}
	return db;
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

/*
 *  run_query --
 *  Performs the searches for the keywords entered by the user.
 *  The 2nd param: snippet_args is an array of strings providing values for the
 *  last three parameters to the snippet function of sqlite. (Look at the docs).
 *  The 3rd param: args contains rest of the search parameters. Look at 
 *  arpopos-utils.h for the description of individual fields.
 *  
 */
int
run_query(sqlite3 *db, const char *snippet_args[3], query_args *args)
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
	if (snippet_args)
		easprintf(&sqlstr, "SELECT section, name, name_desc, "
				"snippet(mandb, \"%s\", \"%s\", \"%s\", -1, 40 ), "
				"rank_func(matchinfo(mandb, \"pclxn\")) AS rank "
				 "FROM mandb WHERE mandb MATCH \'%s\'", snippet_args[0],
				 snippet_args[1], snippet_args[2], args->search_str);
	else
		easprintf(&sqlstr, "SELECT section, name, name_desc, "
				"snippet(mandb, \"%s\", \"%s\", \"%s\", -1, 40 ), "
				"rank_func(matchinfo(mandb, \"pclxn\")) AS rank "
				 "FROM mandb WHERE mandb MATCH \'%s\'", "",
				 "", "...", args->search_str);

	if (args->sec_nums) {
		for (i = 0; i < SECMAX; i++) {
			if (args->sec_nums[i]) {
				if (flag == 0) {
					easprintf(&temp, "AND (section LIKE \'%d\'", i + 1);
					concat(&sqlstr, temp, -1);
					free(temp);
					flag = 1;
				}
				else {
					easprintf(&temp, "OR SECTION LIKE \'%d\'", i + 1);
					concat(&sqlstr, temp, -1);
					free(temp);
				}
			}
		}
		if (flag)
			concat(&sqlstr, ")", 1);
	}
	concat(&sqlstr, "ORDER BY rank DESC", -1);
	fprintf(stderr, "%s\n", sqlstr);
	/* If the user specified a value of nrec, then we need to fetch that many 
	*  number of rows
	*/
	easprintf(&temp, "LIMIT %d OFFSET %d", args->nrec, args->offset);
	concat(&sqlstr, temp, strlen(temp));
	free(temp);
	
	/* Execute the query, and let the callback handle the output */
	sqlite3_exec(db, sqlstr, args->callback, args->callback_data, args->errmsg);
	if (*(args->errmsg) != NULL) {
		free(sqlstr);
		return -1;
	}
	free(sqlstr);
	return 0;
}

/*
 * callback_html --
 *  Callback function for run_query_html. It builds the html output and then
 *  calls the actual user supplied callback function.
 */
static int
callback_html(void *data, int ncol, char **col_values, char **col_names)
{
	char *html_col_values[ncol + 1];
	char *html_col_names[ncol + 1];
	int i;
	const char *tab = "&nbsp;&nbsp;&nbsp;&nbsp;";
	char *section =  col_values[0];
	char *name = col_values[1];
	char *name_desc = col_values[2];
	char *snippet = col_values[3];
	char *buf = NULL;
	char *html_output = NULL;
	struct orig_callback_data *orig_data = (struct orig_callback_data *) data;
	int (*callback) (void *, int, char **, char **) = orig_data->callback;
	
	easprintf(&buf, "<p> <b>%s(%s)</b>%s %s %s <br />\n%s</p>", name, section, tab, tab,
				name_desc, snippet);
	html_output = emalloc(strlen(buf) * 4 + 1);
	strvis(html_output, buf, VIS_CSTYLE);

	for (i = 0; i < ncol; i++) {
		html_col_names[i] = col_names[i];
		html_col_values[i] = col_values[i];
	}
	html_col_names[ncol] = (char *) "html_result";
	html_col_values[ncol] = html_output;
	ncol++;	
	(*callback)(orig_data->data, ncol, html_col_values, html_col_names);
	free(buf);
	free(html_output);
	return 0;
}

/*
 * run_query_html --
 *  Utility function to output query result in HTML format.
 *  It internally calls do_query only, but it first passes the output to it's 
 *  own custom callback function, which builds a single HTML string representing
 *  one row of output.
 *  After that it delegates the call the actual user supplied callback function.
 *  #TODO One limit to this is that, the user supplied data for the callback 
 *  function would be lost.
 */
int
run_query_html(sqlite3 *db, query_args *args)
{
	struct orig_callback_data orig_data;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	const char *snippet_args[] = {"<b>", "</b>", "..."}; 
	args->callback = &callback_html;
	args->callback_data = (void *) &orig_data;
	run_query(db, snippet_args, args);
	return 0;
}

/*
 * pager_highlight --
 *  Builds a string in a form so that a pager like more or less may display it in
 *  bold.
 *  This function is required by run_query_pager, which calls it to replace the
 *  snippet returned from Sqlite.
 *  Caller should free the string returned from this function.
 */
static char *
pager_highlight(char *str)
{
	char *temp = NULL;
	char *buf = NULL;
	ENTRY ent, *result;
	/* Process the string word by word and if the word is found in the hash 
	 * table, then replace that word with it's bold text representation obtained
	 * from the table.
	 */
	for (temp = strtok(str, " "); temp; temp = strtok(NULL, " ")) {
		 ent.key = (char *)temp;
		 ent.data = NULL;
		 if ((result = hsearch(ent, FIND)) == NULL)
		 	concat(&buf, temp, strlen(temp));
		 else
		 	concat(&buf, (char *)result->data, -1);
	}
	return buf;
}

/*
 * callback_pager --
 *  A callback similar to callback_html. Difference being it formats the snippet
 *  so that the pager should be able to dispaly the matching bits of the snippet
 *  in bold and then calls the actual callback function specified by the user.
 *  It passes the snippet to pager_highlight which returns a new string which is
 *  suitable to be passed to a pager.
 */
static int
callback_pager(void *data, int ncol, char **col_values, char **col_names)
{
	struct orig_callback_data *orig_data = (struct orig_callback_data *) data;
	char *snippet = pager_highlight(col_values[3]);
	col_values[3] = snippet;
	int (*callback) (void *, int, char **, char **) = orig_data->callback;
	(*callback)(orig_data->data, ncol, col_values, col_names);
	free(snippet);
	return 0;
}

/*
 * build_cat_string --
 *  Builds a string which is in a form so that a pager may display it in bold.
 *  Basic aim is: If the input string is "A", then output should be: "A\bA"
 */
static char *
build_cat_string(char *str)
{
	int i, j;
	char *catstr = emalloc(3 * strlen(str) + 1);
	for (i = 0, j = 0; str[i] != '\0'; i++, j+=3) {
		catstr[j] = str[i];
		catstr[j + 1] = '\b';
		catstr[ j + 2] = str[i];
	}
	catstr[j] = 0;
	return catstr;
}

/*
 * run_query_pager --
 *  Utility function similar to run_query_html. This function tries to process
 *  the result assuming it will be piped to a pager.
 *  It's basic aim is to pre-process the snippet returned from the result-set
 *  in a form so that the pager may be able to highlight the matching bits of 
 *  the text.
 *  We use a hashtable to store each word of the user query as a key and it's
 *  bold text representation as it's value for faster lookup when building the
 *  snippet in pager_highlight.
 */
int run_query_pager(sqlite3 *db, query_args *args)
{
	char *temp = NULL;
	struct orig_callback_data orig_data;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	char *query = strdup(args->search_str);
	/* initialize the hash table for stop words */
	if (!hcreate(10))
		return -1;

	/* store the query words in the hashtable as key and their equivalent
	 * bold text representation as their values.
	 */	
	for (temp = strtok(query, " "); temp; temp = strtok(NULL, " ")) {
		ENTRY ent;
		ent.key = strdup(temp);
		ent.data = (void *) build_cat_string(temp);
		hsearch(ent, ENTER);
	}
	free(query);
	args->callback = &callback_pager;
	args->callback_data = (void *) &orig_data;
	run_query(db, NULL, args);
	hdestroy();
	return 0;

}

/*
 * Following is an implmentation of a spell corrector based on Peter Norvig's 
 * article: <http://norvig.com/spell-correct.html>. This C implementation is 
 * written completely by me from scratch.
 */

/*
 * edits1--
 *  edits1 generates all permutations of a given word at maximum edit distance 
 *  of 1. All details are in the above article but basically it generates 4 
 *  types of possible permutations in a given word, stores them in an array and 
 *  at the end returns that array to the caller. The 4 different permutations 
 *  are: (n = strlen(word) in the following description)
 *  1. Deletes: Delete one character at a time: n possible permutations
 *  2. Trasnposes: Change positions of two adjacent characters: n -1 permutations
 *  3. Replaces: Replace each character by one of the 26 alphabetes in English:
 *      26 * n possible permutations
 *  4. Inserts: Insert an alphabet at each of the character positions (one at a
 *      time. 26 * (n + 1) possible permutations.
 */
static char **
edits1 (char *word)
{
	int i;
	int len_a;
	int len_b;
	int counter = 0;
	char alphabet;
	int n = strlen(word);
	set splits[n + 1];
	
	/* calculate number of possible permutations and allocate memory */
	size_t size = n + n -1 + 26 * n + 26 * (n + 1);
	char **candidates = emalloc (size * sizeof(char *));

	/* Start by generating a split up of the characters in the word */
	for (i = 0; i < n + 1; i++) {
		splits[i].a = (char *) emalloc(i + 1);
		splits[i].b = (char *) emalloc(n - i + 1);
		memcpy(splits[i].a, word, i);
		memcpy(splits[i].b, word + i, n - i + 1);
		splits[i].a[i] = 0;
	}

	/* Now generate all the permutations at maximum edit distance of 1.
	 * counter keeps track of the current index position in the array candidates
	 * where the next permutation needs to be stored.
	 */
	for (i = 0; i < n + 1; i++) {
		len_a = strlen(splits[i].a);
		len_b = strlen(splits[i].b);
		assert(len_a + len_b == n);

		/* Deletes */
		if (i < n) {
			candidates[counter] = emalloc(n);
			memcpy(candidates[counter], splits[i].a, len_a);
			if (len_b -1 > 0)
				memcpy(candidates[counter] + len_a , splits[i].b + 1, len_b - 1);
			candidates[counter][n - 1] =0;
			counter++;
		}

		/* Transposes */
		if (i < n - 1) {
			candidates[counter] = emalloc(n + 1);
			memcpy(candidates[counter], splits[i].a, len_a);
			if (len_b >= 1)
				memcpy(candidates[counter] + len_a, splits[i].b + 1, 1);
			if (len_b >= 1)
				memcpy(candidates[counter] + len_a + 1, splits[i].b, 1);
			if (len_b >= 2)
				memcpy(candidates[counter] + len_a + 2, splits[i].b + 2, len_b - 2);
			candidates[counter][n] = 0;
			counter++;
		}

		/* For replaces and inserts, run a loop from 'a' to 'z' */
		for (alphabet = 'a'; alphabet <= 'z'; alphabet++) {
			/* Replaces */
			if (i < n) {
				candidates[counter] = emalloc(n + 1);
				memcpy(candidates[counter], splits[i].a, len_a);
				memcpy(candidates[counter] + len_a, &alphabet, 1);
				if (len_b - 1 >= 1)
					memcpy(candidates[counter] + len_a + 1, splits[i].b + 1, len_b - 1);
				candidates[counter][n] = 0;
				counter++;
			}

			/* Inserts */
			candidates[counter] = emalloc(n + 2);
			memcpy(candidates[counter], splits[i].a, len_a);
			memcpy(candidates[counter] + len_a, &alphabet, 1);
			if (len_b >=1)
				memcpy(candidates[counter] + len_a + 1, splits[i].b, len_b);
			candidates[counter][n + 1] = 0;
			counter++;
		}
	}
	return candidates;
}

/*
 * known_word--
 *  Pass an array of strings to this function and it will return the word with 
 *  maximum frequency in the dictionary. If no word in the array list is found 
 *  in the dictionary, it returns NULL
 *  #TODO rename this function
 */
static char *
known_word(sqlite3 *db, char **list, int n)
{
	int i, rc;
	char *sqlstr;
	char *termlist = NULL;
	char *correct = NULL;
	sqlite3_stmt *stmt;

	/* Build termlist: a comma separated list of all the words in the list for 
	 * use in the SQL query later.
	 */
	int total_len = BUFLEN * 20;	/* total bytes allocated to termlist */
	termlist = emalloc (total_len);
	int offset = 0;	/* Next byte to write at in termlist */
	termlist[0] = '(';
	offset++;

	for (i = 0; i < n; i++) {
		int d = strlen(list[i]);
		if (total_len - offset < d + 3) {
			termlist = erealloc(termlist, offset + total_len);
			total_len *= 2;
		}
		memcpy(termlist + offset, "\'", 1);
		offset++;
		memcpy(termlist + offset, list[i], d);
		offset += d;

		if (i == n -1) {
			memcpy(termlist + offset, "\'", 1);
			offset++;
		}
		else {
			memcpy(termlist + offset, "\',", 2);
			offset += 2;
		}

	}
	if (total_len - offset > 3)
		memcpy(termlist + offset, ")", 2);
	else
		concat(&termlist, ")", 1);

	easprintf(&sqlstr, "SELECT term FROM metadb.dict WHERE "
						"occurrences = (SELECT MAX(occurrences) from metadb.dict "
						"WHERE term IN %s) AND term in %s", termlist, termlist);
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return NULL;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW)
			correct = strdup((char *) sqlite3_column_text(stmt, 0));

	sqlite3_finalize(stmt);
	free(sqlstr);
	free(termlist);
	return (correct);
}

static void
free_list(char **list, int n)
{
	int i = 0;
	if (list == NULL)
		return;

	while (i < n) {
		free(list[i]);
		i++;
	}
}

/*
 * spell--
 *  The API exposed to the user. Returns the most closely matched word from the 
 *  dictionary. It will first search for all possible words at distance 1, if no
 *  matches are found, it goes further and tries to look for words at edit 
 *  distance 2 as well. If no matches are found at all, it returns NULL.
 */
char *
spell(sqlite3 *db, char *word)
{
	int i;
	char *correct;
	char **candidates;
	int count2;
	char **cand2 = NULL;
	char *errmsg;
	const char *sqlstr;
	int n;
	int count;
	sqlite3_exec(db, "ATTACH DATABASE \':memory:\' AS metadb", NULL, NULL, 
				&errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		close_db(db);
		exit(EXIT_FAILURE);
	}
	
	sqlstr = "CREATE TABLE metadb.dict AS SELECT term, occurrences FROM "
			"mandb_aux WHERE col=\'*\' ;"
			"CREATE UNIQUE INDEX IF NOT EXISTS metadb.index_term ON "
				"dict (term)";

	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		return NULL;
	}
	
	lower(word);
	correct = known_word(db, &word, 1);
	
	if (!correct) {
		n = strlen(word);
		count = n + n -1 + 26 * n + 26 * (n + 1);
		candidates = edits1(word);
		correct = known_word(db, candidates, count);
		/* No matches found ? Let's go further and find matches at edit distance 2.
		 * To make the search fast we use a heuristic. Take one word at a time from 
		 * candidates, generate it's permutations and look if a match is found.
		 * If a match is found, exit the loop. Works reasonable fast but accuracy 
		 * is not quite there in some cases.
		 */
		if (correct == NULL) {	
			for (i = 0; i < count; i++) {
				n = strlen(candidates[i]);
				count2 = n + n - 1 + 26 * n + 26 * (n + 1);
				cand2 = edits1(candidates[i]);
				if ((correct = known_word(db, cand2, count2)))
					break;
				else {
					free_list(cand2, count2);
					cand2 = NULL;
				}
			}
		}
		free_list(candidates, count);
		free_list(cand2, count2);
	}

	sqlite3_exec(db, "DETACH DATABASE metadb", NULL, NULL, 
				&errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
	}
	return correct;
}
