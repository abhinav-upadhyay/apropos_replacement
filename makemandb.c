/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Abhinav Upadhyay.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "man.h"
#include "mandoc.h"
#include "mdoc.h"
#include "sqlite3.h"

#define MAXLINE 1024	//buffer size for fgets
#define DBPATH "./apropos.db"

static int check_md5(const char *, sqlite3 *);
static void cleanup(void);
static int concat(char **, const char *);
static int create_db(void);
static void get_section(const struct mdoc *, const struct man *);
static int insert_into_db(sqlite3 *);
static	void begin_parse(const char *);
static void pmdoc_node(const struct mdoc_node *);
static void pmdoc_Nm(const struct mdoc_node *);
static void pmdoc_Nd(const struct mdoc_node *);
static void pmdoc_Sh(const struct mdoc_node *);
static void pman_node(const struct man_node *n);
static void pman_parse_node(const struct man_node *);
static void pman_sh(const struct man_node *);
static void traversedir(const char *, sqlite3 *db);
static char *lower(char *);

static char *name = NULL;	// for storing the name of the man page
static char *name_desc = NULL; // for storing the one line description (.Nd)
static char *desc = NULL; // for storing the DESCRIPTION section
static char *md5_hash = NULL;
static char *section = NULL;
static struct mparse *mp = NULL;


typedef	void (*pman_nf)(const struct man_node *n);
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
	NULL,	//B
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
	char line[MAXLINE];
	sqlite3 *db = NULL;
	int rc;
	sqlite3_stmt *stmt = NULL;
	
	mp = mparse_alloc(MPARSE_AUTO, MANDOCLEVEL_FATAL, NULL, NULL);
	
	/* Build the databases */
	if (create_db() < 0) 
		errx(EXIT_FAILURE, "Unable to create database");
			
	/* call man -p to get the list of man page dirs */
	if ((file = popen("man -p", "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed");
	
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
		             SQLITE_OPEN_CREATE, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "traversedir: Could not open database\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		cleanup();
		return -1;
	}
	
	// begin the transaction for indexing the pages	
	const char *sqlstr = "BEGIN";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		cleanup();
		return -1;
	}
	
	if (sqlite3_step(stmt) != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	sqlite3_finalize(stmt);
	
	while (fgets(line, MAXLINE, file) != NULL) {
		/* remove the new line character from the string */
		line[strlen(line) - 1] = '\0';
		/* Traverse the man page directories and parse the pages */
		traversedir(line, db);
	}
	
	if (pclose(file) == -1)
		errx(EXIT_FAILURE, "pclose error");
	mparse_free(mp);
	
	// commit the transaction 
	sqlstr = "COMMIT";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		cleanup();
		return -1;
	}
	
	if (sqlite3_step(stmt) != SQLITE_DONE) {
		sqlite3_finalize(stmt);
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlite3_finalize(stmt);
	sqlite3_close(db);
	sqlite3_shutdown();
	cleanup();
	return 0;
}

/*
* traversedir --
*  traverses the given directory recursively and passes all the files in the
*  way to the parser.
*/
static void
traversedir(const char *file, sqlite3 *db)
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
		if (check_md5(file, db) != 0) {
			cleanup();
			return;
		}
		
		printf("parsing %s\n", file);
		begin_parse(file);
		if (insert_into_db(db) < 0)
			fprintf(stderr, "Error indexing: %s\n", file);
		return;
	}
	
	/* if it is a directory, traverse it recursively */
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
				traversedir(buf, db);
				free(buf);
			}
		}
	
		closedir(dp);
	}
}		

