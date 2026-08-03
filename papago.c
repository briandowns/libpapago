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

#include <linux/limits.h>
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <ctype.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <libwebsockets.h>
#ifdef PAPAGO_USE_MAPLE
#include <maple.h>
#endif
#include <microhttpd.h>
#include <openssl/crypto.h>
#include <zlib.h>

#include "papago.h"

#define PAPAGO_MAX_ROUTES 512
#define PAPAGO_MAX_MIDDLEWARE 256
#define PAPAGO_MAX_WS_ENDPOINTS 32
#define MAX_WS_CONNECTIONS 256
#define PAPAGO_MAX_HEADERS 64
#define MAX_RATE_LIMIT_ENTRIES 1024
#define MAX_CONN_TIMEOUT 30
#define MAX_CONNECTIONS 256
#define METRICS_BUF_SIZE 16384

typedef struct {
    char *key;
    char *value;
} papago_kv_t;

typedef struct {
    papago_method_t method;
    char *path;
    char *path_pattern;
    papago_handler_t handler;
    void *user_data;
    bool has_params;
} papago_route_t;

typedef struct {
    char *path;
    papago_ws_on_connect_t on_connect;
    papago_ws_on_message_t on_message;
    papago_ws_on_close_t on_close;
    papago_ws_on_error_t on_error;
} papago_ws_endpoint_t;

/**
 * Request structure
 */
struct papago_request {
    char client_ip[64];
    papago_method_t method;
    char *path;
    char version[12];
    char *body;
    char host[256];
    char user_agent[256];
    size_t body_length;
    papago_kv_t *headers;
    size_t header_count;
    papago_kv_t *params;
    size_t param_count;
    papago_kv_t *query;
    size_t query_count;
    papago_kv_t *form;
    size_t form_count;
    struct timespec start_time;
    struct MHD_Connection *connection;
    void *user_data;
    bool   body_too_large;
};

/**
 * Response structure 
 */
struct papago_response {
    papago_status_code_t status;
    char *body;
    size_t body_length;
    papago_kv_t *headers;
    size_t header_count;
    FILE *stream_file;
    size_t stream_file_size;
    bool sent;
    bool is_stream_file;
};

/**
 * websocket connection structure
 */
struct papago_ws_connection {
    struct lws *wsi;
    char client_ip[64];
    void *user_data;
    papago_ws_endpoint_t *endpoint;
    struct papago_server *server;
};

/**
 * Metrics structure
 */
typedef struct {
    // request metrics
    uint64_t total_requests;
    uint64_t requests_by_method[10]; // GET, POST, PUT, DELETE, PATCH, HEAD, OPTIONS, CONNECT, TRACE, UNKNOWN
    uint64_t requests_by_status[7]; // 1xx, 2xx, 3xx, 4xx, 5xx, unknown
    // timing metrics
    uint64_t total_duration_ms;
    uint64_t min_duration_ms;
    uint64_t max_duration_ms;
    // error tracking
    uint64_t errors_4xx;
    uint64_t errors_5xx;
    // system metrics
    time_t start_time;
    // per-endpoint metrics (simple array for now)
    struct {
        char path[128];
        uint64_t count;
        uint64_t total_ms;
    } endpoints[64];
    uint16_t endpoint_count;
    pthread_mutex_t mutex;
} papago_metrics_t;

/**
 * Middleware slot structure for global and path-specific middleware
 */
typedef struct {
    papago_middleware_t *middleware;
    char *path;
} papago_middleware_slot_t;

/**
 * Server structure
 */
struct papago_server {
    struct MHD_Daemon *mhd_daemon;
    struct lws_context *lws_context;
    papago_config_t config;
    FILE *log_output_dst;
    papago_route_t routes[PAPAGO_MAX_ROUTES];
    size_t route_count;
    papago_middleware_slot_t middleware[PAPAGO_MAX_MIDDLEWARE];
    int middleware_count;
    papago_ws_endpoint_t ws_endpoints[PAPAGO_MAX_WS_ENDPOINTS];
    size_t ws_endpoint_count;
    pthread_t lws_thread;
#ifdef PAPAGO_USE_MAPLE
    mp_context_t *template_ctx;
#endif
    papago_ws_connection_t *ws_connections[MAX_WS_CONNECTIONS]; // websocket connection tracking
    size_t ws_connection_count;
    pthread_mutex_t ws_mutex;
    pthread_mutex_t template_mutex; // mutex for per request template rendering
    pthread_mutex_t shutdown_mutex; // shutdown synchronization
    pthread_mutex_t rate_limit_mutex;
    pthread_cond_t shutdown_cond;
    papago_metrics_t *metrics;
    const papago_embedded_file_t *embedded_files;
    void *rate_limit_map;
    char *error_message;
    volatile bool running;
};

/**
 * Rate limit structure
 */
typedef struct {
    char ip[INET6_ADDRSTRLEN];
    uint16_t count;
    time_t window_start;
} papago_rate_limit_entry_t;

static _Thread_local papago_error_t papago_error_state;

void
papago_set_error(const int code, const char *fmt, ...)
{
    papago_error_state.code = code;

    va_list args;
    va_start(args, fmt);

    vsnprintf(papago_error_state.message, sizeof(papago_error_state.message),
        fmt, args);

    va_end(args);
}

const char*
papago_error(void)
{
    return papago_error_state.message;
}

int
papago_error_code(void)
{
    return papago_error_state.code;
}

// utility Functions

static char*
_strdup(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (dup != NULL) {
        memcpy(dup, str, len + 1);
    }

    return dup;
}

static void
free_kv_array(papago_kv_t *arr, size_t count)
{
    if (arr == NULL) {
        return;
    }

    for (size_t i = 0; i < count; i++) {
        free(arr[i].key);
        free(arr[i].value);
    }

    free(arr);
}

static int
add_kv(papago_kv_t **arr, size_t *count, const char *key, const char *value)
{
    if (arr == NULL || count == NULL || key == NULL || value == NULL) {
        return 1;
    }

    papago_kv_t *new_arr = realloc(*arr, (*count + 1) * sizeof(papago_kv_t));
    if (new_arr == NULL) {
        return 1;
    }

    new_arr[*count].key = _strdup(key);
    new_arr[*count].value = _strdup(value);

    if (new_arr[*count].key == NULL || new_arr[*count].value == NULL) {
        free(new_arr[*count].key);
        free(new_arr[*count].value);
        free(new_arr);
        return 1;
    }

    *arr = new_arr;
    (*count)++;

    return 0;
}

static const char*
find_kv(const papago_kv_t *arr, size_t count, const char *key)
{
    if (arr == NULL || key == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        if (strcasecmp(arr[i].key, key) == 0) {
            return arr[i].value;
        }
    }

    return NULL;
}

// configuration

#define DEFAULT_PORT 8080
#define DEFAULT_HOST "0.0.0.0"
#define DEFAULT_BODY_SIZE 10 * 1024 * 1024 /* 10MB */

papago_config_t
papago_default_config(void)
{
    papago_config_t config = {0};

    config.port = DEFAULT_PORT;
    config.host = DEFAULT_HOST;
    config.enable_ssl = false;
    config.connection_timeout = MAX_CONN_TIMEOUT;
    config.connection_limit = MAX_CONNECTIONS;
    config.enable_template_rendering = false;
    config.enable_rate_limiting = false;
    config.enable_compression = false;
    config.cert_file = NULL;
    config.key_file = NULL;
    config.static_dir = NULL;
    config.thread_pool_size = 4;
    config.max_body_size = DEFAULT_BODY_SIZE;

    return config;
}

// path parameter matching

static bool
match_route(const char *pattern, const char *path, papago_kv_t **params,
            size_t *param_count)
{
    if (pattern == NULL || path == NULL) {
        return false;
    }

    // simple comparison if no parameters
    if (strchr(pattern, ':') == NULL && strchr(pattern, '*') == NULL) {
        return strcmp(pattern, path) == 0;
    }

    // parse path with parameters
    const char *pattern_copy = _strdup(pattern);
    const char *path_copy = _strdup(path);

    if (pattern_copy == NULL || path_copy == NULL) {
        free((void*)pattern_copy);
        free((void*)path_copy);

        return false;
    }

    bool match = true;
    char *pattern_saveptr = NULL;
    char *path_saveptr = NULL;
    const char *pattern_token = strtok_r((char*)pattern_copy, "/", &pattern_saveptr);
    const char *path_token = strtok_r((char*)path_copy, "/", &path_saveptr);

    while (pattern_token != NULL && path_token != NULL) {
        if (strcmp(pattern_token, "*") == 0) {
            // wildcard: matches all remaining path segments
            // reconstruct the remainder and store it as param "*"
            if (params != NULL && param_count != NULL) {
                char remainder[4096];
                remainder[0] = '\0';
                size_t off = 0;
                const char *pt = path_token;
                char *ps = path_saveptr;

                while (pt != NULL) {
                    size_t seg_len = strlen(pt);

                    if (off + seg_len + 2 < sizeof(remainder)) {
                        if (off > 0) {
                            remainder[off++] = '/';
                        }

                        memcpy(remainder + off, pt, seg_len);
                        off += seg_len;
                        remainder[off] = '\0';
                    }
                    pt = strtok_r(NULL, "/", &ps);
                }
                add_kv(params, param_count, "*", remainder);
            }
            match = true;
            break;
        } else if (pattern_token[0] == ':') {
            // extract named parameter
            const char *param_name = pattern_token + 1;
            if (params != NULL && param_count != NULL) {
                add_kv(params, param_count, param_name, path_token);
            }
        } else if (strcmp(pattern_token, path_token) != 0) {
            match = false;
            break;
        }

        pattern_token = strtok_r(NULL, "/", &pattern_saveptr);
        path_token = strtok_r(NULL, "/", &path_saveptr);
    }

    if ((pattern_token != NULL || path_token != NULL) &&
        !(pattern_token != NULL && strcmp(pattern_token, "*") == 0)) {
        match = false;
    }

    free((void*)pattern_copy);
    free((void*)path_copy);

    return match;
}

