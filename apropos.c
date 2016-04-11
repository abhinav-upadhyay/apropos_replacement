/*	$NetBSD: apropos.c,v 1.17 2015/12/20 19:45:29 christos Exp $	*/
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
//__RCSID("$NetBSD: apropos.c,v 1.17 2015/12/20 19:45:29 christos Exp $");

#ifdef __linux__
#include <bsd/stdlib.h>
#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
	#include <bsd/unistd.h>
#else
	#include <unistd.h>
#endif

#include "apropos-utils.h"
#include "util.h"

typedef struct apropos_flags {
	int sec_nums[SECMAX];
	int nresults;
	int pager;
	int no_context;
	query_format format;
	int legacy;
	const char *machine;
} apropos_flags;

typedef struct callback_data {
	int count;
	FILE *out;
	apropos_flags *aflags;
} callback_data;

static char *const end_table_tags = "</table>\n</body>\n</html>\n";

static char *const html_table_start = "<html>\n<header>\n<title>apropos results "
						"for %s</title></header>\n<body>\n<table cellpadding=\"4\""
						"style=\"border: 1px solid #000000; border-collapse:"
						"collapse;\" border=\"1\">\n";

static int query_callback(void *, const char *, const char * , const char *, const char *,
						  const char *, size_t, unsigned int);
__dead static void usage(void);

char *get_correct_query(const char *query, sqlite3 *db, char **correct_query);

#define _PATH_PAGER	"/usr/bin/more -s"

static void
parseargs(int argc, char **argv, struct apropos_flags *aflags)
{
	int ch;
	while ((ch = getopt(argc, argv, "123456789Cchijln:PprS:s:")) != -1) {
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
			aflags->sec_nums[ch - '1'] = 1;
			break;
		case 'C':
			aflags->no_context = 1;
			break;
		case 'c':
			aflags->no_context = 0;
			break;
		case 'h':
			aflags->format = APROPOS_HTML;
			break;
		case 'i':
			aflags->format = APROPOS_TERM;
			break;
		case 'j':
			aflags->format = APROPOS_JSON;
			break;
		case 'l':
			aflags->legacy = 1;
			aflags->no_context = 1;
			aflags->format = APROPOS_NONE;
			break;
		case 'n':
			aflags->nresults = atoi(optarg);
			break;
		case 'p':	// user wants a pager
			aflags->pager = 1;
			/*FALLTHROUGH*/
		case 'P':
			aflags->format = APROPOS_PAGER;
			break;
		case 'r':
			aflags->format = APROPOS_NONE;
			break;
		case 'S':
			aflags->machine = optarg;
			break;
		case 's':
			ch = atoi(optarg);
			if (ch < 1 || ch > 9)
				errx(EXIT_FAILURE, "Invalid section");
			aflags->sec_nums[ch - 1] = 1;
			break;
		case '?':
		default:
			usage();
		}
	}
}

