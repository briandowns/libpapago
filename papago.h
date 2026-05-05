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
	PAPAGO_CONNECT,
	PAPAGO_OPTIONS,
	PAPAGO_TRACE
} papago_method_t;

typedef enum {
	PAPAGO_STATUS_CONTINUE = 100, // RFC 7231, 6.2.1
	PAPAGO_STATUS_SWITCHING_PROTOCOLS = 101, // RFC 7231, 6.2.2
	PAPAGO_STATUS_PROCESSING = 102, // RFC 2518, 10.1
	PAPAGO_STATUS_EARLY_HINTS = 103, // RFC 8297
	PAPAGO_STATUS_OK = 200,
	PAPAGO_STATUS_CREATED = 201,
	PAPAGO_STATUS_NO_CONTENT = 204,
	PAPAGO_STATUS_RESET_CONTENT = 205, // RFC 7231, 6.3.6
	PAPAGO_STATUS_PARTIAL_CONTENT = 206, // RFC 7233, 4.1
	PAPAGO_STATUS_MULTI_STATUS = 207, // RFC 4918, 11.1
	PAPAGO_STATUS_ALREADY_REPORTED = 208, // RFC 5842, 7.1
	PAPAGO_STATUS_IM_USED = 226, // RFC 3229, 10.4.1
	PAPAGO_STATUS_MULTIPLE_CHOICES = 300, // RFC 7231, 6.4.1
	PAPAGO_STATUS_MOVED_PERMANENTLY = 301, // RFC 7231, 6.4.2
	PAPAGO_STATUS_FOUND = 302, // RFC 7231, 6.4.3
	PAPAGO_STATUS_SEE_OTHER = 303, // RFC 7231, 6.4.4
	PAPAGO_STATUS_NOT_MODIFIED = 304, // RFC 7232, 4.1
	PAPAGO_STATUS_USE_PROXY = 305, // RFC 7231, 6.4.5
	PAPAGO_STATUS_TEMPORARY_REDIRECT = 307, // RFC 7231, 6.4.7
	PAPAGO_STATUS_PERMANENT_REDIRECT = 308, // RFC 7538, 3.1
	PAPAGO_STATUS_BAD_REQUEST = 400,
	PAPAGO_STATUS_UNAUTHORIZED = 401,
	PAPAGO_STATUS_FORBIDDEN = 403,
	PAPAGO_STATUS_NOT_FOUND = 404,
	PAPAGO_STATUS_METHOD_NOT_ALLOWED = 405,
	PAPAGO_STATUS_NOT_ACCEPTABLE = 406, // RFC 7231, 6.5.6
	PAPAGO_STATUS_PROXY_AUTH_REQUIRED = 407, // RFC 7235, 3.2
	PAPAGO_STATUS_REQUEST_TIMEOUT = 408, // RFC 7231, 6.5.7
	PAPAGO_STATUS_CONFLICT = 409, // RFC 7231, 6.5.8
	PAPAGO_STATUS_GONE = 410, // RFC 7231, 6.5.9
	PAPAGO_STATUS_LENGTH_REQUIRED = 411, // RFC 7231, 6.5.10
	PAPAGO_STATUS_PRECONDITION_FAILED = 412, // RFC 7232, 4.2
	PAPAGO_STATUS_REQUEST_ENTITY_TOO_LARGE = 413, // RFC 7231, 6.5.11
	PAPAGO_STATUS_REQUEST_URI_TOO_LONG = 414, // RFC 7231, 6.5.12
	PAPAGO_STATUS_UNSUPPORTED_MEDIA_TYPE = 415, // RFC 7231, 6.5.13
	PAPAGO_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE = 416, // RFC 7233, 4.4
	PAPAGO_STATUS_EXPECTATION_FAILED = 417, // RFC 7231, 6.5.14
	PAPAGO_STATUS_TEAPOT = 418, // RFC 7168, 2.3.3
	PAPAGO_STATUS_MISDIRECTED_REQUEST = 421, // RFC 7540, 9.1.2
	PAPAGO_STATUS_UNPROCESSABLE_ENTITY = 422, // RFC 4918, 11.2
	PAPAGO_STATUS_LOCKED = 423, // RFC 4918, 11.3
	PAPAGO_STATUS_FAILED_DEPENDENCY = 424, // RFC 4918, 11.4
	PAPAGO_STATUS_TOO_EARLY = 425, // RFC 8470, 5.2.
	PAPAGO_STATUS_UPGRADE_REQUIRED = 426, // RFC 7231, 6.5.15
	PAPAGO_STATUS_PRECONDITION_REQUIRED = 428, // RFC 6585, 3
	PAPAGO_STATUS_TOO_MANY_REQUESTS = 429, // RFC 6585, 4
	PAPAGO_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE = 431, // RFC 6585, 5
	PAPAGO_STATUS_UNAVAILABLE_FOR_LEGAL_REASONS = 451, // RFC 7725, 3
	PAPAGO_STATUS_INTERNAL_ERROR = 500,
	PAPAGO_STATUS_NOT_IMPLEMENTED = 501,
	PAPAGO_STATUS_BAD_GATEWAY = 502, // RFC 7231, 6.6.3
	PAPAGO_STATUS_SERVICE_UNAVAILABLE = 503, // RFC 7231, 6.6.4
	PAPAGO_STATUS_GATEWAY_TIMEOUT = 504, // RFC 7231, 6.6.5
	PAPAGO_STATUS_HTTP_VERSION_NOT_SUPPORTED = 505, // RFC 7231, 6.6.6
	PAPAGO_STATUS_VARIANT_ALSO_NEGOTIATES = 506, // RFC 2295, 8.1
	PAPAGO_STATUS_INSUFFICIENT_STORAGE = 507, // RFC 4918, 11.5
	PAPAGO_STATUS_LOOP_DETECTED = 508, // RFC 5842, 7.2
	PAPAGO_STATUS_NOT_EXTENDED = 510, // RFC 2774, 7
	PAPAGO_STATUS_NETWORK_AUTHENTICATION_REQUIRED = 511 // RFC 6585, 6
} papago_status_code_t;

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
	uint16_t rate_limit_requests;
	uint16_t rate_limit_window;
	char *cert_file;
	char *key_file;
	FILE *log_output_dst;
	char *static_dir;
	int thread_pool_size;
	size_t max_body_size;
	bool enable_cors;
	bool enable_ssl;
	bool enable_logging;
	bool enable_template_rendering;
	bool enable_rate_limiting;
	bool enable_compression;
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
 * success or 1 on failure.
 */