/*
* begin_parse --
*  parses the man page using libmandoc
*/
static void
begin_parse(const char *file)
{
	struct mdoc	*mdoc;
	struct man *man;
	mparse_reset(mp);

	if (mparse_readfd(mp, -1, file) >= MANDOCLEVEL_FATAL) {
		fprintf(stderr, "%s: Parse failure\n", file);
		return;
	}

	mparse_result(mp, &mdoc, &man);
	if (mdoc == NULL && man == NULL) {
		fprintf(stderr, "Not a man(7) or mdoc(7) page\n");
		return;
	}

	get_section(mdoc, man);
	if (mdoc)
		pmdoc_node(mdoc_node(mdoc));
	else
		pman_node(man_node(man));
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

static void
pman_node(const struct man_node *n)
{
	if (NULL == n)
		return;
	
	switch (n->type) {
	case (MAN_HEAD):
		/* FALLTHROUGH */
	case (MAN_BODY):
		/* FALLTHROUGH */
	case (MAN_TAIL):
		/* FALLTHROUGH */
	case (MAN_BLOCK):
		/* FALLTHROUGH */
	case (MAN_ELEM):
		if (mans[n->tok] == NULL)
			break;

		(*mans[n->tok])(n);
	default:
		break;
	}

	pman_node(n->child);
	pman_node(n->next);
}

static void
pman_parse_node(const struct man_node *n)
{
	for (n = n->child; n; n = n->next) {
		if (n->type == MAN_TEXT) {
			if (desc == NULL)
					desc = strdup(n->string);
			else if (concat(&desc, n->string) < 0)
				return;
		}		
		else
			pman_parse_node(n);
	}
}

static void
pman_sh(const struct man_node *n)
{
	const struct man_node *head;
	int sz;
	char *start;

	if ((head = n->parent->head) != NULL &&	(head = head->child) != NULL &&
		head->type ==  MAN_TEXT) {
		if (strcmp(head->string, "NAME") == 0) {
			while (n->type != MAN_TEXT) {
				if (n->child)
					n = n->child;
				else if (n->next)
					n = n->next;
				else {
					name_desc = NULL;
					return;
				}
			}
	
			start = n->string;
			for ( ;; ) {
				sz = strcspn(start, " ,");
				if (n->string[(int)sz] == '\0')
					break;

				if (start[(int)sz] == ' ') {
					start += (int)sz + 1;
					break;
				}

				assert(start[(int)sz] == ',' || start[(int)sz] == 0);
				start += (int)sz + 1;
				while (*start == ' ')
					start++;
			}
			if (strcmp(n->string, head->string))
				name_desc = strdup(start+3);
		}
		else {
			for (n = n->child; n; n = n->next) {
				if (n->type == MAN_TEXT && strcmp(n->string, head->string)) {
					if (desc == NULL)
						desc = strdup(n->string);
					else if (concat(&desc, n->string) < 0)
						return;
				}					
				else
					pman_parse_node(n);
			}
		}
	}
}

static void
get_section(const struct mdoc *md, const struct man *m)
{
	if (md) {
		const struct mdoc_meta *md_meta = mdoc_meta(md);
		section = strdup(md_meta->msec);
	}
	else if (m) {
		const struct man_meta *m_meta = man_meta(m);
		section = strdup(m_meta->msec);
		name = lower(strdup(m_meta->title));
	}
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
insert_into_db(sqlite3 *db)
{
	int rc = 0;
	int idx = -1;
	const char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	
	if (name == NULL || name_desc == NULL || desc == NULL || md5_hash == NULL 
		|| section == NULL) {
		cleanup();
		return -1;
	}
	else {

		sqlite3_extended_result_codes(db, 1);

/*------------------------ Populate the mandb table------------------------------ */
		sqlstr = "insert into mandb values (:section, :name, :name_desc, :desc)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name");
		rc = sqlite3_bind_text(stmt, idx, name, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":section");
		rc = sqlite3_bind_text(stmt, idx, section, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}

		idx = sqlite3_bind_parameter_index(stmt, ":name_desc");
		rc = sqlite3_bind_text(stmt, idx, name_desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}
	
		idx = sqlite3_bind_parameter_index(stmt, ":desc");
		rc = sqlite3_bind_text(stmt, idx, desc, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}

		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}
		
		sqlite3_finalize(stmt);
/*------------------------ Populate the mandb_md5 table-----------------------*/
		sqlstr = "insert into mandb_md5 values (:md5_hash)";
		rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			cleanup();
			return -1;
		}
		
		idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
		rc = sqlite3_bind_text(stmt, idx, md5_hash, -1, NULL);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}
		
		rc = sqlite3_step(stmt);
		if (rc != SQLITE_DONE) {
			fprintf(stderr, "%s\n", sqlite3_errmsg(db));
			sqlite3_finalize(stmt);
			cleanup();
			return -1;
		}
	
		sqlite3_finalize(stmt);
		cleanup();
		return 0;
	}

}

static int
create_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	const char *sqlstr = NULL;
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
check_md5(const char *file, sqlite3 *db)
{
	int rc = 0;
	int idx = -1;
	const char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;

	assert(file != NULL);
	char *buf = MD5File(file, NULL);
	if (buf == NULL) {
		fprintf(stderr, "md5 failed: %s\n", file);
		return -1;
	}
	
	sqlstr = "select * from mandb_md5 where md5_hash = :md5_hash";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		free(buf);
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":md5_hash");
	rc = sqlite3_bind_text(stmt, idx, buf, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		free(buf);
		return -1;
	}
	
	if (sqlite3_step(stmt) == SQLITE_ROW) {
		sqlite3_finalize(stmt);	
		free(buf);
		return 1;
	}
	
	md5_hash = strdup(buf);
	sqlite3_finalize(stmt);	
	free(buf);
	return 0;
}

static char *
lower(char *str)
{
	assert(str);
	size_t i;
	char c;
	for (i = 0; i < strlen(str); i++) {
		c = tolower((unsigned char) str[i]);
		str[i] = c;
	}
	return str;
}
