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
#include <time.h>
#include <unistd.h>

#include <jansson.h>

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
	
	const char *html = 
	    "<html><body>"
	    "<h1>Prometheus Metrics Example</h1>"
	    "<h2>Test Endpoints:</h2>"
	    "<ul>"
	    "<li><a href='/slow'>/slow</a> (1 second delay)</li>"
	    "<li><a href='/error'>/error</a> (500 error)</li>"
	    "</ul>"
	    "<h2>Metrics:</h2>"
	    "<ul>"
	    "<li><a href='/metrics'>/metrics</a> (Prometheus format)</li>"
	    "<li><a href='/health'>/health</a> (Health check)</li>"
	    "</ul>"
	    "</body></html>";
	
	papago_res_send(res, html);
}

void
health_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    json_t *root = json_pack("{s:s, s:o}",
        "status", "UP",
        "timestamp", json_integer((json_int_t)time(NULL)));
	if (root == NULL) {
        fprintf(stderr, "failed to create JSON\n");
        papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        return;

    }
    char *result = json_dumps(root, 0);
    if (result == NULL) {
        fprintf(stderr, "failed to serialize JSON\n");
        papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        json_decref(root);
        return;
    }
    json_decref(root);

	papago_res_json(res, result);
	free(result);
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

void
slow_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);
	
	// simulate slow endpoint
	sleep(1);
	
	papago_res_status(res, PAPAGO_STATUS_OK);
	papago_res_send(res, "slow response completed");
}
 
void
error_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
	papago_res_send(res, "simulated server error");
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
	config.enable_logging = false;
	papago_configure(server, &config);

	// register HTTP routes
	papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
	papago_route(server, PAPAGO_GET, "/slow", slow_handler, NULL);
	papago_route(server, PAPAGO_GET, "/error", error_handler, NULL);

    // register observability endpoints
	papago_route(server, PAPAGO_GET, "/metrics", papago_metrics_handler, NULL);
	papago_route(server, PAPAGO_GET, "/health", health_handler, NULL);

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

