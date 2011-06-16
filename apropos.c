#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sqlite3.h"

//#TODO implement a function for removing stopwords 
//static void remove_stopwords(char *);
static int search(const char *);
static void usage(void);

int
main(int argc, char *argv[])
{
	char *query = NULL;	// the user query
	
	if (argc < 2)
		usage();
	
	query = argv[1];
	//remove_stopwords(query);
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
