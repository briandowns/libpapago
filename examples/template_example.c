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

void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	const char *html =
	    "<!DOCTYPE html>\n"
	    "<html>\n"
	    "<head><title>{{ name }} Template Example</title></head>\n"
	    "<body>\n"
	    "<h1>Welcome to the {{ name }} Template Example!</h1>\n"
	    "<p>This is a simple template example for the {{ name }} framework.</p>\n"
	    "</body>\n"
	    "</html>";

	char rendered[1024];
	uint8_t result = papago_res_render(res, html, rendered, sizeof(rendered),
		"name", "Papago");
	if (result != 0) {
		fprintf(stderr, "failed to render template\n");
		papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
		return;
	}
}

int
main(void)
{
	printf("papago template example (v%s)\n", papago_version());

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
	config.enable_template_rendering = true;
	papago_configure(server, &config);

	// register HTTP routes
	papago_get(server, "/", index_handler, NULL);

	printf("Server starting on:\n");
	printf("  HTTP:      http://%s:%d\n", config.host, config.port);

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

