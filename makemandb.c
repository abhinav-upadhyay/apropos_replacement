/*
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * Copyright (c) 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <util.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "man.h"
#include "mandoc.h"
#include "mdoc.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

#define MDOC 0	//If the page is of mdoc(7) type
#define MAN 1	//If the page  is of man(7) type

typedef struct makemandb_flags {
	int optimize;
	int limit;	// limit the indexing to only DESCRIPTION section
} makemandb_flags;

typedef struct mandb_rec {
	char *name;	// for storing the name of the man page
	char *name_desc; // for storing the one line description (.Nd)
	char *desc; // for storing the DESCRIPTION section
	char *lib; // for the LIBRARY section
	char *synopsis; // for the SYNOPSIS section
	char *return_vals; // RETURN VALUES
	char *env; // ENVIRONMENT
	char *files; // FILES
	char *exit_status; // EXIT STATUS
	char *diagnostics; // DIAGNOSTICS
	char *errors; // ERRORS
	char *md5_hash;
	char *section;
	char *machine;
	char *links; //all the links to a page in a space separated form
	char *file_path;
	dev_t device;
	ino_t inode;
	time_t mtime;
	int page_type; //Indicates the type of page: mdoc or man
} mandb_rec;

static int check_md5(const char *, sqlite3 *, const char *, char **);
static void cleanup(struct mandb_rec *);
static void get_section(const struct mdoc *, const struct man *, struct mandb_rec *);
static int insert_into_db(sqlite3 *, struct mandb_rec *);
static	void begin_parse(const char *, struct mparse *, struct mandb_rec *);
static void pmdoc_node(const struct mdoc_node *, struct mandb_rec *);
static void pmdoc_Nm(const struct mdoc_node *, struct mandb_rec *);
static void pmdoc_Nd(const struct mdoc_node *, struct mandb_rec *);
static void pmdoc_Sh(const struct mdoc_node *, struct mandb_rec *);
static void pman_node(const struct man_node *n, struct mandb_rec *);
static void pman_parse_node(const struct man_node *, char **);
static void pman_parse_name(const struct man_node *, struct mandb_rec *);
static void pman_sh(const struct man_node *, struct mandb_rec *);
static void pman_block(const struct man_node *, struct mandb_rec *);
static void traversedir(const char *, sqlite3 *, struct mparse *);
static void mdoc_parse_section(enum mdoc_sec, const char *string, struct mandb_rec *);
static void man_parse_section(enum man_sec, const struct man_node *, struct mandb_rec *);
static void get_machine(const struct mdoc *, struct mandb_rec *);
static void build_file_cache(sqlite3 *, const char *, struct stat *);
static void update_db(sqlite3 *, struct mparse *, struct mandb_rec *);
static void usage(void);
static void optimize(sqlite3 *);

static makemandb_flags mflags;

typedef	void (*pman_nf)(const struct man_node *n, struct mandb_rec *);
typedef	void (*pmdoc_nf)(const struct mdoc_node *n, struct mandb_rec *);
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

static	const pman_nf mans[MAN_MAX] = {
	NULL,	//br
	NULL,	//TH
	pman_sh, //SH
	NULL,	//SS
	NULL,	//TP
	NULL,	//LP
	NULL,	//PP
	NULL,	//P
	NULL,	//IP
	NULL,	//HP
	NULL,	//SM
	NULL,	//SB
	NULL,	//BI
	NULL,	//IB
	NULL,	//BR
	NULL,	//RB
	NULL,	//R
	pman_block,	//B
	NULL,	//I
	NULL,	//IR
	NULL,	//RI
	NULL,	//na
	NULL,	//sp
	NULL,	//nf
	NULL,	//fi
	NULL,	//RE
	NULL,	//RS
	NULL,	//DT
	NULL,	//UC
	NULL,	//PD
	NULL,	//AT
	NULL,	//in
	NULL,	//ft
};


int
main(int argc, char *argv[])
{
	FILE *file;
	const char *sqlstr;
	char *line;
	char *temp = NULL;
	char *errmsg = NULL;
	char ch;
	struct mparse *mp = NULL;
	sqlite3 *db;
	size_t len;
	struct mandb_rec rec;
	
	memset(&rec, 0, sizeof(rec));
	
	while ((ch = getopt(argc, argv, "flo")) != -1) {
		switch (ch) {
		case 'f':
			remove(DBPATH);
			break;
		case 'l':
			mflags.limit = 1;
			break;
		case 'o':
			mflags.optimize = 1;
			break;
		case '?':
		default:
			usage();
			break;
		}
	}
	
	mp = mparse_alloc(MPARSE_AUTO, MANDOCLEVEL_FATAL, NULL, NULL);
	
	if ((db = init_db(DB_CREATE)) == NULL)
		errx(EXIT_FAILURE, "%s", "Could not initialize the database");

	sqlite3_exec(db, "ATTACH DATABASE \':memory:\' AS metadb", NULL, NULL, 
				&errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		close_db(db);
		exit(EXIT_FAILURE);
	}
		
		
	/* Call man -p to get the list of man page dirs */
	if ((file = popen("man -p", "r")) == NULL) {
		close_db(db);
		err(EXIT_FAILURE, "fopen failed");
	}
	
	/* Begin the transaction for indexing the pages	*/
	sqlite3_exec(db, "BEGIN", NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		exit(EXIT_FAILURE);
	}
		
	sqlstr = "CREATE TABLE IF NOT EXISTS metadb.file_cache(device, inode, "
				"mtime, file PRIMARY KEY); "
			"CREATE UNIQUE INDEX IF NOT EXISTS metadb.index_file_cache_dev ON "
				"file_cache (device, inode)";
			

	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		close_db(db);
		exit(EXIT_FAILURE);
	}

	printf("Building temporary file cache\n");	
	while ((line = fgetln(file, &len)) != NULL) {
		/* Replace the new line character at the end of string with '\0' */
		if (line[len - 1] == '\n')
			line[len - 1] = '\0';
		/* Last line will not contain a new line character, so a work around */
		else {
			temp = (char *) emalloc(len + 1);
			memcpy(temp, line, len);
			temp[len] = '\0';
			line = temp;
		}
		/* Traverse the man page directories and parse the pages */
		traversedir(line, db, mp);
		
		if (temp != NULL) {
			free(temp);
			temp = NULL;
		}
	}
	
	if (pclose(file) == -1) {
		close_db(db);
		cleanup(&rec);
		err(EXIT_FAILURE, "pclose error");
	}
	
	update_db(db, mp, &rec);
	mparse_free(mp);
	
	/* Commit the transaction */
	sqlite3_exec(db, "COMMIT", NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		cleanup(&rec);
		exit(EXIT_FAILURE);
	}
	
	if (mflags.optimize)
		optimize(db);
	
	close_db(db);
	cleanup(&rec);
	return 0;
}

