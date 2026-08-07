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

#define _POSIX_C_SOURCE 199309L
#include <signal.h>
#include <stdio.h>
#include <time.h>
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

    papago_res_html(res,
        "<html>"
        "  <h1>Welcome to Papago!</h1>"
        "  <p>Built on libmicrohttpd + libwebsockets</p>"
        "</html>");
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

static bool
logger_before(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(res);
    PAPAGO_UNUSED(user_data);

    return true;
}

static void
logger_after(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    double duration_ms = (now.tv_sec  - papago_req_start_time(req).tv_sec) 
        * 1000.0
        + (now.tv_nsec - papago_req_start_time(req).tv_nsec) / 1.0e6;

    fprintf(stdout,
        "{\"remote\":\"%s\",\"method\":\"%s\",\"path\":\"%s\","
        "\"version\":\"%s\",\"host\":\"%s\",\"user_agent\":\"%s\","
        "\"status\":%d,\"duration_ms\":%.3f}\n",
        papago_req_client_ip(req) != NULL ? papago_req_client_ip(req) : "-",
        papago_req_method(req) != NULL ? papago_req_method(req) : "-",
        papago_req_path(req) != NULL ? papago_req_path(req) : "-",
        papago_req_version(req) != NULL ? papago_req_version(req) : "-",
        papago_req_host(req) != NULL ? papago_req_host(req) : "-",
        papago_req_user_agent(req) != NULL ? papago_req_user_agent(req) : "-",
        papago_res_status(res),
        duration_ms);
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

    papago_middleware_t structured_logger = {
        .before    = logger_before,
        .after     = logger_after,
        .user_data = NULL,
    };
    papago_middleware_add(server, &structured_logger);

    // register HTTP routes
    papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
    papago_route(server, PAPAGO_GET, "/api/hello", api_hello_handler, NULL);
    papago_route(server, PAPAGO_GET, "/user/:username", user_handler, NULL);

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
