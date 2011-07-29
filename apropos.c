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
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

static void rank_func(sqlite3_context *, int, sqlite3_value **);
static void remove_stopwords(char **);
static int search(const char *);
char *stemword(char *);
static void usage(void);

typedef struct  {
double value;
int status;
} inverse_document_frequency;

static inverse_document_frequency idf;

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	
	if (argc < 2)
		usage();
	
	idf.value = 0.0;
	idf.status = 0;
	
	query = argv[1];
	remove_stopwords(&query);
	
	/* if any error occured in remove_stopwords, we continue with the initial
	*  query input by the user
	*/
	if (query == NULL)
		query = argv[1];
	else if (!strcmp(query, "")) {
		fprintf(stderr, "Try specifying more relevant keywords to get some matches\n");
		exit(1);
	}
		
	if (search(query) < 0) 
		return -1;
	return 0;
		
}

/*
* search --
*  Opens apropos.db and performs the search for the keywords entered by the user
*/
static int
search(const char *query)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	const char *sqlstr = NULL;
	char *name = NULL;
	char *section = NULL;
	char *snippet = NULL;
	sqlite3_stmt *stmt = NULL;
	const sqlite3_tokenizer_module *stopword_tokenizer_module;
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Database does not exist. Try running makemandb and "
		"then try again\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_extended_result_codes(db, 1);
	/* Register the tokenizer */
	sqlstr = "select fts3_tokenizer(:tokenizer_name, :tokenizer_ptr)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_name");
	rc = sqlite3_bind_text(stmt, idx, "stopword_tokenizer", -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	sqlite3Fts3PorterTokenizerModule((const sqlite3_tokenizer_module **)&stopword_tokenizer_module);
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_ptr");
	rc = sqlite3_bind_blob(stmt, idx, &stopword_tokenizer_module, sizeof(stopword_tokenizer_module), SQLITE_STATIC);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "%s tokenizer error\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	sqlite3_finalize(stmt);	
	
	/* Register the rank function */
	rc = sqlite3_create_function(db, "rank_func", 1, SQLITE_ANY, NULL, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(-1);
	}
	
	/* Register the compress function: zip (apropos-utils.h) */
	rc = sqlite3_create_function(db, "zip", 1, SQLITE_ANY, NULL, 
	                             zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function: compress\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

	/* Register the uncompress function: unizp (apropos-utils.h) */
	rc = sqlite3_create_function(db, "unzip", 1, SQLITE_ANY, NULL, 
		                         unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function: uncompress\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	/* Now, prepare the statement for doing the actual search query */
	sqlstr = "select section, name, snippet(mandb, \"\033[1m\", \"\033[0m\", \"...\" ), rank_func(matchinfo(mandb, \"pcxln\")) as rank "
			 "from mandb where mandb match :query order by rank desc limit 10 OFFSET 0";
          
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":query");
	rc = sqlite3_bind_text(stmt, idx, query, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":query");
	rc = sqlite3_bind_text(stmt, idx, query, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		section = (char *) sqlite3_column_text(stmt, 0);
		name = (char *) sqlite3_column_text(stmt, 1);
		snippet = (char *) sqlite3_column_text(stmt, 2);
		char *rank = (char *) sqlite3_column_text(stmt, 3);
 		printf("%s(%s)\t%s\n%s\n\n", name, section, rank, snippet);
	}
			
	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	
	return 0;
	
}
/*
* remove_stopwords--
*  Scans the query and removes any stop words from it.
*  It scans the query word by word, and looks up a hash table of stop words
*  to check if it is a stopword or a valid keyword. In the we only have the
*  relevant keywords left in the query.
*  Error Cases: 
*   1. In case of any error, it will set the query to NULL.	
*   2. In case the query is found to be consisting only of stop words, it will
*      set the query to a blank string ""
*/
static void
remove_stopwords(char **query)
{
	int i = 0;
	const char *temp;
	char *buf = NULL;
	const char *stopwords[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k",
	 "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", "z", 
	 "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "again", "willing", 
	 "always", "any", "around", "ask", "back", "been", "case", "did", "does", 
	 "down", "each", "early","either", "end", "enough", "even", "every", "fact",
	 "far", "few", "four", "further", "general", "good", "got", "great", "having",
	 "high", "him", "his", "however", "if", "important", "in", "interest", "into",
	 "it", "just", "keep", "keeps", "kind", "knew", "know", "large", "larger", 
	 "last", "later", "latter", "latest", "least", "let", "like", "likely", 
	 "long", "longer", "made", "many", "may", "me",	"might", "most", "mostly", 
	 "much", "must", "my", "necessary", "need", "never", "needs", "next", "no",
	"non", "noone", "not", "nothing", "number", "often", "old", "older", "once",
	"only", "order", "our", "out", "over", "part", "per", "perhaps", "possible", 
	"present", "problem", "quite", "rather", "really", "right", "room", "said", 
	"same", "saw", "say", "says", "second", "see", "seem", "seemed", "seems",
	"sees", "several", "shall", "should", "side", "sides", "small", "smaller", 
	"so", "some", "something", "state",	"states", "still", "such", "sure", "take", 
	"taken", "then", "them", "their", "there", "therefore", "thing", "think", 
	"thinks", "though", "three", "thus", "together", "too", "took", "toward", 
	"turn", "two", "until",	"upon", "us", "use", "used", "uses", "very", "want", 
	"wanted", "wants", "was", "way", "ways", "we", "well", "went", "were", 
	"whether", "with", "within", "without", "work", "would", "year", "yet", "you",
	"about", "also", "all", "an", "another", "and", "are", "as", "at", "be", 
	"before", "between", "below", "by", "bye", "but", "can", "consist",	"could", 
	"follow", "from", "full", "give", "given", "have", "has", "had", "here", 
	"how", "is", "names", "of", "off", "on", "or", "the", "this", "up",	"that", 
	"to", "new", "what", "when", "why", "will", "because", "these", "those",  
	NULL
	};
	
	/* initialize the hash table for stop words */
	if (!hcreate(sizeof(stopwords) * sizeof(char)))
		return;
	
	/* store the stopwords in the hashtable */	
	for (temp = stopwords[i]; temp != NULL; temp = stopwords[i++]) {
		ENTRY ent;
		ent.key = strdup(temp);
		ent.data = (void *) "y";
		hsearch(ent, ENTER);
	}
	
	/* filter out the stop words from the query */
	for (temp = strtok(*query, " "); temp; temp = strtok(NULL, " ")) {
		 ENTRY ent;
		 ent.key = (char *)temp;
		 ent.data = NULL;
		 if (hsearch(ent, FIND) == NULL) {
		 	if (buf == NULL) {
		 		if ((buf = strdup(temp)) == NULL) {
		 			*query = NULL;
		 			hdestroy();
		 			return;
	 			}
 			}
		 	else {
		 		if ((buf = realloc(buf, strlen(buf) + strlen(temp) + 2)) == NULL) {
		 			*query = NULL;
		 			hdestroy();
		 			return;
	 			}
		 		strcat(buf, " ");
		 		strcat(buf, temp);
	 		}
		 	
 		}
	}
	
	hdestroy();
	if (buf != NULL)
		*query = strdup(buf);
	else
		*query = strdup("");
	free(buf);
}

/*
 * usage --
 *	print usage message and die
 */
static void
usage(void)
{

	(void)fprintf(stderr,
	    "usage: %s query\n", getprogname());
	exit(1);
}

/*
* rank_func
*  Sqlite user defined function for ranking the documents.
*  For each phrase of the query, it computes the tf and idf adds them over.
*  It computes the final rank, by multiplying tf and idf together.
*  Weight of term t for document d = term frequency of t in d * inverse document frequency of t
*
*  Term Frequency of term t in document d = Number of times t occurs in d / 
*                                        Number of times t appears in all documents
*
*  Inverse document frequenct of t = log(Total number of documents / 
*										Number of documents in which t occurs)
*/
static void
rank_func(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	double tf = 0.0;
	double col_weights[] = {
	2.0,	// NAME
	1.5,	// Name-description
	0.25,	// DESCRIPTION
	0.75,	// LIBRARY
	0.75,	//SYNOPSIS
	0.50,	//RETURN VALUES
	0.75,	//ENVIRONMENT
	0.40,	//FILES
	0.50,	//EXIT STATUS
	1.00,	//DIAGNOSTICS
	0.75	//ERRORS
	};
	unsigned int *matchinfo;
	int ncol;
	int nphrase;
	int iphrase;
	int ndoc;
	int doclen = 0;
	/* Check that the number of arguments passed to this function is correct.
	** If not, jump to wrong_number_args. 
	*/
	if( nval != 1 ) {
		fprintf(stderr, "nval != ncol\n");
		goto wrong_number_args;
	}
	
	matchinfo = (unsigned int *) sqlite3_value_blob(apval[0]);
	nphrase = matchinfo[0];
	ncol = matchinfo[1];
	ndoc = matchinfo[2 + 3 * ncol * nphrase + ncol];
	for (iphrase = 0; iphrase < nphrase; iphrase++) {
		int icol;
		unsigned int *phraseinfo = &matchinfo[2 + iphrase * ncol * 3];
		for(icol = 1; icol < ncol; icol++) {
  			int nhitcount = phraseinfo[3 * icol];
			int nglobalhitcount = phraseinfo[3 * icol + 1];
			int ndocshitcount = phraseinfo[3 * icol + 2];
			doclen += matchinfo[2 + 3 * ncol * nphrase + icol];
			double weight = col_weights[icol - 1];
			if (idf.status == 0 && ndocshitcount)
				idf.value += log(((double)ndoc  / ndocshitcount)) / log(ndoc);
	
			if (nglobalhitcount > 0 && nhitcount)
				tf += ((double)nhitcount / nglobalhitcount) * weight;
		}
	}
	idf.status = 1;
	double score = (tf * idf.value / doclen );
	sqlite3_result_double(pctx, score);
	return;

	/* Jump here if the wrong number of arguments are passed to this function */
	wrong_number_args:
		sqlite3_result_error(pctx, "wrong number of arguments to function rank()", -1);
}
