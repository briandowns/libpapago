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

#include "../papago.h"

static papago_t *server = NULL;

/**
 * Signal handler
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

// HTTP handlers

void
handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(user_data);

	const char *cn = papago_req_client_cert_cn(req);

	char payload[1024];
	snprintf(payload, sizeof(payload),
        "{\"status\": \"mutually authenticated tls\", \"client_cn\":\"%s\"}",
        (cn != NULL) ? cn : "{\"status\": \"no client cert\"}");

	papago_res_json(res, payload);
}

int
main(void)
{
	printf("Papago mTLS (mutual TLS) Example\n\n");

	// check for server cert/key and CA/client cert files
	static const char *required_files[] = {
		"server.crt",
        "server.key",
        "ca.crt",
        "client.crt",
        "client.key",
	};

	for (size_t i = 0; i < 5; i++) {
		FILE *f = fopen(required_files[i], "r");
		if (f == NULL) {
			fprintf(stderr, "error: %s not found\n\n", required_files[i]);
			fprintf(stderr, "Generate the server, CA, and client "
			    "certificates first:\n");
			fprintf(stderr, "  ./generate_certs.sh\n\n");

			return 1;
		}
		fclose(f);
	}

	// setup signal handling
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// create server
	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	papago_route(server, PAPAGO_GET, "/", handler, NULL);

	papago_config_t config = papago_default_config();
	config.port = 8443;
	config.enable_ssl = true;
	config.cert_file = "server.crt";
	config.key_file = "server.key";
	config.ca_cert_file = "ca.crt";
	config.require_client_cert = true;

	printf("Papago mTLS Configuration:\n");
	printf("  Server cert: %s\n", config.cert_file);
	printf("  Server key: %s\n", config.key_file);
	printf("  CA:    %s\n", config.ca_cert_file);
	printf("  Client cert required: yes\n\n");

	printf("Server:\n");
	printf("  HTTPS: https://localhost:%d\n\n", config.port);

	printf("Run:\n");
	printf("# succeeds: presents a client cert signed by ca.crt\n");
	printf("curl --cacert ca.crt --cert client.crt --key client.key \\\n");
	printf("  https://localhost:8443/\n\n");
	printf("curl --cacert ca.crt --cert client.crt --key client.key \\\n");
	printf("  https://localhost:8443/api/whoami\n\n");

	printf("# fail - 403: no client certificate presented\n");
	printf("curl --cacert ca.crt https://localhost:8443/\n\n");

	printf("Press Ctrl+C to stop\n");

	// start server (blocking)
	if (papago_start(server, &config) != 0) {
		fprintf(stderr, "%s\n", papago_error());
		papago_destroy(server);

		return 1;
	}

	papago_destroy(server);

	return 0;
}
