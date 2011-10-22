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

typedef struct orig_callback_data {
	void *data;
	int (*callback) (void *, int, char **, char **);
} orig_callback_data;

typedef struct inverse_document_frequency {
	double value;
	int status;
} inverse_document_frequency;

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
				"uncompress=unzip, tokenize=porter); "	//mandb table
			"CREATE TABLE IF NOT EXISTS mandb_meta(device, inode, mtime, file UNIQUE, "
				"md5_hash UNIQUE, id  INTEGER PRIMARY KEY); "	//mandb_meta
			"CREATE TABLE IF NOT EXISTS mandb_links(link, target, section, "
				"machine); ";	//mandb_links

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
	int nin;
	long int nout;
	const unsigned char * inbuf;
	unsigned char *outbuf;

	assert(nval == 1);
	nin = sqlite3_value_bytes(apval[0]);
	inbuf = (const unsigned char *) sqlite3_value_blob(apval[0]);
	nout = nin + 13 + (nin + 999) / 1000;
	outbuf = emalloc(nout);
	compress(outbuf, (unsigned long *) &nout, inbuf, nin);
	sqlite3_result_blob(pctx, outbuf, nout, free);
}

/*
 * unzip --
 *  User defined Sqlite function to uncompress the FTS table.
 */
static void
unzip(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	unsigned int rc;
	unsigned char *outbuf;
	z_stream stream;

	assert(nval == 1);
	stream.next_in = __UNCONST(sqlite3_value_blob(apval[0]));
	stream.avail_in = sqlite3_value_bytes(apval[0]);
	stream.avail_out = stream.avail_in * 2 + 100;
	stream.next_out = outbuf = emalloc(stream.avail_out);
	stream.zalloc = NULL;
	stream.zfree = NULL;
	inflateInit(&stream);

	if (inflateInit(&stream) != Z_OK) {
		free(outbuf);
		return;
	}

	while ((rc = inflate(&stream, Z_SYNC_FLUSH)) != Z_STREAM_END) {
		if (rc != Z_OK ||
		    (stream.avail_out != 0 && stream.avail_in == 0)) {
			free(outbuf);
			return;
		}
		outbuf = erealloc(outbuf, stream.total_out * 2);
		stream.next_out = outbuf + stream.total_out;
		stream.avail_out = stream.total_out;
	}
	if (inflateEnd(&stream) != Z_OK) {
		free(outbuf);
		return;
	}
	outbuf = erealloc(outbuf, stream.total_out);
	sqlite3_result_blob(pctx, outbuf, stream.total_out, free);
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
