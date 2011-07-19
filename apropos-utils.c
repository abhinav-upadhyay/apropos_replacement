#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "sqlite3.h"

void
zip(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{	
	const Bytef *source = sqlite3_value_text(apval[0]);
	uLong sourcelen = strlen((const char *)source);
	uLong destlen = (sourcelen + 12) + (int)(sourcelen + 12) * .01/100;
	Bytef *dest = (Bytef *) malloc(sizeof(Bytef) * destlen);
	int ret_val = compress(dest, &destlen, source, sourcelen);
	if (ret_val != Z_OK) {
		sqlite3_result_error(pctx, "Error in compression", -1);
	}
	sqlite3_result_text(pctx, (const char *)dest, -1, NULL);
	return;
}


void
unzip(sqlite3_context *pctx, int nval, sqlite3_value **apval)
{	
	const Bytef *source = (const Bytef *) sqlite3_value_text(apval[0]);
	uLong sourcelen = strlen((const char *)source);
	uLong destlen = (sourcelen + 12) + (int)(sourcelen + 12) * .01/100;
	Bytef *dest = (Bytef *) malloc(sizeof(Bytef) * destlen);
	int ret_val = uncompress(dest, &destlen, source, sourcelen);
	if (ret_val != Z_OK) {
		sqlite3_result_error(pctx, "Error in compression", -1);
	}
	sqlite3_result_text(pctx, (const char *)dest, -1, NULL);
	return;
}

char *
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

/*
* concat--
*  Utility function. Concatenates together: dst, a space character and src. 
* dst + " " + src 
*/
int
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