/*
 * traversedir --
 *  Traverses the given directory recursively and passes all the man page files 
 *  in the way to build_file_cache()
 */
static void
traversedir(const char *file, sqlite3 *db, struct mparse *mp)
{
	struct stat sb;
	struct dirent *dirp;
	DIR *dp;
	char *buf;
		
	if (stat(file, &sb) < 0) {
		warn("stat failed: %s", file);
		return;
	}
	
	/* If it is a regular file or a symlink, pass it to build_cache() */
	if (S_ISREG(sb.st_mode) || S_ISLNK(sb.st_mode)) {
		build_file_cache(db, file, &sb);
		return;
	}
	
	/* If it is a directory, traverse it recursively */
	else if (S_ISDIR(sb.st_mode)) {
		if ((dp = opendir(file)) == NULL) {
			warn("opendir error: %s", file);
			return;
		}
		
		while ((dirp = readdir(dp)) != NULL) {
			/* Avoid . and .. entries in a directory */
			if (strncmp(dirp->d_name, ".", 1)) {
				if ((asprintf(&buf, "%s/%s", file, dirp->d_name) == -1)) {
					closedir(dp);
					if (errno == ENOMEM)
						warn(NULL);
					continue;
				}
				traversedir(buf, db, mp);
				free(buf);
			}
		}
	
		closedir(dp);
	}
}

/* build_file_cache --
 *	This function generates an md5 hash of the file passed as it's 2nd parameter
 *	and stores it in a temporary table file_cache along with the full file path.
 *	This is done to support incremental updation of the database.
 *	The temporary table file_cache is dropped thereafter in the function 
 *	update_db(), once the database has been updated.
 */
static void
build_file_cache(sqlite3 *db, const char *file, struct stat *sb)
{
	const char *sqlstr;
	sqlite3_stmt *stmt = NULL;
	int rc, idx;
	assert(file != NULL);
	dev_t device_cache = sb->st_dev;
	ino_t inode_cache = sb->st_ino;
	time_t mtime_cache = sb->st_mtime;

	sqlstr = "INSERT INTO metadb.file_cache VALUES (:device, :inode, :mtime, :file)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":device");
	rc = sqlite3_bind_int64(stmt, idx, device_cache);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":inode");
	rc = sqlite3_bind_int64(stmt, idx, inode_cache);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":mtime");
	rc = sqlite3_bind_int64(stmt, idx, mtime_cache);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return;
	}

	
	idx = sqlite3_bind_parameter_index(stmt, ":file");
	rc = sqlite3_bind_text(stmt, idx, file, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return;
	}
	
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}

