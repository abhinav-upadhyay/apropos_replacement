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
#include <err.h>
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

#define SEC_MAX 9
typedef struct apropos_flags {
const char *sec_nums[SEC_MAX];
int pager;
} apropos_flags;

typedef struct  {
double value;
int status;
} inverse_document_frequency;

static void rank_func(sqlite3_context *, int, sqlite3_value **);
static void remove_stopwords(char **);
static int search(const char *, apropos_flags *);
char *stemword(char *);
static void usage(void);

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

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	char ch;
	apropos_flags aflags = {{0}, 0};
		
	setprogname(argv[0]);
	if (argc < 2)
		usage();
	
	/*If the user specifies a section number as an option, the correspondingly 
	 * indexed element in sec_nums is set to the string representing that 
	 * section number.
	 */
	while ((ch = getopt(argc, argv, "123456789p")) != -1) {
	switch (ch) {
		case '1':
			aflags.sec_nums[0] = (char *)"1";
			break;
		case '2':
			aflags.sec_nums[1] = (char *)"2";
			break;
		case '3':
			aflags.sec_nums[2] = (char *)"3";
			break;
		case '4':
			aflags.sec_nums[3] = (char *)"4";
			break;
		case '5':
			aflags.sec_nums[4] = (char *)"5";
			break;
		case '6':
			aflags.sec_nums[5] = (char *)"6";
			break;
		case '7':
			aflags.sec_nums[6] = (char *)"7";
			break;
		case '8':
			aflags.sec_nums[7] = (char *)"8";
			break;
		case '9':
			aflags.sec_nums[8] = (char *)"9";
			break;
		case 'p':	//user wants to view more than 10 results and page them
			aflags.pager = 1;
			break;
		case '?':
		default:
			usage();
			break;
		}		
	}
	
	argc -= optind;
	argv += optind;		
	query = *argv;
	
	/* Eliminate any stopwords from the query */
	remove_stopwords(&query);
	
	/* if any error occured in remove_stopwords, we continue with the initial
	 *  query input by the user
	 */
	if (query == NULL)
		query = *argv;
	else if (!strcmp(query, ""))
		errx(EXIT_FAILURE, "Try specifying more relevant keywords to get some "
			"matches");

	if (search(query, &aflags) < 0)
		errx(EXIT_FAILURE, "Sorry, no relevant results could be obtained");

	return 0;
}

/*
 * search --
 *  Opens apropos.db and performs the searches for the keywords entered by the 
 *  user.
 *  The 2nd param: sec_nums indicates if the user has specified any specific 
 *  sections to search in. We build the query string accordingly.
 */
