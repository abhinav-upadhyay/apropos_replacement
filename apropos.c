/*	$NetBSD: apropos.c,v 1.8 2012/10/06 15:33:59 wiz Exp $	*/
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
__RCSID("$NetBSD: apropos.c,v 1.8 2012/10/06 15:33:59 wiz Exp $");

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
	int no_context;
	const char *machine;
} apropos_flags;

typedef struct callback_data {
	int count;
	FILE *out;
	apropos_flags *aflags;
} callback_data;

static int query_callback(void *, const char * , const char *, const char *,
	const char *, size_t);
__dead static void usage(void);

#define _PATH_PAGER	"/usr/bin/more -s"

int
main(int argc, char *argv[])
{
#ifdef NOTYET
	static const char *snippet_args[] = {"\033[1m", "\033[0m", "..."};
#endif
	query_args args;
	char *query = NULL;	// the user query
	char *errmsg = NULL;
	char *str;
	int ch, rc = 0;
	char *correct_query;
	char *correct;
	int s;
	callback_data cbdata;
	cbdata.out = stdout;		// the default output stream
	cbdata.count = 0;
	apropos_flags aflags;
	cbdata.aflags = &aflags;
	sqlite3 *db;
	setprogname(argv[0]);
	if (argc < 2)
		usage();

	memset(&aflags, 0, sizeof(aflags));
	
	/*If the user specifies a section number as an option, the corresponding 
	 * index element in sec_nums is set to the string representing that 
	 * section number.
	 */
	while ((ch = getopt(argc, argv, "123456789Ccn:pS:s:")) != -1) {
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
			aflags.sec_nums[ch - '1'] = 1;
			break;
		case 'C':
			aflags.no_context = 1;
			break;
		case 'c':
			aflags.no_context = 0;
			break;
		case 'n':
			aflags.nresults = atoi(optarg);
			break;
		case 'p':	//user wants to view more than 10 results and page them
			aflags.pager = 1;
			aflags.nresults = -1;	// Fetch all records
			break;
		case 'S':
			aflags.machine = optarg;
			break;
		case 's':
			s = atoi(optarg);
			if (s < 1 || s > 9)
				errx(EXIT_FAILURE, "Invalid section");
			aflags.sec_nums[s - 1] = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
	
	argc -= optind;
	argv += optind;
	
	if (!argc)
		usage();

	str = NULL;
	while (argc--)
		concat(&str, *argv++);
	/* Eliminate any stopwords from the query */
	query = remove_stopwords(lower(str));
	free(str);

	/* if any error occured in remove_stopwords, exit */
	if (query == NULL)
		errx(EXIT_FAILURE, "Try using more relevant keywords");

	build_boolean_query(query);
	if ((db = init_db(MANDB_READONLY, MANCONF)) == NULL)
		exit(EXIT_FAILURE);

	/* If user wants to page the output, then set some settings */
	if (aflags.pager) {
		const char *pager = getenv("PAGER");
		if (pager == NULL)
			pager = _PATH_PAGER;
		/* Open a pipe to the pager */
		if ((cbdata.out = popen(pager, "w")) == NULL) {
			close_db(db);
			err(EXIT_FAILURE, "pipe failed");
		}
	}

	args.search_str = query;
	args.sec_nums = aflags.sec_nums;
	args.nrec = aflags.nresults ? aflags.nresults : 10;
	args.offset = 0;
	args.machine = aflags.machine;
	args.callback = &query_callback;
	args.callback_data = &cbdata;
	args.errmsg = &errmsg;

#ifdef NOTYET
	rc = run_query(db, snippet_args, &args);
#else
	rc = run_query_pager(db, &args);
#endif

	if (errmsg || rc < 0) {
		warnx("%s", errmsg);
		free(errmsg);
		free(query);
		close_db(db);
		exit(EXIT_FAILURE);
	}

	char *orig_query =  query;
	char *term;
	if (cbdata.count == 0) {
		correct_query = NULL;
		for (term = strtok(query, " "); term; term = strtok(NULL, " ")) {
			if ((correct = spell(db, term)))
				concat(&correct_query, correct);
			else
				concat(&correct_query, term);
		}

		printf("Did you mean %s ?\n", correct_query);
/*		warnx("No relevant results obtained.\n"
			  "Please make sure that you spelled all the terms correctly "
			  "or try using better keywords.");*/
		free(correct_query);
	}
	free(orig_query);
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
query_callback(void *data, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length)
{
	callback_data *cbdata = (callback_data *) data;
	FILE *out = cbdata->out;
	cbdata->count++;
	fprintf(out, "%s (%s)\t%s\n", name, section, name_desc);

	if (cbdata->aflags->no_context == 0)
		fprintf(out, "%s\n\n", snippet);

	return 0;
}

/*
 * usage --
 *	print usage message and die
 */
static void
usage(void)
{
	fprintf(stderr,
		"Usage: %s [-n Number of records] [-123456789Ccp] [-S machine] query\n",
		getprogname());
	exit(1);
}
