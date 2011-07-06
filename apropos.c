#include <assert.h>
#include <math.h>
#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

#define DBPATH "./apropos.db"

static double get_weight(int, const char *);
static double get_idf(const char *);
static void rank_func(sqlite3_context *, int, sqlite3_value **);
static void remove_stopwords(char **);
static int search(const char *);
char *stemword(char *);
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
	
	rc = sqlite3_create_function(db, "rank_func", 2, SQLITE_ANY, NULL, 
	                             rank_func, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		exit(-1);
	}
	
	sqlstr = "select docid as d, section, name, snippet(mandb, \"\033[1m\", \"\033[0m\", \"...\" ), rank_func(docid, :query) as rank "
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
		section = (char *) sqlite3_column_text(stmt, 1);
		name = (char *) sqlite3_column_text(stmt, 2);
		snippet = (char *) sqlite3_column_text(stmt, 3);
		char *rank = (char *) sqlite3_column_text(stmt, 4);
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
	char *temp, *buf = NULL;
	char *stopwords[] = {"a", "about", "also", "all", "an", "another", "and", "are", "be",
	"how", "is", "new", "or", "the", "to", "how", "what", "when", "which", "why", NULL};
	
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
* rank_func
*  Sqlite user defined function for ranking the documents.
*  For each phrase of the query, it fetches the tf and idf from the db and adds them over.
*  It computes the final rank, by multiplying tf and idf together.
*/
static void
rank_func(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	int docid;
	char *query, *temp, *term;
	double weight = 0.0;
	int i =0;
	
	/* Check that the number of arguments passed to this function is correct.
	** If not, jump to wrong_number_args. 
	*/
	if( nval != 2 ) {
		fprintf(stderr, "nval != ncol\n");
		goto wrong_number_args;
	}
	
	docid = (int) sqlite3_value_int(apval[0]);
 	query = strdup((char *) sqlite3_value_text(apval[1]));
	
	for (temp = strtok(query, " "); temp; temp = strtok(NULL, " ")) {
		term = stemword(temp);
		weight += get_weight(docid, term);
		i++;
		free(term);
	}
	
	weight /= i;

	sqlite3_result_double(pctx, weight);
	free(query);
	return;

	/* Jump here if the wrong number of arguments are passed to this function */
	wrong_number_args:
		sqlite3_result_error(pctx, "wrong number of arguments to function rank()", -1);
}

static double
get_weight(int docid, const char *term)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	double ret_val = 0.0;
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0.0;
	}

	sqlite3_extended_result_codes(db, 1);
	sqlstr = "select weight / (select SUM(weight) from mandb_weights where docid = :docid1) "
			"from mandb_weights where docid = :docid2 and term = :term";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0.0;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":docid1");
	rc = sqlite3_bind_int(stmt, idx, docid);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0.0;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":docid2");
	rc = sqlite3_bind_int(stmt, idx, docid);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0.0;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":term");
	rc = sqlite3_bind_text(stmt, idx, term, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return 0.0;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		ret_val = (double) sqlite3_column_double(stmt, 0);
	}
	
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	sqlite3_shutdown();
	return ret_val;
}
