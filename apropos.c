#include <assert.h>
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

#define DBPATH "./apropos.db"

static int get_ndoc(void);
static void rank_func(sqlite3_context *, int, sqlite3_value **);
static void remove_stopwords(char **);
static int search(const char *);
static void usage(void);

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	
	if (argc < 2)
		usage();
	
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
	char *sqlstr = NULL;
	char *name = NULL;
	char *section = NULL;
	char *snippet = NULL;
	sqlite3_stmt *stmt = NULL;
	
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
	
	rc = sqlite3_create_function(db, "rank_func", 4, SQLITE_ANY, NULL, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(-1);
	}
	
	sqlstr = "select section, name, snippet(mandb, \"\033[1m\", \"\033[0m\", \"...\" )"
			 "from mandb where mandb match :query order by rank_func(matchinfo(mandb), 2.0, 1.50, 0.75) desc limit 10 OFFSET 0";
          
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
	
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		section = (char *) sqlite3_column_text(stmt, 0);
		name = (char *) sqlite3_column_text(stmt, 1);
		snippet = (char *) sqlite3_column_text(stmt, 2);
		printf("%s(%s)\n%s\n\n", name, section, snippet);
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
	char *temp, *buf = NULL;
	char *stopwords[] = {"a", "about", "also", "all", "an", "another", "and", "are", 
	"how", "is", "or", "the", "how", "what", "when", "which", "why", NULL};

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
		 ent.key = temp;
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
** SQLite user defined function to use with matchinfo() to calculate the
** relevancy of an FTS match. The value returned is the relevancy score
** (a real value greater than or equal to zero). A larger value indicates 
** a more relevant document.
**
** The overall relevancy returned is the sum of the relevancies of each 
** column value in the FTS table. The relevancy of a column value is the
** sum of the following for each reportable phrase in the FTS query:
	**
**   (<hit count> / <global hit count>) * <column weight>
**
** where <hit count> is the number of instances of the phrase in the
** column value of the current row and <global hit count> is the number
** of instances of the phrase in the same column of all rows in the FTS
** table. The <column weight> is a weighting factor assigned to each
** column by the caller (see below).
**
** The first argument to this function must be the return value of the FTS 
** matchinfo() function. Following this must be one argument for each column 
** of the FTS table containing a numeric weight factor for the corresponding 
** column. Example:
**
**     CREATE VIRTUAL TABLE documents USING fts3(title, content)
**
** The following query returns the docids of documents that match the full-text
** query <query> sorted from most to least relevant. When calculating
** relevance, query term instances in the 'title' column are given twice the
** weighting of those in the 'content' column.
**
**     SELECT docid FROM documents 
**     WHERE documents MATCH <query> 
**     ORDER BY rank(matchinfo(documents), 1.0, 0.5) DESC
*/
static void
rank_func(sqlite3_context *pCtx, int nVal, sqlite3_value **apVal)
{
	int *aMatchinfo;                /* Return value of matchinfo() */
	int nCol;                       /* Number of columns in the table */
	int nPhrase;                    /* Number of phrases in the query */
	int iPhrase;                    /* Current phrase */
	double tf = 0.0;          	 /* term frequency */
	double idf = 0.0;
	double score = 0.0;
	int ndoc = get_ndoc();
		  
	assert( sizeof(int)==4 && ndoc != 0  );

	/* Check that the number of arguments passed to this function is correct.
	** If not, jump to wrong_number_args. Set aMatchinfo to point to the array
	** of unsigned integer values returned by FTS function matchinfo. Set
	** nPhrase to contain the number of reportable phrases in the users full-text
	** query, and nCol to the number of columns in the table.
	*/
	if( nVal < 1 ) {
		fprintf(stderr, "nval < 1\n");
		goto wrong_number_args;
	}
	
	aMatchinfo = (unsigned int *)sqlite3_value_blob(apVal[0]);
	nPhrase = aMatchinfo[0];
	nCol = aMatchinfo[1];
	
	if( nVal != (nCol ) ) {
		fprintf(stderr, "nval != ncol\n");
		goto wrong_number_args;
	}

	/* Iterate through each phrase in the users query. */
	for(iPhrase = 0; iPhrase < nPhrase; iPhrase++){
		int iCol;                     /* Current column */

		/* Now iterate through each column in the users query. For each column,
		** increment the relevancy score by:
		**
		**   (<hit count> / <global hit count>) * <column weight>
		**
		** aPhraseinfo[] points to the start of the data for phrase iPhrase. So
		** the hit count and global hit counts for each column are found in 
		** aPhraseinfo[iCol*3] and aPhraseinfo[iCol*3+1], respectively.
		*/
		int *aPhraseinfo = &aMatchinfo[2 + iPhrase * nCol * 3];
		for(iCol = 1; iCol < nCol; iCol++) {
	  		int nHitCount = aPhraseinfo[3*iCol];
			int nGlobalHitCount = aPhraseinfo[3*iCol+1];
			double weight = sqlite3_value_double(apVal[iCol]);
			int nDocsHitCount = aPhraseinfo[3 * iCol + 2]; 
			if ( nHitCount > 0 )
				tf += ((double)nHitCount / nGlobalHitCount ) * weight;
		  
			if (nGlobalHitCount > 0)
			  	idf += log(ndoc/nDocsHitCount)/ log(ndoc);
		}

		tf /= (double) nPhrase;
		idf /= (double) nPhrase;
		score = tf * idf;
	}

	sqlite3_result_double(pCtx, score);
	return;

	/* Jump here if the wrong number of arguments are passed to this function */
	wrong_number_args:
		sqlite3_result_error(pCtx, "wrong number of arguments to function rank()", -1);
}

static int
get_ndoc(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	int ndoc = 0;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

		
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0;
	}

	sqlstr = "select count(name) from mandb";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		ndoc = (int) sqlite3_column_int(stmt, 0);
	}

	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	return ndoc;
}