/* update_db --
 *	Does an incremental updation of the database by checking the file_cache.
 *	It parses and adds the pages which are present in file_cache but not in the
 *	database.
 *	It also removes the pages which are present in the databse but not in the 
 *	file_cache.
 */
static void
update_db(sqlite3 *db, struct mparse *mp, struct mandb_rec *rec)
{
	const char *sqlstr;
	const char *inner_sqlstr;
	sqlite3_stmt *stmt = NULL;
	sqlite3_stmt *inner_stmt = NULL;
	char *file;
	char *errmsg = NULL;
	char *buf = NULL;
	int new_count = 0, total_count = 0, err_count = 0;
	int md5_status;
	int rc, idx;

	sqlstr = "SELECT device, inode, mtime, file FROM metadb.file_cache EXCEPT "
				" SELECT device, inode, mtime, file from mandb_meta";
	
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		close_db(db);
		errx(EXIT_FAILURE, "Could not query file cache");
	}
	
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		total_count++;
		rec->device = sqlite3_column_int64(stmt, 0);
		rec->inode = sqlite3_column_int64(stmt, 1);
		rec->mtime = sqlite3_column_int64(stmt, 2);
		file = (char *) sqlite3_column_text(stmt, 3);
		md5_status = check_md5(file, db, "mandb_meta", &buf);
		assert(buf != NULL);
		if (md5_status == -1) {
			warnx("An error occurred in checking md5 value for file %s", file);
			continue;
		}
		else if (md5_status == 0) {
			/* The md5 is already present in the database, so simply update the 
			 * metadata, ignoring symlinks.
			 */
			struct stat sb;
			stat(file, &sb);
			if (S_ISLNK(sb.st_mode)) {
				free(buf);
				continue;
			}
			
			inner_sqlstr = "UPDATE mandb_meta SET device = :device, "
							"inode = :inode, mtime = :mtime WHERE "
							"md5_hash = :md5 AND file = :file AND "
							"(device <> :device2 OR "
							"inode <> :inode2 OR mtime <> :mtime2)";
			rc = sqlite3_prepare_v2(db, inner_sqlstr, -1, &inner_stmt, NULL);
			if (rc != SQLITE_OK) {
				warnx("%s", sqlite3_errmsg(db));
				free(buf);
				continue;
			}
			idx = sqlite3_bind_parameter_index(inner_stmt, ":device");
			sqlite3_bind_int64(inner_stmt, idx, rec->device);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":inode");
			sqlite3_bind_int64(inner_stmt, idx, rec->inode);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":mtime");
			sqlite3_bind_int64(inner_stmt, idx, rec->mtime);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":md5");
			sqlite3_bind_text(inner_stmt, idx, buf, -1, NULL);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":file");
			sqlite3_bind_text(inner_stmt, idx, file, -1, NULL);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":device2");
			sqlite3_bind_int64(inner_stmt, idx, rec->device);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":inode2");
			sqlite3_bind_int64(inner_stmt, idx, rec->inode);
			idx = sqlite3_bind_parameter_index(inner_stmt, ":mtime2");
			sqlite3_bind_int64(inner_stmt, idx, rec->mtime);

			rc = sqlite3_step(inner_stmt);
			if (rc != SQLITE_DONE) {
				warnx("Could not update the meta data for %s", file);
				free(buf);
				sqlite3_finalize(inner_stmt);
				err_count++;
				continue;
			}
			else {
				printf("Updating %s\n", file);
			}
			sqlite3_finalize(inner_stmt);
		}
		else if (md5_status == 1) {
			/* The md5 was not present in the database, which means this is 
			 * either a new file or an updated file. We should go ahead with 
			 * parsing.
			 */
			printf("Parsing: %s\n", file);
			rec->md5_hash = buf;
			rec->file_path = estrdup(file);	//freed by insert_into_db itself.
			begin_parse(file, mp, rec);
			if (insert_into_db(db, rec) < 0) {
				warnx("Error in indexing %s", file);
				cleanup(rec);
				err_count++;
				continue;
			}
			else
				new_count++;
		}
	}
	
	sqlite3_finalize(stmt);
	
	printf("Total Number of new or updated pages enountered = %d\n"
			"Total number of pages that were successfully indexed = %d\n"
			"Total number of pages that could not be indexed due to parsing "
			"errors = %d\n",
			total_count, new_count, err_count);
	
	sqlstr = "DELETE FROM mandb WHERE rowid IN (SELECT id FROM mandb_meta "
				"WHERE file NOT IN (SELECT file FROM metadb.file_cache)); "
			"DELETE FROM mandb_meta WHERE file NOT IN (SELECT file FROM"
				" metadb.file_cache); "
			"DROP TABLE metadb.file_cache";
			
	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("Attempt to remove old entries failed. You may want to run: "
			"makemandb -f to prune and rebuild the database from scratch");
		warnx("%s", errmsg);
		free(errmsg);
		return;
	}
}
	
