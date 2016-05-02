/*-
 * Copyright (c) 2016 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
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

#include <stdio.h>

#ifdef __linux__

#include <bsd/stdlib.h>

#endif

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#ifdef __linux__

#include <bsd/unistd.h>

#else
#include <unistd.h>
#endif

#include "apropos-utils.h"
#include "util.h"

typedef struct inverse_document_frequency {
	double value;
	int status;
} inverse_document_frequency;


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
		0.05,	//ERRORS
		0.00,	//md5_hash
		1.00	//machine
};

static const char *section_names[] = {
		"name",
		"name_desc",
		"desc",
		"lib",
		"return_vals",
		"env",
		"files",
		"exit_status",
		"diagnostics",
		"errors",
		"md5_hash",
		"machine"
};

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
 * score_func --
 *  Sqlite user defined function for calculating section wise tf-idf score of the documents.
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
score_func(sqlite3_context *pctx, int nval, sqlite3_value **apval)
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
	double tf_values[12] = {0.0};

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
			const char *section_name = section_names[icol - 1];
			if (idf->status == 0 && ndocshitcount)
				idf->value += log(((double)ndoc / ndocshitcount))* weight;

			/* Dividing the tf by document length to normalize the effect of
			 * longer documents.
			 */
			if (nglobalhitcount > 0 && nhitcount)
				tf_values[icol - 1] += (((double)nhitcount  * weight) / (nglobalhitcount * doclen));
		}
	}
	idf->status = 1;

#define get_tfidf_score(tf) tf * idf->value/(k + tf)
	/* Final score = (tf * idf)/ ( k + tf)
	 *	Dividing by k+ tf further normalizes the weight leading to better
	 *  results.
	 *  The value of k is experimental
	 */
	char *score_json = NULL;
	easprintf(&score_json, "{\"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f,"
					  "\"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f, \"%s\": %f}",
			  section_names[0], get_tfidf_score(tf_values[0]),
			  section_names[1], get_tfidf_score(tf_values[1]),
			  section_names[2], get_tfidf_score(tf_values[2]),
			  section_names[3], get_tfidf_score(tf_values[3]),
			  section_names[4], get_tfidf_score(tf_values[4]),
			  section_names[5], get_tfidf_score(tf_values[5]),
			  section_names[6], get_tfidf_score(tf_values[6]),
			  section_names[7], get_tfidf_score(tf_values[7]),
			  section_names[8], get_tfidf_score(tf_values[8]),
			  section_names[9], get_tfidf_score(tf_values[9]),
			  section_names[10], get_tfidf_score(tf_values[10]),
			  section_names[11], get_tfidf_score(tf_values[11]),
			  "total", get_tfidf_score(tf_values[0]) + get_tfidf_score(tf_values[1])
			  + get_tfidf_score(tf_values[2]) + get_tfidf_score(tf_values[3]) +
					  get_tfidf_score(tf_values[4]) + get_tfidf_score(tf_values[5]) +
					  get_tfidf_score(tf_values[6]) + get_tfidf_score(tf_values[7]) +
					  get_tfidf_score(tf_values[8]) + get_tfidf_score(tf_values[9]) +
					  get_tfidf_score(tf_values[10]) + get_tfidf_score(tf_values[11])

	);
	sqlite3_result_text(pctx, score_json, strlen(score_json), free);
	return;
}



int
main(int argc, char **argv)
{
	char *query = argv[1];
	inverse_document_frequency idf = {0, 0};
	sqlite3 *db = init_db(MANDB_READONLY, get_dbpath(MANCONF));
	int rc = sqlite3_create_function(db, "score_func", 1, SQLITE_UTF8, (void *)&idf,
								 score_func, NULL, NULL);
	sqlite3_create_function(db, "rank_func", 1, SQLITE_UTF8, (void *)&idf,
									 rank_func, NULL, NULL);
	char *str = argv[1];
	/* Eliminate any stopwords from the query */
	query = remove_stopwords(lower(str));

	if (query == NULL)
		query = estrdup(str);
	build_boolean_query(query);
	char *sqlquery = sqlite3_mprintf("SELECT name, section, score_func(matchinfo(mandb, \"pclxn\")) AS score, rank_func(matchinfo(mandb, \"pclxn\")) as rank"
									" FROM mandb"
									" WHERE mandb MATCH %Q order by rank desc"
									,query);
	sqlite3_stmt *stmt;
	rc = sqlite3_prepare_v2(db, sqlquery, -1, &stmt, NULL);
	if (rc == SQLITE_IOERR) {
		warnx("Corrupt database. Please rerun makemandb");
		sqlite3_free(query);
		return -1;
	} else if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		free(query);
		return -1;
	}

	printf("%s", "[");
	int c = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		if (c++)
			printf(",");
		printf("{ \"name\": \"%s\", \"section\": \"%s\", \"weights\": %s}",
			   (const char *) sqlite3_column_text(stmt, 0),
			   (const char *) sqlite3_column_text(stmt, 1),
			   (const char *) sqlite3_column_text(stmt, 2));
	}
	printf("%s", "]\n");
	sqlite3_finalize(stmt);
	sqlite3_free(sqlquery);
	free(query);
	close_db(db);
}

