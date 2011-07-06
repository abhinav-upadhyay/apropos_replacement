#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mdoc.h"
#include "sqlite3.h"

#define MAXLINE 1024	//buffer size for fgets
#define DBPATH "./apropos.db"

static int build_term_weights(void);
static int check_md5(const char *);
static void cleanup(void);
static int concat(char **, const char *);
static int create_db(void);
static void get_section(const struct mdoc *);
static int insert_into_db(void);
static	void pmdoc(const char *);
static void pmdoc_node(const struct mdoc_node *);
static void pmdoc_Nm(const struct mdoc_node *);
static void pmdoc_Nd(const struct mdoc_node *);
static void pmdoc_Sh(const struct mdoc_node *);
static void traversedir(const char *);
static void get_tf(sqlite3_context *, int, sqlite3_value **);
static void get_idf(sqlite3_context *, int, sqlite3_value **);

static char *name = NULL;	// for storing the name of the man page
static char *name_desc = NULL; // for storing the one line description (.Nd)
static char *desc = NULL; // for storing the DESCRIPTION section
static char *md5_hash = NULL;
static char *section = NULL;
static struct mparse *mp = NULL;



typedef	void (*pmdoc_nf)(const struct mdoc_node *n);
static	const pmdoc_nf mdocs[MDOC_MAX] = {
	NULL, /* Ap */
	NULL, /* Dd */
	NULL, /* Dt */
	NULL, /* Os */
	pmdoc_Sh, /* Sh */ 
	NULL, /* Ss */ 
	NULL, /* Pp */ 
	NULL, /* D1 */
	NULL, /* Dl */
	NULL, /* Bd */
	NULL, /* Ed */
	NULL, /* Bl */ 
	NULL, /* El */
	NULL, /* It */
	NULL, /* Ad */ 
	NULL, /* An */ 
	NULL, /* Ar */
	NULL, /* Cd */ 
	NULL, /* Cm */
	NULL, /* Dv */ 
	NULL, /* Er */ 
	NULL, /* Ev */ 
	NULL, /* Ex */ 
	NULL, /* Fa */ 
	NULL, /* Fd */
	NULL, /* Fl */
	NULL, /* Fn */ 
	NULL, /* Ft */ 
	NULL, /* Ic */ 
	NULL, /* In */ 
	NULL, /* Li */
	pmdoc_Nd, /* Nd */
	pmdoc_Nm, /* Nm */
	NULL, /* Op */
	NULL, /* Ot */
	NULL, /* Pa */
	NULL, /* Rv */
	NULL, /* St */ 
	NULL, /* Va */
	NULL, /* Vt */ 
	NULL, /* Xr */ 
	NULL, /* %A */
	NULL, /* %B */
	NULL, /* %D */
	NULL, /* %I */
	NULL, /* %J */
	NULL, /* %N */
	NULL, /* %O */
	NULL, /* %P */
	NULL, /* %R */
	NULL, /* %T */
	NULL, /* %V */
	NULL, /* Ac */
	NULL, /* Ao */
	NULL, /* Aq */
	NULL, /* At */ 
	NULL, /* Bc */
	NULL, /* Bf */
	NULL, /* Bo */
	NULL, /* Bq */
	NULL, /* Bsx */
	NULL, /* Bx */
	NULL, /* Db */
	NULL, /* Dc */
	NULL, /* Do */
	NULL, /* Dq */
	NULL, /* Ec */
	NULL, /* Ef */ 
	NULL, /* Em */ 
	NULL, /* Eo */
	NULL, /* Fx */
	NULL, /* Ms */ 
	NULL, /* No */
	NULL, /* Ns */
	NULL, /* Nx */
	NULL, /* Ox */
	NULL, /* Pc */
	NULL, /* Pf */
	NULL, /* Po */
	NULL, /* Pq */
	NULL, /* Qc */
	NULL, /* Ql */
	NULL, /* Qo */
	NULL, /* Qq */
	NULL, /* Re */
	NULL, /* Rs */
	NULL, /* Sc */
	NULL, /* So */
	NULL, /* Sq */
	NULL, /* Sm */ 
	NULL, /* Sx */
	NULL, /* Sy */
	NULL, /* Tn */
	NULL, /* Ux */
	NULL, /* Xc */
	NULL, /* Xo */
	NULL, /* Fo */ 
	NULL, /* Fc */ 
	NULL, /* Oo */
	NULL, /* Oc */
	NULL, /* Bk */
	NULL, /* Ek */
	NULL, /* Bt */
	NULL, /* Hf */
	NULL, /* Fr */
	NULL, /* Ud */
	NULL, /* Lb */
	NULL, /* Lp */ 
	NULL, /* Lk */ 
	NULL, /* Mt */ 
	NULL, /* Brq */ 
	NULL, /* Bro */ 
	NULL, /* Brc */ 
	NULL, /* %C */
	NULL, /* Es */
	NULL, /* En */
	NULL, /* Dx */
	NULL, /* %Q */
	NULL, /* br */
	NULL, /* sp */
	NULL, /* %U */
	NULL, /* Ta */
};