// request/Response Helpers

const char*
papago_req_header(papago_request_t *req, const char *key)
{
    return find_kv(req->headers, req->header_count, key);
}

const char*
papago_req_param(papago_request_t *req, const char *key)
{
    return find_kv(req->params, req->param_count, key);
}

const char*
papago_req_query(papago_request_t *req, const char *key)
{
    return find_kv(req->query, req->query_count, key);
}

const char*
papago_req_form(papago_request_t *req, const char *key)
{
    return find_kv(req->form, req->form_count, key);
}

const char*
papago_req_body(const papago_request_t *req)
{
    return req->body;
}

uint64_t
papago_req_body_len(const papago_request_t *req)
{
    return req->body_length;
}

struct timespec
papago_req_start_time(const papago_request_t *req)
{
    return req->start_time;
}

const char*
papago_req_method(const papago_request_t *req)
{
    switch (req->method) {
    case PAPAGO_GET:
        return "GET";
    case PAPAGO_POST:
        return "POST";
    case PAPAGO_PUT:
        return "PUT";
    case PAPAGO_DELETE:
        return "DELETE";
    case PAPAGO_PATCH:
        return "PATCH";
    case PAPAGO_HEAD:
        return "HEAD";
    case PAPAGO_OPTIONS:
        return "OPTIONS";
    case PAPAGO_CONNECT:
        return "CONNECT";
    case PAPAGO_TRACE:
        return "TRACE";
    default:
        return "UNKNOWN";
    }
}

const char*
papago_req_path(const papago_request_t *req)
{
    return req->path;
}

const char*
papago_req_client_ip(const papago_request_t *req)
{
    const union MHD_ConnectionInfo *info = MHD_get_connection_info(
        req->connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);

    if (info && info->client_addr->sa_family == AF_INET) {
        struct sockaddr_in *sptr = (struct sockaddr_in *)info->client_addr;
        inet_ntop(AF_INET, &sptr->sin_addr, (char*)req->client_ip, INET_ADDRSTRLEN);

        return req->client_ip;
    }

    return NULL;
}

const char*
papago_req_host(const papago_request_t *req)
{
    return req->host;
}

const char*
papago_req_user_agent(const papago_request_t *req)
{   
    return req->user_agent;
}

const char*
papago_req_version(const papago_request_t *req)
{
    return req->version;
}

int
papago_res_status(papago_response_t *res)
{
    return res->status;
}

void
papago_res_set_status(papago_response_t *res, papago_status_code_t status)
{
    res->status = status;
}

void
papago_res_header(papago_response_t *res, const char *key, const char *value)
{
    add_kv(&res->headers, &res->header_count, key, value);
}

int
papago_res_send(papago_response_t *res, const char *body)
{
    if (res->body != NULL) {
        free(res->body);
    }

    if (res->is_stream_file) {
        // if response is file-based, we should not have a body set
        return 1;
    }

    res->body = _strdup(body);
    res->body_length = (body != NULL) ? strlen(body) : 0;
    res->sent = true;

    return 0;
}

int
papago_res_json(papago_response_t *res, const char *json)
{
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_TYPE,
        "application/json");
    return papago_res_send(res, json);
}

/**
 * Set file streaming headers
 */
static void
set_file_headers(papago_response_t *res, const char *filepath,
                 const char *mime_type, int64_t file_size)
{
    if (mime_type != NULL) {
        papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_TYPE, mime_type);
    } else {
        papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_TYPE,
            papago_mime_type(filepath));
    }

    char size_str[64];
    snprintf(size_str, sizeof(size_str), "%ju", (uintmax_t)file_size);
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_LENGTH, size_str);
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_X_CONTENT_TYPE_OPTIONS,
        "nosniff");
}
 

/**
 * Validate file for streaming. Returns file size on success or -1 on error.
 */
static int64_t
validate_file(const char *filepath)
{
    struct stat st;
 
    if (filepath == NULL)
        return -1;
 
    if (stat(filepath, &st) != 0) {
        fprintf(stderr, "file not found: %s\n", filepath);
        return -1;
    }
 
    if (!S_ISREG(st.st_mode)) {
        fprintf(stderr, "not a regular file: %s\n", filepath);
        return -1;
    }
 
    return (int64_t)st.st_size;
}

int
papago_res_sendfile_mime(papago_t *server, papago_response_t *res,
                         const char *filepath, const char *mime_type)
{
    if (server == NULL || res == NULL || filepath == NULL) {
        return 1;
    }
 
    int64_t file_size = validate_file(filepath);
    if (file_size == -1) {
        return 1;
    }
 
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        const char *err_msg = "failed to open file for streaming";
        papago_set_error(PAPAGO_ERR, err_msg);

        return 1;
    }
 
    int fd = fileno(fp);
    if (fd < 0) {
        fclose(fp);
        papago_set_error(PAPAGO_ERR, "failed to get file descriptor");

        return 1;
    }
 
    set_file_headers(res, filepath, mime_type, file_size);

    if (res->body != NULL) {
        free(res->body);
        res->body = NULL;
        res->body_length = 0;
        res->sent = true;
    }
 
    // mark response as file-based for special handling in send_response
    res->is_stream_file = true;
    res->stream_file = fp; // MHD will close fd
    res->stream_file_size = (unsigned)file_size;

    return 0;
}

int
papago_res_sendfile(papago_t *server, papago_response_t *res,
                    const char *filepath)
{
    return papago_res_sendfile_mime(server, res, filepath, NULL);
}

// metrics

static void
update_metrics(papago_t *server, const char *url, const char *method,
               papago_status_code_t status, uint64_t duration_ms)
{
    if (server == NULL || url == NULL || method == NULL) {
        return;
    }
 
    pthread_mutex_lock(&server->metrics->mutex);
 
    server->metrics->total_requests++;
 
    // by method
    int method_idx = 9;
    if (strcmp(method, "GET") == 0) {
        method_idx = 0;
    } else if (strcmp(method, "POST") == 0) {
        method_idx = 1;
    } else if (strcmp(method, "PUT") == 0) {
        method_idx = 2;
    } else if (strcmp(method, "DELETE") == 0) {
        method_idx = 3;
    } else if (strcmp(method, "PATCH") == 0) {
        method_idx = 4;
    } else if (strcmp(method, "HEAD") == 0) {
        method_idx = 5;
    } else if  (strcmp(method, "OPTIONS") == 0) {
        method_idx = 6;
    } else if  (strcmp(method, "CONNECT") == 0) {
        method_idx = 7;
    } else if (strcmp(method, "TRACE") == 0) {
        method_idx = 8;
    }
    server->metrics->requests_by_method[method_idx]++;
 
    // by status
    int status_idx = 5;
    if (status >= 100 && status < 200) {
        status_idx = 0;
    } else if (status >= 200 && status < 300) {
        status_idx = 1;
    } else if (status >= 300 && status < 400) {
        status_idx = 2;
    } else if (status >= 400 && status < 500) {
        status_idx = 3;
    } else if (status >= 500 && status < 600) {
        status_idx = 4;
    }
    server->metrics->requests_by_status[status_idx]++;

    // error tracking
    if (status >= 400 && status < 500) {
        server->metrics->errors_4xx++;
    }
    if (status >= 500 && status < 600) {
        server->metrics->errors_5xx++;
    }
 
    // duration metrics
    server->metrics->total_duration_ms += duration_ms;
    if (duration_ms < server->metrics->min_duration_ms) {
        server->metrics->min_duration_ms = duration_ms;
    }
    if (duration_ms > server->metrics->max_duration_ms) {
        server->metrics->max_duration_ms = duration_ms;
    }
 
    // per-endpoint metrics
    bool found = false;
    for (size_t i = 0; i < server->metrics->endpoint_count; i++) {
        if (strcmp(server->metrics->endpoints[i].path, url) == 0) {
            server->metrics->endpoints[i].count++;
            server->metrics->endpoints[i].total_ms += duration_ms;
            found = true;
            break;
        }
    }
 
    // add new endpoint if not found and space available
    if (!found && server->metrics->endpoint_count < 64) {
#ifdef __FreeBSD__
        strlcpy(server->metrics->endpoints[server->metrics->endpoint_count].path,
            url, 128);
#else
        snprintf(server->metrics->endpoints[server->metrics->endpoint_count].path,
            128, "%s", url);
#endif
        server->metrics->endpoints[server->metrics->endpoint_count].path[127] = '\0';
        server->metrics->endpoints[server->metrics->endpoint_count].count = 1;
        server->metrics->endpoints[server->metrics->endpoint_count].total_ms = duration_ms;
        server->metrics->endpoint_count++;
    }
 
    pthread_mutex_unlock(&server->metrics->mutex);
}