uint8_t
papago_configure(papago_t *server, const papago_config_t *config);

/**
 * Retrieves default configuration.
 */
papago_config_t
papago_default_config(void);

/**
 * Start the server. (blocking) Returns 0 on success or 1 on failure.
 */
uint8_t
papago_start(papago_t *server);

/**
 * Stop the server.
 */
void
papago_stop(papago_t *server);

/**
 * Destroy server and free resources.
 */
void
papago_destroy(papago_t *server);

// routing

int
papago_add_route(papago_t *server, papago_method_t method, const char *path,
                 papago_handler_t handler, void *user_data);

/**
 * Register a GET route. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_get(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data);

/**
 * Register a POST route.
 */
uint8_t
papago_post(papago_t *server, const char *path, papago_handler_t handler,
            void *user_data);

/**
 * Register a PUT route.
 */
uint8_t
papago_put(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data);

/**
 * Register a DELETE route.
 */
uint8_t
papago_delete(papago_t *server, const char *path, papago_handler_t handler,
              void *user_data);

/**
 * Register a PATCH route.
 */
uint8_t
papago_patch(papago_t *server, const char *path, papago_handler_t handler,
             void *user_data);

/**
 * Register a route for any method. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_route(papago_t *server, papago_method_t method, const char *path,
             papago_handler_t handler, void *user_data);

// middleware

/**
 * Register global middleware. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_middleware_add(papago_t *server, papago_middleware_fn_t middleware);

/**
 * Register path-specific middleware. Returns 0 on success or 1 on failure.
 */
