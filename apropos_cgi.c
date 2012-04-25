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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "cgi-utils.h"

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
	printf("<script type=\"text/javascript\" src=\"/jquery.js\"></script>\n");
	printf("<script type=\"text/JavaScript\" src=\"/jquery.autocomplete.js\"></script>\n");
	printf("<script type=\"text/javascript\" src=\"/ac.js\"></script>\n");	
	printf("<link href=\"/ac.css\" rel=\"stylesheet\" type=\"text/css\" />\n");
	printf("</head>\n");
	printf("<body>\n");
	printf("<center>\n");
	printf("<img src=\"http://netbsd.org/images/NetBSD.png\" height=\"200\" "
			"width=\"200\" />");
	printf("<table style=\"%s\">\n", "margin:10px;>\n"); 
	printf("<form action=\"/cgi-bin/apropos.cgi\">\n");
	printf("<tr >\n");
	printf("<td> <input type=\"text\" name=\"q\" value=\"%s\" size=\"30\" id=\"query\"></td>\n",
			query ? query : "" );
	printf("<td> <input type=\"submit\" value=\"Search\"> </td>\n");
	printf("</tr>\n");
	printf("</table>");

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
	lower(query);
	query = remove_stopwords(query);
	build_boolean_query(query);
	
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

