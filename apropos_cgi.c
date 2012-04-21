/*-
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"

typedef struct callback_data {
	int count;
} callback_data;
static const char *HTMLTAB = "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;";

/*
 * print_form --
 *   Generates the beginning HTML body of the search form.
 */   
static void
print_form(char *query)
{
	printf("<html>\n");
	printf("<head>\n");
	printf("<title> NetBSD apropos </title>\n");
	printf("</head>\n");
	printf("<body>\n");
	printf("<center>\n");
	printf("<img src=\"http://netbsd.org/images/NetBSD.png\" height=\"200\" "
			"width=\"200\" />");
	printf("<table style=\"%s\">\n", "margin:10px;>\n"); 
	printf("<form action=\"/cgi-bin/apropos.cgi\">\n");
	printf("<tr >\n");
	printf("<td> <input type=\"text\" name=\"q\" value=\"%s\" size=\"30\"></td>\n",
			query ? query : "" );
	printf("<td> <input type=\"submit\" value=\"Search\"> </td>\n");
	printf("</tr>\n");
	printf("</table>");

}

/*
 * Replaces all the occurrences of '+' in the given string with a space
 */
static char *
parse_space(char *str)
{
	if (str == NULL)
		return str;

	char *iter = str;
	while ((iter = strchr(iter, '+')) != NULL)
		*iter = ' ';
	return str;
}

static char *
parse_hex(char *str)
{
	char *percent_ptr;
	char hexcode[2];
	char *retval;
	retval = malloc(strlen(str) + 1);
	int i = 2;
	int decval = 0;
	size_t offset = 0;
	size_t sz;
	while ((percent_ptr = strchr(str, '%')) != NULL) {
		i = 2;
		sz = 0;
		sz = percent_ptr - str;
		decval = 0;
		memcpy(retval + offset, str, sz);
		percent_ptr++;
		offset += sz;
		while (i > 0) {
			switch (*percent_ptr) {
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				decval += (*percent_ptr - 48) * pow(16, --i);
				break;
			case 'A':
				decval += 10 * pow(16, --i);
				break;
			case 'B':
				decval += 11 * pow(16, --i);
				break;
			case 'C':
				decval += 12 * pow(16, --i);
				break;
			case 'D':
				decval += 13 * pow(16, --i);
				break;
			case 'E':
				decval += 14 * pow(16, --i);
				break;
			case 'F':
				decval += 15 * pow(16, --i);
				break;
			}
			percent_ptr++;
		}
		str = percent_ptr;
		retval[offset++] = decval;
	}
	sz =  strlen(str);
	memcpy(retval + offset, str, sz + 1);
	return retval;
}

/*
 * Parse the given query string to extract the parameter pname
 * and return to the caller. (Not the best way to do it but
 * going for simplicity at the moment.)
 */
static char *
get_param(char *qstr, char *pname)
{
	if (qstr == NULL)
		return NULL;

	char *temp;
	char *param = NULL;
	char *value = NULL;
	size_t sz;
	while (*qstr) {
		sz = strcspn(qstr, "=&");
		if (qstr[sz] == '=') {
			qstr[sz] = 0;
			param = qstr;
		}
		qstr += sz + 1;
		
		if (param && strcmp(param, pname) == 0)
			break;
		else
			param = NULL;
	}
	if (param == NULL) 
		return NULL;

	if ((temp = strchr(qstr, '&')) == NULL)
		value = strdup(qstr);
	else {
		sz = temp - qstr;
		value = malloc(sz + 1);
		if (value == NULL)
			errx(EXIT_FAILURE, "malloc failed");
		memcpy(value, qstr, sz);
		value[sz] = 0;
	}
	value = parse_space(value);
	char *retval = parse_hex(value);
	free(value);
	return retval;
}