uint8_t
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
 * Retrieve request body length. Returns body length or 0.
 */
uint64_t
papago_req_body_len(const papago_request_t *req);

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
papago_res_status(papago_response_t *res, papago_status_code_t status);

/**
 * Set response header.
 */
void 
papago_res_header(papago_response_t *res, const char *key, const char *value);

/**
 * Send response body. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_res_send(papago_response_t *res, const char *body);

/**
 * Send JSON response. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_res_json(papago_response_t *res, const char *json);

// static content embedding
 
// embedded file entry
typedef struct {
	const char	*path;         // virtual path (e.g., "/index.html")
	const char	*content_type; // MIME type
	const unsigned char *data; // file data
	size_t		size;
} papago_embedded_file_t;
 
/**
 * Register embedded files.
 * An array of embedded files (terminated with path = NULL)
 */
void
papago_register_embedded_files(papago_t *server,
                               const papago_embedded_file_t *files);
 
/**
 * Serve embedded file handler. Use with catch-all route.
 */
void
papago_serve_embedded_handler(papago_request_t *req, papago_response_t *res,
                              void *user_data);

// static files

/**
 * Set static files directory
 */
void
papago_set_static_dir(papago_t *server, const char *directory);
 
/**
 * Static file handler serves files from static directory. Use with
 * papago_get(server, "/static/*", papago_serve_static_handler, NULL);
 */
void
papago_serve_static_handler(papago_request_t *req, papago_response_t *res,
                            void *user_data);

// websocket

/**
 * Register websocket endpoint. Returns 0 on success or 1 on failure.
 */
uint8_t
papago_ws_endpoint(papago_t *server, const char *path,
                   papago_ws_on_connect_t on_connect,
                   papago_ws_on_message_t on_message,
                   papago_ws_on_close_t on_close,
                   papago_ws_on_error_t on_error);

/**
 * Send text message to websocket client. Returns 0 on success or 1 on
 * failure.
 */
uint8_t
papago_ws_send(papago_ws_connection_t *conn, const char *message);

/**
 * Send binary message to websocket client. Returns 0 on success or 1 on
 * failure.
 */
uint8_t
papago_ws_send_binary(papago_ws_connection_t *conn, const void *data,
                      size_t length);

/**
 * Broadcast message to all websocket clients. Returns: Number of clients
 * reached
 */
uint16_t 
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

// rate limiting

/**
 * Enable rate limiting by IP address. max_requests is the maximum requests
 * allowed. window_seconds is the time window in seconds.
 */
void
papago_enable_rate_limit(papago_t *server, uint16_t max_requests,
                         uint16_t window_seconds);
 
/**
 * Check if request has exceeded the rate limit. Returns true when the rate
 * limit is exceeded. In that case, this function also mutates res by setting
 * a 429 response, adding a "Retry-After" header, and marking the response as
 * sent, otherwise returns false.
 */
bool
papago_check_rate_limit(papago_request_t *req, papago_response_t *res);

// template rendering

/**
 * Render template string with variables. Returns 0 on success or 1 on
 * failure.
 */
uint8_t
papago_render_file(const char *tmpl_path, char *output,
                   size_t output_size, ...);

/**
 * Render template with variables. Returns 0 on success.
 */
uint8_t
papago_render_template(const char *tmpl, char *output,
                       size_t output_size, ...);

/**
 * Send rendered template as response. Returns 0 on success or -1 on failure.
 * Since this is a variadic function, make sure to include the NULL sentinel.
 */
int
papago_res_render(papago_response_t *res, const char *tmpl, char *output,
                  size_t output_size, ...);

// metrics
 
/**
 * Prometheus metrics endpoint handler.
 */
