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

#ifndef __PAPAGO_H
#define __PAPAGO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define PAPAGO_UNUSED(x) (void)x;

typedef enum {
	PAPAGO_GET,
	PAPAGO_POST,
	PAPAGO_PUT,
	PAPAGO_DELETE,
	PAPAGO_PATCH,
	PAPAGO_HEAD,
	PAPAGO_OPTIONS
} papago_method_t;

typedef enum {
	PAPAGO_STATUS_OK = 200,
	PAPAGO_STATUS_CREATED = 201,
	PAPAGO_STATUS_NO_CONTENT = 204,
	PAPAGO_STATUS_BAD_REQUEST = 400,
	PAPAGO_STATUS_UNAUTHORIZED = 401,
	PAPAGO_STATUS_FORBIDDEN = 403,
	PAPAGO_STATUS_NOT_FOUND = 404,
	PAPAGO_STATUS_METHOD_NOT_ALLOWED = 405,
	PAPAGO_STATUS_TOO_MANY_REQUESTS = 429,
	PAPAGO_STATUS_INTERNAL_ERROR = 500,
	PAPAGO_STATUS_NOT_IMPLEMENTED = 501
} papago_status_t;

typedef struct papago_server papago_t;
typedef struct papago_request papago_request_t;
typedef struct papago_response papago_response_t;
typedef struct papago_ws_connection papago_ws_connection_t;

// callback types 
typedef void (*papago_handler_t)(papago_request_t *req, papago_response_t *res,
                                 void *user_data);
typedef bool (*papago_middleware_fn_t)(papago_request_t *req,
                                       papago_response_t *res, void *user_data);

// websocket callback types
typedef void (*papago_ws_on_connect_t)(papago_ws_connection_t *conn);
typedef void (*papago_ws_on_message_t)(papago_ws_connection_t *conn,
                                       const char *message, size_t length,
                                       bool is_binary);
typedef void (*papago_ws_on_close_t)(papago_ws_connection_t *conn);
typedef void (*papago_ws_on_error_t)(papago_ws_connection_t *conn,
                                     const char *error);

typedef struct {
	int port;
	char *host;
	bool enable_ssl;
	bool enable_template_rendering;
	char *cert_file;
	char *key_file;
	FILE *log_output_dst;
	char *static_dir;
	int thread_pool_size;
	size_t max_body_size;
	bool enable_cors;
} papago_config_t;

// server management

/**
 * New Papago server value. NULL is returned on failure. User is responsible
 * for freeing returned memory.
 */
papago_t*
papago_new(void);

/**
 * Retrieve current error message. Returns error message string or NULL if no
 * error.
 */
const char*
papago_error(const papago_t *server);

/**
 * Configure the server. This must be called before papago_start. Returns 0 on
 * success or -1 on failure.
 */
int
papago_configure(papago_t *server, const papago_config_t *config);

/**
 * Retrieves default configuration.
 */
papago_config_t
papago_default_config(void);

/**
 * Start the server. (blocking) Returns 0 on success or -1 on failure.
 */
int
papago_start(papago_t *server);

/**
 * Stop the server.
 */
void
papago_stop(papago_t *server);

/*
 * Destroy server and free resources.
 */
void
papago_destroy(papago_t *server);

// routing

int
papago_add_route(papago_t *server, papago_method_t method, const char *path,
                 papago_handler_t handler, void *user_data);

/**
 * Register a GET route .Returns 0 on success or -1 on failure.
 */
int
papago_get(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data);

/**
 * Register a POST route.
 */
int
papago_post(papago_t *server, const char *path, papago_handler_t handler,
            void *user_data);

/**
 * Register a PUT route.
 */
int
papago_put(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data);

/**
 * Register a DELETE route.
 */
int
papago_delete(papago_t *server, const char *path, papago_handler_t handler,
              void *user_data);

/**
 * Register a PATCH route.
 */
int
papago_patch(papago_t *server, const char *path, papago_handler_t handler,
             void *user_data);

/**
 * Register a route for any method. Returns 0 on success or -1 on failure.
 */
int
papago_route(papago_t *server, papago_method_t method, const char *path,
             papago_handler_t handler, void *user_data);

// middleware

/**
 * Register global middleware. Returns 0 on success or -1 on failure.
 */
int
papago_middleware_add(papago_t *server, papago_middleware_fn_t middleware);