void
papago_metrics_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);


    papago_t *server = (papago_t*)user_data; 
    if (server == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_send(res, "metrics unavailable\n");
        return;
    }
 
    time_t now = time(NULL);
    time_t uptime = now - server->metrics->start_time;

    pthread_mutex_lock(&server->metrics->mutex);
 
    // calculate average duration
    double avg_duration = server->metrics->total_requests > 0 ?
        (double)server->metrics->total_duration_ms /
        (double)server->metrics->total_requests : 0.0;

    char metrics[METRICS_BUF_SIZE]; // buffer for metrics output
    memset(metrics, 0, METRICS_BUF_SIZE);

    size_t len = 0;
    int n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
        "# HELP http_requests_total Total number of HTTP requests\n"
        "# TYPE http_requests_total counter\n"
        "http_requests_total %lu\n\n",
        server->metrics->total_requests);
    if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        pthread_mutex_unlock(&server->metrics->mutex);
        return;
    }
    len += (size_t)n;
 
    n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
        "# HELP http_request_duration_milliseconds HTTP request latencies\n"
        "# TYPE http_request_duration_milliseconds summary\n"
        "http_request_duration_milliseconds_min %" PRIu64 "\n"
        "http_request_duration_milliseconds_max %" PRIu64 "\n"
        "http_request_duration_milliseconds_avg %.2f\n"
        "http_request_duration_milliseconds_sum %" PRIu64 "\n\n",
        server->metrics->min_duration_ms == UINT_MAX ? 0 : server->metrics->min_duration_ms,
        server->metrics->max_duration_ms,
        avg_duration,
        server->metrics->total_duration_ms);
    if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        pthread_mutex_unlock(&server->metrics->mutex);
        return;
    }
    len += (size_t)n;
 
    n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
        "# HELP http_requests_by_method Requests by HTTP method\n"
        "# TYPE http_requests_by_method counter\n"
        "http_requests_by_method{method=\"GET\"} %lu\n"
        "http_requests_by_method{method=\"POST\"} %lu\n"
        "http_requests_by_method{method=\"PUT\"} %lu\n"
        "http_requests_by_method{method=\"DELETE\"} %lu\n"
        "http_requests_by_method{method=\"PATCH\"} %lu\n"
        "http_requests_by_method{method=\"HEAD\"} %lu\n"
        "http_requests_by_method{method=\"OPTIONS\"} %lu\n"
        "http_requests_by_method{method=\"CONNECT\"} %lu\n"
        "http_requests_by_method{method=\"TRACE\"} %lu\n\n",
        server->metrics->requests_by_method[0],
        server->metrics->requests_by_method[1],
        server->metrics->requests_by_method[2],
        server->metrics->requests_by_method[3],
        server->metrics->requests_by_method[4],
        server->metrics->requests_by_method[5],
        server->metrics->requests_by_method[6],
        server->metrics->requests_by_method[7],
        server->metrics->requests_by_method[8]);
    if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        pthread_mutex_unlock(&server->metrics->mutex);
        return;
    }
    len += (size_t)n;
 
    n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
        "# HELP http_requests_by_status Requests by status code class\n"
        "# TYPE http_requests_by_status counter\n"
        "http_requests_by_status{status=\"1xx\"} %" PRIu64 "\n"
        "http_requests_by_status{status=\"2xx\"} %" PRIu64 "\n"
        "http_requests_by_status{status=\"3xx\"} %" PRIu64 "\n"
        "http_requests_by_status{status=\"4xx\"} %" PRIu64 "\n"
        "http_requests_by_status{status=\"5xx\"} %" PRIu64 "\n"
        "http_requests_by_status{status=\"other\"} %" PRIu64 "\n\n",
        server->metrics->requests_by_status[0],
        server->metrics->requests_by_status[1],
        server->metrics->requests_by_status[2],
        server->metrics->requests_by_status[3],
        server->metrics->requests_by_status[4],
        server->metrics->requests_by_status[5]);
    if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        pthread_mutex_unlock(&server->metrics->mutex);
        return;
    }
    len += (size_t)n;
 
    n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
        "# HELP process_uptime_seconds Process uptime in seconds\n"
        "# TYPE process_uptime_seconds gauge\n"
        "process_uptime_seconds %ld\n\n",
        (long)uptime);
    if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        pthread_mutex_unlock(&server->metrics->mutex);
        return;
    }
    len += (size_t)n;
 
    // per-endpoint metrics
    for (size_t i = 0; i < server->metrics->endpoint_count && len < METRICS_BUF_SIZE - 256; i++) {
        double ep_avg = server->metrics->endpoints[i].count > 0 ?
            (double)server->metrics->endpoints[i].total_ms / 
            server->metrics->endpoints[i].count : 0.0;

        n = snprintf(metrics + len, METRICS_BUF_SIZE - len,
            "http_requests_by_endpoint{endpoint=\"%s\"} %" PRIu64 "\n"
            "http_request_duration_by_endpoint{endpoint=\"%s\"} %.2f\n",
            server->metrics->endpoints[i].path,
            server->metrics->endpoints[i].count,
            server->metrics->endpoints[i].path,
            ep_avg);
        if (n < 0 || len + (size_t)n >= METRICS_BUF_SIZE) {
            papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
            pthread_mutex_unlock(&server->metrics->mutex);
            return;
        }
        len += (size_t)n;
    }
 
    pthread_mutex_unlock(&server->metrics->mutex);
 
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_TYPE, 
        "text/plain; version=0.0.4; charset=utf-8");
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_X_CONTENT_TYPE_OPTIONS,
        "nosniff");
    papago_res_send(res, metrics);
}

// compression 

/**
 * Compress data using gzip.
 */
static char*
compress_gzip(const char *data, size_t data_len, size_t *compressed_len)
{
    // estimate maximum compressed size
    size_t max_compressed = compressBound(data_len);
    char *compressed = malloc(max_compressed);
    if (compressed == NULL) {
        return NULL;
    }
 
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;
 
    // use gzip format (windowBits = 15 + 16)
    int ret = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
        15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        free(compressed);
        return NULL;
    }
 
    stream.avail_in = (uInt)data_len;
    stream.next_in = (Bytef*)data;
    stream.avail_out = (uInt)max_compressed;
    stream.next_out = (Bytef*)compressed;
 
    // compress
    if (deflate(&stream, Z_FINISH) != Z_STREAM_END) {
        deflateEnd(&stream);
        free(compressed);
        return NULL;
    }
 
    *compressed_len = stream.total_out;
    deflateEnd(&stream);
 
    return compressed;
}

// libmicrohttpd request handler

static enum MHD_Result
query_string_parser(void *cls, enum MHD_ValueKind kind, const char *key,
                    const char *value)
{
    PAPAGO_UNUSED(kind);

    if (cls == NULL || key == NULL) {
        return MHD_NO;
    }

    papago_request_t *req = (papago_request_t*)cls;
    int ret = add_kv(&req->query, &req->query_count, key,
        value != NULL ? value : "");
    if (ret == 1) {
        return MHD_NO;
    }
    
    return MHD_YES;
}

static void
form_body_parser(papago_request_t *req)
{
    if (req->body == NULL || req->body_length == 0) {
        return;
    }

    char *body = _strdup(req->body);
    if (body == NULL) {
        return;
    }

    char *saveptr = NULL;
    char *pair = strtok_r(body, "&", &saveptr);

    while (pair != NULL) {
        char *eq = strchr(pair, '=');
        if (eq != NULL) {
            *eq = '\0';
            char *key   = papago_url_decode(pair);
            char *value = papago_url_decode(eq + 1);
            if (key != NULL && value != NULL) {
                add_kv(&req->form, &req->form_count, key, value);
            }
            free(key);
            free(value);
        } else {
            // bare key with no '=' — store as empty string
            char *key = papago_url_decode(pair);
            if (key != NULL) {
                add_kv(&req->form, &req->form_count, key, "");
                free(key);
            }
        }
        pair = strtok_r(NULL, "&", &saveptr);
    }

    free(body);
}

static enum MHD_Result
header_iterator(void *cls, enum MHD_ValueKind kind, const char *key,
                const char *value)
{
    PAPAGO_UNUSED(kind);

    if (cls == NULL || key == NULL) {
         return MHD_NO;
    }

    papago_request_t *req = (papago_request_t*)cls;
    if (add_kv(&req->headers, &req->header_count,
            key, value != NULL ? value : "") != 0) {
        return MHD_NO;
    }

    return MHD_YES;
}