void
papago_metrics_handler(papago_request_t *req, papago_response_t *res,
                       void *user_data);

// streaming
 
/**
 * Stream a file as the response. Automatically detects MIME type from
 * file extension. Efficiently serves files of any size without loading into
 * memory. Returns 0 on success or 1 on error.
 */
uint8_t
papago_res_sendfile(papago_response_t *res, const char *filepath);
 
/**
 * Stream a file with custom MIME type. Returns 0 on success or 1 on error.
 */
uint8_t
papago_res_sendfile_mime(papago_response_t *res, const char *filepath,
                         const char *mime_type);
 
/**
 * Get MIME type from file extension. Returns MIME type (e.g., "text/html").
 */
const char*
papago_mime_type(const char *filename);

// HTTP status messages

#define PAPAGO_STATUS_MESSAGE_CONTINUE                        "Continue"
#define PAPAGO_STATUS_MESSAGE_SWITCHING_PROTOCOLS             "Switching Protocols"
#define PAPAGO_STATUS_MESSAGE_PROCESS                         "Processing"
#define PAPAGO_STATUS_MESSAGE_EARLY_HINTS                     "Early Hints"
#define PAPAGO_STATUS_MESSAGE_OK                              "OK"
#define PAPAGO_STATUS_MESSAGE_CREATED                         "Created"
#define PAPAGO_STATUS_MESSAGE_ACCEPTED                        "Accepted"
#define PAPAGO_STATUS_MESSAGE_NONAUTHORITATIVE_INFO           "Non-Authoritative Information"
#define PAPAGO_STATUS_MESSAGE_NO_CONTENT                      "No Content"
#define PAPAGO_STATUS_MESSAGE_RESET_CONTENT                   "Reset Content"
#define PAPAGO_STATUS_MESSAGE_PARTIAL_CONTENT                 "Partial Content"
#define PAPAGO_STATUS_MESSAGE_MULTI_STATUS                    "Multi-Status"
#define PAPAGO_STATUS_MESSAGE_ALREADY_REPORTED                "Already Reported"
#define PAPAGO_STATUS_MESSAGE_IM_USED                         "IM Used"
#define PAPAGO_STATUS_MESSAGE_MULTIPLE_CHOICES                "Multiple Choices"
#define PAPAGO_STATUS_MESSAGE_MOVED_PERMANENTLY               "Moved Permanently"
#define PAPAGO_STATUS_MESSAGE_FOUND                           "Found"
#define PAPAGO_STATUS_MESSAGE_SEE_OTHER                       "See Other"
#define PAPAGO_STATUS_MESSAGE_NOT_MODIFIED                    "Not Modified"
#define PAPAGO_STATUS_MESSAGE_USE_PROXY                       "Use Proxy"
#define PAPAGO_STATUS_MESSAGE_TEMPORARY_REDIRECT              "Temporary Redirect"
#define PAPAGO_STATUS_MESSAGE_PERMANENT_REDIRECT              "Permanent Redirect"
#define PAPAGO_STATUS_MESSAGE_BAD_REQUEST                     "Bad Request"
#define PAPAGO_STATUS_MESSAGE_UNAUTHORIZED                    "Unauthorized"
#define PAPAGO_STATUS_MESSAGE_PAYMENT_REQUIRED                "Payment Required"
#define PAPAGO_STATUS_MESSAGE_FORBIDDEN                       "Forbidden"
#define PAPAGO_STATUS_MESSAGE_NOT_FOUND                       "Not Found"
#define PAPAGO_STATUS_MESSAGE_METHOD_NOT_ALLOWED              "Method Not Allowed"
#define PAPAGO_STATUS_MESSAGE_NOT_ACCEPTABLE                  "Not Acceptable"
#define PAPAGO_STATUS_MESSAGE_PROXY_AUTH_REQUIRED             "Proxy Authentication Required"
#define PAPAGO_STATUS_MESSAGE_REQUEST_TIMEOUT                 "Request Timeout"
#define PAPAGO_STATUS_MESSAGE_CONFLICT                        "Conflict"
#define PAPAGO_STATUS_MESSAGE_GONE                            "Gone"
#define PAPAGO_STATUS_MESSAGE_LENGTH_REQUIRED                 "Length Required"
#define PAPAGO_STATUS_MESSAGE_PRECONDITION_FAILED             "Precondition Failed"
#define PAPAGO_STATUS_MESSAGE_REQUEST_ENTITY_TOO_LARGE        "Request Entity Too Large"
#define PAPAGO_STATUS_MESSAGE_REQUEST_URI_TOO_LONG            "Request URI Too Long"
#define PAPAGO_STATUS_MESSAGE_UNSUPPORTED_MEDIA_TYPE          "Unsupported Media Type"
#define PAPAGO_STATUS_MESSAGE_REQUESTED_RANGE_NOT_SATISFIABLE "Requested Range Not Satisfiable"
#define PAPAGO_STATUS_MESSAGE_EXPECTATION_FAILED              "Expectation Failed"
#define PAPAGO_STATUS_MESSAGE_TEAPOT                          "I'm a teapot"
#define PAPAGO_STATUS_MESSAGE_MISDIRECTED_REQUEST             "Misdirected Request"
#define PAPAGO_STATUS_MESSAGE_UNPROCESSABLE_ENTITY            "Unprocessable Entity"
#define PAPAGO_STATUS_MESSAGE_LOCKED                          "Locked"
#define PAPAGO_STATUS_MESSAGE_FAILED_DEPENDENCY               "Failed Dependency"
#define PAPAGO_STATUS_MESSAGE_TOO_EARLY                       "Too Early"
#define PAPAGO_STATUS_MESSAGE_UPGRADE_REQUIRED                "Upgrade Required"
#define PAPAGO_STATUS_MESSAGE_PRECONDITION_REQUIRED           "Precondition Required"
#define PAPAGO_STATUS_MESSAGE_TOO_MANY_REQUESTS               "Too Many Requests"
#define PAPAGO_STATUS_MESSAGE_REQUEST_HEADER_FIELDS_TOO_LARGE "Request Header Fields Too Large"
#define PAPAGO_STATUS_MESSAGE_UNAVAILABLE_FOR_LEGAL_REASONS   "Unavailable For Legal Reasons"
#define PAPAGO_STATUS_MESSAGE_INTERNAL_SERVER_ERROR           "Internal Server Error"
#define PAPAGO_STATUS_MESSAGE_NOT_IMPLEMENTED                 "Not Implemented"
#define PAPAGO_STATUS_MESSAGE_BAD_GATEWAY                     "Bad Gateway"
#define PAPAGO_STATUS_MESSAGE_SERVICE_UNAVAILABLE             "Service Unavailable"
#define PAPAGO_STATUS_MESSAGE_GATEWAY_TIMEOUT                 "Gateway Timeout"
#define PAPAGO_STATUS_MESSAGE_HTTP_VERSION_NOT_SUPPORTED      "HTTP Version Not Supported"
#define PAPAGO_STATUS_MESSAGE_VARIANT_ALSO_NEGOTIATES          "Variant Also Negotiates"
#define PAPAGO_STATUS_MESSAGE_INSUFFICIENT_STORAGE            "Insufficient Storage"
#define PAPAGO_STATUS_MESSAGE_LOOP_DETECTED                   "Loop Detected"
#define PAPAGO_STATUS_MESSAGE_NOT_EXTENDED                    "Not Extended"
#define PAPAGO_STATUS_MESSAGE_NETWORK_AUTHENTICATION_REQUIRED "Network Authentication Required"