/*
 * begin_parse --
 *  parses the man page using libmandoc
 */
static void
begin_parse(const char *file, struct mparse *mp, struct mandb_rec *rec)
{
	struct mdoc *mdoc;
	struct man *man;
	mparse_reset(mp);

	if (mparse_readfd(mp, -1, file) >= MANDOCLEVEL_FATAL) {
		warn("%s: Parse failure", file);
		return;
	}

	mparse_result(mp, &mdoc, &man);
	if (mdoc == NULL && man == NULL) {
		warnx("Not a man(7) or mdoc(7) page");
		return;
	}

	get_machine(mdoc, rec);
	get_section(mdoc, man, rec);
	if (mdoc) {
		rec->page_type = MDOC;
		pmdoc_node(mdoc_node(mdoc), rec);
	}
	else {
		rec->page_type = MAN;
		pman_node(man_node(man), rec);
	}
}

/*
 * get_section --
 *  Extracts the section naumber and normalizes it to only the numeric part
 *  (Which should be the first character of the string).
 */
static void
get_section(const struct mdoc *md, const struct man *m, struct mandb_rec *rec)
{
	rec->section = emalloc(2);
	if (md) {
		const struct mdoc_meta *md_meta = mdoc_meta(md);
		memcpy(rec->section, md_meta->msec, 1);
	}
	else if (m) {
		const struct man_meta *m_meta = man_meta(m);
		memcpy(rec->section, m_meta->msec, 1);
	}
	rec->section[1] = '\0';
}

/*
 * get_machine --
 *  Extracts the machine architecture information if available.
 */
static void
get_machine(const struct mdoc *md, struct mandb_rec *rec)
{
	if (md == NULL)
		return;
	const struct mdoc_meta *md_meta = mdoc_meta(md);
	if (md_meta->arch)
		rec->machine = estrdup(md_meta->arch);
}

static void
pmdoc_node(const struct mdoc_node *n, struct mandb_rec *rec)
{
	
	if (n == NULL)
		return;

	switch (n->type) {
	case (MDOC_BODY):
		/* FALLTHROUGH */
	case (MDOC_TAIL):
		/* FALLTHROUGH */
	case (MDOC_ELEM):
		if (mdocs[n->tok] == NULL)
			break;

		(*mdocs[n->tok])(n, rec);
		
	default:
		break;
	}

	pmdoc_node(n->child, rec);
	pmdoc_node(n->next, rec);
}

/*
 * pmdoc_Nm --
 *  Extracts the Name of the manual page from the .Nm macro
 */
static void
pmdoc_Nm(const struct mdoc_node *n, struct mandb_rec *rec)
{
	if (n->sec == SEC_NAME) {
		for (n = n->child; n; n = n->next) {
			if (n->type == MDOC_TEXT)
				concat(&rec->name, n->string, strlen(n->string));
		}
	}
}

/*
 * pmdoc_Nd --
 *  Extracts the one line description of the man page from the .Nd macro
 */
static void
pmdoc_Nd(const struct mdoc_node *n, struct mandb_rec *rec)
{
	if (n == NULL)
		return;
	if (n->type == MDOC_TEXT) 
		concat(&(rec->name_desc), n->string, strlen(n->string));
	
	if (n->child)
		pmdoc_Nd(n->child, rec);
	if(n->next)
		pmdoc_Nd(n->next, rec);
}

/*
 * pmdoc_Sh --
 *  Extracts the complete DESCRIPTION section of the man page
 */
static void
pmdoc_Sh(const struct mdoc_node *n, struct mandb_rec *rec)
{
	for(n = n->child; n; n = n->next) {
		if (n->type == MDOC_TEXT) {
			mdoc_parse_section(n->sec, n->string, rec);
		}
		else { 
			/* On encountering a .Nm macro, substitute it with it's previously
			 * cached value of the argument
			 */
			if (mdocs[n->tok] == pmdoc_Nm && rec->name != NULL)
				mdoc_parse_section(n->sec, rec->name, rec);
			/* otherwise call pmdoc_Sh again to handle the nested macros */
			else
				pmdoc_Sh(n, rec);
			
		}
	}
}