static enum MHD_Result
mhd_handler(void *cls, struct MHD_Connection *connection, const char *url,
            const char *method, const char *version, const char *upload_data,
            size_t *upload_data_size, void **con_cls)
{
    PAPAGO_UNUSED(version);

    papago_t *server = (papago_t *)cls;
    struct MHD_Response *mhd_response = NULL;
    papago_request_t *req;
    enum MHD_Result ret;
    bool route_found;

    // first call - allocate request structure
    if (*con_cls == NULL) {
        req = calloc(1, sizeof(papago_request_t));
        if (req == NULL) {
            return MHD_NO;
        }

        clock_gettime(CLOCK_MONOTONIC, &req->start_time);

        req->path = _strdup(url);
        req->connection = connection;

        // parse method
        if (strcmp(method, "GET") == 0) {
            req->method = PAPAGO_GET;
        } else if (strcmp(method, "POST") == 0) {
            req->method = PAPAGO_POST;
        } else if (strcmp(method, "PUT") == 0) {
            req->method = PAPAGO_PUT;
        } else if (strcmp(method, "DELETE") == 0) {
            req->method = PAPAGO_DELETE;
        } else if (strcmp(method, "PATCH") == 0) {
            req->method = PAPAGO_PATCH;
        } else if (strcmp(method, "HEAD") == 0) {
            req->method = PAPAGO_HEAD;
        } else if (strcmp(method, "OPTIONS") == 0) {
            req->method = PAPAGO_OPTIONS;
        } else {
            req->method = PAPAGO_GET;
        }
        *con_cls = req;

        return MHD_YES;
    }

    req = *con_cls;

    if (req->headers == NULL) {
        MHD_get_connection_values(connection, MHD_HEADER_KIND, header_iterator,
            req);
    }

    // handle upload data POST/PUT
    if (*upload_data_size > 0) {
        if (req->body_length + *upload_data_size > server->config.max_body_size) {
            free(req->body);
            req->body = NULL;
            req->body_length = 0;
            *upload_data_size = 0;
            req->body_too_large = true;

            return MHD_YES;
        }

        char *new_body = realloc(req->body, req->body_length + *upload_data_size + 1);
        if (new_body == NULL) {
            *upload_data_size = 0;
            return MHD_YES;
        }

        memcpy(new_body + req->body_length, upload_data, *upload_data_size);
        req->body_length += *upload_data_size;
        new_body[req->body_length] = '\0';
        req->body = new_body;

        *upload_data_size = 0;

        return MHD_YES;
    }

    if (req->query == NULL) {
        MHD_get_connection_values(connection, MHD_GET_ARGUMENT_KIND,
            query_string_parser, req);
    }

    if (req->form == NULL) {
        const char *ct = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
            PAPAGO_REQUEST_HEADER_CONTENT_TYPE);
        if (ct != NULL &&
            strncasecmp(ct, PAPAGO_REQUEST_HEADER_FORM_URLENCODED,
                sizeof(PAPAGO_REQUEST_HEADER_FORM_URLENCODED)-1) == 0) {
            form_body_parser(req);
        }
    }

    // create response
    papago_response_t *res = calloc(1, sizeof(papago_response_t));
    if (res == NULL) {
        free(req->path);
        free(req->body);

        free_kv_array(req->headers, req->header_count);
        free_kv_array(req->params, req->param_count);
        free_kv_array(req->query, req->query_count);
        free_kv_array(req->form, req->form_count);

        free(req);

        return MHD_NO;
    }
    res->status = PAPAGO_STATUS_OK;

    const char *user_agent = MHD_lookup_connection_value(connection,
        MHD_HEADER_KIND, "User-Agent");
    if (user_agent == NULL) {
        req->user_agent[0] = '\0';
    } else {
        snprintf(req->user_agent, sizeof(req->user_agent), "%s", user_agent);
    }

    const char *host = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
        "Host");
    if (host == NULL) {
        req->host[0] = '\0';
    } else {
        snprintf(req->host, sizeof(req->host), "%s", host);
    }

    if (version != NULL) {
        snprintf(req->version, sizeof(req->version), "%s", version);
    } else {
        req->version[0] = '\0';
    }

    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &req->start_time);

    // run all before() functions
    for (int j = 0; j < server->middleware_count; j++) {
        papago_middleware_slot_t *slot = &server->middleware[j];
        if (slot->path != NULL &&
            strncmp(req->path, slot->path, strlen(slot->path)) != 0) {
            continue;
        }
        if (!slot->middleware->before(req, res, slot->middleware->user_data)) {
            goto send_response;
        }
    }

    if (req->body_too_large) {
        papago_res_set_status(res, PAPAGO_STATUS_REQUEST_ENTITY_TOO_LARGE);
        papago_res_json(res, "{\"error\":\"payload too large\"}");
        goto send_response;
    }

    // find matching route
    route_found = false;
    for (size_t i = 0; i < server->route_count; i++) {
        papago_route_t *route = &server->routes[i];

        if (route->method != req->method) {
            continue;
        }

        if (match_route(route->path, req->path, &req->params,
                &req->param_count)) {
            route->handler(req, res, route->user_data);
            route_found = true;
            break;
        }
    }

    if (!route_found) {
        papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
        papago_res_json(res, "{\"error\":\"not found\"}");
    }

    // run all after() functions (reverse order)
    for (int i = server->middleware_count-1; i >= 0; i--) {
        papago_middleware_slot_t *slot = &server->middleware[i];
        if (slot->middleware->after == NULL) {
            continue;
        }

        if (slot->path != NULL &&
            strncmp(req->path, slot->path, strlen(slot->path)) != 0) {
            continue;
        }
        slot->middleware->after(req, res, slot->middleware->user_data);
    }

    double duration_ms = 0;
    
send_response:
    clock_gettime(CLOCK_MONOTONIC, &now);
    duration_ms = (now.tv_sec  - papago_req_start_time(req).tv_sec) *
        1000.0 + (now.tv_nsec - papago_req_start_time(req).tv_nsec) /
        1.0e6;
    update_metrics(server, req->path, papago_req_method(req),
        res->status, (uint64_t)duration_ms);

    if (res->is_stream_file && res->stream_file != NULL) {
        int fd = fileno(res->stream_file);

        mhd_response = MHD_create_response_from_fd(res->stream_file_size, fd);
        if (mhd_response == NULL) {
            fclose(res->stream_file);
            return MHD_NO;
        }

        // add headers
        for (size_t i = 0; i < res->header_count; i++) {
            MHD_add_response_header(mhd_response, res->headers[i].key,
                res->headers[i].value);
        }

        // send response
        ret = MHD_queue_response(connection, res->status, mhd_response);
        MHD_destroy_response(mhd_response);

        // file will be closed by MHD after sending

        free(req->path);
        free(req->body);
        free_kv_array(req->headers, req->header_count);
        free_kv_array(req->params, req->param_count);
        free_kv_array(req->query, req->query_count);
        free_kv_array(req->form, req->form_count);
        free(req);

        free_kv_array(res->headers, res->header_count);
        free(res);

        return ret;
    }

    if (server->config.enable_compression) {
        char *compressed_body = NULL;
        size_t compressed_len = 0;

        const char *accept_encoding = MHD_lookup_connection_value(connection,
            MHD_HEADER_KIND, "Accept-Encoding");
        bool use_compression = false;

        if (server->config.enable_compression && 
            accept_encoding != NULL &&
            strstr(accept_encoding, "gzip") != NULL &&
            res->body != NULL && res->body_length > 0) {

            compressed_body = compress_gzip(res->body, res->body_length,
                &compressed_len);

            if (compressed_body != NULL && compressed_len < res->body_length) {
                // compression successful and beneficial
                use_compression = true;
            } else {
                // compression failed or not beneficial
                free(compressed_body);
                compressed_body = NULL;
            }
        }

        if (use_compression && compressed_body != NULL) {
            mhd_response = MHD_create_response_from_buffer(compressed_len,
                compressed_body, MHD_RESPMEM_MUST_FREE);
            if (mhd_response == NULL) {
                return MHD_NO;
            }


            MHD_add_response_header(mhd_response, "Content-Encoding", "gzip");
            MHD_add_response_header(mhd_response, "Vary", "Accept-Encoding");
        } else {
            mhd_response = MHD_create_response_from_buffer(res->body_length,
                res->body != NULL ? res->body : "", MHD_RESPMEM_MUST_COPY);
            if (mhd_response == NULL) {
                free(compressed_body);
                return MHD_NO;
            }
        }
    } else {
        // create MHD response
        mhd_response = MHD_create_response_from_buffer(res->body_length,
            res->body != NULL ? res->body : "", MHD_RESPMEM_MUST_COPY);
        if (mhd_response == NULL) {
            return MHD_NO;
        }
    }

    // add headers
    for (size_t i = 0; i < res->header_count; i++) {
        MHD_add_response_header(mhd_response, res->headers[i].key,
            res->headers[i].value);
    }

    // send response
    ret = MHD_queue_response(connection, res->status, mhd_response);
    MHD_destroy_response(mhd_response);

    free(req->path);
    free(req->body);

    free_kv_array(req->headers, req->header_count);
    free_kv_array(req->params, req->param_count);
    free_kv_array(req->query, req->query_count);
    free_kv_array(req->form, req->form_count);

    free(req);
    free(res->body);
    free_kv_array(res->headers, res->header_count);
    free(res);

    return ret;
}

// libwebsockets Protocol Handler

static int
lws_callback(struct lws *wsi, enum lws_callback_reasons reason, void *user,
             void *in, size_t len)
{
    papago_ws_connection_t *conn = (papago_ws_connection_t*)user;
    papago_t *server;

    switch (reason) {
    case LWS_CALLBACK_ESTABLISHED:
        // connection established
        server = (papago_t*)lws_context_user(lws_get_context(wsi));

        // get the URI path from the HTTP request
        char buf[256];
        lws_hdr_copy(wsi, buf, sizeof(buf), WSI_TOKEN_GET_URI);

        // find matching endpoint
        for (size_t i = 0; i < server->ws_endpoint_count; i++) {
            if (strcmp(server->ws_endpoints[i].path, buf) == 0) {
                conn->wsi = wsi;
                conn->endpoint = &server->ws_endpoints[i];
                conn->server = server;

                // get client IP
                int fd = lws_get_socket_fd(wsi);
                if (fd >= 0) {
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);

                    if (getpeername(fd, (struct sockaddr*)&addr, &addr_len) == 0) {
                        inet_ntop(AF_INET, &addr.sin_addr, conn->client_ip,
                            sizeof(conn->client_ip));
                    }
                }

                // register connection for broadcast
                pthread_mutex_lock(&server->ws_mutex);
                if (server->ws_connection_count < 256) {
                    server->ws_connections[server->ws_connection_count++] = conn;
                }
                pthread_mutex_unlock(&server->ws_mutex);

                if (conn->endpoint->on_connect != NULL) {
                    conn->endpoint->on_connect(conn);
                }
                break;
            }
        }
        break;

    case LWS_CALLBACK_RECEIVE:
        // message received
        if (conn != NULL && conn->endpoint != NULL &&
            conn->endpoint->on_message != NULL) {
            bool is_binary = lws_frame_is_binary(wsi);
            conn->endpoint->on_message(conn, (const char *)in, len, is_binary);
        }
        break;

    case LWS_CALLBACK_CLOSED:
        // connection closed
        if (conn != NULL && conn->endpoint != NULL) {
            // unregister connection
            if (conn->server != NULL) {
                server = conn->server;
                pthread_mutex_lock(&server->ws_mutex);

                for (size_t i = 0; i < server->ws_connection_count; i++) {
                    if (server->ws_connections[i] == conn) {
                        // shift remaining connections
                        for (; i < server->ws_connection_count - 1; i++) {
                            server->ws_connections[i] =
                                server->ws_connections[i + 1];
                        }

                        server->ws_connection_count--;
                        break;
                    }
                }
                pthread_mutex_unlock(&server->ws_mutex);
            }

            if (conn->endpoint->on_close != NULL) {
                conn->endpoint->on_close(conn);
            }
        }
        break;
    default:
        break;
    }

    return 0;
}