// HTTP request header values

#define PAPAGO_REQUEST_HEADER_AIM                            "A-IM"
#define PAPAGO_REQUEST_HEADER_ACCEPT                         "Accept"
#define PAPAGO_REQUEST_HEADER_ACCEPT_CHARSET                 "Accept-Charset"
#define PAPAGO_REQUEST_HEADER_ACCEPT_DATETIME                "Accept-Datetime"
#define PAPAGO_REQUEST_HEADER_ACCEPT_ENCODING                "Accept-Encoding"
#define PAPAGO_REQUEST_HEADER_ACCEPT_LANGUAGE                "Accept-Language"
#define PAPAGO_REQUEST_HEADER_ACCEPT_CONTROL_REQUEST_METHOD  "Access-Control-Request-Method"
#define PAPAGO_REQUEST_HEADER_ACCEPT_CONTROL_REQUEST_HEADERS "Access-Control-Request-Headers"
#define PAPAGO_REQUEST_HEADER_AUTHORIZATION                  "Authorization"
#define PAPAGO_REQUEST_HEADER_CACHE_CONTROL                  "Cache-Control"
#define PAPAGO_REQUEST_HEADER_CONNECTION                     "Connection"
#define PAPAGO_REQUEST_HEADER_CONTENT_ENCODING               "Content-Encoding"
#define PAPAGO_REQUEST_HEADER_CONTENT_LENGTH                 "Content-Length"
#define PAPAGO_REQUEST_HEADER_CONTENT_MD5                    "Content-MD5"
#define PAPAGO_REQUEST_HEADER_CONTENT_TYPE                   "Content-Type"
#define PAPAGO_REQUEST_HEADER_COOKIE                         "Cookie"
#define PAPAGO_REQUEST_HEADER_DATE                           "Date"
#define PAPAGO_REQUEST_HEADER_EXPECT                         "Expect"
#define PAPAGO_REQUEST_HEADER_FORWARDED                      "Forwarded"
#define PAPAGO_REQUEST_HEADER_FROM                           "From"
#define PAPAGO_REQUEST_HEADER_HOST                           "Host"	
#define PAPAGO_REQUEST_HEADER_HTTP2_SETTINGS                 "HTTP2-Settings"
#define PAPAGO_REQUEST_HEADER_IF_MATCH                       "If-Match"
#define PAPAGO_REQUEST_HEADER_IF_MODIFIED_SINCE              "If-Modified-Since"
#define PAPAGO_REQUEST_HEADER_IF_NONE_MATCH                  "If-None-Match"
#define PAPAGO_REQUEST_HEADER_IF_RANGE                       "If-Range"
#define PAPAGO_REQUEST_HEADER_IF_UNMODIFIED_SINCE            "If-Unmodified-Since"
#define PAPAGO_REQUEST_HEADER_MAX_FORWARDS                   "Max-Forwards"
#define PAPAGO_REQUEST_HEADER_PRAGMA                         "Pragma"
#define PAPAGO_REQUEST_HEADER_PROXY_AUTHORIZATION            "Proxy-Authorization"
#define PAPAGO_REQUEST_HEADER_RANGE                          "Range"
#define PAPAGO_REQUEST_HEADER_REFERRER                       "Referer"
#define PAPAGO_REQUEST_HEADER_TE                             "TE"
#define PAPAGO_REQUEST_HEADER_TRAILER                        "Trailer"	
#define PAPAGO_REQUEST_HEADER_TRANSFER_ENCODING              "Transfer-Encoding"
#define PAPAGO_REQUEST_HEADER_USER_AGENT                     "User-Agent"
#define PAPAGO_REQUEST_HEADER_UPGRADE                        "Upgrade"
#define PAPAGO_REQUEST_HEADER_WARNING                        "Warning"

