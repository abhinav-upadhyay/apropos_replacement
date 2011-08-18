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

#include <err.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

typedef struct apropos_flags {
	const char *sec_nums[SECMAX];
	int pager;
} apropos_flags;

static void remove_stopwords(char **);
static int query_callback(void *, int , char **, char **);
static void usage(void);

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	char ch;
	char *errmsg = NULL;
	const char *nrec = "10";	// The number of records to fetch from db
	const char *snippet_args[] = {"\033[1m", "\033[0m", "..."};
	FILE *out = stdout;		// the default stream for the search output
	apropos_flags aflags = {{0}, 0};
	sqlite3 *db;	
	setprogname(argv[0]);
	if (argc < 2)
		usage();
	
	/*If the user specifies a section number as an option, the corresponding 
	 * index element in sec_nums is set to the string representing that 
	 * section number.
	 */
	while ((ch = getopt(argc, argv, "123456789p")) != -1) {
	switch (ch) {
		case '1':
			aflags.sec_nums[0] = "1";
			break;
		case '2':
			aflags.sec_nums[1] = "2";
			break;
		case '3':
			aflags.sec_nums[2] = "3";
			break;
		case '4':
			aflags.sec_nums[3] = "4";
			break;
		case '5':
			aflags.sec_nums[4] = "5";
			break;
		case '6':
			aflags.sec_nums[5] = "6";
			break;
		case '7':
			aflags.sec_nums[6] = "7";
			break;
		case '8':
			aflags.sec_nums[7] = "8";
			break;
		case '9':
			aflags.sec_nums[8] = "9";
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
	if ((db = init_db(DB_READONLY)) == NULL)
		errx(EXIT_FAILURE, "The database does not exist. Please run makemandb "
			"first and then try again");

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

	/* If user wants to page the output, then set some settings */
	if (aflags.pager) {
		/* Open a pipe to the pager */
		if ((out = popen("more", "w")) == NULL) {
			close_db(db);
			err(EXIT_FAILURE, "pipe failed");
		}
		/* NULL value of nrec means fetch all matching rows */
		nrec = NULL;
	}

	query_args args;
	args.search_str = query;
	args.sec_nums = aflags.sec_nums;
	args.nrec = nrec;
	args.callback = &query_callback;
	args.callback_data = out;
	args.errmsg = &errmsg;
	if (aflags.pager) {
		if (run_query_pager(db, &args) < 0)
			errx(EXIT_FAILURE, "%s", errmsg);
	}
	else {
		if (run_query(db, snippet_args, &args) < 0)
			errx(EXIT_FAILURE, "%s", errmsg);
	}
		

	free(query);
	free(errmsg);
	if (aflags.pager)
		pclose(out);
	close_db(db);
	return 0;
}

/*
 * query_callback --
 *  Callback function for run_query.
 *  It simply outputs the results from do_query. If the user specified the -p
 *  option, then the output is sent to a pager, otherwise stdout is the default
 *  output stream.
 */
static int
query_callback(void *data, int ncol, char **col_values, char **col_names)
{
	char *name = NULL;
	char *section = NULL;
	char *snippet = NULL;
	char *name_desc = NULL;
	FILE *out = (FILE *) data;

	section =  col_values[0];
	name = col_values[1];
	name_desc = col_values[2];
	snippet = col_values[3];
	fprintf(out, "%s(%s)\t%s\n%s\n\n", name, section, name_desc, 
				snippet);
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
	const char *stopwords[] = {"a", "b", "d", "e", "f", "g", "h", "i", "j",
	 "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x", "y", 
	 "z", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", 
	 "about", "also", "all", "an", "another", "and", "are", "as", "ask", "at",
	 "again", "always", "any", "around", 
	 "back", "be", "been", "before", "between", "below", "by", "bye", "but", 
	  "because", 
	 "case", "can", "consist", "could",
	 "did", "does", "down", 
	 "each", "early","either", "end", "enough",  "even", "every", 
	 "fact", "far", "few", "four", "further", "follow", "from", "full", 
	 "general", "good", "got", "great", "give", "given", 
	 "have", "has", "had", "here", "how", "having", "high", "him", "his", 
	 "however", 
	 "if", "important", "in", "interest", "into", "is", "it", 
	 "just", 
	 "keep", "keeps", "kind", "knew", "know", 
	 "large", "larger", "last", "later", "latter", "latest", "least", "let", 
	 "like", "likely", "long", "longer", 
	 "made", "many", "may", "me", "might", "most", "mostly", "much", "must", 
	 "my", 
	 "necessary", "need", "never", "needs", "next", "no", "non", "noone", "not",
	  "nothing",  "names", "new", 
	 "often", "old", "older", "once", "only", "order", "our", "out", "over", 
	 "of", "off", "on", "or",
	 "part", "per", "perhaps", "possible", "present", "problem", 
	 "quite", 
	 "rather", "really", "right", "room", 
	 "said", "same", "saw", "say", "says", "second", "see", "seem", "seemed", 
	 "seems", "sees", "several", "shall", "should", "side", "sides", "small", 
	 "smaller", "so", "some", "something", "state",	"states", "still", "such", 
	 "sure", 
	 "take", "taken", "then", "them", "their", "there", "therefore", "thing", 
	 "think", "thinks", "though", "three", "thus", "together", "too", "took", 
	 "toward", "turn", "two", "the", "this", "up", "that", "to", "these", 
	 "those", 
	 "until", "upon", "us", "use", "used", "uses", 
	 "very", 
	 "want", "wanted", "wants", "was", "way", "ways", "we", "well", "went", 
	 "were", "whether", "with", "within", "without", "work", "would", "what", 
	 "when", "why", "will", "willing", 
	 "year", "yet", "you",  
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
		 if (hsearch(ent, FIND) == NULL)
		 	concat(&buf, temp, strlen(temp));
	}
	
	hdestroy();
	if (buf != NULL)
		*query = buf;
	else
		*query = (char *)"";
}

/*
 * usage --
 *	print usage message and die
 */
static void
usage(void)
{
	(void)warnx("Usage: %s [-p] [-123456789] query", getprogname());
	exit(1);
}