int
main(int argc, char *argv[])
{
	FILE *file;
	char line[MAXLINE];
	mp = mparse_alloc(MPARSE_AUTO, MANDOCLEVEL_FATAL, NULL, NULL);
	
	/* Build the databases */
	if (create_db() < 0) 
		errx(EXIT_FAILURE, "Unable to create database");
			
	/* call man -p to get the list of man page dirs */
	if ((file = popen("man -p", "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed");
	
	while (fgets(line, MAXLINE, file) != NULL) {
		/* remove the new line character from the string */
		line[strlen(line) - 1] = '\0';
		/* Traverse the man page directories and parse the pages */
		traversedir(line);
	}
	
	if (pclose(file) == -1)
		errx(EXIT_FAILURE, "pclose error");
	mparse_free(mp);

	/* Now, calculate the weights of each unique term in the index */
	if (build_term_weights() < 0)
		fprintf(stderr, "Could not compute the term weights. Please run makemandb again\n");
		
	cleanup();
	return 0;
}

/*
* traversedir --
*  traverses the given directory recursively and passes all the files in the
*  way to the parser.
*/
static void
traversedir(const char *file)
{
	struct stat sb;
	struct dirent *dirp;
	DIR *dp;
	char *buf;
	
	if (stat(file, &sb) < 0) {
		fprintf(stderr, "stat failed: %s", file);
		return;
	}
	
	/* if it is a regular file, pass it to the parser */
	if (S_ISREG(sb.st_mode)) {
		/* Avoid hardlinks to prevent duplicate entries in the database */
		if (check_md5(file) != 0) {
			cleanup();
			return;
		}
		
		printf("parsing %s\n", file);
		pmdoc(file);
		if (insert_into_db() < 0)
			fprintf(stderr, "Error indexing: %s\n", file);
		return;
	}
	
	/* if it is a directory`, traverse it recursively */
	else if (S_ISDIR(sb.st_mode)) {
		if ((dp = opendir(file)) == NULL) {
			fprintf(stderr, "opendir error: %s\n", file);
			return;
		}
		
		while ((dirp = readdir(dp)) != NULL) {
			/* Avoid . and .. entries in a directory */
			if (strncmp(dirp->d_name, ".", 1)) {
				if ((asprintf(&buf, "%s/%s", file, dirp->d_name) == -1)) {
					closedir(dp);
					if (errno == ENOMEM)
						fprintf(stderr, "ENOMEM\n");
					continue;
				}
				traversedir(buf);
				free(buf);
			}
		}
	
		closedir(dp);
	}
}		

/*
* parsemanpage --
*  parses the man page using libmandoc
*/
static void
pmdoc(const char *file)
{
	struct mdoc	*mdoc; /* resulting mdoc */
	mparse_reset(mp);

	if (mparse_readfd(mp, -1, file) >= MANDOCLEVEL_FATAL) {
		fprintf(stderr, "%s: Parse failure\n", file);
		return;
	}

	mparse_result(mp, &mdoc, NULL);
	if (mdoc == NULL)
		return;

	get_section(mdoc);
	pmdoc_node(mdoc_node(mdoc));
}

static void
pmdoc_node(const struct mdoc_node *n)
{
	
	if (n == NULL)
		return;

	switch (n->type) {
	case (MDOC_HEAD):
		/* FALLTHROUGH */
	case (MDOC_BODY):
		/* FALLTHROUGH */
	case (MDOC_TAIL):
		/* FALLTHROUGH */
	case (MDOC_BLOCK):
		/* FALLTHROUGH */
	case (MDOC_ELEM):
		if (mdocs[n->tok] == NULL)
			break;

		(*mdocs[n->tok])(n);
		
	default:
		break;
	}

	pmdoc_node(n->child);
	pmdoc_node(n->next);
}

/*
* pmdoc_Nm --
*  Extracts the Name of the manual page from the .Nm macro
*/
static void
pmdoc_Nm(const struct mdoc_node *n)
{
	if (n->sec == SEC_NAME && n->child->type == MDOC_TEXT) {
		if ((name = strdup(n->child->string)) == NULL) {
			fprintf(stderr, "Memory allocation error");
			return;
		}
	}
	
	/* on encountering a .Nm macro in the DESCRIPTION section, copy the cached 
	* value of name at the end of desc
	*/
	else if (n->sec == SEC_DESCRIPTION && name != NULL) {
		if (desc == NULL)
			desc = strdup(name);
		else
			concat(&desc, name);
		}
}

/*
* pmdoc_Nd --
*  Extracts the one line description of the man page from the .Nd macro
*/
static void
pmdoc_Nd(const struct mdoc_node *n)
{
	for (n = n->child; n; n = n->next) {
		if (n->type != MDOC_TEXT)
			continue;

		if (name_desc == NULL) 
			name_desc = strdup(n->string);
		else 
			concat(&name_desc, n->string);
	}
}

/*
* pmdoc_Sh --
*  Extracts the complete DESCRIPTION section of the man page
*/
static void
pmdoc_Sh(const struct mdoc_node *n)
{
	if (n->sec == SEC_DESCRIPTION) {
		for(n = n->child; n; n = n->next) {
			if (n->type == MDOC_TEXT) {
				if (desc == NULL)
					desc = strdup(n->string);
				else {
					if (concat(&desc, n->string) < 0)
						return;
				}
			}
			else { 
				/* On encountering a .Nm macro, substitute it with it's previously
				* cached value of the argument
				*/
				if (mdocs[n->tok] == pmdoc_Nm && name != NULL)
					(*mdocs[n->tok])(n);
				/* otherwise call pmdoc_Sh again to handle the nested macros */
				else
					pmdoc_Sh(n);
			}
		}
	}
}

static void
get_section(const struct mdoc *md)
{
	const struct mdoc_meta *md_meta = mdoc_meta(md);
	section = strdup(md_meta->msec);
}

/* cleanup --
*   cleans up the global buffers
*/
static void
cleanup(void)
{
	if (name)
		free(name);
	if (name_desc)
		free(name_desc);
	if (desc)
		free(desc);
	if (md5_hash)
		free(md5_hash);
	if (section)
		free(section);
	
	name = name_desc = desc = md5_hash = section = NULL;
}

/* insert_into_db --
*   Inserts the parsed data of the man page in the Sqlite databse.
*   If any of the values is NULL, then we cleanup and return -1 indicating an 
*   error. Otherwise, store the data in the database and return 0
*/
static int
insert_into_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	
	if (name == NULL || name_desc == NULL || desc == NULL || md5_hash == NULL 
		|| section == NULL) {
		cleanup();
		return -1;
	}
	else {

		sqlite3_initialize();
		rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
				             SQLITE_OPEN_CREATE, NULL);
		if (rc != SQLITE_OK) {
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		sqlite3_extended_result_codes(db, 1);

/*------------------------ Populate the mandb table------------------------------ */
		sqlstr = "insert into mandb values (:section, :name, :name_desc, :desc)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name");
		rc = sqlite3_bind_text(stmt, idx, name, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":section");
		rc = sqlite3_bind_text(stmt, idx, section, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name_desc");
		rc = sqlite3_bind_text(stmt, idx, name_desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
	
		idx = sqlite3_bind_parameter_index(stmt, ":desc");
		rc = sqlite3_bind_text(stmt, idx, desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		sqlite3_finalize(stmt);
/*------------------------ Populate the mandb_md5 table-----------------------*/
		sqlstr = "insert into mandb_md5 values (:md5_hash)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
		rc = sqlite3_bind_text(stmt, idx, md5_hash, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
		
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			sqlite3_close(db);
			sqlite3_shutdown();
			cleanup();
			return -1;
		}
	
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		cleanup();
		return 0;
	}

}

static int
create_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	struct stat sb;
	
	if (stat(DBPATH, &sb) == 0 && S_ISREG(sb.st_mode)) {
		if (remove(DBPATH) < 0)
			return -1;
	}
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
			                 SQLITE_OPEN_CREATE, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
/*------------------------ Build the mandb table------------------------------ */
	sqlstr = "create virtual table mandb using fts4(section, name, \
	name_desc, desc, tokenize=porter)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_aux table------------------------------ */	
	sqlstr = "create virtual table mandb_aux using fts4aux(mandb)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_md5 table------------------------------ */	
	sqlstr = "create table mandb_md5(md5_hash)";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
		
	sqlite3_finalize(stmt);

/*------------------------ Build the mandb_weights table------------------------------ */	
	sqlstr = "create table mandb_weights(docid, term, weight, "
			"constraint pk_mandb_weights primary key(docid, term)) ";

	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	sqlite3_shutdown();
	return 0;
}

/*
* concat--
*  Utility function. Concatenates together: dst, a space character and src. 
* dst + " " + src 
*/
static int
concat(char **dst, const char *src)
{
	int total_len, dst_len;
	if (src == NULL)
		return -1;

	/* we should allow the destination string to be NULL */
	if (*dst == NULL)
		dst_len = 0;
	else	
		dst_len = strlen(*dst);
	
	/* calculate total string length:
	*one extra character for a space and one for the nul byte 
	*/	
	total_len = dst_len + strlen(src) + 2;
		
	if ((*dst = (char *) realloc(*dst, total_len)) == NULL)
		return -1;
		
	if (*dst != NULL) {	
		memcpy(*dst + dst_len, " ", 1);
		dst_len++;
	}
	memcpy(*dst + dst_len, src, strlen(src) + 1);
	
	return 0;
}

/*
* check_md5--
*  Generates the md5 hash of the file and checks if it already doesn't exist in 
*  the database. This function is being used to avoid hardlinks.
*  Return values: 0 if the md5 hash does not exist in the datbase
*                 1 if the hash exists in the database
*                 -1 if an error occured while generating the hash or checking
*                 against the database
*/
static int 
check_md5(const char *file)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

	assert(file != NULL);
	char *buf = MD5File(file, NULL);
	if (buf == NULL) {
		fprintf(stderr, "md5 failed: %s\n", file);
		return -1;
	}
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READONLY, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}

	sqlstr = "select * from mandb_md5 where md5_hash = :md5_hash";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
	rc = sqlite3_bind_text(stmt, idx, buf, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_finalize(stmt);	
		sqlite3_close(db);
		sqlite3_shutdown();
		free(buf);
		return 1;
	}
	
	md5_hash = strdup(buf);
	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	free(buf);
	return 0;
}