/*
 * mdoc_parse_section--
 *  Utility function for parsing sections of the mdoc type pages.
 *  Takes two params:
 *   1. sec is an enum which indicates the section in which we are present
 *   2. string is the string which we need to append to the buffer for this 
 *	    particular section.
 *  The function appends string to the global section buffer and returns.
 */
static void
mdoc_parse_section(enum mdoc_sec sec, const char *string, struct mandb_rec *rec)
{
	/* If the user specified the 'l' flag, then parse and store only the
	 * NAME section. Ignore the rest.
	 */
	if (mflags.limit)
		return;

	switch (sec) {
		case SEC_LIBRARY:
			concat(&rec->lib, string, strlen(string));
			break;
		case SEC_SYNOPSIS:
			concat(&rec->synopsis, string, strlen(string));
			break;
		case SEC_RETURN_VALUES:
			concat(&rec->return_vals, string, strlen(string));
			break;
		case SEC_ENVIRONMENT:
			concat(&rec->env, string, strlen(string));
			break;
		case SEC_FILES:
			concat(&rec->files, string, strlen(string));
			break;
		case SEC_EXIT_STATUS:
			concat(&rec->exit_status, string, strlen(string));
			break;
		case SEC_DIAGNOSTICS:
			concat(&rec->diagnostics, string, strlen(string));
			break;
		case SEC_ERRORS:
			concat(&rec->errors, string, strlen(string));
			break;
		case SEC_NAME:
			break;
		default:
			concat(&rec->desc, string, strlen(string));
			break;
	}
}

static void
pman_node(const struct man_node *n, struct mandb_rec *rec)
{
	if (n == NULL)
		return;
	
	switch (n->type) {
	case (MAN_BODY):
		/* FALLTHROUGH */
	case (MAN_TAIL):
		/* FALLTHROUGH */
	case (MAN_BLOCK):
		/* FALLTHROUGH */
	case (MAN_ELEM):
		if (mans[n->tok] == NULL)
			break;

		(*mans[n->tok])(n, rec);
	default:
		break;
	}

	pman_node(n->child, rec);
	pman_node(n->next, rec);
}

/* 
 * pman_parse_name --
 *  Parses the NAME section and puts the complete content in the name_desc 
 *  variable.
 */
static void
pman_parse_name(const struct man_node *n, struct mandb_rec *rec)
{
	if (n == NULL)
		return;
	if (n->type == MAN_TEXT) 
		concat(&rec->name_desc, n->string, strlen(n->string));
	
	if (n->child)
		pman_parse_name(n->child, rec);
	if(n->next)
		pman_parse_name(n->next, rec);
}

/* A stub function to be able to parse the macros like .B embedded inside a
 *  section
 */
static void
pman_block(const struct man_node *n, struct mandb_rec *rec)
{
// empty stub
}

/* 
 * pman_sh --
 * This function does one of the two things:
 *  1. If the present section is NAME, then it will:
 *    (a) extract the name of the page (in case of multiple comma separated 
 *	      names, it will pick up the first one).
 *	  (b) It will also build a space spearated list of all the sym/hardlinks to
 *        this page and store in the buffer 'links'. These are extracted from
 *        the comma separated list of names in the NAME section as well.
 *    (c) And then it will move on to the one line description section, which is
 *        after the list of names in the NAME section.
 *  2. Otherwise, it will check the section name and call the man_parse_section
 *     function, passing the enum corresponding that section.
 */
