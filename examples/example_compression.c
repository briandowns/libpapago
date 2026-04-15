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

#include <jansson.h>
#include <zlib.h>

#include "../papago.h"

static papago_t *server = NULL;

void
signal_handler(int sig)
{
	PAPAGO_UNUSED(sig);

	printf("\nShutting down...\n");
	if (server != NULL) {
        papago_stop(server);
    }
}


/*
 * Generate large JSON response for compression testing
 */
void
large_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(user_data);

	char *large_text = malloc(10000);
	if (large_text != NULL) {
		strcpy(large_text, "{\"data\":[");

		for (int i = 0; i < 100; i++) {
			char item[100];
			snprintf(item, sizeof(item), 
			    "{\"id\":%d,\"name\":\"Item %d\",\"value\":\"data\"}%s",
			    i, i, i < 99 ? "," : "");
			strcat(large_text, item);
		}
		strcat(large_text, "]}");
		
		printf("sending large text: %zu bytes\n", strlen(large_text));
		papago_res_send(res, large_text);
		free(large_text);
	} else {
        papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_json(res, "{\"error\":\"Failed to generate response\"}");
    }
}

/**
 * Small handler
 */
void
small_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(user_data);

	papago_res_send(res, "Small response");
}

/**
 * Info handler
 */
void
info_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(user_data);

    char info[512];
	snprintf(info, sizeof(info),
	    "Compression Test Server\n"
	    "======================\n\n"
	    "Compression:\n"
	    "Endpoints:\n"
	    "  /large - Large JSON response (~10KB)\n"
	    "  /small - Small response\n"
	    "  /info  - This page\n\n"
	    "Test compression:\n"
	    "  curl -i -H \"Accept-Encoding: gzip\" http://localhost:8080/large\n"
	);

	papago_res_send(res, info);
}

int
main(void)
{
	papago_config_t config;

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "Failed to create server\n");
		return 1;
	}

	config = papago_default_config();
	config.port = 8282;
    config.enable_compression = true;
	papago_configure(server, &config);

	papago_get(server, "/large", large_handler, NULL);
	papago_get(server, "/small", small_handler, NULL);
	papago_get(server, "/info", info_handler, NULL);
	papago_get(server, "/", info_handler, NULL);

	papago_start(server);
	papago_destroy(server);

	return 0;
}