static int
search(const char *query, apropos_flags *aflags)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	char *name = NULL;
	char *section = NULL;
	char *snippet = NULL;
	char *name_desc = NULL;
	sqlite3_stmt *stmt = NULL;
	inverse_document_frequency idf = {0, 0};
	const sqlite3_tokenizer_module *stopword_tokenizer_module;
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Database does not exist. Try running makemandb and "
		"then try again");

	}
	
	sqlite3_extended_result_codes(db, 1);

	/* Register the tokenizer */
	sqlstr = (char *) "SELECT fts3_tokenizer(:tokenizer_name, :tokenizer_ptr)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_name");
	rc = sqlite3_bind_text(stmt, idx, "stopword_tokenizer", -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		exit(EXIT_FAILURE);
	}
	
	sqlite3Fts3PorterTokenizerModule((const sqlite3_tokenizer_module **)
		&stopword_tokenizer_module);
		
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_ptr");
	rc = sqlite3_bind_blob(stmt, idx, &stopword_tokenizer_module, 
		sizeof(stopword_tokenizer_module), SQLITE_STATIC);
	if (rc != SQLITE_OK) {
		sqlite3_finalize(stmt);
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_close(db);
		exit(EXIT_FAILURE);
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		warnx("%s Tokenizer error", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	sqlite3_finalize(stmt);	
	
	/* Register the rank function */
	rc = sqlite3_create_function(db, "rank_func", 1, SQLITE_ANY, (void *)&idf, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register the ranking function function");
	}
	
	/* Register the compress function: zip (apropos-utils.h) */
	rc = sqlite3_create_function(db, "zip", 1, SQLITE_ANY, NULL, 
	                             zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: compress");
	}

	/* Register the uncompress function: unzip (apropos-utils.h) */
	rc = sqlite3_create_function(db, "unzip", 1, SQLITE_ANY, NULL, 
		                         unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		errx(EXIT_FAILURE, "Not able to register function: uncompress");
	}
	
	/* Now, prepare the statement for doing the actual search query */
	int i, flag = 0;
	
	/* We want to build a query of the form: "select x,y,z from mandb where
	 * mandb match :query [AND (section LIKE '1' OR section LIKE '2' OR...)]
	 * ORDER BY rank desc..."
	 * NOTES: 1. The portion in square brackets is optional, it will be there 
	 * only if the user has specified an option on the command line to search in 
	 * one or more specific sections.
	 * 2. I am using LIKE operator because '=' or IN operators do not seem to be
	 * working with the compression option enabled.
	 */
	if (!aflags->pager)
		asprintf(&sqlstr, "SELECT section, name, name_desc, "
				"snippet(mandb, \"\033[1m\", \"\033[0m\", \"...\" ), "
				"rank_func(matchinfo(mandb, \"pclxn\")) AS rank "
				 "FROM mandb WHERE mandb MATCH :query");
	else
		/* We are using a pager, so avoid the code sequences for bold text in 
		 *	snippet.  
		 */
		asprintf(&sqlstr, "SELECT section, name, name_desc, "
				"snippet(mandb, \"\", \"\", \"...\" ), "
				"rank_func(matchinfo(mandb, \"pclxn\")) AS rank "
				 "FROM mandb WHERE mandb MATCH :query");
	
	for (i = 0; i < SEC_MAX; i++) {
		if (aflags->sec_nums[i]) {
			if (flag == 0) {
				concat(&sqlstr, "AND (section LIKE", -1);
				flag = 1;
			}
			else
				concat(&sqlstr, "OR section LIKE", -1);
			concat(&sqlstr, aflags->sec_nums[i], strlen(aflags->sec_nums[i]));
		}
	}
	if (flag)
		concat(&sqlstr, ")", 1);
	concat(&sqlstr, "ORDER BY rank DESC", -1);
	if (!aflags->pager)
		concat(&sqlstr, "LIMIT 10 OFFSET 0", -1);
		
	          
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":query");
	rc = sqlite3_bind_text(stmt, idx, query, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(EXIT_FAILURE);
	}
	
	if (!aflags->pager) {	
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			section = (char *) sqlite3_column_text(stmt, 0);
			name = (char *) sqlite3_column_text(stmt, 1);
			name_desc = (char *) sqlite3_column_text(stmt, 2);
			snippet = (char *) sqlite3_column_text(stmt, 3);
			printf("%s(%s)\t%s\n%s\n\n", name, section, name_desc, snippet);
		}
	}
	
	/* using a pager, open a pipe to more and push the output to it */
	else {
		FILE *less = popen("more", "w");
		if (less == NULL) {
			sqlite3_finalize(stmt);	
			sqlite3_close(db);
			sqlite3_shutdown();
			err(EXIT_FAILURE, "pipe failed");
		}
		
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			section = (char *) sqlite3_column_text(stmt, 0);
			name = (char *) sqlite3_column_text(stmt, 1);
			name_desc = (char *) sqlite3_column_text(stmt, 2);
			snippet = (char *) sqlite3_column_text(stmt, 3);
			fprintf(less, "%s(%s)\t%s\n%s\n\n", name, section, name_desc, 
					snippet);
		}
		pclose(less);
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
 *  to check if it is a stopword or a valid keyword. In the end we only have the
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
	const char *stopwords[] = {"a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
	 "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", 
	 "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "again", "willing", 
	 "always", "any", "around", "ask", "back", "been", "case", "did", "does", 
	 "down", "each", "early","either", "end", "enough", "even", "every", "fact"
	 , "far", "few", "four", "further", "general", "good", "got", "great", 
	 "having", "high", "him", "his", "however", "if", "important", "in", 
	 "interest", "into", "it", "just", "keep", "keeps", "kind", "knew", "know", 
	 "large", "larger", "last", "later", "latter", "latest", "least", "let", 
	 "like", "likely", "long", "longer", "made", "many", "may", "me", "might", 
	 "most", "mostly", "much", "must", "my", "necessary", "need", "never", 
	 "needs", "next", "no", "non", "noone", "not", "nothing", "number", "often", 
	 "old", "older", "once", "only", "order", "our", "out", "over", "part", 
	 "per", "perhaps", "possible", "present", "problem", "quite", "rather", 
	 "really", "right", "room", "said", "same", "saw", "say", "says", "second", 
	 "see", "seem", "seemed", "seems", "sees", "several", "shall", "should", 
	 "side", "sides", "small", "smaller", "so", "some", "something", "state",	
	 "states", "still", "such", "sure", "take", "taken", "then", "them", "their", 
	 "there", "therefore", "thing", "think", "thinks", "though", "three", "thus"
	 , "together", "too", "took", "toward", "turn", "two", "until",	"upon", "us"
	 , "use", "used", "uses", "very", "want", "wanted", "wants", "was", "way", 
	 "ways", "we", "well", "went", "were", "whether", "with", "within", "without"
	 , "work", "would", "year", "yet", "you", "about", "also", "all", "an", 
	 "another", "and", "are", "as", "at", "be", "before", "between", "below", 
	 "by", "bye", "but", "can", "consist",	"could", "follow", "from", "full", 
	 "give", "given", "have", "has", "had", "here", "how", "is", "names", "of", 
	 "off", "on", "or", "the", "this", "up", "that", "to", "new", "what", "when"
	 , "why", "will", "because", "these", "those", NULL
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
	(void)warnx("Usage: %s [-p] [-s <section-number>] query", getprogname());
	exit(1);
}

/*
 * rank_func --
 *  Sqlite user defined function for ranking the documents.
 *  For each phrase of the query, it computes the tf and idf adds them over.
 *  It computes the final rank, by multiplying tf and idf together.
 *  Weight of term t for document d = (term frequency of t in d * 
 *                                      inverse document frequency of t) 
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
