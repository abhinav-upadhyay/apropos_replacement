#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mandoc.h"
#include "mdoc.h"

#define MAXLINE 1024	//buffer size for fgets

static	void pmdoc(const char *);
static void pmdoc_node(const struct mdoc_node *);
static void pmdoc_Nm(const struct mdoc_node *);
static void pmdoc_Nd(const struct mdoc_node *);
static void pmdoc_Sh(const struct mdoc_node *);
static void traversedir(const char *);

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
	NULL,//pmdoc_Fd, /* Fd */
	NULL, /* Fl */
	NULL,//pmdoc_Fn, /* Fn */ 
	NULL, /* Ft */ 
	NULL, /* Ic */ 
	NULL,//pmdoc_In, /* In */ 
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
	/* call man -p to get the list of man page dirs */
	if ((file = popen("/home/abhinav/development/man_printmanpath_using_glob/man -p", "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed");
	
	while (fgets(line, MAXLINE, file) != NULL) {
		/* remove the new line character from the string */
		line[strlen(line) - 1] = '\0';
		traversedir(line);
		
	}
	
	if (pclose(file) == -1)
		err(EXIT_FAILURE, "pclose error");
	mparse_free(mp);
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
	if (stat(file, &sb) < 0)
		err(EXIT_FAILURE, "stat failed");
	
	/* if it is a regular file, pass it to the parser */
	if (S_ISREG(sb.st_mode)) {
		printf("parsing %s\n", file);
		pmdoc(strdup(file));
		return;
	}
	
	/* if it is a directory, traverse it recursively */
	else if (S_ISDIR(sb.st_mode)) {
		if ((dp = opendir(file)) == NULL)
			err(EXIT_FAILURE, "opendir error");
		
		while ((dirp = readdir(dp)) != NULL) {
			/* Avoid . and .. entries in a directory */
			if (strncmp(dirp->d_name, ".", 1)) {
				if ((asprintf(&buf, "%s/%s", file, dirp->d_name) == -1)) {
					closedir(dp);
					err(EXIT_FAILURE, "memory allocation error");
				}
				traversedir(buf);
				free(buf);
			}
		}
	
		closedir(dp);
	}
	else
		err(EXIT_FAILURE, "unknown file type");
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

	if (mp == NULL)
		errx(EXIT_FAILURE, "mparse_alloc failed");

	if (mparse_readfd(mp, -1, file) >= MANDOCLEVEL_FATAL) {
			fprintf(stderr, "%s: Parse failure\n", file);
			return;
	}

	mparse_result(mp, &mdoc, NULL);
		if (NULL == mdoc)
			return;

	pmdoc_node(mdoc_node(mdoc));
}

static void
pmdoc_node(const struct mdoc_node *n)
{
	
	if (NULL == n)
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
		if (NULL == mdocs[n->tok])
			break;

		(*mdocs[n->tok])(n);
		
	default:
		break;
	}

	pmdoc_node(n->child);
	pmdoc_node(n->next);
}

static void
pmdoc_Nm(const struct mdoc_node *n)
{
	
	if (n->sec == SEC_NAME && n->child->type == MDOC_TEXT) {
			printf("%s\n", n->child->string);
	}
	
}

static void
pmdoc_Nd(const struct mdoc_node *n)
{
	char *buf = NULL;
	
	for (n = n->child; n; n = n->next) {
		if (n->type != MDOC_TEXT)
			continue;

		if (buf == NULL)
			buf = strdup(n->string);
		else
			asprintf(&buf, "%s %s", buf, n->string);
	}
	if (buf) {
		printf("%s\n", buf);
		free(buf);
	}
}

static void
pmdoc_Sh(const struct mdoc_node *n)
{
	if (n->sec == SEC_DESCRIPTION) {
		for(n = n->child; n; n = n->next) {
			if (n->type == MDOC_TEXT) {
				printf("%s ", n->string);
			}
			else { 
				pmdoc_Sh(n);
			}
		}
	}
}
	
