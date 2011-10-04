/*-
 * Copyright (c) 2011 Abhinav Upadhyay <er.abhinav.upadhyay@gmail.com>
 * All rights reserved.
 *
 * This code was developed as part of Google's Summer of Code 2011 program.
 * Thanks to Google for sponsoring.
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


#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "apropos-utils.h"
#include "mongoose.h"

#define BUFLEN 512

typedef struct apropos_data {
	struct mg_connection *conn;
	int count;
} apropos_data;

static const char *standard_reply = "HTTP/1.1 200 OK\r\n"
  "Content-Type: text/html\r\n"
  "Connection: close\r\n\r\n";

static const char *html_start_template = "<html>\n<head>\n<title>\napropos\n</title>"
	"<style type = \"text/css\">\n"
	"table.results { width: 60%; margin: 10px; padding: 0px\n}"
	"div.results {margin: 10px; padding: 5px}\n"
	"</style>\n"
	"</head>\n"
	"<body>\n"
	"<center>\n"
	"<div>\n"
	"<table style = \"margin: 10px;\">\n"
	"<tr> \n"
	"<td colspan = \"2\"> <img src = \"./images/NetBSD.png\" height = \"200\" width = \"200\" />\n"
	"</td>\n";
	
static const char *html_end_template = "</div>\n"
	"</center>\n"
	"</body>\n"
	"</html>";

static int
apropos_callback(void *data, int ncol, char **col_values, char **col_names)
{
	const char *tab = "&nbsp;&nbsp;&nbsp;&nbsp;";
	char *section =  col_values[0];
	char *name = col_values[1];
	char *name_desc = col_values[2];
	char *snippet = col_values[3];
	apropos_data *ap_data = (apropos_data *) data;
	ap_data->count++;
	mg_printf(ap_data->conn, "<div>\n"
				"<tr>\n\t"
				"<td><b><a href=\"/html%s/%s.html\">%s(%s)</a></b>%s%s %s</td>\n"
				"</tr>\n"
				"<tr>\n\t"
				"<td>%s</td>\n"
				"</tr>\n"
				"</div>", section, name, name, section, tab, tab, 
				name_desc, snippet);
	return 0;
}

static void
run_apropos(struct mg_connection *conn, sqlite3 *db, char *query, int page)
{
	apropos_data ap_data;
	query_args args;
	char *errmsg = NULL;
	char *word;
	char *correct_query = NULL;
	char *correct = NULL;
	int flag = 0;
	ap_data.conn = conn;
	ap_data.count = 0;
	
	if (query) {
		mg_printf(conn, "<table  cellspacing = \"10px\" class = \"results\">\n");
		db = init_db(DB_WRITE);
		args.search_str = query;
		args.sec_nums = NULL;
		args.nrec = page * 10;
		args.offset = args.nrec - 10;
		args.callback = &apropos_callback;
		args.callback_data = &ap_data;
		args.errmsg = &errmsg;

		if (run_query_html(db, &args) < 0) {
			mg_printf(conn, "<h3>SQL Error</h3>\n"
						"</table>");
			return;
		}
		if (errmsg != NULL) {
			mg_printf(conn, "An error occurred while executing the "
					"query\n </table>");
			free(errmsg);
			return ;
		}
		if (ap_data.count == 0) {
			for (word = strtok(query, " "); word; 
				word = strtok(NULL, " ")) {
				correct = spell(db, word);
				if (correct) {
					concat(&correct_query, correct, -1);
					flag = 1;
					free(correct);
				}
				else
					concat(&correct_query, word, -1);
			}
			
			if (flag) {
				mg_printf(conn, "<h3>Did you mean \"%s\" ?\n", 
							correct_query);
				run_apropos(conn, db, correct_query, page);
			}
			else
				mg_printf(conn, "<h3> No results</h3>\n");
		}
		else if (ap_data.count == 10)			
			mg_printf(conn, "<div>\n"
					"<tr colspan=\"2\">\n\t"
					"<td>\n\t<h3>"
					"<a href=\"/apropos?q=%s&p=%d\">Next</a>"
					"</h3>\n</td>"
					"</tr>"
					"</div>", query, ++page);
		mg_printf(conn, "</table>\n");
		close_db(db);
	}
				
	mg_printf(conn, "%s", html_end_template);
	return ;
}



static void *
callback(enum mg_event event, struct mg_connection *conn,
		const struct mg_request_info *request_info)
{
	char query[BUFLEN] = {0};
	char p[5];	//I doubt there will ever be more than 99,999 result pages
	int page = 1;
	
	sqlite3 *db;
	assert(request_info);

	if (event == MG_NEW_REQUEST ) {
		
		if (strcmp(request_info->uri, "/apropos") == 0 ) {
			if (request_info->query_string && 
				mg_get_var(request_info->query_string, 
				strlen(request_info->query_string), 
				"q", query, BUFLEN) == -1)
			return NULL;

			if (request_info->query_string && 
				mg_get_var(request_info->query_string, 
				strlen(request_info->query_string),
				"p", p, 5) != -1) {
				page = atoi(p);
			}
			
			mg_printf(conn, "%s", standard_reply);
			mg_printf(conn, "%s", html_start_template);
			mg_printf(conn,	"<form action=\"/apropos\">\n"
							"<tr>\n"
							"<td>\n"
							"<input type = \"text\" name = \"q\" value = \"%s\">\n"
							"</td>\n"
							"<td>\n"
							"<input type=\"submit\" value = \"Search\"> </td>\n"
							"</tr>\n"
							"</form>\n"
							"</table>\n"
							"</div>\n",
					strlen(query)?query:"");
			if (query) {
				db = init_db(DB_WRITE);
				run_apropos(conn, db, query, page);
				close_db(db);
			}
			
		else
			return (void *)NULL;
		}
	return (void *)NULL;
	}
	return (void *) NULL;
}

int
main(void)
{
	struct mg_context *ctx;
	const char *options[] = {"document_root", "./www,"
											"/html1=/usr/share/man/html1,"
											"/html2=/usr/share/man/html2,"
											"/html3=/usr/share/man/html3,"
											"/html4=/usr/share/man/html4,"
											"/html5=/usr/share/man/html5,"
											"/html6=/usr/share/man/html6,"
											"/html7=/usr/share/man/html7,"
											"/html8=/usr/share/man/html8,"
											"/html9=/usr/share/man/html9,",
							"listening_ports", "8080",
							NULL};
	ctx = mg_start(&callback, NULL, options);
	getchar();	// Wait until user hits "enter"
	mg_stop(ctx);
	return 0;
}
