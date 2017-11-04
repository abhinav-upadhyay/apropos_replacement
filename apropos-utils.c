/*	$NetBSD: apropos-utils.c,v 1.19 2015/12/03 21:01:50 christos Exp $	*/
/*-
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * This code was developed as part of Google's Summer of Code 2011 program.
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

#include <sys/cdefs.h>
//__RCSID("$NetBSD: apropos-utils.c,v 1.19 2015/12/03 21:01:50 christos Exp $");

#include <sys/queue.h>
#include <sys/stat.h>

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#define BUFLEN 1024

#include "apropos-utils.h"
#ifndef __linux__
#include <term.h>
#undef tab	// XXX: manconf.h
#include "manconf.h"
#endif
#include "_util.h"


typedef struct orig_callback_data {
	void *data;
	int (*callback) (void *, const char *, const char *, const char *, const char *,
		const char *, size_t, unsigned int);
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
	/*0.443071898317,
	0.178932929836,
	0.262571333118,
	0.00441664505383,
	0.0183718963827,
	0.0145578875988,
	0.0254842389045,
	0.000554415729557,
	0.0166829975955,
	0.0324855298792,
	0.0,
	0.00287022758498 */ //Obtained by random forest on data.csv
    0.59104587689, // Name, obtained by random forest 15,300 on data2.csv
    0.11724895332, // name_desc
    0.219824148589, // desc
    0.00291541950947, //lib
    0.00622822136478, //return_vals
    0.00661976189148, //env
    0.0145063796903, //files
    0.000223590525925, //authors
    0.000223590525925, //history
    0.0107496100264, //diagnostics
    0.0304011695254, //errors
    0.0304011695254, //special_keywords
    0.0, //md5_hash
    0.000236868666647 //machine

};

#include "stopwords.c"
/*
 * remove_stopwords--
 *  Scans the query and removes any stop words from it.
 *  Returns the modified query or NULL, if it contained only stop words.
 */

int
is_stopword(const char *w, size_t len)
{
#ifndef __linux__
	unsigned int idx = stopwords_hash(w, len);
	if (memcmp(stopwords[idx], w, len) == 0 &&
		stopwords[idx][len] == '\0')
		return 1;
	return 0;
#else
	return (int) in_word_set(w, len);
#endif
}

char *
remove_stopwords(const char *query)
{
	size_t len;
	char *output, *buf;
	const char *sep, *next;

	output = buf = emalloc(strlen(query) + 1);

	for (; query[0] != '\0'; query = next) {
		sep = strchr(query, ' ');
		if (sep == NULL) {
			len = strlen(query);
			next = query + len;
		} else {
			len = sep - query;
			next = sep + 1;
		}
		if (len == 0)
			continue;

		if (is_stopword(query, len))
			continue;

		memcpy(buf, query, len);
		buf += len;
		*buf++ = ' ';
	}

	if (output == buf) {
		free(output);
		return NULL;
	}
	buf[-1] = '\0';
	return output;
}

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
concat(char **dst, const char *src)
{
	concat2(dst, src, strlen(src));
}

void
concat2(char **dst, const char *src, size_t srclen)
{
	size_t total_len, dst_len;
	assert(src != NULL);

	/* If destination buffer dst is NULL, then simply strdup the source buffer */
	if (*dst == NULL) {
		*dst = estrdup(src);
		return;
	}

	dst_len = strlen(*dst);
	/*
	 * NUL Byte and separator space
	 */
	total_len = dst_len + srclen + 2;

	*dst = erealloc(*dst, total_len);

	/* Append a space at the end of dst */
	(*dst)[dst_len++] = ' ';

	/* Now, copy src at the end of dst */	
	memcpy(*dst + dst_len, src, srclen + 1);
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
	char *schemasql;
	char *errmsg = NULL;
	
/*------------------------ Create the tables------------------------------*/

#if NOTYET
	sqlite3_exec(db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL);
#else
	sqlite3_exec(db, "PRAGMA journal_mode = DELETE", NULL, NULL, NULL);
#endif

	schemasql = sqlite3_mprintf("PRAGMA user_version = %d",
	    APROPOS_SCHEMA_VERSION);
	sqlite3_exec(db, schemasql, NULL, NULL, &errmsg);
	if (errmsg != NULL)
		goto out;
	sqlite3_free(schemasql);

	sqlstr = "CREATE VIRTUAL TABLE mandb USING fts4(section, name, "
			    "name_desc, desc, lib, return_vals, env, files, "
			    "authors, history, diagnostics, errors,special_keywords, md5_hash UNIQUE, machine, "
			    "tokenize=porter, compress=zip, uncompress=unzip); "	//mandb
			"CREATE TABLE IF NOT EXISTS mandb_meta(device, inode, mtime, "
			    "file UNIQUE, md5_hash UNIQUE, id  INTEGER PRIMARY KEY); "
				//mandb_meta
			"CREATE TABLE IF NOT EXISTS mandb_links(link COLLATE NOCASE, target, section, "
			    "machine, md5_hash); "	//mandb_links
			"CREATE TABLE mandb_dict(word UNIQUE, frequency); "	//mandb_dict;
            " CREATE TABLE mandb_xrs(src_name, sec_section, target_name, "
                "target_section);";


	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL)
		goto out;

	sqlstr = "CREATE INDEX IF NOT EXISTS index_mandb_links ON mandb_links "
			"(link); "
			"CREATE INDEX IF NOT EXISTS index_mandb_meta_dev ON mandb_meta "
			"(device, inode); "
			"CREATE INDEX IF NOT EXISTS index_mandb_links_md5 ON mandb_links "
			"(md5_hash);";
	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL)
		goto out;
	return 0;

