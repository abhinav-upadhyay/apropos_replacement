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
#include "sqlite3.h"

typedef struct apropos_flags {
	int sec_nums[SECMAX];
	int nresults;
	int pager;
} apropos_flags;

typedef struct callback_data {
	int count;
	FILE *out;
} callback_data;

static void remove_stopwords(char **);
static int query_callback(void *, const char * , const char *, const char *,
	const char *, size_t);
__dead static void usage(void);

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	char ch;
	char *errmsg = NULL;
	int rc = 0;
	callback_data cbdata;
	const char *snippet_args[] = {"\033[1m", "\033[0m", "..."};
	cbdata.out = stdout;		// the default output stream
	cbdata.count = 0;
	apropos_flags aflags;
	sqlite3 *db;
	setprogname(argv[0]);
	if (argc < 2)
		usage();

	memset(&aflags, 0, sizeof(aflags));
	
	/*If the user specifies a section number as an option, the corresponding 
	 * index element in sec_nums is set to the string representing that 
	 * section number.
	 */
	while ((ch = getopt(argc, argv, "123456789n:p")) != -1) {
		switch (ch) {
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			aflags.sec_nums[atoi(&ch) - 1] = 1;
			break;
		case 'n':
			aflags.nresults = atoi(optarg);
			break;
		case 'p':	//user wants to view more than 10 results and page them
			aflags.pager = 1;
			aflags.nresults = -1;	// Fetch all records
			break;
		case '?':
		default:
			usage();
			break;
		}		
	}
	
	argc -= optind;
	argv += optind;
	
	if (!argc)
		usage();

	query = lower(*argv);
	if ((db = init_db(MANDB_READONLY)) == NULL)
		errx(EXIT_FAILURE, "The database does not exist. Please run makemandb "
			"first and then try again");

	/* Eliminate any stopwords from the query */
	remove_stopwords(&query);
	
	/* if any error occured in remove_stopwords, we continue with the initial
	 *  query input by the user
	 */
	if (query == NULL) {
		query = lower(*argv);
	} else if (!strcmp(query, "")) {
		errx(EXIT_FAILURE, "Try specifying more relevant keywords to get some "
			"matches");
	}

	/* If user wants to page the output, then set some settings */
	if (aflags.pager) {
		/* Open a pipe to the pager */
		if ((cbdata.out = popen("more", "w")) == NULL) {
			close_db(db);
			err(EXIT_FAILURE, "pipe failed");
		}
	}

	query_args args;
	args.search_str = query;
	args.sec_nums = aflags.sec_nums;
	args.nrec = aflags.nresults ? aflags.nresults : 10;
	args.offset = 0;
	args.callback = &query_callback;
	args.callback_data = &cbdata;
	args.errmsg = &errmsg;

	if (aflags.pager) {
		rc = run_query_pager(db, &args);
		pclose(cbdata.out);
	}
	else {
		rc = run_query(db, snippet_args, &args);
	}

	free(query);
	close_db(db);
	if (errmsg) {
		warnx("%s", errmsg);
		free(errmsg);
		exit(EXIT_FAILURE);
	}

	if (rc < 0) {
		/* Something wrong with the database. Exit */
		exit(EXIT_FAILURE);
	}
	
	if (cbdata.count == 0) {
		warnx("No relevant results obtained.\n"
			  "Please make sure that you spelled all the terms correctly\n"
			  "Or try using better keywords.");
	}
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
query_callback(void *data, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length)
{
	callback_data *cbdata = (callback_data *) data;
	FILE *out = cbdata->out;
	cbdata->count++;
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
	 "general", "get", "good", "got", "great", "give", "given", 
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
	fprintf(stderr,
		"Usage: %s [-n Number of records] [-p] [-123456789] query\n",
		getprogname());
	exit(1);
}
