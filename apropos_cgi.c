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
	"</head>\n"
	"<body>\n"
	"<br><br><br><br><br>\n"
	"<center>\n"
	"<div>\n"
	"<table>\n"
	"<form action=\"/apropos\">\n"
	"<tr>\n"
	"<td> <input type = \"text\" name = \"q\"> </td>\n"
	"<td> <input type=\"submit\" value = \"Search\"> </td>\n"
	"</tr>\n"
	"</form>\n"
	"</table>\n"
	"</div>\n"
	"<div>\n";

static const char *html_end_template = "</div>\n"
	"</center>\n"
	"</body>\n"
	"</html>";

static int
apropos_callback(void *data, int ncol, char **col_values, char **col_names)
{
	apropos_data *ap_data = (apropos_data *) data;
	ap_data->count++;
	mg_printf(ap_data->conn, "%s", col_values[0]);
	return 0;
}

static void *
callback(enum mg_event event, struct mg_connection *conn,
		const struct mg_request_info *request_info)
{
	char query[BUFLEN];
	char *errmsg;
	query_args args;
	apropos_data ap_data;
	ap_data.conn = conn;
	ap_data.count = 0;
	sqlite3 *db;
	assert(request_info);

	if (event == MG_NEW_REQUEST ) {
		if (strcmp(request_info->uri, "/apropos") ==0 ) {
			mg_printf(conn, "%s", standard_reply);
			if (mg_get_var(request_info->query_string, strlen(request_info->query_string), 
				"q", query, BUFLEN) == -1)
				return NULL;

			db = init_db(DB_READONLY);
			args.search_str = query;
			args.sec_nums = NULL;
			args.nrec = 10;
			args.callback = &apropos_callback;
			args.callback_data = &ap_data;
			args.errmsg = &errmsg;
			mg_printf(conn, "%s", html_start_template);
			if (run_query_html(db, &args) < 0)
				mg_printf(conn, "<h3>SQL Error</h3>");
			if (ap_data.count == 0)
				mg_printf(conn, "<h3> No results</h3>\n");
			close_db(db);
			mg_printf(conn, "%s", html_end_template);
			return (void *)"";	// Mark as processed*/
		}
		else
			return (void *)NULL;
	}
	return NULL;
}

int
main(void)
{
	struct mg_context *ctx;
	const char *options[] = {"document_root", "./www", "listening_ports", "8080", NULL};
	ctx = mg_start(&callback, NULL, options);
	getchar();	// Wait until user hits "enter"
	mg_stop(ctx);
	return 0;
}