static void
pman_sh(const struct man_node *n, struct mandb_rec *rec)
{
	const struct man_node *head;
	int sz;

	if ((head = n->parent->head) != NULL &&	(head = head->child) != NULL &&
		head->type ==  MAN_TEXT) {
		if (strcmp(head->string, "NAME") == 0) {
			/* We are in the NAME section. pman_parse_name will put the complete
			 * content in name_desc
			 */			
			pman_parse_name(n, rec);

			/* Take out the name of the man page. name_desc contains complete
			 * NAME section content, e.g: "gcc \- GNU project C and C++ compiler"
			 * It might be a comma separated list of multiple names, for now to 
			 * keep things simple just take the first name out before the comma.
			 */
			
			/* Remove any leading spaces */
			while (*(rec->name_desc) == ' ') 
				rec->name_desc++;
			
			/* If the line begins with a "\&", avoid those */
			if (rec->name_desc[0] == '\\' && rec->name_desc[1] == '&')
				rec->name_desc += 2;
			/* Again remove any leading spaces left */
			while (*(rec->name_desc) == ' ') 
				rec->name_desc++;
			
			/* Assuming the name of a man page is a single word, we can easily
			 * take out the first word out of the string
			 */
			char *temp = estrdup(rec->name_desc);
			char *link;
			sz = strcspn(temp, " ,\0");
			rec->name = malloc(sz+1);
			int i;
			for(i=0; i<sz; i++)
				rec->name[i] = *temp++;
			rec->name[i] = 0;
			
			/* Build a space separated list of all the links to this page */
			for(link = strtok(temp, " "); link; link = strtok(NULL, " ")) {
				if (link[strlen(link)] == ',') {
					link[strlen(link)] = 0;
					concat(&rec->links, link, strlen(link));
				}
				else
					break;
			}
			free(temp);
			/*   The name might be surrounded by escape sequences of the form:
			 *   \fBname\fR or similar. So remove those as well.
			 */
			if (rec->name[0] == '\\' && rec->name[1] != '&') {
				rec->name += 3;
				rec->name[strlen(rec->name) -3] = 0;
			}
			
			
			/* Now remove the name(s) of the man page(s) so that we are left with
			 * the one line description.
			 * So we know we have passed over the NAME if we:
			 * 1. encounter a space not preceeded by a comma and not succeeded by a \\
			 *    e.g.: foo-bar This is a simple foo-bar utility.
			 * 2. enconter a '-' which is preceeded by a '\' and succeeded by a space
			 *    e.g.: foo-bar \- This is a simple foo-bar utility
			 *          foo-bar, blah-blah \- foo-bar with blah-blah
			 *          foo-bar \-\- another foo-bar
			 * 3. encounter a '-' preceeded by a space and succeeded by a space
			 *     e.g.: foo-bar - This is a simple foo-bar utility
			 * (I hope this covers all possible sane combinations)
			 */
			char prev = *rec->name_desc++;
			while (*(rec->name_desc)) {
				/* case 1 */
				if (*(rec->name_desc) == ' ' && prev != ',' && *(rec->name_desc + 1) != '\\') {
					rec->name_desc++;
					/* Well, there might be a '-' without a leading '\\', 
					 * get over it 
					 */
					if (*(rec->name_desc) == '-')
						rec->name_desc += 2;
					break;
				}
				/* case 2 */
				else if (*(rec->name_desc) == '-' && prev == '\\' 
					&& *(rec->name_desc + 1) == ' ') {
					rec->name_desc += 2;
					break;
				}
				prev = *(rec->name_desc);
				rec->name_desc++;
			}
		}
		
		/* Check the section, and if it is of our concern, extract it's 
		 * content
		 */
		 
		else if (strcmp((const char *)head->string, "DESCRIPTION") == 0)
			man_parse_section(MANSEC_DESCRIPTION, n, rec);
		
		else if (strcmp((const char *)head->string, "SYNOPSIS") == 0)
			man_parse_section(MANSEC_SYNOPSIS, n, rec);

		else if (strcmp((const char *)head->string, "LIBRARY") == 0)
			man_parse_section(MANSEC_LIBRARY, n, rec);

		else if (strcmp((const char *)head->string, "ERRORS") == 0)
			man_parse_section(MANSEC_ERRORS, n, rec);

		else if (strcmp((const char *)head->string, "FILES") == 0)
			man_parse_section(MANSEC_FILES, n, rec);

		/* The RETURN VALUE section might be specified in multiple ways */
		else if (strcmp((const char *) head->string, "RETURN VALUE") == 0
			|| strcmp((const char *)head->string, "RETURN VALUES") == 0
			|| (strcmp((const char *)head->string, "RETURN") == 0 && 
			head->next->type == MAN_TEXT && (strcmp((const char *)head->next->string, "VALUE") == 0 ||
			strcmp((const char *)head->next->string, "VALUES") == 0)))
				man_parse_section(MANSEC_RETURN_VALUES, n, rec);

		/* EXIT STATUS section can also be specified all on one line or on two
		 * separate lines.
		 */
		else if (strcmp((const char *)head->string, "EXIT STATUS") == 0
			|| (strcmp((const char *) head->string, "EXIT") ==0 &&
			head->next->type == MAN_TEXT &&
			strcmp((const char *)head->next->string, "STATUS") == 0))
			man_parse_section(MANSEC_EXIT_STATUS, n, rec);

		/* Store the rest of the content in desc */
		else
			man_parse_section(MANSEC_NONE, n, rec);
	}
}

/*
 * pman_parse_node --
 *  Generic function to iterate through a node. Usually called from 
 *  man_parse_section to parse a particular section of the man page.
 */
static void
pman_parse_node(const struct man_node *n, char **s)
{
	for (n = n->child; n; n = n->next) {
		if (n->type == MAN_TEXT)
			concat(s, n->string, strlen(n->string));
		else
			pman_parse_node(n, s);
	}
}

