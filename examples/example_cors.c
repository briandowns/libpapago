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

    papago_res_set_status(res, PAPAGO_STATUS_OK);
    papago_res_send(res,
        "<h1>Welcome to Papago!</h1>"
        "<p>CORS example -- open tests/cors_test.html from a different "
        "origin to exercise this route.</p>");
}

void
api_hello_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    papago_res_set_status(res, PAPAGO_STATUS_OK);
    papago_res_json(res, "{\"message\":\"Hello from Papago!\"}");
}

/*
 * A POST route so there's a real non-"simple" request to preflight --
 * browsers only send a preflight for methods/headers outside the
 * CORS-safelisted set, and a bare GET usually won't trigger one.
 */
void
api_widget_create_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    papago_res_set_status(res, PAPAGO_STATUS_CREATED);
    papago_res_json(res, "{\"id\":42,\"status\":\"created\"}");
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

    static const char *allowed_origins[] = {"http://0.0.0.0:8000"};

    papago_cors_config_t cors_cfg = papago_cors_default_config();
    cors_cfg.allowed_origins = allowed_origins;
    cors_cfg.allowed_origins_count = 1;
    cors_cfg.allowed_methods = "GET,POST,OPTIONS";
    cors_cfg.allowed_headers = "Content-Type,Authorization";
    cors_cfg.allow_credentials = true;
    cors_cfg.max_cache_age = 600;

    papago_enable_cors(server);
    papago_middleware_t rate_cors_mw = {
        .before    = papago_cors_mw,
        .after     = NULL,
        .user_data = &cors_cfg
    };
    papago_middleware_path_add(server, "/", &rate_cors_mw);

    // register HTTP routes
    papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
    papago_route(server, PAPAGO_GET, "/api/hello", api_hello_handler, NULL);
    papago_route(server, PAPAGO_POST, "/api/widgets", api_widget_create_handler, NULL);

    /*
     * If your router requires an explicit route per verb rather than
     * dispatching OPTIONS to whatever route matched the path, register
     * an OPTIONS route per path here too so preflight requests reach
     * the CORS middleware instead of 404ing:
     *
     *   papago_route(server, PAPAGO_OPTIONS, "/api/hello", api_hello_handler, NULL);
     *   papago_route(server, PAPAGO_OPTIONS, "/api/widgets", api_widget_create_handler, NULL);
     *
     * cors_before() short-circuits before the handler body runs, so
     * reusing the same handler function is safe.
     */

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