/*
* build_term_weights--
*  Compute the weight of all the terms in the corpus and store them in the mandb_weights table.
*
*  Weight of term t for document d = term frequency of t in d * inverse document frequency of t
*
*  Term Frequency of term t in document d = Number of times t occurs in d / 
*                                        Number of times t appears in all documents
*
*  Inverse document frequenct of t = log(Total number of documents / 
*										Number of documents in which t occurs)
*/
static int
build_term_weights(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	char *term = NULL;
	sqlite3_stmt *stmt = NULL;
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_extended_result_codes(db, 1);
	
	rc = sqlite3_create_function(db, "get_tf", 1, SQLITE_ANY, NULL, 
	                             get_tf, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	
	rc = sqlite3_create_function(db, "get_idf", 2, SQLITE_ANY, NULL, 
	                             get_idf, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlstr = "insert into mandb_weights select mandb.docid, mandb_aux.term, "
	"get_tf(matchinfo(mandb)) * get_idf((select count(docid) from mandb), mandb_aux.documents)"
	" from mandb, mandb_aux where mandb_aux.col=\'*\' and mandb match mandb_aux.term";
          
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Could not calculate term weights\n");
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}		
		
	sqlite3_finalize(stmt);	
	sqlite3_close(db);
	sqlite3_shutdown();
	
	return 0;
}

static void
get_tf(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	double tf = 0.0;
	double col_weights[] = {2.0, 1.5, 0.75};
	int *matchinfo;
	int ncol;
	
	/* Check that the number of arguments passed to this function is correct.
	** If not, jump to wrong_number_args. 
	*/
	if( nval != 1 ) {
		fprintf(stderr, "nval != ncol\n");
		goto wrong_number_args;
	}
	
	matchinfo = (unsigned int *) sqlite3_value_blob(apval[0]);
	ncol = matchinfo[1];
		
		
	int icol;
	int *phraseinfo = &matchinfo[2];
	for(icol = 1; icol < ncol; icol++) {
  		int nhitcount = phraseinfo[3*icol];
		int nglobalhitcount = phraseinfo[3*icol+1];
		double weight = col_weights[icol - 1];
		
		if (nglobalhitcount > 0)
			tf += ((double)nhitcount / nglobalhitcount) * weight;
	}
	
	sqlite3_result_double(pctx, tf);
	return;

	/* Jump here if the wrong number of arguments are passed to this function */
	wrong_number_args:
		sqlite3_result_error(pctx, "wrong number of arguments to function rank()", -1);
}

static void
get_idf(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{
	double idf = 0.0;
		
	/* Check that the number of arguments passed to this function is correct.
	** If not, jump to wrong_number_args. 
	*/
	if( nval != 2 ) {
		fprintf(stderr, "nval != ncol\n");
		goto wrong_number_args;
	}
	
	int ndoc = sqlite3_value_int(apval[0]);
	int ndocshit = (int) sqlite3_value_int(apval[1]);
	
	if (ndocshit != 0)
		idf = log((double)ndoc / ndocshit) / log(ndoc);
		
	sqlite3_result_double(pctx, idf);
	return;

	/* Jump here if the wrong number of arguments are passed to this function */
	wrong_number_args:
		sqlite3_result_error(pctx, "wrong number of arguments to function rank()", -1);
}	
