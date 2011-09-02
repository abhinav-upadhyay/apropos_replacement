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

static const char *html_template = "<html>\n<head>\n<title>\napropos\n</title>"
	"</head>\n<body>\n";

static int
apropos_callback(void *data, int ncol, char **col_values, char **col_names)
{
	apropos_data *ap_data = (apropos_data *) data;
	ap_data->count++;
	mg_printf(ap_data->conn, "%s\n", col_values[0]);
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
		fprintf(stderr, "%s\n", request_info->uri);
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
			run_query_html(db, &args);
			close_db(db);
			mg_printf(conn, "</body></html>");
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
