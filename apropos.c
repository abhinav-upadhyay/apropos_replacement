#include <search.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

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
	char *snippet = NULL;
	sqlite3_stmt *stmt = NULL;
	
	sqlite3_initialize();
	rc = sqlite3_open_v2("apropos.db", &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Database does not exist. Try running makemandb and "
		"then try again\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_extended_result_codes(db, 1);
	
	sqlstr = "select name, snippet(mandb, \"\033[1m\", \"\033[0m\", \"...\" )"
			 "from mandb where mandb match :query limit 10 OFFSET 0";
          
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
		name = (char *) sqlite3_column_text(stmt, 0);
		snippet = (char *) sqlite3_column_text(stmt, 1);
		printf("%s\n%s\n\n", name, snippet);
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
	char *stopwords[] = {"a", "about", "also", "an", "another", "and", "are", 
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