static struct lws_protocols papago_lws_protocols[] = {
    {
        .name = "papago-ws",
        .callback = lws_callback,
        .per_session_data_size = sizeof(papago_ws_connection_t),
        .rx_buffer_size = 65536,
    },
    { 
        .name = NULL, 
        .callback = NULL,
        .per_session_data_size = 0,
        .rx_buffer_size = 0,
    } // terminator
};

/**
 * websocket thread
 */
static void*
lws_thread_func(void *arg)
{
    papago_t *server = (papago_t *)arg;

    while (server->running) {
        // use short timeout for responsive shutdown
        lws_service(server->lws_context, 10);  // 10ms instead of 50ms
    }

    // request clean shutdown of all connections
    lws_cancel_service(server->lws_context);

    return NULL;
}

/**
 * Load file contents into memory
 */
static char*
load_file(const char *filepath)
{
    FILE *f = fopen(filepath, "rb");
    if (f == NULL) {
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    fseek(f, 0, SEEK_SET);

    char *content = malloc((size_t)size + 1);
    if (content == NULL) {
        fclose(f);
        return NULL;
    }

    size_t read_size = fread(content, 1, (size_t)size, f);
    content[size] = '\0';
    fclose(f);

    if ((long)read_size != size) {
        free(content);
        return NULL;
    }

    return content;
}

// server Management

papago_t*
papago_new(void)
{
    papago_t *server = calloc(1, sizeof(papago_t));
    if (server == NULL) {
        return NULL;
    }

    server->config = papago_default_config();

    pthread_mutex_init(&server->ws_mutex, NULL);
    pthread_mutex_init(&server->template_mutex, NULL);
    pthread_mutex_init(&server->shutdown_mutex, NULL);
    pthread_mutex_init(&server->rate_limit_mutex, NULL);
    pthread_cond_init(&server->shutdown_cond, NULL);

    server->ws_connection_count = 0;
    server->rate_limit_map = NULL;
    server->embedded_files = NULL;

    server->metrics = calloc(1, sizeof(papago_metrics_t));
    if (server->metrics == NULL) {
        free(server);
        return NULL;
    }
    server->metrics->start_time = time(NULL);
    server->metrics->min_duration_ms = ULONG_MAX;
    pthread_mutex_init(&server->metrics->mutex, NULL);

    return server;
}

#ifndef PAPAGO_DEBUG
static void
suppress_lws_output(int level, const char *line)
{
    PAPAGO_UNUSED(level);
    PAPAGO_UNUSED(line);
}
#endif

int
papago_start(papago_t *server, const papago_config_t *config)
{
    if (server == NULL || config == NULL) {
        return 1;
    }

    papago_config_t existing_config = server->config;

    if (existing_config.enable_rate_limiting) {
        server->config.enable_rate_limiting = true;
        server->config.rate_limit_requests = existing_config.rate_limit_requests;
        server->config.rate_limit_window = existing_config.rate_limit_window;
    }
    if (server->config.static_dir == NULL && existing_config.static_dir != NULL) {
        server->config.static_dir = existing_config.static_dir;
    }
    if (server->config.enable_rate_limiting && server->rate_limit_map == NULL) {
        server->rate_limit_map = calloc(MAX_RATE_LIMIT_ENTRIES,
            sizeof(papago_rate_limit_entry_t));
        if (server->rate_limit_map == NULL) {
            server->config.enable_rate_limiting = false;
        }
    }
    
    server->config = *config;
    server->running = true;

    char *cert_pem = NULL;
    char *key_pem = NULL;

    unsigned int mhd_flags = MHD_USE_INTERNAL_POLLING_THREAD;
 
    if (MHD_is_feature_supported(MHD_FEATURE_EPOLL) == MHD_YES) {
        mhd_flags |= MHD_USE_EPOLL; // linux only
    } else {
        mhd_flags |= MHD_USE_POLL; // *BSD, macOS
    }

#ifdef PAPAGO_USE_MAPLE
    if (server->config.enable_template_rendering) {
        server->template_ctx = mp_init();
        if (server->template_ctx == NULL) {
            server->template_ctx = NULL;
            server->running = false;
            papago_set_error(PAPAGO_ERR, "failed to initialize template engine");
            return 1;
        }
    }
#endif

    // start libmicrohttpd daemon with optional SSL
    if (server->config.enable_ssl) {
        if (server->config.cert_file == NULL ||
            server->config.key_file == NULL) {
            papago_set_error(PAPAGO_ERR, "SSL enabled but cert_file or key_file not set");
            return 1;
        }

        // load certificate and key into memory
        cert_pem = load_file(server->config.cert_file);
        key_pem = load_file(server->config.key_file);

        if (cert_pem == NULL || key_pem == NULL) {
            free(cert_pem);
            free(key_pem);

            papago_set_error(PAPAGO_ERR, "failed to load SSL certificate or key");

            return 1;
        }

        mhd_flags |= MHD_USE_TLS;

        server->mhd_daemon = MHD_start_daemon(
            mhd_flags,
            server->config.port,
            NULL, NULL,
            &mhd_handler, server,
            MHD_OPTION_HTTPS_MEM_KEY, key_pem,
            MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
            MHD_OPTION_CONNECTION_TIMEOUT, server->config.connection_timeout,
            MHD_OPTION_CONNECTION_LIMIT, server->config.connection_limit,
            MHD_OPTION_END);

        // free certificate memory after daemon starts
        free(cert_pem);
        free(key_pem);
    } else {
        server->mhd_daemon = MHD_start_daemon(
            mhd_flags,
            server->config.port,
            NULL, NULL,
            &mhd_handler, server,
            MHD_OPTION_CONNECTION_TIMEOUT, server->config.connection_timeout,
            MHD_OPTION_CONNECTION_LIMIT, server->config.connection_limit,
            MHD_OPTION_END);
    }

    if (server->mhd_daemon == NULL) {
        if (errno == EADDRINUSE) {
            papago_set_error(PAPAGO_ERR, "port already in use");
        } else if (errno == EACCES) {
            papago_set_error(PAPAGO_ERR, "insufficient permissions to start HTTP server");
        } else {
            perror("Failed to start HTTP server");
        }

        return 1;
    }

    struct lws_context_creation_info info = {0};

    // start libwebsockets context if we have websocket endpoints
    if (server->ws_endpoint_count > 0) {
        info.port = server->config.port + 1; // WS on different port
        info.protocols = papago_lws_protocols;
        info.gid = (gid_t)-1;
        info.uid = (uid_t)-1;
        info.user = server;

        // SSL/TLS for websocket
        if (server->config.enable_ssl) {
            if (server->config.cert_file == NULL ||
                server->config.key_file == NULL) {
                MHD_stop_daemon(server->mhd_daemon);
                server->running = false;
                papago_set_error(PAPAGO_ERR, "SSL enabled but cert_file or key_file not set for WebSocket");

                return 1;
            }

            info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;
            info.ssl_cert_filepath = server->config.cert_file;
            info.ssl_private_key_filepath = server->config.key_file;
        }

        #ifndef PAPAGO_DEBUG
        lws_set_log_level(LLL_ERR | LLL_WARN | LLL_NOTICE, suppress_lws_output);
        #endif
        server->lws_context = lws_create_context(&info);
        if (server->lws_context == NULL) {
            MHD_stop_daemon(server->mhd_daemon);
            server->running = false;
            papago_set_error(PAPAGO_ERR, "failed to create libwebsockets context");

            return 1;
        }

        // start websocket service thread
        pthread_create(&server->lws_thread, NULL,
        lws_thread_func, server);
    }

    // wait for shutdown signal using condition variable for instant response
    pthread_mutex_lock(&server->shutdown_mutex);
    while (server->running) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;  // 1 second timeout

        // wait for signal or timeout
        pthread_cond_timedwait(&server->shutdown_cond,
            &server->shutdown_mutex, &ts);
    }
    pthread_mutex_unlock(&server->shutdown_mutex);

    return 0;
}

void
papago_stop(papago_t *server)
{
    if (server != NULL) {
        server->running = false;

        // signal condition variable for instant wakeup
        pthread_mutex_lock(&server->shutdown_mutex);
        pthread_cond_signal(&server->shutdown_cond);
        pthread_mutex_unlock(&server->shutdown_mutex);

        // cancel websocket service for immediate response
        if (server->lws_context != NULL) {
            lws_cancel_service(server->lws_context);
        }
    }
}

void
papago_destroy(papago_t *server)
{
    if (server == NULL) {
        return;
    }

    // signal shutdown
    server->running = false;

    // stop HTTP daemon first
    if (server->mhd_daemon != NULL) {
        MHD_stop_daemon(server->mhd_daemon);
    }

    // cancel websocket service and wait for thread
    if (server->lws_context != NULL) {
        // cancel any pending operations
        lws_cancel_service(server->lws_context);

        // wait for thread to exit
        pthread_join(server->lws_thread, NULL);

        // destroy context
        lws_context_destroy(server->lws_context);
    }

    // free route data
    for (size_t i = 0; i < server->route_count; i++) {
        free(server->routes[i].path);
        free(server->routes[i].path_pattern);
    }

    // free middleware data
    for (int i = 0; i < server->middleware_count; i++) {
        free(server->middleware[i].path);
    }

    // free websocket endpoint data
    for (size_t i = 0; i < server->ws_endpoint_count; i++) {
        free(server->ws_endpoints[i].path);
    }

    // destroy synchronization primitives
    pthread_mutex_destroy(&server->ws_mutex);
    pthread_mutex_destroy(&server->template_mutex);
    pthread_mutex_destroy(&server->shutdown_mutex);
    pthread_mutex_destroy(&server->rate_limit_mutex);
    pthread_mutex_destroy(&server->metrics->mutex);
    pthread_cond_destroy(&server->shutdown_cond);

    // free template engine memory
#ifdef PAPAGO_USE_MAPLE
    if (server->template_ctx != NULL) {
        mp_free(server->template_ctx);
    }
#endif

    if (server->metrics != NULL) {
        free(server->metrics);
    }

    free(server);
}

