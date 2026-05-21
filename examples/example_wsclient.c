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

/**
 * papago_wsc example — connect to a running example_websocket server,
 * exchange a few messages, then exit cleanly.
 *
 * Build:
 *   make example_wsclient
 *
 * Run (start the websocket server first):
 *   ./example_websocket &
 *   ./example_wsclient
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../papago_wsc.h"

static papago_wsc_t *g_client = NULL;

static void
on_connect(papago_wsc_t *client)
{
    printf("[connect] connected to server\n");

    papago_wsc_send(client, "{\"type\":\"message\",\"text\":\"Hello from C client!\"}");
}

static void
on_message(papago_wsc_t *client, const char *message, size_t length,
           bool is_binary)
{
    PAPAGO_WSC_UNUSED(length);

    if (is_binary) {
        printf("[message] received %zu bytes of binary data\n", length);
        return;
    }

    printf("[message] %s\n", message);

    /**
     * The example_websocket server sends a JSON welcome message on connect
     * and echoes/broadcasts chat messages.  Once we have received the
     * welcome we send one more message and then ask for a clean disconnect.
     */
    if (strstr(message, "\"type\":\"welcome\"") != NULL) {
        papago_wsc_send(client,
            "{\"type\":\"message\",\"text\":\"bye from C client!\"}");
        papago_wsc_stop(client);
    }
}

static void
on_close(papago_wsc_t *client)
{
    PAPAGO_WSC_UNUSED(client);
    printf("[close] connection closed\n");
}

static void
on_error(papago_wsc_t *client, const char *error)
{
    PAPAGO_WSC_UNUSED(client);
    fprintf(stderr, "[error] %s\n", error);
}

static void
signal_handler(int sig)
{
    PAPAGO_WSC_UNUSED(sig);

    printf("\nshutting down...\n");
    if (g_client != NULL) {
        papago_wsc_stop(g_client);
    }
}

int
main(void)
{
    const char *host = "127.0.0.1";
    int port = 8485;
    const char *path = "/ws";

    signal(SIGINT,  signal_handler);
    signal(SIGTERM, signal_handler);

    g_client = papago_wsc_new();
    if (g_client == NULL) {
        fprintf(stderr, "papago_wsc_new: out of memory\n");
        return 1;
    }

    papago_wsc_config_t config = papago_wsc_default_config();
    config.host = host;
    config.port = port;
    config.path = path;

    printf("connecting to ws://%s:%d%s\n", host, port, path);

    if (papago_wsc_connect(g_client, &config, on_connect, on_message, on_close,
        on_error) != 0) {
        fprintf(stderr, "papago_wsc_connect: %s\n",
            papago_wsc_error(g_client));
        papago_wsc_destroy(g_client);

        return 1;
    }

    // blocks until on_close fires, papago_wsc_stop() is called, or signal
    papago_wsc_run(g_client);

    papago_wsc_destroy(g_client);
    g_client = NULL;

    printf("done\n");
    return 0;
}