static int
query_callback(void *data, const char *section, const char *name,
	const char *name_desc, const char *snippet, size_t snippet_length)
{
	printf("<div style=\"%s\">\n<tr>\n", "margin:20px; width: 60%");
	printf("<td> <a href=\"/man/%s.html\">%s(%s) </a> %s%s</tr><tr><td>%s</tr> "
			"<tr></tr></div>", name, name, section, HTMLTAB, name_desc, 
			snippet);
	callback_data *cbdata = (callback_data *) data;
	cbdata->count++;
	return 0;
}

static void
search(sqlite3 *db, char *query, struct callback_data *cbdata, int page)
{
	char *errmsg;
	query_args args;
	args.search_str = query;
	args.sec_nums = NULL;
	args.nrec = 10;
	args.offset = (page - 1) * 10;
	args.machine = NULL;
	args.callback = &query_callback;
	args.callback_data = cbdata;
	args.errmsg = &errmsg;
	printf("<table cellspacing=\"5px\" cellpadding=\"2px\" style=\"%s\">",
			"align:left; margin:15px; width:65%; padding:10px;");
	cbdata->count = 0;
	run_query_html(db, &args);
	printf("</table>");
	printf("<div><h3>\n");
}

int
main(int argc, char *argv[])
{
	int page;
	int spell_correct = 0;
	struct callback_data cbdata;
	printf("Content-type:text/html;\n\n");
	char *qstr = getenv("QUERY_STRING");
	char *errmsg;
	
	sqlite3 *db = init_db(MANDB_READONLY);
	if (db == NULL) {
		printf("Could not open database connection\n");
		exit(EXIT_FAILURE);
	}
	char *query = get_param(qstr, "q");
	query = remove_stopwords(lower(query));
	
	char *temp;
	char *str = query;
	while ((temp = strstr(str, "and")) || (temp = strstr(str, "not"))
			|| (temp = strstr(str, "or"))) {
		if (*(temp -1) == ' ') {
			switch (temp[0]) {
			case 'a':
				temp[0] = 'A';
				temp[1] = 'N';
				temp[2] = 'D';
				break;

			case 'n':
				temp[0] = 'N';
				temp[1] = 'O';
				temp[2] = 'T';
				break;
			case 'o':
				temp[0] = 'O';
				temp[1] = 'R';
				break;
			}
		}
		str = temp + 1;
	}
	char *p = get_param(qstr, "p");
	if (p == NULL)
		page = 1;
	else
		page = atoi(p);
	print_form(query);	
	search(db, query, &cbdata, page);

	char *correct_query;
	char *term;
	char *correct;
	int spell_flag = 0;
	if (spell_correct == 0 && cbdata.count == 0) {
		correct_query = NULL;
		spell_correct = 1;
		for (term = strtok(query, " "); term; term = strtok(NULL, " ")) {
			if ((correct = spell(db, term))) {
				spell_flag = 1;
				concat(&correct_query, correct);
			}
			else
				concat(&correct_query, term);
		}
		if (spell_flag) {
			printf("<h4>Did you mean %s ?</h2>\n", correct_query);
			search(db, correct_query, &cbdata, page);
		}
		
/*		warnx("No relevant results obtained.\n"
			  "Please make sure that you spelled all the terms correctly "
			  "or try using better keywords.");*/
		free(correct_query);
	}
	/* If 10 results were displayed then there might be more results,
	 * so display a link for Next page. 
	 */
	if (cbdata.count == 10)
		printf("<a href=\"/cgi-bin/apropos.cgi?q=%s&p=%d\"> Next </a>\n",
				query, page + 1);

	/* If we are on Page 2 or onwards, display a link for Previous page as well. */
	if (page > 1) {
		printf("%s\n", HTMLTAB);
		printf("<a href=\"/cgi-bin/apropos.cgi?q=%s&p=%d\"> Previous </a>\n"
				"</h3>\n",
				query, page - 1);
	}

	printf("</h3></div>\n");
	printf("</center>\n");
	close_db(db);
	free(query);
	printf("</body>\n");
	printf("</html>");
	return 0;
}

