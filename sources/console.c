
/*
 * Odissey.
 *
 * Advanced PostgreSQL connection pooler.
*/

#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <ctype.h>
#include <signal.h>

#include <machinarium.h>
#include <shapito.h>

#include "sources/macro.h"
#include "sources/version.h"
#include "sources/atomic.h"
#include "sources/list.h"
#include "sources/pid.h"
#include "sources/id.h"
#include "sources/log_file.h"
#include "sources/log_system.h"
#include "sources/logger.h"
#include "sources/daemon.h"
#include "sources/scheme.h"
#include "sources/scheme_mgr.h"
#include "sources/config.h"
#include "sources/msg.h"
#include "sources/system.h"
#include "sources/instance.h"
#include "sources/server.h"
#include "sources/server_pool.h"
#include "sources/client.h"
#include "sources/client_pool.h"
#include "sources/route_id.h"
#include "sources/route.h"
#include "sources/route_pool.h"
#include "sources/io.h"
#include "sources/router.h"
#include "sources/pooler.h"
#include "sources/relay.h"
#include "sources/frontend.h"
#include "sources/backend.h"
#include "sources/console.h"
#include "sources/parser.h"

typedef struct
{
	od_consolestatus_t status;
	od_client_t *client;
	char *request;
	int request_len;
	machine_queue_t *response;
} od_msgconsole_t;

enum
{
	OD_LSHOW,
	OD_LSTATS,
	OD_LSERVERS
};

static od_keyword_t od_console_keywords[] =
{
	od_keyword("show",    OD_LSHOW),
	od_keyword("stats",   OD_LSTATS),
	od_keyword("servers", OD_LSERVERS),
	{ 0, 0, 0 }
};

static inline int
od_console_show_stats_describe(shapito_stream_t *stream)
{
	int offset;
	offset = shapito_be_write_row_description(stream);

	int rc;
	rc = shapito_be_write_row_description_add(stream, offset, "database", 8,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "total_requests", 14,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "total_received", 14,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "total_sent", 10,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "total_query_time", 16,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "avg_req", 7,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "avg_recv", 8,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "avg_sent", 8,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "avg_query", 9,
	                                          0, 0, 25, 8, 0, 0);
	if (rc == -1)
		return -1;

	return 0;
}

static inline int
od_console_show_stats_add(shapito_stream_t *stream,
                          char *database,
                          int   database_len,
                          od_serverstat_t *total, od_serverstat_t *avg)
{
	int offset;
	offset = shapito_be_write_data_row(stream);

	int rc;
	rc = shapito_be_write_data_row_add(stream, offset, database, database_len);
	if (rc == -1)
		return -1;

	char data[64];
	int  data_len;

	/* total_requests */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, total->count_request);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* total_received */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, total->recv_client);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* total_sent */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, total->recv_server);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* total_query_time */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, total->query_time);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* avg_req */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, avg->count_request);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* avg_recv */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, avg->recv_client);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* avg_sent */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, avg->recv_server);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	/* avg_query */
	data_len = snprintf(data, sizeof(data), "%" PRIu64, avg->query_time);
	rc = shapito_be_write_data_row_add(stream, offset, data, data_len);
	if (rc == -1)
		return -1;

	return 0;
}

static int
od_console_show_stats_callback(char *database,
                               int   database_len,
                               od_serverstat_t *total,
                               od_serverstat_t *avg, void *arg)
{
	od_client_t *client = arg;
	return od_console_show_stats_add(&client->stream,
	                                 database, database_len,
	                                 total, avg);
}

static inline int
od_console_show_stats(od_client_t *client)
{
	od_router_t *router = client->system->router;
	shapito_stream_t *stream = &client->stream;
	shapito_stream_reset(stream);
	int rc;
	rc = od_console_show_stats_describe(stream);
	if (rc == -1)
		return -1;
	rc = od_routepool_stats(&router->route_pool,
	                        od_console_show_stats_callback,
	                        client);
	if (rc == -1)
		return -1;
	shapito_be_write_complete(stream, "SHOW", 4);
	shapito_be_write_ready(stream, 'I');
	return 0;
}

static inline int
od_console_show_servers_describe(shapito_stream_t *stream)
{
	int offset;
	offset = shapito_be_write_row_description(stream);
	int rc;
	rc = shapito_be_write_row_description_add(stream, offset, "type", 4,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "user", 4,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "database", 8,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "state", 5,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "addr", 4,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "port", 4,
	                                          0, 0, 23, 4, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "local_addr", 10,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "local_port", 10,
	                                          0, 0, 23, 4, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "connect_time", 12,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "request_time", 12,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "ptr", 3,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "link", 4,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "remote_pid", 10,
	                                          0, 0, 23, 4, 0, 0);
	if (rc == -1)
		return -1;
	rc = shapito_be_write_row_description_add(stream, offset, "tls", 3,
	                                          0, 0, 20, -1, 0, 0);
	if (rc == -1)
		return -1;
	return 0;
}


static inline int
od_console_show_servers(od_client_t *client)
{
	od_router_t *router = client->system->router;
	shapito_stream_t *stream = &client->stream;
	shapito_stream_reset(stream);
	int rc;
	rc = od_console_show_servers_describe(stream);
	if (rc == -1)
		return -1;
	(void)router;
	shapito_be_write_complete(stream, "SHOW", 4);
	shapito_be_write_ready(stream, 'I');
	return 0;
}