/*
 * man_parse_section --
 *  Takes two parameters: 
 *   sec: Tells which section we are present in
 *   n: Is the present node of the AST.
 * Depending on the section, we call pman_parse_node to parse that section and
 * concatenate the content from that section into the buffer for that section.
 */
static void
man_parse_section(enum man_sec sec, const struct man_node *n, struct mandb_rec *rec)
{
	/* If the user sepecified the 'l' flag then just parse the NAME
	 *  section, ignore the rest.
	 */
	if (mflags.limit)
		return;

	switch (sec) {
		case MANSEC_LIBRARY:
			pman_parse_node(n, &rec->lib);
			break;
		case MANSEC_SYNOPSIS:
			pman_parse_node(n, &rec->synopsis);
			break;
		case MANSEC_RETURN_VALUES:
			pman_parse_node(n, &rec->return_vals);
			break;
		case MANSEC_ENVIRONMENT:
			pman_parse_node(n, &rec->env);
			break;
		case MANSEC_FILES:
			pman_parse_node(n, &rec->files);
			break;
		case MANSEC_EXIT_STATUS:
			pman_parse_node(n, &rec->exit_status);
			break;
		case MANSEC_DIAGNOSTICS:
			pman_parse_node(n, &rec->diagnostics);
			break;
		case MANSEC_ERRORS:
			pman_parse_node(n, &rec->errors);
			break;
		case MANSEC_NAME:
			break;
		default:
			pman_parse_node(n, &rec->desc);
			break;
	}

}

/* 
 * cleanup --
 *  cleans up the global buffers
 */
static void
cleanup(struct mandb_rec *rec)
{
	free(rec->name);
	free(rec->name_desc);
	free(rec->desc);
	free(rec->md5_hash);
	free(rec->section);
	free(rec->lib);
	free(rec->synopsis);
	free(rec->return_vals);
	free(rec->env);
	free(rec->files);
	free(rec->exit_status);
	free(rec->diagnostics);
	free(rec->errors);
	free(rec->links);
	free(rec->machine);
	free(rec->file_path);
		
/*	name = name_desc = desc = md5_hash = section = lib = synopsis = return_vals =
	 env = files = exit_status = diagnostics = errors = links = machine = 
	 file_path = NULL;*/
	 memset(rec, 0, sizeof(*rec));
}

/*
 * insert_into_db --
 *  Inserts the parsed data of the man page in the Sqlite databse.
 *  If any of the values is NULL, then we cleanup and return -1 indicating an 
 *  error. Otherwise, store the data in the database and return 0
 */
static int
insert_into_db(sqlite3 *db, struct mandb_rec *rec)
{
	int rc = 0;
	int idx = -1;
	const char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	char *link = NULL;
	char *errmsg = NULL;
	long int mandb_rowid;
	
	/* At the very minimum we want to make sure that we store the following data:
	 *  Name, One line description, the section number, and the md5 hash
	 */		
	if (rec->name == NULL || rec->name_desc == NULL || rec->md5_hash == NULL 
		|| rec->section == NULL) {
		cleanup(rec);
		return -1;
	}

	/* In case of a mdoc page: (sorry, no better place to put this block of code)
	 *  parse the comma separated list of names of man pages, the first name will
	 *  be stored in the mandb table, rest will be treated as links and put in the
	 *  mandb_links table
	 */	
	if (rec->page_type == MDOC) {
		rec->links = strdup(rec->name);
		free(rec->name);
		int sz = strcspn(rec->links, " \0");
		rec->name = malloc(sz + 1);
		memcpy(rec->name, rec->links, sz);
		if(rec->name[sz - 1] == ',')
			rec->name[sz - 1] = 0;
		else
			rec->name[sz] = 0;
		rec->links += sz;
		if (*(rec->links) == ' ')
			rec->links++;
	}
	
/*------------------------ Populate the mandb table---------------------------*/
	sqlstr = "INSERT INTO mandb VALUES (:section, :name, :name_desc, :desc, "
			":lib, :synopsis, :return_vals, :env, :files, :exit_status, "
			":diagnostics, :errors)";
	
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":name");
	rc = sqlite3_bind_text(stmt, idx, rec->name, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":section");
	rc = sqlite3_bind_text(stmt, idx, rec->section, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":name_desc");
	rc = sqlite3_bind_text(stmt, idx, rec->name_desc, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":desc");
	rc = sqlite3_bind_text(stmt, idx, rec->desc, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":lib");
	rc = sqlite3_bind_text(stmt, idx, rec->lib, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":synopsis");
	rc = sqlite3_bind_text(stmt, idx, rec->synopsis, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":return_vals");
	rc = sqlite3_bind_text(stmt, idx, rec->return_vals, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":env");
	rc = sqlite3_bind_text(stmt, idx, rec->env, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":files");
	rc = sqlite3_bind_text(stmt, idx, rec->files, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":exit_status");
	rc = sqlite3_bind_text(stmt, idx, rec->exit_status, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":diagnostics");
	rc = sqlite3_bind_text(stmt, idx, rec->diagnostics, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":errors");
	rc = sqlite3_bind_text(stmt, idx, rec->errors, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	sqlite3_finalize(stmt);
	
	/* Get the row id of the last inserted row */
	mandb_rowid = sqlite3_last_insert_rowid(db);
		
/*------------------------ Populate the mandb_md5 table-----------------------*/
	sqlstr = "INSERT INTO mandb_meta VALUES (:device, :inode, :mtime, :file, "
				":md5_hash, :id)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":device");
	rc = sqlite3_bind_int64(stmt, idx, rec->device);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":inode");
	rc = sqlite3_bind_int64(stmt, idx, rec->inode);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":mtime");
	rc = sqlite3_bind_int64(stmt, idx, rec->mtime);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	idx = sqlite3_bind_parameter_index(stmt, ":file");
	rc = sqlite3_bind_text(stmt, idx, rec->file_path, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
	rc = sqlite3_bind_text(stmt, idx, rec->md5_hash, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":id");
	rc = sqlite3_bind_int64(stmt, idx, mandb_rowid);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup(rec);
		return -1;
	}

	sqlite3_finalize(stmt);
	