int
main(int argc, char *argv[])
{
	query_args args;
	char *query = NULL;	// the user query
	char *errmsg = NULL;
	char *str;
	int rc = 0;
	int s;
	callback_data cbdata;
	cbdata.out = stdout;		// the default output stream
	cbdata.count = 0;
	apropos_flags aflags;
	cbdata.aflags = &aflags;
	sqlite3 *db;
	char * correct_query;
	setprogname(argv[0]);
	if (argc < 2)
		usage();

	memset(&aflags, 0, sizeof(aflags));

	if (!isatty(STDOUT_FILENO))
		aflags.format = APROPOS_NONE;
	else
		aflags.format = APROPOS_TERM;

	if ((str = getenv("APROPOS")) != NULL) {
		char **ptr = emalloc((strlen(str) + 2) * sizeof(*ptr));
#define WS "\t\n\r "
		ptr[0] = __UNCONST(getprogname());
		for (s = 1, str = strtok(str, WS); str;
		    str = strtok(NULL, WS), s++)
			ptr[s] = str;
		ptr[s] = NULL;
		parseargs(s, ptr, &aflags);
		free(ptr);
		optreset = 1;
		optind = 1;
	}

	parseargs(argc, argv, &aflags);

	/*
	 * If the user specifies a section number as an option, the
	 * corresponding index element in sec_nums is set to the string
	 * representing that section number.
	 */
	
	argc -= optind;
	argv += optind;
	
	if (!argc)
		usage();

	str = NULL;
	while (argc--)
		concat(&str, *argv++);
	/* Eliminate any stopwords from the query */
	query = remove_stopwords(lower(str));

	/*
	 * If the query consisted only of stopwords and we removed all of
	 * them, use the original query.
	 */
	if (query == NULL)
		query = str;
	else
		free(str);

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
	args.legacy = aflags.legacy;
	args.nrec = aflags.nresults ? aflags.nresults : -1;
	args.offset = 0;
	args.machine = aflags.machine;
	args.callback = &query_callback;
	args.callback_data = &cbdata;
	args.errmsg = &errmsg;


	if (aflags.format == APROPOS_HTML)
		fprintf(cbdata.out, html_table_start, query);

	if (aflags.format == APROPOS_JSON)
		fprintf(cbdata.out, "{\"results\": [");

	rc = run_query(db, aflags.format, &args);
	if (cbdata.count < 10) {
		correct_query = get_correct_query(query, db, &correct_query);
		if (strcmp(correct_query, query) == 0) {
			if (cbdata.count == 0) {
				if (aflags.format == APROPOS_HTML) {
					fprintf(cbdata.out,
							"<tr><td> No relevant results obtained.<br/> Please try using better keywords</tr></td>");
				} else if (aflags.format == APROPOS_JSON) {
					fprintf(cbdata.out, "],\"error\": {\"message\": \"no results found\", \"category\": \"bad_query\"}}");
				} else {
					warnx("No relevant results obtained\n"
								  "Please try using better keywords");
				}
			}
			free(correct_query);
			goto error;
		} else {
			if (aflags.format == APROPOS_JSON) {
				//TODO new format "error: {"message": "asndsa", "category": "adasd"
				fprintf(cbdata.out,
						"],\"error\": {\"message\": \"no results found\", \"category\": \"spell\", \"suggestion\": \"%s\"}}",
						correct_query);
			} else if (aflags.format == APROPOS_HTML) {
				fprintf(cbdata.out, "<tr><td> Did you mean %s?</td></tr>\n", correct_query);
				fprintf(cbdata.out, "%s", end_table_tags);
			} else
				warnx("Did you mean %s?", correct_query);
			free(correct_query);
			goto error;
		}
	}

	if (aflags.format == APROPOS_HTML)
		fprintf(cbdata.out, "%s", end_table_tags);
	if (aflags.format == APROPOS_JSON)
		fprintf(cbdata.out, "]}");

	error:
	free(query);
	close_db(db);
	if (errmsg) {
		warnx("%s", errmsg);
		free(errmsg);
		exit(EXIT_FAILURE);
	}
	return rc;
}

char *get_correct_query(const char *query, sqlite3 *db, char **correct_query) {
	(*correct_query) = NULL;
	char *correct;
	char *term;
	char *dupq = estrdup(query);
	for (term = strtok(dupq, " "); term; term = strtok(NULL, " ")) {
			if ((correct = spell(db, term)))
				concat(correct_query, correct);
			else
				concat(correct_query, term);
            if (correct)
                free(correct);
	}
	free(dupq);
	return (*correct_query);
}

/*
 * query_callback --
 *  Callback function for run_query.
 *  It simply outputs the results from do_query. If the user specified the -p
 *  option, then the output is sent to a pager, otherwise stdout is the default
 *  output stream.
 */
static int
query_callback(void *data, const char *query, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length, unsigned int result_index)
{
	callback_data *cbdata = (callback_data *) data;
	FILE *out = cbdata->out;
	cbdata->count++;
	switch (cbdata->aflags->format) {
		case APROPOS_NONE:
		case APROPOS_PAGER:
		case APROPOS_TERM:
			fprintf(out, cbdata->aflags->legacy ? "%s(%s) - %s\n" :
						 "%s (%s)\t%s\n", name, section, name_desc);

			if (cbdata->aflags->no_context == 0)
				fprintf(out, "%s\n\n", snippet);
			break;
		case APROPOS_HTML:

			fprintf(out, "<tr><td>%s(%s)</td><td>%s</td></tr>\n", name,
					section, name_desc);
			if (cbdata->aflags->no_context == 0)
				fprintf(out, "<tr><td colspan=2>%s</td></tr>\n", snippet);
			break;
		case APROPOS_JSON:
			/**
			 * {
			 *  "results: [
			 *		{"name": "ls", "section": "1", "short_description": "foo", "snippet": "bar"},
			 *		{"name": "cp", "section": "1", "short_description": "foo", "snippet": "bar"},
			 *		{"name": "cp", "section": "1", "short_description": "foo", "snippet": "bar"}
			 *   ]
			 *  }
			 */
			if (result_index > 0)
				fprintf(out, ",");
			fprintf(out, "{\"name\": \"%s\", \"section\": \"%s\", \"description\": \"%s\"", name,
					section, name_desc);
			if (cbdata->aflags->no_context == 0)
				fprintf(out, ", \"snippet\": \"%s\"", snippet);
			fprintf(out, "}\n");
	}
	return 0;
}

/*
 * usage --
 *	print usage message and die
 */
static void
usage(void)
{
	fprintf(stderr, "Usage: %s [-123456789Ccilpr] [-n results] "
	    "[-S machine] [-s section] query\n",
		getprogname());
	exit(1);
}
