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

// HTTP Handlers

void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	static const char html[] =
	    "<!DOCTYPE html>\n"
	    "<html>\n"
	    "<head><title>Papago SSL Demo</title></head>\n"
	    "<body>\n"
	    "  <h1>Secure Connection!</h1>\n"
	    "  <p>This page is served over HTTPS</p>\n"
	    "  <p>WebSocket is available over WSS (secure WebSocket)</p>\n"
	    "  <script>\n"
	    "    const ws = new WebSocket('wss://localhost:8444/ws');\n"
	    "    ws.onopen = () => console.log('Secure WebSocket connected!');\n"
	    "    ws.onmessage = (e) => console.log('Received:', e.data);\n"
	    "  </script>\n"
	    "</body>\n"
	    "</html>\n";

	papago_res_header(res, "Content-Type", "text/html; charset=utf-8");
	papago_res_send(res, html);
}

void
api_secure_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(res);
    PAPAGO_UNUSED(user_data);

    papago_res_header(res, "Content-Type", "application/json");
	papago_res_json(res,
	    "{\"message\":\"This API is served over HTTPS\","
	    "\"secure\":true}");
}

// websocket handlers

void
ws_on_connect(papago_ws_connection_t *conn)
{
	printf("[WSS] Secure connection from %s\n", papago_ws_get_client_ip(conn));
	papago_ws_send(conn,
	    "{\"type\":\"welcome\",\"message\":\"Secure WebSocket!\"}");
}

void
ws_on_message(papago_ws_connection_t *conn, const char *message,
              size_t length, bool is_binary)
{
	PAPAGO_UNUSED(length);

	printf("[WSS] Received %s message: %s\n",
	    is_binary ? "binary" : "text", message);

	// echo back with confirmation
	char response[512];
	snprintf(response, sizeof(response),
	    "{\"type\":\"echo\",\"message\":\"%s\",\"secure\":true}",
	    message);
	papago_ws_send(conn, response);
}

void
ws_on_close(papago_ws_connection_t *conn)
{
	printf("[WSS] client disconnected from %s\n",
	    papago_ws_get_client_ip(conn));
}

void
ws_on_error(papago_ws_connection_t *conn, const char *error)
{
    PAPAGO_UNUSED(conn);

	fprintf(stderr, "[WSS] error: %s\n", error);
}

int
main(void)
{
	papago_config_t config;

	printf("Papago HTTPS + WSS (Secure WebSocket)\n\n");

	// check for certificate files
	FILE *cert_check = fopen("server.crt", "r");
	FILE *key_check = fopen("server.key", "r");

	if (cert_check == NULL || key_check == NULL) {
		fprintf(stderr, "ERROR: Certificate files not found!\n\n");
		fprintf(stderr, "Please generate SSL certificates first:\n");
		fprintf(stderr, "  ./generate_certs.sh\n\n");

		return 1;
	}
	fclose(cert_check);
	fclose(key_check);

	// setup signal handling
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// create server
	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	// configure with SSL enabled
	config = papago_default_config();
	config.port = 8443;
	config.enable_logging = true;
	config.enable_ssl = true;
	config.cert_file = "server.crt";
	config.key_file = "server.key";

	papago_configure(server, &config);

	// register HTTP routes
	papago_get(server, "/", index_handler, NULL);
	papago_get(server, "/api/secure", api_secure_handler, NULL);

	// register WebSocket endpoint
	papago_ws_endpoint(server, "/ws", ws_on_connect, ws_on_message,
        ws_on_close, ws_on_error);

	printf("SSL/TLS Configuration:\n");
	printf("  Certificate: %s\n", config.cert_file);
	printf("  Private Key: %s\n\n", config.key_file);

	printf("Server listening on:\n");
	printf("  HTTPS: https://localhost:%d\n", config.port);
	printf("  WSS:   wss://localhost:%d/ws\n\n", config.port + 1);

	printf("Test commands:\n");
	printf("  # Test HTTPS (ignore self-signed cert warning)\n");
	printf("  curl -k https://localhost:8443/\n");
	printf("  curl -k https://localhost:8443/api/secure\n\n");

	printf("  # Test WSS with wscat\n");
	printf("  wscat -c wss://localhost:8444/ws --no-check\n\n");

	printf("  # Or open in browser (accept certificate warning)\n");
	printf("  open https://localhost:8443\n\n");

	printf("Press Ctrl+C to stop\n");
	printf("══════════════════════════════════════════\n\n");

	// start server (blocking)
	if (papago_start(server) != 0) {
        fprintf(stderr, "%s\n", papago_error(server));
        papago_destroy(server);

        return 1;
    }

	papago_destroy(server);

	printf("\nserver stopped.\n");

	return 0;
}