// routing 

int
papago_route(papago_t *server, papago_method_t method,
             const char *path, papago_handler_t handler, void *user_data)
{
    if (server == NULL || path == NULL || handler == NULL ||
        server->route_count >= PAPAGO_MAX_ROUTES) {
        return 1;
    }

    papago_route_t *route = &server->routes[server->route_count];
    route->method = method;
    route->path = _strdup(path);
    route->path_pattern = _strdup(path);
    route->handler = handler;
    route->has_params = (strchr(path, ':') != NULL);
    route->user_data = user_data;

    server->route_count++;

    return 0;
}

// middleware

int
papago_middleware_add(papago_t *server, papago_middleware_t *middleware)
{
    return papago_middleware_path_add(server, NULL, middleware);
}

int
papago_middleware_path_add(papago_t *server, const char *path,
                           papago_middleware_t *middleware)
{
    if (server == NULL || middleware == NULL || middleware->before == NULL ||
        server->middleware_count >= PAPAGO_MAX_MIDDLEWARE) {
        return 1;
    }

    papago_middleware_slot_t *slot =
        &server->middleware[server->middleware_count++];
    slot->middleware = middleware;
    if (path != NULL) {
        slot->path = _strdup(path);
        if (slot->path == NULL) {
             server->middleware_count--;
             return 1;
         }
    } else {
        slot->path = NULL;
    }

    return 0;
}

/**
 * Embedded Static Files
 */
static const papago_embedded_file_t *g_embedded_files = NULL;
 
void
papago_register_embedded_files(papago_t *server,
                               const papago_embedded_file_t *files)
{
    PAPAGO_UNUSED(server);
    g_embedded_files = files;
}
 
void
papago_serve_embedded_handler(papago_request_t *req, papago_response_t *res,
                              void *user_data)
{
    PAPAGO_UNUSED(user_data);
 
    if (g_embedded_files == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
        papago_res_send(res, "No embedded files registered");
        return;
    }
 
    const char *path = papago_req_path(req);
 
    // default to /index.html if requesting "/"
    if (strcmp(path, "/") == 0) {
        path = "/index.html";
    }

    // find file
    char *data_copy;
    for (size_t i = 0; g_embedded_files[i].path != NULL; i++) {
        if (strcmp(g_embedded_files[i].path, path) == 0) {
            // found file so serve it
            papago_res_header(res, PAPAGO_REQUEST_HEADER_CONTENT_TYPE,
                g_embedded_files[i].content_type);

            // copy data to send (papago_res_send expects null-terminated string)
            data_copy = malloc(g_embedded_files[i].size + 1);
            if (data_copy != NULL) {
                memcpy(data_copy, g_embedded_files[i].data,
                    g_embedded_files[i].size);
                data_copy[g_embedded_files[i].size] = '\0';

                // set body directly
                res->body = data_copy;
                res->body_length = g_embedded_files[i].size;
            }
            return;
        }
    }
 
    // not found
    papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
    papago_res_send(res, "file not found");
}

// static file serving

// void
// papago_serve_static_handler(papago_request_t *req, papago_response_t *res,
//                             void *user_data)
// {
//     papago_t *server = (papago_t *)user_data;
//     char filepath[PATH_MAX];

//     if (server == NULL) {
//         papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
//         papago_res_send(res, "server context is NULL");
//         return;
//     } else if (server->config.static_dir == NULL) {
//         papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
//         papago_res_send(res, "static directory not configured");
//         return;
//     }

//     if (server == NULL || server->config.static_dir == NULL) {
//         papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
//         papago_res_send(res, "static directory not configured");
//         return;
//     }

//     // prefer the wildcard remainder captured by the router; fall back to
//     // the raw path so the handler still works when mounted at "/*" 
//     const char *rel = papago_req_param(req, "*");
//     char path_buf[PATH_MAX];
//     const char *path;
//     if (rel != NULL) {
//         snprintf(path_buf, sizeof(path_buf), "/%s", rel);
//         path = path_buf;
//     } else {
//         path = papago_req_path(req);
//     }

//     char resolved_root[PATH_MAX];
//     if (realpath(server->config.static_dir, resolved_root) == NULL) {
//         papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
//         papago_res_send(res, "static directory not configured");
//         return;
//     }
//     size_t root_len = strlen(resolved_root);

//     snprintf(filepath, sizeof(filepath), "%s%s", server->config.static_dir,
//         path);

//     struct stat st;
//     if (stat(filepath, &st) != 0) {
//         papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
//         papago_res_send(res, "file not found");
//         return;
//     }
 
//     if (!S_ISREG(st.st_mode)) {
//         if (S_ISDIR(st.st_mode)) {
//             snprintf(filepath, sizeof(filepath), "%s%s/index.html",
//                 server->config.static_dir, path);
 
//             if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
//                 papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
//                 papago_res_send(res, "directory listing not allowed");
//                 return;
//             }
//         } else {
//             papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
//             papago_res_send(res, "not a regular file");
//             return;
//         }
//     }
 
//     // prevent directory traversal: resolve the actual file path, collapsing
//     // any "..", symlinks, etc., and verify it still lives inside the
//     // configured static_dir before serving it
//     char resolved_path[PATH_MAX];
//     if (realpath(filepath, resolved_path) == NULL) {
//         papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
//         papago_res_send(res, "file not found");
//         return;
//     }
//     if (strncmp(resolved_path, resolved_root, root_len) != 0 ||
//         (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
//         papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
//         papago_res_send(res, "invalid path");
//         return;
//     }
 
//     if (papago_res_sendfile(server, res, resolved_path) != 0) {
//         papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
//         papago_res_send(res, "failed to serve file");
//     }
// }
void
papago_serve_static_handler(papago_request_t *req, papago_response_t *res,
                            void *user_data)
{
    papago_t *server = (papago_t *)user_data;
    char filepath[PATH_MAX];
    int n;

    if (server == NULL || server->config.static_dir == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_send(res, "static directory not configured");
        return;
    }

    /* prefer the wildcard remainder captured by the router; fall back to
     * the raw path so the handler still works when mounted at "/*" */
    const char *rel = papago_req_param(req, "*");
    char path_buf[PATH_MAX];
    const char *path;
    if (rel != NULL) {
        n = snprintf(path_buf, sizeof(path_buf), "/%s", rel);
        if (n < 0 || (size_t)n >= sizeof(path_buf)) {
            papago_res_set_status(res, PAPAGO_STATUS_REQUEST_URI_TOO_LONG);
            papago_res_send(res, "path too long");
            return;
        }
        path = path_buf;
    } else {
        path = papago_req_path(req);
    }

    // resolve the configured static root once
    char resolved_root[PATH_MAX];
    if (realpath(server->config.static_dir, resolved_root) == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_send(res, "static directory not configured");
        return;
    }
    size_t root_len = strlen(resolved_root);

    n = snprintf(filepath, sizeof(filepath), "%s%s", server->config.static_dir,
        path);
    if (n < 0 || (size_t)n >= sizeof(filepath)) {
        papago_res_set_status(res, PAPAGO_STATUS_REQUEST_URI_TOO_LONG);
        papago_res_send(res, "path too long");
        return;
    }

    struct stat st;
    if (stat(filepath, &st) != 0) {
        papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
        papago_res_send(res, "file not found");
        return;
    }

    if (!S_ISREG(st.st_mode)) {
        if (S_ISDIR(st.st_mode)) {
            n = snprintf(filepath, sizeof(filepath), "%s%s/index.html",
                server->config.static_dir, path);
            if (n < 0 || (size_t)n >= sizeof(filepath)) {
                papago_res_set_status(res, PAPAGO_STATUS_REQUEST_URI_TOO_LONG);
                papago_res_send(res, "path too long");
                return;
            }

            if (stat(filepath, &st) != 0 || !S_ISREG(st.st_mode)) {
                papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
                papago_res_send(res, "directory listing not allowed");
                return;
            }
        } else {
            papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
            papago_res_send(res, "not a regular file");
            return;
        }
    }

    // prevent directory traversal: resolve the actual file path, collapsing
    // any "..", symlinks, etc., and verify it still lives inside the
    // configured static_dir before serving it
    char resolved_path[PATH_MAX];
    if (realpath(filepath, resolved_path) == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_NOT_FOUND);
        papago_res_send(res, "file not found");
        return;
    }
    if (strncmp(resolved_path, resolved_root, root_len) != 0 ||
        (resolved_path[root_len] != '/' && resolved_path[root_len] != '\0')) {
        papago_res_set_status(res, PAPAGO_STATUS_FORBIDDEN);
        papago_res_send(res, "invalid path");
        return;
    }

    if (papago_res_sendfile(server, res, resolved_path) != 0) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_send(res, "failed to serve file");
    }
}

// websocket Functions

int
papago_ws_endpoint(papago_t *server, const char *path,
                   papago_ws_on_connect_t on_connect,
                   papago_ws_on_message_t on_message,
                   papago_ws_on_close_t on_close,
                   papago_ws_on_error_t on_error)
{
    papago_ws_endpoint_t *endpoint;

    if (server == NULL || path == NULL ||
        server->ws_endpoint_count >= PAPAGO_MAX_WS_ENDPOINTS) {
        return 1;
    }

    endpoint = &server->ws_endpoints[server->ws_endpoint_count];
    endpoint->path = _strdup(path);
    endpoint->on_connect = on_connect;
    endpoint->on_message = on_message;
    endpoint->on_close = on_close;
    endpoint->on_error = on_error;

    server->ws_endpoint_count++;

    return 0;
}

