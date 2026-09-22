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

void
ws_on_connect(papago_ws_connection_t *conn)
{
	const char *cn = papago_ws_client_cert_cn(conn);

	printf("[WS] client connected from %s\n", papago_ws_get_client_ip(conn));
	printf("[WS] verified client cert CN: %s\n", cn != NULL ? cn : "(none)");

	char welcome[256];
	snprintf(welcome, sizeof(welcome),
	    "{\"type\":\"welcome\",\"client_cn\":\"%s\"}", cn != NULL ? cn : "");
	papago_ws_send(conn, welcome);
}

void
ws_on_message(papago_ws_connection_t *conn, const char *message,
    size_t length, bool is_binary)
{
	PAPAGO_UNUSED(length);
	PAPAGO_UNUSED(is_binary);

	printf("[WS] received: %s\n", message);

	// echo back with the authenticated identity attached
	const char *cn = papago_ws_client_cert_cn(conn);
	char reply[512];
	snprintf(reply, sizeof(reply),
	    "{\"type\":\"echo\",\"from\":\"%s\",\"message\":\"%s\"}",
	    cn != NULL ? cn : "unknown", message);
	papago_ws_send(conn, reply);
}

void
ws_on_close(papago_ws_connection_t *conn)
{
	PAPAGO_UNUSED(conn);
	printf("[WS] client disconnected\n");
}

void
ws_on_error(papago_ws_connection_t *conn, const char *error)
{
	PAPAGO_UNUSED(conn);
	fprintf(stderr, "[WS] error: %s\n", error);
}

int
main(void)
{
	printf("Papago WebSocket mTLS Example\n\n");

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

	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	papago_ws_endpoint(server, "/ws", ws_on_connect, ws_on_message,
	    ws_on_close, ws_on_error);

	papago_config_t config = papago_default_config();
	config.http_port = 8080;
	config.ws_port = 8181;
	config.enable_ssl = true;
	config.cert_file = "server.crt";
	config.key_file = "server.key";
	config.ca_cert_file = "ca.crt";
	config.require_client_cert = true;

	printf("Papago WebSocket mTLS Configuration:\n");
	printf("  Server cert: %s\n", config.cert_file);
	printf("  Server key:  %s\n", config.key_file);
	printf("  CA:          %s\n", config.ca_cert_file);
	printf("  Client cert required: yes\n\n");

	printf("Server:\n");
	printf("  WSS: wss://localhost:%d/ws\n\n", config.ws_port);

	printf("Run the paired client:\n");
	printf("  ./example_wsclient_mtls\n\n");

    printf("Then run\n");
    printf("  ./example_wsclient_mtls\n\n");

	printf("Press Ctrl+C to stop\n");

	if (papago_start(server, &config) != 0) {
		fprintf(stderr, "%s\n", papago_error());
		papago_destroy(server);

		return 1;
	}

	papago_destroy(server);

	return 0;
}

