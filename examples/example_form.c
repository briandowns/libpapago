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

// HTTP route handlers

void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    papago_res_send(res,
        "<html><head><title>Example Form</title></head><body>"
            "<form action=\"/login\" method=\"POST\">"
                "<label for=\"username\">Username:</label>"
                "<input type=\"text\" id=\"username\" name=\"username\" required>"
                "</br><br>"
                "<label for=\"password\">Password:</label>"
                "<input type=\"password\" id=\"password\" name=\"password\" required>"
                "<button type=\"submit\">Submit</button>"
            "</form>"
        "</body></html>");
}

void
login_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(res);
    PAPAGO_UNUSED(user_data);

    const char *username = papago_req_form(req, "username");
    const char *password = papago_req_form(req, "password");

    printf("Received login request: username=%s, password=%s\n",
        username != NULL ? username : "(null)",
        password != NULL ? password : "(null)");
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

    // register HTTP routes
    papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
    papago_route(server, PAPAGO_POST, "/login", login_handler, NULL);

    papago_config_t config = papago_default_config();

    // start server (blocking)
    if (papago_start(server, &config) != 0) {
        fprintf(stderr, "%s\n", papago_error());
        papago_destroy(server);

        return 1;
    }

    // cleanup
    papago_destroy(server);

    return 0;
}