/**
 * Register path-specific middleware. Returns 0 on success or -1 on failure.
 */
int
papago_middleware_path_add(papago_t *server, const char *path,
                           papago_middleware_fn_t middleware);

// request helpers

/**
 * Retrieve request header value. Returns header value or NULL.
 */
const char*
papago_req_header(papago_request_t *req, const char *key);

/**
 * Retrieve path parameter value. Returns parameter value or NULL.
 */
const char*
papago_req_param(papago_request_t *req, const char *key);

/**
 * Retrieve query parameter value. Returns query value or NULL.
 */
const char*
papago_req_query(papago_request_t *req, const char *key);

/**
 * Retrieve request body. Returns body string or NULL.
 */
const char*
papago_req_body(const papago_request_t *req);

/**
 * Retrieve request method. Returns HTTP method.
 */
const char*
papago_req_method(const papago_request_t *req);

/**
 * Retrieve request path. Returns request path.
 */
const char*
papago_req_path(const papago_request_t *req);

/**
 * Retrieve client IP address. Returns IP address string.
 */
const char*
papago_req_client_ip(const papago_request_t *req);

// response helpers

/**
 * Set response status code.
 */
void
papago_res_status(papago_response_t *res, papago_status_t status);

/**
 * Set response header.
 */
void 
papago_res_header(papago_response_t *res, const char *key, const char *value);

/**
 * Send response body. Returns 0 on success or -1 on failure.
 */
int
papago_res_send(papago_response_t *res, const char *body);

/**
 * Send JSON response. Returns 0 on success or -1 on failure.
 */
int
papago_res_json(papago_response_t *res, const char *json);

/**
 * Send file as response. Returns 0 on success or -1 on failure.
 */
int
papago_res_sendfile(papago_response_t *res, const char *filepath);

// static files

/**
 * Enable static file serving. Returns 0 on success or -1 on failure.
 */
int
papago_static(papago_t *server, const char *directory);

// websocket

/**
 * Register websocket endpoint. Returns 0 on success or -1 on failure.
 */
int
papago_ws_endpoint(papago_t *server, const char *path,
                   papago_ws_on_connect_t on_connect,
                   papago_ws_on_message_t on_message,
                   papago_ws_on_close_t on_close,
                   papago_ws_on_error_t on_error);

/**
 * Send text message to websocket client. Returns 0 on success or -1 on
 * failure.
 */
int
papago_ws_send(papago_ws_connection_t *conn, const char *message);

/**
 * Send binary message to websocket client. Returns 0 on success or -1 on
 * failure.
 */
int
papago_ws_send_binary(papago_ws_connection_t *conn, const void *data,
                      size_t length);

/**
 * Broadcast message to all websocket clients. Returns: Number of clients
 * reached
 */
int 
papago_ws_broadcast(papago_t *server, const char *message);

/**
 * Close websocket connection.
 */
void
papago_ws_close(papago_ws_connection_t *conn, const char *reason);

/**
 * Get user data on websocket connection.
 */
void*
papago_ws_get_userdata(papago_ws_connection_t *conn);

/**
 * Set user data on websocket connection.
 */
void
papago_ws_set_userdata(papago_ws_connection_t *conn, void *data);

/**
 * Retrieve client IP address. Returns IP address string.
 */
const char*
papago_ws_get_client_ip(papago_ws_connection_t *conn);

/**
 * Retrieve current server instance. Returns current server or NULL.
 */
papago_t*
papago_get_current_server(void);

/**
 * URL encode a string. Returns encoded string. Caller is responsible to free
 * the returned memory.
 */
char*
papago_url_encode(const char *str);

/**
 * URL decode a string. Returns decoded string. Caller is responsible to free
 * the returned memory.
 */
char*
papago_url_decode(const char *str);

// template rendering

/**
 * Render template string with variables. Returns 0 on success or -1 on
 * failure.
 */
int
papago_render_file(const char *tmpl_path, char *output,
                   size_t output_size, ...);

/**
 * Render template with variables. Returns 0 on success or -1 on failure.
 */
uint8_t
papago_render_template(const char *tmpl, char *output,
                       size_t output_size, ...);

/**
 * Send rendered template as response. Returns 0 on success or -1 on failure.
 */
int
papago_res_render(papago_response_t *res, const char *tmpl, char *output,
                  size_t output_size, ...);

#ifdef __cplusplus
}
#endif
#endif /** end __PAPAGO_H */