static inline int
od_console_query_show(od_client_t *client, od_parser_t *parser)
{
	od_token_t token;
	int rc;
	rc = od_parser_next(parser, &token);
	switch (rc) {
	case OD_PARSER_KEYWORD:
		break;
	case OD_PARSER_EOF:
	default:
		return -1;
	}
	od_keyword_t *keyword;
	keyword = od_keyword_match(od_console_keywords, &token);
	if (keyword == NULL)
		return -1;
	switch (keyword->id) {
	case OD_LSTATS:
		return od_console_show_stats(client);
	case OD_LSERVERS:
		return od_console_show_servers(client);
	}
	return -1;
}

static inline int
od_console_query(od_console_t *console, od_msgconsole_t *msg_console)
{
	od_instance_t *instance = console->system->instance;
	od_client_t *client = msg_console->client;
	int rc;

	uint32_t query_len;
	char *query;
	rc = shapito_be_read_query(&query, &query_len, msg_console->request,
	                           msg_console->request_len);
	if (rc == -1)
		goto bad_command;

	od_debug_client(&instance->logger, &client->id, "console",
	                "%.*s", query_len, query);

	od_parser_t parser;
	od_parser_init(&parser, query, query_len);

	od_token_t token;
	rc = od_parser_next(&parser, &token);
	switch (rc) {
	case OD_PARSER_KEYWORD:
		break;
	case OD_PARSER_EOF:
	default:
		goto bad_command;
	}
	od_keyword_t *keyword;
	keyword = od_keyword_match(od_console_keywords, &token);
	if (keyword == NULL)
		goto bad_command;
	switch (keyword->id) {
	case OD_LSHOW:
		rc = od_console_query_show(client, &parser);
		if (rc == -1)
			goto bad_command;
		break;
	default:
		goto bad_command;
	}

	return 0;

bad_command:
	od_error_client(&instance->logger, &client->id, "console",
	                "bad console command");
	shapito_stream_reset(&client->stream);
	od_frontend_errorf(client, SHAPITO_SYNTAX_ERROR, "bad console command");
	shapito_be_write_ready(&client->stream, 'I');
	return -1;
}

static void
od_console(void *arg)
{
	od_console_t *console = arg;
	od_instance_t *instance = console->system->instance;
	(void)instance;

	for (;;) {
		machine_msg_t *msg;
		msg = machine_queue_get(console->queue, UINT32_MAX);
		if (msg == NULL)
			break;
		od_msg_t msg_type;
		msg_type = machine_msg_get_type(msg);
		switch (msg_type) {
		case OD_MCONSOLE_REQUEST:
		{
			od_msgconsole_t *msg_console;
			msg_console = machine_msg_get_data(msg);
			int rc;
			rc = od_console_query(console, msg_console);
			if (rc == -1) {
				msg_console->status = OD_CERROR;
			} else {
				msg_console->status = OD_COK;
			}
			machine_queue_put(msg_console->response, msg);
			break;
		}
		default:
			assert(0);
			break;
		}
	}
}

int od_console_init(od_console_t *console, od_system_t *system)
{
	od_instance_t *instance = system->instance;
	console->system = system;
	console->queue = machine_queue_create();
	if (console->queue == NULL) {
		od_error(&instance->logger, "console", "failed to create queue");
		return -1;
	}
	return 0;
}

int od_console_start(od_console_t *console)
{
	od_instance_t *instance = console->system->instance;
	int64_t coroutine_id;
	coroutine_id = machine_coroutine_create(od_console, console);
	if (coroutine_id == -1) {
		od_error(&instance->logger, "console", "failed to start console coroutine");
		return -1;
	}
	return 0;
}

static od_consolestatus_t
od_console_do(od_client_t *client, od_msg_t msg_type, char *request, int request_len,
              int wait_for_response)
{
	od_console_t *console = client->system->console;

	/* send request to console */
	machine_msg_t *msg;
	msg = machine_msg_create(msg_type, sizeof(od_msgconsole_t));
	if (msg == NULL)
		return OD_CERROR;
	od_msgconsole_t *msg_console;
	msg_console = machine_msg_get_data(msg);
	msg_console->status = OD_CERROR;
	msg_console->client = client;
	msg_console->request = request;
	msg_console->request_len = request_len;
	msg_console->response = NULL;

	/* create response queue */
	machine_queue_t *response;
	if (wait_for_response) {
		response = machine_queue_create();
		if (response == NULL) {
			machine_msg_free(msg);
			return OD_CERROR;
		}
		msg_console->response = response;
	}
	machine_queue_put(console->queue, msg);

	if (! wait_for_response)
		return OD_COK;

	/* wait for reply */
	msg = machine_queue_get(response, UINT32_MAX);
	if (msg == NULL) {
		/* todo:  */
		abort();
		machine_queue_free(response);
		return OD_CERROR;
	}
	msg_console = machine_msg_get_data(msg);
	od_consolestatus_t status;
	status = msg_console->status;
	machine_queue_free(response);
	machine_msg_free(msg);
	return status;
}

od_consolestatus_t
od_console_request(od_client_t *client, char *request, int request_len)
{
	return od_console_do(client, OD_MCONSOLE_REQUEST, request, request_len, 1);
}
