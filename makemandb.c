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
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <math.h>
#include <md5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "fts3_tokenizer.h"
#include "man.h"
#include "mandoc.h"
#include "mdoc.h"
#include "sqlite3.h"
#include "stopword_tokenizer.h"

#define MAXLINE 1024	//buffer size for fgets

static int check_md5(const char *, sqlite3 *);
static void cleanup(void);
static int create_db(void);
static void get_section(const struct mdoc *, const struct man *);
static int insert_into_db(sqlite3 *);
static	void begin_parse(const char *, struct mparse *mp);
static void pmdoc_node(const struct mdoc_node *);
static void pmdoc_Nm(const struct mdoc_node *);
static void pmdoc_Nd(const struct mdoc_node *);
static void pmdoc_Sh(const struct mdoc_node *);
static void pman_node(const struct man_node *n);
static void pman_parse_node(const struct man_node *, char **);
static void pman_sh(const struct man_node *);
static void pman_block(const struct man_node *);
static void traversedir(const char *, sqlite3 *db, struct mparse *mp);
static void mdoc_parse_section(enum mdoc_sec, const char *string);

static char *name = NULL;	// for storing the name of the man page
static char *name_desc = NULL; // for storing the one line description (.Nd)
static char *desc = NULL; // for storing the DESCRIPTION section
static char *lib = NULL; // for the LIBRARY section
static char *synopsis = NULL; // for the SYNOPSIS section
static char *return_vals = NULL; // RETURN VALUES
static char *env = NULL; // ENVIRONMENT
static char *files = NULL; // FILES
static char *exit_status = NULL; // EXIT STATUS
static char *diagnostics = NULL; // DIAGNOSTICS
static char *errors = NULL; // ERRORS
static char *md5_hash = NULL;
static char *section = NULL;

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
	char line[MAXLINE];
	const char *sqlstr;
	sqlite3 *db = NULL;
	int rc, idx;
	sqlite3_stmt *stmt = NULL;
	struct mparse *mp = NULL;
	const sqlite3_tokenizer_module *stopword_tokenizer_module;
	
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
	
	sqlite3_extended_result_codes(db, 1);
	sqlstr = "select fts3_tokenizer(:tokenizer_name, :tokenizer_ptr)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_name");
	rc = sqlite3_bind_text(stmt, idx, "stopword_tokenizer", -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	sqlite3Fts3PorterTokenizerModule((const sqlite3_tokenizer_module **)&stopword_tokenizer_module);
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_ptr");
	rc = sqlite3_bind_blob(stmt, idx, &stopword_tokenizer_module, sizeof(stopword_tokenizer_module), SQLITE_STATIC);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "%s tokenizer error\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	sqlite3_finalize(stmt);	
	
	
	// begin the transaction for indexing the pages	
	sqlstr = "BEGIN";
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
		traversedir(line, db, mp);
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
traversedir(const char *file, sqlite3 *db, struct mparse *mp)
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
		begin_parse(file, mp);
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
				traversedir(buf, db, mp);
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
begin_parse(const char *file, struct mparse *mp)
{
	struct mdoc *mdoc;
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
	case (MDOC_BODY):
		/* FALLTHROUGH */
	case (MDOC_TAIL):
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

		 if (concat(&name_desc, n->string) < 0)
		 	return;
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
			mdoc_parse_section(n->sec, n->string);
		}
		else { 
			/* On encountering a .Nm macro, substitute it with it's previously
			* cached value of the argument
			*/
			if (mdocs[n->tok] == pmdoc_Nm && name != NULL)
				mdoc_parse_section(n->sec, name);
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
pman_parse_node(const struct man_node *n, char **s)
{
	for (n = n->child; n; n = n->next) {
		if (n->type == MAN_TEXT) {
			if (concat(s, n->string) < 0)
				return;
		}	
		else
			pman_parse_node(n, s);
	}
}

/* A stub function to be able to parse the macros like .B embedded inside a
*  section
*/
static void
pman_block(const struct man_node *n)
{
// empty stub
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
			
			/* Extract the name of the man page 
			*   There might be multiple names comma separated, just take out 
			*   the first name.
			*   The name might be surrounded by escape sequences of the form:
			*   \fBname\fR or similar. So remove those as well.
			*/
			if (name == NULL) {
				name = strdup(n->string);
				name = strtok(name, " ,");
				if (name[0] == '\\') {
					name = name + 3;
					name[strlen(name) -3] = 0;
				}
			}
			
			/* Some hackery to go over the comma separated list of names */
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
		
		/* Check the section, and if it is of our concern, extract it's content */
		else if (strcmp((const char *)head->string, "SYNOPSIS") == 0)
			pman_parse_node(n, &synopsis);
		
		else if (strcmp((const char *)head->string, "LIBRARY") == 0)
			pman_parse_node(n, &lib);
		
		else if (strcmp((const char *)head->string, "ERRORS") == 0)
			pman_parse_node(n, &errors);
		
		else if (strcmp((const char *)head->string, "FILES") == 0)
			pman_parse_node(n, &files);
		
		// The RETURN VALUE section might be specified in multiple ways 
		else if (strcmp((const char *) head->string, "RETURN VALUE") == 0
			|| strcmp((const char *)head->string, "RETURN VALUES") == 0
			|| (strcmp((const char *)head->string, "RETURN") == 0 && 
			head->next->type == MAN_TEXT && (strcmp((const char *)head->next->string, "VALUE") == 0 ||
			strcmp((const char *)head->next->string, "VALUES") == 0)))
				pman_parse_node(n, &return_vals);
		
		// EXIT STATUS section can also be specified all on one line or on two
		// separate lines.
		else if (strcmp((const char *)head->string, "EXIT STATUS") == 0
			|| (strcmp((const char *) head->string, "EXIT") ==0 &&
			head->next->type == MAN_TEXT &&
			strcmp((const char *)head->next->string, "STATUS") == 0))
			pman_parse_node(n, &exit_status);

		// Store the rest of the content in desc
		else
			pman_parse_node(n, &desc);
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
	if (lib)
		free(lib);
	if (synopsis)
		free(synopsis);
	if (return_vals)
		free(return_vals);
	if (env)
		free(env);
	if (files)
		free(files);
	if (exit_status)
		free(exit_status);
	if (diagnostics)
		free(diagnostics);
	if (errors)
		free(errors);
		
	name = name_desc = desc = md5_hash = section = lib = synopsis = return_vals =
	 env = files = exit_status = diagnostics = errors = NULL;
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

	/* At the very minimum we want to make sure that we store the following data:
	*  Name, One line description, the section number, and the md5 hash
	*/		
	if (name == NULL || name_desc == NULL || md5_hash == NULL 
		|| section == NULL) {
		cleanup();
		return -1;
	}
	
	rc = sqlite3_create_function(db, "zip", 1, SQLITE_ANY, NULL, 
                             zip, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function: compress\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

	rc = sqlite3_create_function(db, "unzip", 1, SQLITE_ANY, NULL, 
		                         unzip, NULL, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Not able to register function: uncompress\n");
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}

/*------------------------ Populate the mandb table------------------------------ */
	sqlstr = "insert into mandb values (:section, :name, :name_desc, :desc, :lib, "
	":synopsis, :return_vals, :env, :files, :exit_status, :diagnostics, :errors)";
	
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
	
	idx = sqlite3_bind_parameter_index(stmt, ":lib");
	rc = sqlite3_bind_text(stmt, idx, lib, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":synopsis");
	rc = sqlite3_bind_text(stmt, idx, synopsis, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":return_vals");
	rc = sqlite3_bind_text(stmt, idx, return_vals, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":env");
	rc = sqlite3_bind_text(stmt, idx, env, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":files");
	rc = sqlite3_bind_text(stmt, idx, files, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":exit_status");
	rc = sqlite3_bind_text(stmt, idx, exit_status, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":diagnostics");
	rc = sqlite3_bind_text(stmt, idx, diagnostics, -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		cleanup();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":errors");
	rc = sqlite3_bind_text(stmt, idx, errors, -1, NULL);
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

static int
create_db(void)
{
	sqlite3 *db = NULL;
	int rc = 0;
	int idx;
	const char *sqlstr = NULL;
	sqlite3_stmt *stmt = NULL;
	struct stat sb;
	const sqlite3_tokenizer_module *stopword_tokenizer_module;
	
	if (stat(DBPATH, &sb) == 0 && S_ISREG(sb.st_mode)) {
		if (remove(DBPATH) < 0)
			return -1;
	}
	
	sqlite3_initialize();
	rc = sqlite3_open_v2(DBPATH, &db, SQLITE_OPEN_READWRITE | 
			                 SQLITE_OPEN_CREATE, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	sqlstr = "select fts3_tokenizer(:tokenizer_name, :tokenizer_ptr)";
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_name");
	rc = sqlite3_bind_text(stmt, idx, "stopword_tokenizer", -1, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	
	sqlite3Fts3PorterTokenizerModule((const sqlite3_tokenizer_module **)&stopword_tokenizer_module);
	idx = sqlite3_bind_parameter_index(stmt, ":tokenizer_ptr");
	rc = sqlite3_bind_blob(stmt, idx, &stopword_tokenizer_module, sizeof(stopword_tokenizer_module), SQLITE_STATIC);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		fprintf(stderr, "%s tokenizer error\n", sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return -1;
	}
	sqlite3_finalize(stmt);
	
/*------------------------ Build the mandb table------------------------------ */

	sqlstr = "create virtual table mandb using fts4(section, name, "
	"name_desc, desc, lib, synopsis, return_vals, env, files, exit_status, diagnostics,"
	" errors, compress=zip, uncompress=unzip, tokenize=stopword_tokenizer )";
	
	rc = sqlite3_prepare_v2(db, sqlstr, -1, &stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%s yo\n", sqlite3_errmsg(db));
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
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		sqlite3_shutdown();
		return -1;
	}
	
	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "%s\n", sqlite3_errmsg(db));
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

/*
* mdoc_parse_section--
*  Utility function for parsing sections of the mdoc type pages.
*  Takes two params:
*   1. sec is an enum which indicates the section in which we are parsing presently
*   2. string is the string which we need to append to the buffer for this particular
*      section.
*  The function appends string to the global section buffer and returns.
*/
static void
mdoc_parse_section(enum mdoc_sec sec, const char *string)
{
	switch (sec) {
		case SEC_LIBRARY:
			concat(&lib, string);
			break;
		case SEC_SYNOPSIS:
			concat(&synopsis, string);
			//fprintf(stderr, "syn: %s\n", n->string);
			break;
		case SEC_RETURN_VALUES:
			concat(&return_vals, string);
			break;
		case SEC_ENVIRONMENT:
			concat(&env, string);
			break;
		case SEC_FILES:
			concat(&files, string);
			break;
		case SEC_EXIT_STATUS:
			concat(&exit_status, string);
			break;
		case SEC_DIAGNOSTICS:
			concat(&diagnostics, string);
			break;
		case SEC_ERRORS:
			concat(&errors, string);
			break;
		case SEC_NAME:
			break;
		case SEC_DESCRIPTION:
			concat(&desc, string);
			break;
		default:
			break;
	}
}