#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN      "Access-Control-Allow-Origin"
#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_ALLOW_CREDENTIALS "Access-Control-Allow-Credentials"
#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_EXPOSE_HEADERS    "Access-Control-Expose-Headers"
#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_MAX_AGE           "Access-Control-Max-Age"
#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_ALLOW_METHODS     "Access-Control-Allow-Methods"
#define PAPAGO_RESPONSE_HEADER_ACCESS_CONTROL_ALLOW_HEADERS     "Access-Control-Allow-Headers"
#define PAPAGO_RESPONSE_HEADER_ACCEPT_PATCH                     "Accept-Patch"
#define PAPAGO_RESPONSE_HEADER_ACCEPT_RANGES                    "Accept-Ranges"
#define PAPAGO_RESPONSE_HEADER_AGE                              "Age"
#define PAPAGO_RESPONSE_HEADER_ALLOW                            "Allow"	
#define PAPAGO_RESPONSE_HEADER_ALT_SVC                          "Alt-Svc"
#define PAPAGO_RESPONSE_HEADER_CACHE_CONTROL                    "Cache-Control"
#define PAPAGO_RESPONSE_HEADER_CONNECTION                       "Connection"
#define PAPAGO_RESPONSE_HEADER_CONTENT_DISPOSITION              "Content-Disposition"
#define PAPAGO_RESPONSE_HEADER_CONTENT_ENCODING                 "Content-Encoding"
#define PAPAGO_RESPONSE_HEADER_CONTENT_LANGUAGE                 "Content-Language"
#define PAPAGO_RESPONSE_HEADER_CONTENT_LENGTH                   "Content-Length"
#define PAPAGO_RESPONSE_HEADER_CONTENT_LOCATION                 "Content-Location"
#define PAPAGO_RESPONSE_HEADER_CONTENT_MD5                      "Content-MD5"
#define PAPAGO_RESPONSE_HEADER_CONTENT_RANGE                    "Content-Range"
#define PAPAGO_RESPONSE_HEADER_CONTENT_TYPE                     "Content-Type"
#define PAPAGO_RESPONSE_HEADER_DATE                             "Date"
#define PAPAGO_RESPONSE_HEADER_DELTA_BASE                       "Delta-Base"
#define PAPAGO_RESPONSE_HEADER_ETAG                             "ETag"
#define PAPAGO_RESPONSE_HEADER_EXPIRES                          "Expires"
#define PAPAGO_RESPONSE_HEADER_IM                               "IM"
#define PAPAGO_RESPONSE_HEADER_LAST_MODIFIED                    "Last-Modified"
#define PAPAGO_RESPONSE_HEADER_LINK                             "Link"
#define PAPAGO_RESPONSE_HEADER_LOCATION                         "Location"
#define PAPAGO_RESPONSE_HEADER_P3P                              "P3P"
#define PAPAGO_RESPONSE_HEADER_PRAGMA                           "Pragma"
#define PAPAGO_RESPONSE_HEADER_PROXY_AUTHENTICATE               "Proxy-Authenticate"
#define PAPAGO_RESPONSE_HEADER_PUBLIC_KEY_PINS                  "Public-Key-Pins"
#define PAPAGO_RESPONSE_HEADER_RETRY_AFTER                      "Retry-After"
#define PAPAGO_RESPONSE_HEADER_SET_COOKIE                       "Set-Cookie"
#define PAPAGO_RESPONSE_HEADER_STRICT_TRANSPORT_SECURITY        "Strict-Transport-Security"
#define PAPAGO_RESPONSE_HEADER_TRAILER                          "Trailer"
#define PAPAGO_RESPONSE_HEADER_TRANSFER_ENCODING                "Transfer-Encoding"
#define PAPAGO_RESPONSE_HEADER_TK                               "Tk"
#define PAPAGO_RESPONSE_HEADER_UPGRADE                          "Upgrade"
#define PAPAGO_RESPONSE_HEADER_VARY                             "Vary"
#define PAPAGO_RESPONSE_HEADER_VIA                              "Via"
#define PAPAGO_RESPONSE_HEADER_WARNING                          "Warning"
#define PAPAGO_RESPONSE_HEADER_WWW_AUTHENTICATE                 "WWW-Authenticate"
#define PAPAGO_RESPONSE_HEADER_X_FRAME_OPTIONS                  "X-Frame-Options"

#ifdef __cplusplus
}
#endif
#endif /** end __PAPAGO_H */