out:
	warnx("%s", errmsg);
	free(errmsg);
	sqlite3_close(db);
	sqlite3_shutdown();
	return -1;
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
	sqlite3_result_text(pctx, (const char *) outbuf, stream.total_out, free);
}

/*
 * get_dbpath --
 *   Read the path of the database from man.conf and return.
 */
char *
get_dbpath(const char *manconf)
{
	char *dbpath;
#ifndef __linux__
	TAG *tp;

	config(manconf);
	tp = gettag("_mandb", 1);
	if (!tp)
		return NULL;
	
	if (TAILQ_EMPTY(&tp->entrylist))
		return NULL;

	dbpath = TAILQ_LAST(&tp->entrylist, tqh)->s;
	return dbpath;
#else
	dbpath = getenv("MAKEMANDB_DBPATH");
	if (dbpath == NULL)
		return "/var/man.db";
	else
		return dbpath;
#endif
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
init_db(mandb_access_mode db_flag, const char *dbpath)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *stmt;
	struct stat sb;
	int rc;
	int create_db_flag = 0;
	int access_mode;

	if (dbpath == NULL)
		errx(EXIT_FAILURE, "No value passed for dbpath");
	/* Check if the database exists or not */
	if (!(stat(dbpath, &sb) == 0 && S_ISREG(sb.st_mode))) {
		/* Database does not exist, check if DB_CREATE was specified, and set
		 * flag to create the database schema
		 */
		if (db_flag != (MANDB_CREATE)) {
			warnx("Missing apropos database. "
			      "Please run makemandb to create it.");
			return NULL;
		}
		create_db_flag = 1;
	} else {
		access_mode = db_flag == MANDB_CREATE || db_flag == MANDB_WRITE? R_OK | W_OK: R_OK;
		if ((access(dbpath, access_mode)) != 0) {
			warnx("Unable to access the database, please check permissions for %s", dbpath);
			return NULL;
		}
	}

	/* Now initialize the database connection */
	sqlite3_initialize();
	rc = sqlite3_open_v2(dbpath, &db, db_flag, NULL);
	
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		goto error;
	}

	if (create_db_flag && create_db(db) < 0) {
		warnx("%s", "Unable to create database schema");
		goto error;
	}

	rc = sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("Unable to query schema version: %s",
		    sqlite3_errmsg(db));
		goto error;
	}
	if (sqlite3_step(stmt) != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		warnx("Unable to query schema version: %s",
		    sqlite3_errmsg(db));
		goto error;
	}
	if (sqlite3_column_int(stmt, 0) != APROPOS_SCHEMA_VERSION) {
		sqlite3_finalize(stmt);
		warnx("Incorrect schema version found. "
		      "Please run makemandb -f.");
		goto error;
	}
	sqlite3_finalize(stmt);

	sqlite3_extended_result_codes(db, 1);
	
	/* Register the zip and unzip functions for FTS compression */
	rc = sqlite3_create_function(db, "zip", 1, SQLITE_ANY, NULL, zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		warnx("Unable to register function: compress: %s",
		    sqlite3_errmsg(db));
		goto error;
	}

	rc = sqlite3_create_function(db, "unzip", 1, SQLITE_ANY, NULL, 
                                 unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		warnx("Unable to register function: uncompress: %s",
		    sqlite3_errmsg(db));
		goto error;
	}
	return db;