/*------------------------ Populate the mandb_links table---------------------*/
	char *str = NULL;
	if (rec->links && strlen(rec->links) == 0) {
		free(rec->links);
		rec->links = NULL;
	}
		
	if (rec->links) {
		if (rec->machine == NULL)
			easprintf(&rec->machine, "%s", "");
		
		for(link = strtok(rec->links, " "); link; link = strtok(NULL, " ")) {
			if (link[0] == ',')
				link++;
			if(link[strlen(link) - 1] == ',')
				link[strlen(link) - 1] = 0;
			
			easprintf(&str, "INSERT INTO mandb_links VALUES (\'%s\', \'%s\', "
				"\'%s\', \'%s\')", link, rec->name, rec->section, rec->machine);
			sqlite3_exec(db, str, NULL, NULL, &errmsg);
			if (errmsg != NULL) {
				warnx("%s", errmsg);
				cleanup(rec);
				free(str);
				free(errmsg);
				return -1;
			}
			free(str);
		}
	}
	
	cleanup(rec);
	free(rec->links);
	return 0;
}

/*
 * check_md5--
 *  Generates the md5 hash of the file and checks if it already doesn't exist in 
 *  the table passed as the 3rd parameter. This function is being used to avoid 
 *  hardlinks.
 *  On successful completion it will also set the value of the fourth parameter 
 *  to the md5 hash of the file which was computed previously. It is the duty of
 *  the caller to free this buffer.
 *  Return values:
 *		-1: If an error occurs somewhere and sets the md5 return buffer to NULL.
 *		0: If the md5 hash does not exist in the table.
 *		1: If the hash exists in the database.
 */
static int
check_md5(const char *file, sqlite3 *db, const char *table, char **buf)
{
	int rc = 0;
	int idx = -1;
	char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

	assert(file != NULL);
	*buf = MD5File(file, NULL);
	if (*buf == NULL) {
		warn("md5 failed: %s", file);
		return -1;
	}
	
	easprintf(&sqlstr, "SELECT * from %s WHERE md5_hash = :md5_hash", table);
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		free(sqlstr);
		free(*buf);
		*buf = NULL;
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
	rc = sqlite3_bind_text(stmt, idx, *buf, -1, NULL);
	if (rc != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		free(sqlstr);
		free(*buf);
		*buf = NULL;
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_finalize(stmt);	
		free(sqlstr);
		return 0;
	}
	
	sqlite3_finalize(stmt);
	free(sqlstr);
	return 1;
}

/* Optimize the index for faster search */
static void
optimize(sqlite3 *db)
{
	const char *sqlstr;
	char *errmsg = NULL;

	printf("Optimizing the database index\n");
	sqlstr = "INSERT INTO mandb(mandb) VALUES (\'optimize\'); "
			"VACUUM";
	sqlite3_exec(db, sqlstr, NULL, NULL, &errmsg);
	if (errmsg != NULL) {
		warnx("%s", errmsg);
		free(errmsg);
		return;
	}	
}

static void
usage(void)
{
	(void)warnx("usage: %s [-flo]", getprogname());
	exit(1);
}