int
papago_ws_send(papago_ws_connection_t *conn, const char *message)
{
    if (conn == NULL || conn->wsi == NULL || message == NULL) {
        return 1;
    }

    size_t len = strlen(message);
    unsigned char *buf = malloc(LWS_PRE + len);
    if (buf == NULL) {
        return 1;
    }

    memcpy(&buf[LWS_PRE], message, len);
    lws_write(conn->wsi, &buf[LWS_PRE], len, LWS_WRITE_TEXT);

    free(buf);

    return 0;
}

int
papago_ws_send_binary(papago_ws_connection_t *conn, const void *data,
                      size_t length)
{
    if (conn == NULL || conn->wsi == NULL || data == NULL) {
        return 1;
    }

    unsigned char *buf = malloc(LWS_PRE + length);
    if (buf == NULL) {
        return 1;
    }

    memcpy(&buf[LWS_PRE], data, length);
    lws_write(conn->wsi, &buf[LWS_PRE], length, LWS_WRITE_BINARY);

    free(buf);

    return 0;
}

uint16_t
papago_ws_broadcast(papago_t *server, const char *message)
{
    if (server == NULL || message == NULL) {
        return 0;
    }

    size_t len = strlen(message);
    unsigned char * buf = malloc(LWS_PRE + len);
    if (buf == NULL) {
        return 0;
    }

    memcpy(&buf[LWS_PRE], message, len);

    uint16_t count = 0;
    pthread_mutex_lock(&server->ws_mutex);

    for (size_t i = 0; i < server->ws_connection_count; i++) {
        if (server->ws_connections[i] != NULL &&
            server->ws_connections[i]->wsi != NULL) {
            lws_write(server->ws_connections[i]->wsi,
                &buf[LWS_PRE], len, LWS_WRITE_TEXT);
            count++;
        }
    }

    pthread_mutex_unlock(&server->ws_mutex);
    free(buf);

    return count;
}

void
papago_ws_close(papago_ws_connection_t *conn, const char *reason)
{
    if (conn == NULL || conn->wsi == NULL) {
        return;
    }

    PAPAGO_UNUSED(reason);
    lws_close_reason(conn->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
}

void*
papago_ws_get_userdata(papago_ws_connection_t *conn)
{
    return (conn != NULL) ? conn->user_data : NULL;
}

void
papago_ws_set_userdata(papago_ws_connection_t *conn, void *data)
{
    if (conn != NULL) {
        conn->user_data = data;
    }
}

/**
 * Retrieve the client's IP address for a websocket connection. Returns NULL if
 * not available.
 */
const char*
papago_ws_get_client_ip(papago_ws_connection_t *conn)
{
    return (conn != NULL) ? conn->client_ip : NULL;
}

// URL encoding / decoding

/**
 * URL encode a string. Returns newly allocated string, caller must free.
 */
char*
papago_url_encode(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *encoded = malloc(len * 3 + 1);
    if (encoded == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (isalnum((unsigned char)str[i]) || str[i] == '-' ||
            str[i] == '_' || str[i] == '.' || str[i] == '~') {
            encoded[j++] = str[i];
        } else if (str[i] == ' ') {
            encoded[j++] = '+';
        } else {
            sprintf(encoded + j, "%%%02X", (unsigned char)str[i]);
            j += 3;
        }
    }
    encoded[j] = '\0';

    return encoded;
}

/**
 * URL decode a string. Returns newly allocated string, caller must free.
 */
char*
papago_url_decode(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    size_t len = strlen(str);
    char *decoded = malloc(len + 1);
    if (decoded == NULL) {
        return NULL;
    }

    unsigned int value;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '%' && i + 2 < len) {
            if (sscanf(str + i + 1, "%2x", &value) == 1) {
                decoded[j++] = (char)value;
                i += 2;
            } else {
                decoded[j++] = str[i];
            }
        } else if (str[i] == '+') {
            decoded[j++] = ' ';
        } else {
            decoded[j++] = str[i];
        }
    }
    decoded[j] = '\0';

    return decoded;
}

#ifdef PAPAGO_USE_MAPLE
typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    size_t pos;
} memstream_t;

#if defined(__APPLE__)
static int
ms_read(void *cookie, char *buf, int size)
{
    memstream_t *m = cookie;

    if (m->pos >= m->size)
        return 0;

    int remain = m->size - m->pos;
    if (size > remain)
        size = remain;

    memcpy(buf, m->data + m->pos, size);
    m->pos += size;

    return size;
}
#else
static ssize_t
ms_read(void *cookie, char *buf, size_t size)
{
    memstream_t *m = cookie;

    if (m->pos >= m->size)
        return 0;

    size_t remain = m->size - m->pos;
    if (size > remain)
        size = remain;

    memcpy(buf, m->data + m->pos, size);
    m->pos += size;

    return size;
}
#endif

#if defined(__APPLE__)
static int
ms_write(void *cookie, const char *buf, int size)
{
    memstream_t *m = cookie;

    if (m->pos + size + 1 > m->capacity) {
        size_t newcap = (m->pos + size + 1) * 2;
        char *newdata = realloc(m->data, newcap);
        if (!newdata) {
            return 1;
        }
        m->data = newdata;
        m->capacity = newcap;
    }

    memcpy(m->data + m->pos, buf, size);
    m->pos += size;

    if (m->pos > m->size)
        m->size = m->pos;

    m->data[m->size] = '\0';

    return size;
}
#else
static ssize_t
ms_write(void *cookie, const char *buf, size_t size)
{
    memstream_t *m = cookie;

    if (m->pos + size + 1 > m->capacity) {
        size_t newcap = (m->pos + size + 1) * 2;
        char *newdata = realloc(m->data, newcap);
        if (!newdata) {
            return 1;
        }
        m->data = newdata;
        m->capacity = newcap;
    }

    memcpy(m->data + m->pos, buf, size);
    m->pos += size;

    if (m->pos > m->size)
        m->size = m->pos;

    m->data[m->size] = '\0';

    return size;
}
#endif

#if defined(__APPLE__)
static fpos_t
ms_seek(void *cookie, fpos_t offset, int whence)
{
    memstream_t *m = cookie;
    size_t newpos;

    if (whence == SEEK_SET) {
        newpos = offset;
    } else if (whence == SEEK_CUR) {
        newpos = m->pos + offset;
    } else if (whence == SEEK_END) {
        newpos = m->size + offset;
    } else {
        return 1;
    }

    if (newpos > m->size) {
        return 1;
    }

    m->pos = newpos;
    offset = newpos;

    return 0;
}
#else
static int
ms_seek(void *cookie, off_t *offset, int whence)
{
    memstream_t *m = cookie;
    size_t newpos;

    if (whence == SEEK_SET) {
        newpos = *offset;
    } else if (whence == SEEK_CUR) {
        newpos = m->pos + *offset;
    } else if (whence == SEEK_END) {
        newpos = m->size + *offset;
    } else {
        return 1;
    }

    if (newpos > m->size) {
        return 1;
    }

    m->pos = newpos;
    *offset = newpos;

    return 0;
}
#endif

static int
ms_close(void *cookie)
{
    memstream_t *m = cookie;
    free(m->data);
    free(m);

    return 0;
}

static
memstream_t*
ms_create(void)
{
    memstream_t *m = calloc(1, sizeof(*m));
    if (!m) {
        return NULL;
    }

    m->capacity = 128;
    m->data = malloc(m->capacity);
    if (m->data == NULL) {
        free(m);
        return NULL;
    }

    m->data[0] = '\0';
    return m;
}

static FILE*
_fmemopen(memstream_t **out_mem)
{
    memstream_t *m = ms_create();
    if (m == NULL) {
        return NULL;
    }

#if defined(__APPLE__) //|| defined(__FreeBSD__)
    FILE *fp = funopen(m, ms_read, ms_write, ms_seek, ms_close);
#else
    cookie_io_functions_t io = {
        .read  = ms_read,
        .write = ms_write,
        .seek  = ms_seek,
        .close = ms_close
    };
    FILE *fp = fopencookie(m, "w+", io);
#endif

    if (fp == NULL) {
        ms_close(m);
        return NULL;
    }

    if (out_mem != NULL) {
        *out_mem = m;
    }

    return fp;
}

static const char*
memstream_data(const memstream_t *m)
{
    return m->data;
}

static size_t
memstream_size(const memstream_t *m)
{
    return m->size;
}
#endif

void
papago_enable_rate_limit(papago_t *server, uint16_t max_requests,
                         uint16_t window_seconds)
{
    if (server == NULL) {
        return;
    }

    server->config.enable_rate_limiting = true;
    server->config.rate_limit_requests = max_requests;
    server->config.rate_limit_window = window_seconds;

    if (server->rate_limit_map == NULL) {
        void *rate_limit_map = calloc(MAX_RATE_LIMIT_ENTRIES,
            sizeof(papago_rate_limit_entry_t));
        if (rate_limit_map == NULL) {
            server->config.enable_rate_limiting = false;
            return;
        }
        server->rate_limit_map = rate_limit_map;
    }
}
 