error:
	close_db(db);
	return NULL;
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
edits1 (char *word, size_t *result_size)
{
	unsigned int i;
	size_t len_a;
	size_t len_b;
	unsigned int counter = 0;
	char alphabet;
	size_t n = strlen(word);
	set splits[n + 1];
	
	/* calculate number of possible permutations and allocate memory */
	size_t size = COMBINATIONS(n);
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
			char *candidate = emalloc(n);
			memcpy(candidate, splits[i].a, len_a);
			if (len_b -1 > 0)
				memcpy(candidate + len_a , splits[i].b + 1, len_b - 1);
			candidate[n - 1] =0;
			if (is_stopword(candidate, n - 1)) {
				free(candidate);
			} else {
				candidates[counter++] = candidate;
			}
		}

		/* Transposes */
		if (i < n - 1) {
			char *candidate = emalloc(n + 1);
			memcpy(candidate, splits[i].a, len_a);
			if (len_b >= 1)
				memcpy(candidate + len_a, splits[i].b + 1, 1);
			if (len_b >= 1)
				memcpy(candidate + len_a + 1, splits[i].b, 1);
			if (len_b >= 2)
				memcpy(candidate + len_a + 2, splits[i].b + 2, len_b - 2);
			candidate[n] = 0;
			if (is_stopword(candidate, n)) {
				free(candidate);
			} else {
				candidates[counter++] = candidate;
			}
		}

		/* For replaces and inserts, run a loop from 'a' to 'z' */
		for (alphabet = 'a'; alphabet <= 'z'; alphabet++) {
			/* Replaces */
			if (i < n) {
				char *candidate = emalloc(n + 1);
				memcpy(candidate, splits[i].a, len_a);
				memcpy(candidate + len_a, &alphabet, 1);
				if (len_b - 1 >= 1)
					memcpy(candidate + len_a + 1, splits[i].b + 1, len_b - 1);
				candidate[n] = 0;
				if (is_stopword(candidate, n)) {
					free(candidate);
				} else {
					candidates[counter++] = candidate;
				}
			}

			/* Inserts */
			char *candidate = emalloc(n + 2);
			memcpy(candidate, splits[i].a, len_a);
			memcpy(candidate + len_a, &alphabet, 1);
			if (len_b >=1)
				memcpy(candidate + len_a + 1, splits[i].b, len_b);
			candidate[n + 1] = 0;
			if (is_stopword(candidate, n + 1)) {
				free(candidate);
			} else {
				candidates[counter++] = candidate;
			}
		}
	}

    for (i = 0; i < n + 1; i++) {
        free(splits[i].a);
        free(splits[i].b);
    }
	*result_size = counter;
	return candidates;
}

/* Build termlist: a comma separated list of all the words in the list for
 * use in the SQL query later.
 */
static char *
build_termlist(char **list, size_t n)
{
	char *termlist = NULL;
	size_t total_len = BUFLEN * 20;	/* total bytes allocated to termlist */
	termlist = emalloc(total_len);
	int offset = 0;	/* Next byte to write at in termlist */
	int i;
	termlist[0] = '(';
	offset++;

	for (i = 0; i < n; i++) {
		size_t len_i = strlen(list[i]);
		if (total_len - offset < len_i + 3) {
			termlist = erealloc(termlist, offset + total_len);
			total_len *= 2;
		}
		memcpy(termlist + offset++, "\'", 1);
		memcpy(termlist + offset, list[i], len_i);
		offset += len_i;

		if (i == n - 1) {
			memcpy(termlist + offset++, "\'", 1);
		} else {
			memcpy(termlist + offset, "\',", 2);
			offset += 2;
		}
	}
	if (total_len - offset > 3)
		memcpy(termlist + offset, ")", 2);
	else
		concat2(&termlist, ")", 1);

	return termlist;
}

/*
 * known_word--
 *  Pass an array of strings to this function and it will return the word with 
 *  maximum frequency in the dictionary. If no word in the array list is found 
 *  in the dictionary, it returns NULL
 *  #TODO rename this function
 */
static char *
known_word(sqlite3 *db, char **list, size_t n)
{
	int rc;
	char *sqlstr;
	char *correct = NULL;
	sqlite3_stmt *stmt;
	char *termlist = build_termlist(list, n);

	easprintf(&sqlstr, "SELECT MAX(frequency), word FROM mandb_dict WHERE "
						"word IN %s", termlist);
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return NULL;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		correct = (char *) sqlite3_column_text(stmt, 1);
		if (correct) {
			correct = estrdup(correct);
		}
	}

	sqlite3_finalize(stmt);
	free(sqlstr);
	free(termlist);
	return (correct);
}

