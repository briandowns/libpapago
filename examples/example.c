/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Brian J. Downs
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
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../papago.h"

static papago_t *server = NULL;

/**
 * Signal handler for graceful shutdown.
 */
static void
signal_handler(int sig)
{
	PAPAGO_UNUSED(sig);

	printf("\nShutting down...\n");
	if (server != NULL) {
		papago_stop(server);
	}
}

// HTTP route handlers

void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	papago_res_send(res,
	    "<h1>Welcome to Papago!</h1>"
	    "<p>Built on libmicrohttpd + libwebsockets</p>");
}

void
api_hello_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	papago_res_json(res, "{\"message\":\"Hello from Papago!\"}");
}

void
user_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

	const char *username;
	char json[256];

	username = papago_req_param(req, "username");

	snprintf(json, sizeof(json), "{\"username\":\"%s\",\"id\":123}",
	    (username != NULL) ? username : "unknown");

	papago_res_json(res, json);
}

// websocket handlers

void
ws_on_connect(papago_ws_connection_t *conn)
{
	printf("[WS] client connected from %s\n", papago_ws_get_client_ip(conn));
	papago_ws_send(conn, "{\"type\":\"welcome\",\"message\":\"Connected!\"}");
}

void
ws_on_message(papago_ws_connection_t *conn, const char *message, size_t length,
	          bool is_binary)
{
	printf("[WS] received %s message (%zu bytes): %s\n",
	    is_binary ? "binary" : "text", length, message);

	// echo message back
	if (is_binary) {
		papago_ws_send_binary(conn, message, length);
	} else {
		papago_ws_send(conn, message);
	}
}

void
ws_on_close(papago_ws_connection_t *conn)
{
	printf("[WS] client disconnected from %s\n",
	    papago_ws_get_client_ip(conn));
}

void
ws_on_error(papago_ws_connection_t *conn, const char *error)
{
	PAPAGO_UNUSED(conn);
	printf("[WS] error: %s\n", error);
}

int
main(void)
{
	// setup signal handling
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// create server
	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	papago_config_t config = papago_default_config();
	config.port = 8282;
	config.enable_logging = true;
	papago_configure(server, &config);

	// register HTTP routes
	papago_get(server, "/", index_handler, NULL);
	papago_get(server, "/api/hello", api_hello_handler, NULL);
	papago_get(server, "/user/:username", user_handler, NULL);

	// register websocket endpoint
	papago_ws_endpoint(server, "/ws",
	    ws_on_connect, ws_on_message, ws_on_close, ws_on_error);

	printf("Server starting on:\n");
	printf("  HTTP:      http://%s:%d\n", config.host, config.port);
	printf("  WebSocket: ws://%s:%d/ws\n\n", config.host, config.port + 1);

	printf("Run\n\ncurl http://%s:%d/\n", config.host, config.port);
	printf("curl http://%s:%d/api/hello\n", config.host, config.port);
	printf("curl http://%s:%d/user/alice\n\n", config.host, config.port);

	// start server (blocking)
	if (papago_start(server) != 0) {
        fprintf(stderr, "%s\n", papago_error(server));
        papago_destroy(server);

        return 1;
    }

	// cleanup
	papago_destroy(server);

	return 0;
}