bool
papago_check_rate_limit(papago_t *server, papago_request_t *req,
                        papago_response_t *res)
{ 
    if (server == NULL || !server->config.enable_rate_limiting) {
        return false;
    }

    // get client ip address
    const union MHD_ConnectionInfo *conn_info = MHD_get_connection_info(req->connection,
        MHD_CONNECTION_INFO_CLIENT_ADDRESS);
    if (conn_info == NULL) {
        return false;
    }

    char client_ip[INET6_ADDRSTRLEN];
    client_ip[0] = '\0';

    if (conn_info->client_addr->sa_family == AF_INET) {
        struct sockaddr_in *addr = (struct sockaddr_in *)conn_info->client_addr;
        inet_ntop(AF_INET, &addr->sin_addr, client_ip, sizeof(client_ip));
    } else if (conn_info->client_addr->sa_family == AF_INET6) {
        struct sockaddr_in6 *addr = (struct sockaddr_in6 *)conn_info->client_addr;
        inet_ntop(AF_INET6, &addr->sin6_addr, client_ip, sizeof(client_ip));
    }
 
    if (client_ip[0] == '\0') {
        return false;
    }
 
    papago_rate_limit_entry_t *entries = (papago_rate_limit_entry_t *)server->rate_limit_map;
    time_t now = time(NULL);
    bool found = false;
    int slot = -1;
 
    pthread_mutex_lock(&server->rate_limit_mutex);
 
    // find existing entry or free slot
    for (int i = 0; i < MAX_RATE_LIMIT_ENTRIES; i++) {
        if (entries[i].ip[0] == '\0') {
            if (slot == -1) {
                slot = i;
            }
            continue;
        }
 
        // clean expired entries
        if (now - entries[i].window_start >= server->config.rate_limit_window) {
            entries[i].ip[0] = '\0';
            entries[i].count = 0;
            if (slot == -1) {
                slot = i;
            }
            continue;
        }
 
        // found existing entry
        if (strcmp(entries[i].ip, client_ip) == 0) {
            found = true;
            entries[i].count++;
 
            // check if rate limit exceeded 
            if (entries[i].count > server->config.rate_limit_requests) {
                pthread_mutex_unlock(&server->rate_limit_mutex);
 
                // send rate limit response
                papago_res_set_status(res, PAPAGO_STATUS_TOO_MANY_REQUESTS);

                char buf[32];
                snprintf(buf, sizeof(buf), "%d", server->config.rate_limit_window);
                papago_res_header(res, "Retry-After", buf);
                papago_res_send(res, "rate limit exceeded");
 
                return true;
            }
            break;
        }
    }
 
    if (!found && slot != -1) { 
#ifdef __FreeBSD__
        strlcpy(entries[slot].ip, client_ip, sizeof(entries[slot].ip));
#else
        snprintf(entries[slot].ip, sizeof(entries[slot].ip),"%s", client_ip);
#endif
        entries[slot].ip[sizeof(entries[slot].ip)-1] = '\0';
        entries[slot].count = 1;
        entries[slot].window_start = now;
    }
 
    pthread_mutex_unlock(&server->rate_limit_mutex);
 
    return false;
}

#ifdef PAPAGO_USE_MAPLE
int
papago_render_file(papago_t *server, const char *tmpl_path, char *output,
                   size_t output_size, ...)
{
    if (tmpl_path == NULL) {
        return 1;
    }

    if (server == NULL) {
        return 2;
    }
 
    if (server->template_ctx == NULL) {
        return 3;
    }

    if (output == NULL || output_size == 0) {
        return 4;
    }

    va_list args;
    va_start(args, output_size);

    pthread_mutex_lock(&server->template_mutex);
    const char *key;
    while ((key = va_arg(args, const char *)) != NULL) {
        const char *value = va_arg(args, const char *);
        if (value != NULL) {
            mp_set_var(server->template_ctx, key, value);
        }
    }
    va_end(args);

    memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
    if (buf == NULL) {
        pthread_mutex_unlock(&server->template_mutex);
        return 5;
    }
    uint8_t ret = mp_render_file(server->template_ctx, buf, tmpl_path, ".");
    if (ret != 0) {
        pthread_mutex_unlock(&server->template_mutex);
        return 6;
    }
    pthread_mutex_unlock(&server->template_mutex);

    fseek(buf, 0, SEEK_SET);
    memcpy(output, memstream_data(mem), memstream_size(mem));
    fclose(buf);
 
    return 0;
}
 
int
papago_render_template(papago_t *server, const char *tmpl, char *output,
                       size_t output_size, ...)
{
    if (server == NULL) {
        return 1;
    }
    if (tmpl == NULL) {
        return 2;
    }

    if (server->template_ctx == NULL) {
        return 3;
    }

    if (output == NULL || output_size == 0) {
        return 4;
    }
 
    va_list args;
    va_start(args, output_size);

    pthread_mutex_lock(&server->template_mutex);
    const char *key;
    while ((key = va_arg(args, const char*)) != NULL && strcmp(key, "") != 0) {
        const char *value = va_arg(args, const char*);
        if (value != NULL) {
            mp_set_var(server->template_ctx, key, value);
        }
    }
    va_end(args);

    memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
    if (buf == NULL) {
        pthread_mutex_unlock(&server->template_mutex);
        return 4;
    }
    uint8_t ret = mp_render_segment(server->template_ctx, buf, tmpl, NULL, ".");
    if (ret != 0) {
        pthread_mutex_unlock(&server->template_mutex);
        return 5;
    }
    pthread_mutex_unlock(&server->template_mutex);

    fseek(buf, 0, SEEK_SET);
    memcpy(output, memstream_data(mem), memstream_size(mem));
    fclose(buf);

    return 0;
}
 
int
papago_res_render(papago_t *server, papago_response_t *res, const char *tmpl,
                  char *output, size_t output_size, ...)
{
    if (server == NULL || res == NULL || tmpl == NULL || output == NULL
        || output_size == 0) {
        return 1;
    }

    if (server->template_ctx == NULL) {
        return 1;
    }

    va_list args;
    va_start(args, output_size);

    pthread_mutex_lock(&server->template_mutex);
    const char *key;
    while ((key = va_arg(args, const char*)) != NULL) {
        const char *value = va_arg(args, const char*);
        if (value != NULL) {
            mp_set_var(server->template_ctx, key, value);
        }
    }
    va_end(args);
 
    memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
    if (buf == NULL) {
        pthread_mutex_unlock(&server->template_mutex);
        return 1;
    }
    uint8_t ret = mp_render_segment(server->template_ctx, buf, tmpl, NULL, ".");
    if (ret != 0) {
        pthread_mutex_unlock(&server->template_mutex);
        return 1;
    }
    pthread_mutex_unlock(&server->template_mutex);

    fseek(buf, 0, SEEK_SET);
    memcpy(output, memstream_data(mem), memstream_size(mem));
    fclose(buf);
 
    // send as HTML response
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_TYPE,
        "text/html; charset=utf-8");
    papago_res_header(res, PAPAGO_RESPONSE_HEADER_X_CONTENT_TYPE_OPTIONS,
        "nosniff");
 
    return papago_res_send(res, output);
}
#endif

// streaming

const char*
papago_mime_type(const char *filename)
{
    if (filename == NULL) {
        return "application/octet-stream";
    }
 
    size_t len = strlen(filename);
    const char *ext = NULL;
 
    // find last dot
    for (size_t i = len; i > 0; i--) {
        if (filename[i - 1] == '.') {
            ext = &filename[i];
            break;
        }
        if (filename[i - 1] == '/') {
            break;
        }
    }
 
    if (ext == NULL) {
        return "application/octet-stream";
    }
 
    // common MIME types
    if (strcasecmp(ext, "html") == 0 || strcasecmp(ext, "htm") == 0) {
        return "text/html; charset=utf-8";
    }
    if (strcasecmp(ext, "css") == 0) {
        return "text/css; charset=utf-8";
    }
    if (strcasecmp(ext, "js") == 0) {
        return "application/javascript; charset=utf-8";
    }
    if (strcasecmp(ext, "json") == 0) {
        return "application/json";
    }
    if (strcasecmp(ext, "xml") == 0) {
        return "application/xml";
    }
    if (strcasecmp(ext, "txt") == 0) {
        return "text/plain; charset=utf-8";
    }

    // images
    if (strcasecmp(ext, "png") == 0) {
        return "image/png";
    }
    if (strcasecmp(ext, "jpg") == 0 || strcasecmp(ext, "jpeg") == 0) {
        return "image/jpeg";
    }
    if (strcasecmp(ext, "gif") == 0) {
        return "image/gif";
    }
    if (strcasecmp(ext, "svg") == 0) {
        return "image/svg+xml";
    }
    if (strcasecmp(ext, "ico") == 0) {
        return "image/x-icon";
    }
    if (strcasecmp(ext, "webp") == 0) {
        return "image/webp";
    }

    // video
    if (strcasecmp(ext, "mp4") == 0) {
        return "video/mp4";
    }
    if (strcasecmp(ext, "webm") == 0) {
        return "video/webm";
    }
    if (strcasecmp(ext, "ogg") == 0) {
        return "video/ogg";
    }

    // audio
    if (strcasecmp(ext, "mp3") == 0) {
        return "audio/mpeg";
    }
    if (strcasecmp(ext, "wav") == 0) {
        return "audio/wav";
    }
    if (strcasecmp(ext, "m4a") == 0) {
        return "audio/mp4";
    }

    // Documents
    if (strcasecmp(ext, "pdf") == 0) {
        return "application/pdf";
    }
    if (strcasecmp(ext, "zip") == 0) {
        return "application/zip";
    }
    if (strcasecmp(ext, "tar") == 0) {
        return "application/x-tar";
    }
    if (strcasecmp(ext, "gz") == 0) {
        return "application/gzip";
    }

    // fonts
    if (strcasecmp(ext, "woff") == 0) {
        return "font/woff";
    }
    if (strcasecmp(ext, "woff2") == 0) {
        return "font/woff2";
    }
    if (strcasecmp(ext, "ttf") == 0) {
        return "font/ttf";
    }
 
    return "application/octet-stream";
}