static void
free_list(char **list, size_t n)
{
    size_t i = 0;
    if (list == NULL)
        return;

    while (i < n) {
        free(list[i]);
        i++;
	}
    free(list);
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
	char *correct = NULL;
	char **candidates;
	size_t count2 = 0;
	char **cand2 = NULL;
	size_t count;
	
	lower(word);
	//correct = known_word(db, &word, 1);


	candidates = edits1(word, &count);
	correct = known_word(db, candidates, count);
	/* No matches found ? Let's go further and find matches at edit distance 2.
	 * To make the search fast we use a heuristic. Take one word at a time from 
	 * candidates, generate it's permutations and look if a match is found.
	 * If a match is found, exit the loop. Works reasonably fast but accuracy 
	 * is not quite there in some cases.
	 */
	if (correct == NULL) {
		for (i = 0; i < count; i++) {
			cand2 = edits1(candidates[i], &count2);
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
	return correct;
}

char *
get_suggestions(sqlite3 *db, char *query)
{
	char *retval = NULL;
	char *term;
	char *temp;
	char *sqlstr;
	size_t count;
	int rc;
	sqlite3_stmt *stmt;

	if ((term = strrchr(query, ' ')) == NULL) {
		term = query;
		query = NULL;
	} else {
		*term++ = 0;
	}

	char **list = edits1(term, &count);
	char *termlist = build_termlist(list, count);
	easprintf(&sqlstr, "SELECT word FROM mandb_dict "
						"WHERE word IN %s ORDER BY frequency DESC LIMIT 10", termlist);
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return NULL;
	}
	easprintf(&temp, "{\n{ query:\'%s%s%s\',\n "
			"suggestions:[", query ? query : "", query ? " " : "", term);
	concat(&retval, temp);
	free(temp);
	count = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (count++)
			concat(&retval, ",");
		easprintf(&temp, "\'%s %s\'\n", query ? query : "",sqlite3_column_text(stmt, 0));
		concat(&retval, temp);
		free(temp);
	}
	concat(&retval, "]\n}");
	sqlite3_finalize(stmt);
	free(sqlstr);
	free(termlist);
	free_list(list, count);
	return retval;
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
	inverse_document_frequency *idf = sqlite3_user_data(pctx);
	double tf = 0.0;
	const unsigned int *matchinfo;
	int ncol;
	int nphrase;
	int iphrase;
	int ndoc;
	int doclen = 0;
	const double k = 3.75;
	/* Check that the number of arguments passed to this function is correct. */
	assert(nval == 1);

	matchinfo = (const unsigned int *) sqlite3_value_blob(apval[0]);
	nphrase = matchinfo[0];
	ncol = matchinfo[1];
	ndoc = matchinfo[2 + 3 * ncol * nphrase + ncol];
	for (iphrase = 0; iphrase < nphrase; iphrase++) {
		int icol;
		const unsigned int *phraseinfo = &matchinfo[2 + ncol+ iphrase * ncol * 3];
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
static int
run_query_internal(sqlite3 *db, const char *snippet_args[3], query_args *args)
{
	const char *default_snippet_args[3];
	char *section_clause = NULL;
	char *limit_clause = NULL;
	char *machine_clause = NULL;
	char *query;
	const char *section;
	char *name;
	const char *name_desc;
	const char *machine;
	const char *snippet;
	const char *name_temp;
	char *slash_ptr;
	char *m = NULL;
	int rc;
	inverse_document_frequency idf = {0, 0};
	sqlite3_stmt *stmt;

	if (args->machine)
		easprintf(&machine_clause, "AND mandb.machine = \'%s\' ", args->machine);

	/* Register the rank function */
	rc = sqlite3_create_function(db, "rank_func", 1, SQLITE_ANY, (void *)&idf, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		warnx("Unable to register the ranking function: %s",
		    sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	/* We want to build a query of the form: "select x,y,z from mandb where
	 * mandb match :query [AND (section LIKE '1' OR section LIKE '2' OR...)]
	 * ORDER BY rank DESC..."
	 * NOTES: 1. The portion in square brackets is optional, it will be there 
	 * only if the user has specified an option on the command line to search in 
	 * one or more specific sec_nums.
	 * 2. I am using LIKE operator because '=' or IN operators do not seem to be
	 * working with the compression option enabled.
	 */

	char *sections_str = args->sec_nums;
    char *temp;
	if (sections_str) {
		while (*sections_str) {
			size_t len = strcspn(sections_str, " ");
			char *sec = sections_str;
			if (sections_str[len] == 0) {
				sections_str += len;
			} else {
				sections_str[len] = 0;
				sections_str += len + 1;
			}
			easprintf(&temp, "\'%s\',", sec);
			if (section_clause) {
				concat(&section_clause, temp);
                free(temp);
			} else {
                section_clause = temp;
			}
		}
        if (section_clause) {
            /*
             * At least one section requested, add glue for query.
             */
			size_t section_clause_len = strlen(section_clause);
			if (section_clause[section_clause_len - 1] == ',')
				section_clause[section_clause_len - 1] = 0;
            temp = section_clause;
            easprintf(&section_clause, " AND mandb.section in (%s)", temp);
            free(temp);
        }
	}


	if (args->nrec >= 0) {
		/* Use the provided number of records and offset */
		easprintf(&limit_clause, " LIMIT %d OFFSET %d",
		    args->nrec, args->offset);
	}

	if (snippet_args == NULL) {
		default_snippet_args[0] = "";
		default_snippet_args[1] = "";
		default_snippet_args[2] = "...";
		snippet_args = default_snippet_args;
	}
	if (args->legacy) {
		char *wild;
		easprintf(&wild, "%%%s%%", args->search_str);
		query = sqlite3_mprintf("SELECT section, name, name_desc, machine,"
				" snippet(mandb, %Q, %Q, %Q, -1, 40 )"
				" FROM mandb"
				" WHERE name LIKE %Q OR name_desc LIKE %Q "
				"%s"
				"%s",
				snippet_args[0], snippet_args[1], snippet_args[2],
				wild, wild,
				section_clause ? section_clause : "",
				limit_clause ? limit_clause : "");
		free(wild);
	} else if (strchr(args->search_str, ' ') == NULL) {
		/*
		 * If it's a single word query, we want to search in the
		 * links table as well. If the link table contains an entry
		 * for the queried keyword, we want to use that as the name of
		 * the man page.
		 * For example, for `apropos realloc` the output should be
		 * realloc(3) and not malloc(3).
		 */
		query = sqlite3_mprintf(
				"SELECT section, name, name_desc, machine,"
				" snippet(mandb, %Q, %Q, %Q, -1, 40 ),"
				" rank_func(matchinfo(mandb, \"pclxn\")) AS rank"
				" FROM mandb WHERE name NOT IN ("
				" SELECT target FROM mandb_links WHERE link=%Q AND"
				" mandb_links.section=mandb.section) AND mandb MATCH %Q %s %s"
				" UNION"
				" SELECT mandb.section, mandb_links.link AS name, mandb.name_desc,"
				" mandb.machine, '' AS snippet, 100.00 AS rank"
				" FROM mandb JOIN mandb_links ON mandb.name=mandb_links.target and"
				" mandb.section=mandb_links.section WHERE mandb_links.link=%Q"
				" %s %s"
				" ORDER BY rank DESC %s",
				snippet_args[0], snippet_args[1], snippet_args[2],
				args->search_str, args->search_str, section_clause ? section_clause : "",
				machine_clause ? machine_clause : "", args->search_str,
				machine_clause ? machine_clause : "",
				section_clause ? section_clause : "",
				limit_clause ? limit_clause : "");

	} else {
		query = sqlite3_mprintf("SELECT section, name, name_desc, machine,"
				" snippet(mandb, %Q, %Q, %Q, -1, 40 ),"
				" rank_func(matchinfo(mandb, \"pclxn\")) AS rank"
				" FROM mandb"
				" WHERE mandb MATCH %Q %s "
				"%s"
				" ORDER BY rank DESC"
				"%s",
				snippet_args[0], snippet_args[1], snippet_args[2],
				args->search_str, machine_clause ? machine_clause : "",
				section_clause ? section_clause : "",
				limit_clause ? limit_clause : "");
	}

	free(machine_clause);
	free(section_clause);
	free(limit_clause);

	if (query == NULL) {
		*args->errmsg = estrdup("malloc failed");
		return -1;
	}
	rc = sqlite3_prepare_v2(db, query, -1, &stmt, NULL);
	if (rc == SQLITE_IOERR) {
		warnx("Corrupt database. Please rerun makemandb");
		sqlite3_free(query);
		return -1;
	} else if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_free(query);
		return -1;
	}

	unsigned int result_index = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		section = (const char *) sqlite3_column_text(stmt, 0);
		name_temp = (const char *) sqlite3_column_text(stmt, 1);
		name_desc = (const char *) sqlite3_column_text(stmt, 2);
		machine = (const char *) sqlite3_column_text(stmt, 3);
		snippet = (const char *) sqlite3_column_text(stmt, 4);
		if ((slash_ptr = strrchr(name_temp, '/')) != NULL)
			name_temp = slash_ptr + 1;
		if (machine && machine[0]) {
			m = estrdup(machine);
			easprintf(&name, "%s/%s", lower(m),
				name_temp);
			free(m);
		} else {
			name = estrdup((const char *) sqlite3_column_text(stmt, 1));
		}

		(args->callback)(args->callback_data, args->search_str, section, name, name_desc, snippet,
			strlen(snippet), result_index++);
		free(name);
	}

	sqlite3_finalize(stmt);
	sqlite3_free(query);
	return result_index == 0 ? -1 : 0;
}

static char *
get_escaped_html_string(const char *string, size_t *string_length)
{
	/* First scan the string to find out the number of occurrences of {'>', '<'
	 * '"', '&'}.
	 * Then allocate a new buffer with sufficient space to be able to store the
	 * quoted versions of the special characters {&gt;, &lt;, &quot;, &amp;}.
	 * Copy over the characters from the original string into this buffer while
	 * replacing the special characters with their quoted versions.
	 */

	int i = 0;
	size_t sz;
	int count = 0;
	const char *temp = string;
	while (*temp) {
		sz = strcspn(temp, "<>\"&\002\003");
		temp += sz + 1;
		count++;
	}
	size_t new_string_length = *string_length + count * 5 + 1;
	*string_length = new_string_length;
	char *new_string = emalloc(new_string_length);
	sz = 0;
	while (*string) {
		sz = strcspn(string, "<>\"&\002\003");
		if (sz) {
			memcpy(&new_string[i], string, sz);
			string += sz;
			i += sz;
		}

		switch (*string++) {
			case '<':
				memcpy(&new_string[i], "&lt;", 4);
				i += 4;
				break;
			case '>':
				memcpy(&new_string[i], "&gt;", 4);
				i += 4;
				break;
			case '\"':
				memcpy(&new_string[i], "&quot;", 6);
				i += 6;
				break;
			case '&':
				/* Don't perform the quoting if this & is part of an mdoc escape
				 * sequence, e.g. \&
				 */
				if (i && *(string - 2) != '\\') {
					memcpy(&new_string[i], "&amp;", 5);
					i += 5;
				} else {
					new_string[i++] = '&';
				}
				break;
			case '\002':
				memcpy(&new_string[i], "<b>", 3);
				i += 3;
				break;
			case '\003':
				memcpy(&new_string[i], "</b>", 4);
				i += 4;
				break;
			default:
				break;
		}
	}
	new_string[i] = 0;
	return new_string;
}

/*
 * callback_html --
 *  Callback function for run_query_html. It builds the html output and then
 *  calls the actual user supplied callback function.
 */
static int
callback_html(void *data, const char *query, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length, unsigned int result_index)
{
	struct orig_callback_data *orig_data = (struct orig_callback_data *) data;
	int (*callback) (void *, const char *, const char *, const char *, const char *,
		const char *, size_t, unsigned int) = orig_data->callback;


	size_t length = snippet_length;
	size_t name_description_length = strlen(name_desc);
	char *qsnippet = get_escaped_html_string(snippet, &length);
	char *qname_description = get_escaped_html_string(name_desc, &name_description_length);
	(*callback)(orig_data->data, query, section, name, qname_description,
		(const char *)qsnippet,	length, result_index);
	free(qsnippet);
	free(qname_description);
	return 0;
}


/*
 * run_query_html --
 *  Utility function to output query result in HTML format.
 *  It internally calls run_query only, but it first passes the output to its
 *  own custom callback function, which preprocess the snippet for quoting
 *  inline HTML fragments.
 *  After that it delegates the call the actual user supplied callback function.
 */
static int
run_query_html(sqlite3 *db, query_args *args)
{
	struct orig_callback_data orig_data;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	const char *snippet_args[] = {"\002", "\003", "..."};
	args->callback = &callback_html;
	args->callback_data = (void *) &orig_data;
	return run_query_internal(db, snippet_args, args);
}

/*
 * underline a string, pager style.
 */
static char *
ul_pager(int ul, const char *s)
{
	size_t len;
	char *dst, *d;

	if (!ul)
		return estrdup(s);

	// a -> _\ba
	len = strlen(s) * 3 + 1;

	d = dst = emalloc(len);
	while (*s) {
		*d++ = '_';
		*d++ = '\b';
		*d++ = *s++;
	}
	*d = '\0';
	return dst;
}

/*
 * callback_pager --
 *  A callback similar to callback_html. It overstrikes the matching text in
 *  the snippet so that it appears emboldened when viewed using a pager like
 *  more or less.
 */
static int
callback_pager(void *data, const char * query, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length, unsigned int result_index)
{
	struct orig_callback_data *orig_data = (struct orig_callback_data *) data;
	char *psnippet;
	const char *temp = snippet;
	int count = 0;
	int i = 0, did;
	size_t sz = 0;
	size_t psnippet_length;

	/* Count the number of bytes of matching text. For each of these bytes we
	 * will use 2 extra bytes to overstrike it so that it appears bold when
	 * viewed using a pager.
	 */
	while (*temp) {
		sz = strcspn(temp, "\002\003");
		temp += sz;
		if (*temp == '\003') {
			count += 2 * (sz);
		}
		temp++;
	}

	psnippet_length = snippet_length + count;
	psnippet = emalloc(psnippet_length + 1);

	/* Copy the bytes from snippet to psnippet:
	 * 1. Copy the bytes before \002 as it is.
	 * 2. The bytes after \002 need to be overstriked till we encounter \003.
	 * 3. To overstrike a byte 'A' we need to write 'A\bA'
	 */
	did = 0;
	while (*snippet) {
		sz = strcspn(snippet, "\002");
		memcpy(&psnippet[i], snippet, sz);
		snippet += sz;
		i += sz;

		/* Don't change this. Advancing the pointer without reading the byte
		 * is causing strange behavior.
		 */
		if (*snippet == '\002')
			snippet++;
		while (*snippet && *snippet != '\003') {
			did = 1;
			psnippet[i++] = *snippet;
			psnippet[i++] = '\b';
			psnippet[i++] = *snippet++;
		}
		if (*snippet)
			snippet++;
	}

	psnippet[i] = 0;
	char *ul_section = ul_pager(did, section);
	char *ul_name = ul_pager(did, name);
	char *ul_name_desc = ul_pager(did, name_desc);
	(orig_data->callback)(orig_data->data, query, ul_section, ul_name,
	    ul_name_desc, psnippet, psnippet_length, result_index);
	free(ul_section);
	free(ul_name);
	free(ul_name_desc);
	free(psnippet);
	return 0;
}

#ifndef __linux__
struct term_args {
	struct orig_callback_data *orig_data;
	const char *smul;
	const char *rmul;
};

/*
 * underline a string, pager style.
 */
static char *
ul_term(const char *s, const struct term_args *ta)
{
	char *dst;

	easprintf(&dst, "%s%s%s", ta->smul, s, ta->rmul);
	return dst;
}

/*
 * callback_term --
 *  A callback similar to callback_html. It overstrikes the matching text in
 *  the snippet so that it appears emboldened when viewed using a pager like
 *  more or less.
 */
static int
callback_term(void *data, const char * query, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length, unsigned int result_index)
{
	struct term_args *ta = data;
	struct orig_callback_data *orig_data = ta->orig_data;

	char *ul_section = ul_term(section, ta);
	char *ul_name = ul_term(name, ta);
	char *ul_name_desc = ul_term(name_desc, ta);
	(orig_data->callback)(orig_data->data, query, ul_section, ul_name,
	    ul_name_desc, snippet, strlen(snippet), result_index);
	free(ul_section);
	free(ul_name);
	free(ul_name_desc);
	return 0;
}
#endif

/*
 * run_query_pager --
 *  Utility function similar to run_query_html. This function tries to
 *  pre-process the result assuming it will be piped to a pager.
 *  For this purpose it first calls its own callback function callback_pager
 *  which then delegates the call to the user supplied callback.
 */
static int
run_query_pager(sqlite3 *db, query_args *args)
{
	struct orig_callback_data orig_data;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	const char *snippet_args[3] = { "\002", "\003", "..." };
	args->callback = &callback_pager;
	args->callback_data = (void *) &orig_data;
	return run_query_internal(db, snippet_args, args);
}

struct nv {
	char *s;
	size_t l;
};

#ifndef __linux__
static int
term_putc(int c, void *p)
{
	struct nv *nv = p;
	nv->s[nv->l++] = c;
	return 0;
}

static char *
term_fix_seq(TERMINAL *ti, const char *seq)
{
	char *res = estrdup(seq);
	struct nv nv;

	if (ti == NULL)
	    return res;

	nv.s = res;
	nv.l = 0;
	ti_puts(ti, seq, 1, term_putc, &nv);
	nv.s[nv.l] = '\0';

	return res;
}

static void
term_init(int fd, const char *sa[5])
{
	TERMINAL *ti;
	int error;
	const char *bold, *sgr0, *smso, *rmso, *smul, *rmul;

	if (ti_setupterm(&ti, NULL, fd, &error) == -1) {
		bold = sgr0 = NULL;
		smso = rmso = smul = rmul = "";
		ti = NULL;
	} else {
		bold = ti_getstr(ti, "bold");
		sgr0 = ti_getstr(ti, "sgr0");
		if (bold == NULL || sgr0 == NULL) {
			smso = ti_getstr(ti, "smso");

			if (smso == NULL ||
			    (rmso = ti_getstr(ti, "rmso")) == NULL)
				smso = rmso = "";
			bold = sgr0 = NULL;
		} else
			smso = rmso = "";

		smul = ti_getstr(ti, "smul");
		if (smul == NULL || (rmul = ti_getstr(ti, "rmul")) == NULL)
			smul = rmul = "";
	}
	sa[0] = term_fix_seq(ti, bold ? bold : smso);
	sa[1] = term_fix_seq(ti, sgr0 ? sgr0 : rmso);
	sa[2] = estrdup("...");
	sa[3] = term_fix_seq(ti, smul);
	sa[4] = term_fix_seq(ti, rmul);

	if (ti)
		del_curterm(ti);
}

/*
 * run_query_term --
 *  Utility function similar to run_query_html. This function tries to
 *  pre-process the result assuming it will be displayed on a terminal
 *  For this purpose it first calls its own callback function callback_pager
 *  which then delegates the call to the user supplied callback.
 */
static int
run_query_term(sqlite3 *db, query_args *args)
{
	struct orig_callback_data orig_data;
	struct term_args ta;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	const char *snippet_args[5];

	term_init(STDOUT_FILENO, snippet_args);
	ta.smul = snippet_args[3];
	ta.rmul = snippet_args[4];
	ta.orig_data = (void *) &orig_data;

	args->callback = &callback_term;
	args->callback_data = &ta;
	return run_query_internal(db, snippet_args, args);
}
#endif

static int
run_query_none(sqlite3 *db, query_args *args)
{
	struct orig_callback_data orig_data;
	orig_data.callback = args->callback;
	orig_data.data = args->callback_data;
	const char *snippet_args[3] = { "", "", "..." };
	args->callback = &callback_pager;
	args->callback_data = (void *) &orig_data;
	return run_query_internal(db, snippet_args, args);
}

int
run_query(sqlite3 *db, query_format fmt, query_args *args)
{
	switch (fmt) {
	case APROPOS_NONE:
		return run_query_none(db, args);
	case APROPOS_HTML:
	case APROPOS_JSON:
		return run_query_html(db, args);
	case APROPOS_TERM:
		#ifdef __linux__
			return run_query_none(db, args);
		#else
			return run_query_term(db, args);
		#endif
	case APROPOS_PAGER:
		return run_query_pager(db, args);
	default:
		warnx("Unknown query format %d", (int)fmt);
		return -1;
	}
}

char *
build_boolean_query(char *query)
{
	char *boolop_ptr;
	char *str;
	str = query;
	while ((boolop_ptr = strstr(str, "and")) || (boolop_ptr = strstr(str, "not"))
		   || (boolop_ptr = strstr(str, "or"))) {
		switch (boolop_ptr[0]) {
			case 'a':
				boolop_ptr[0] = 'A';
				boolop_ptr[1] = 'N';
				boolop_ptr[2] = 'D';
				break;

			case 'n':
				boolop_ptr[0] = 'N';
				boolop_ptr[1] = 'O';
				boolop_ptr[2] = 'T';
				break;
			case 'o':
				boolop_ptr[0] = 'O';
				boolop_ptr[1] = 'R';
				break;
		}
		str = boolop_ptr + 1;
	}
	return query;
}
