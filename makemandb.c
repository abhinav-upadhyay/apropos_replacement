#include <sys/stat.h>

#include <dirent.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXLINE 1024	//buffer size for fgets

static void traversedir(const char *);
static void parsemanpage(const char *);

int
main(int argc, char *argv[])
{
	FILE *file;
	char line[MAXLINE];
	
	/* call man -p to get the list of man page dirs */
	if ((file = popen("man -p ", "r")) == NULL)
		err(EXIT_FAILURE, "fopen failed");
	
	while (fgets(line, MAXLINE, file) != NULL) {
		/* remove the new line character from the string */
		line[strlen(line) - 1] = '\0';
		traversedir(line);
	}
	
	if (pclose(file) == -1)
		err(EXIT_FAILURE, "pclose error");
	
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
		printf("%s\n", file);
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
parsemanpage(const char *file)
{
	//TODO Need to write code for parsing man pages here using mandoc(3).
	// Right now just printing the file name on the screen
	printf("%s\n", file);
}	
