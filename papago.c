#include <openssl/crypto.h>
#define _GNU_SOURCE

#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <libwebsockets.h>
#include <microhttpd.h>

#include "logger.h"
#include "maple.h"
#include "papago.h"

#define	PAPAGO_MAX_ROUTES 256
#define	PAPAGO_MAX_MIDDLEWARE 64
#define	PAPAGO_MAX_WS_ENDPOINTS	32
#define MAX_WS_CONNECTIONS 256
#define	PAPAGO_MAX_HEADERS 64

typedef struct {
	char *key;
	char *value;
} papago_kv_t;

typedef struct {
	papago_method_t method;
	char *path;
	char *path_pattern;
	papago_handler_t handler;
	bool has_params;
    void *user_data;
} papago_route_t;

typedef struct {
	char *path;
	papago_middleware_fn_t	fn;
    void *user_data;
} papago_middleware_t;

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
	char *query_string;
	char *body;
	size_t body_length;
	papago_kv_t *headers;
	size_t header_count;
	papago_kv_t *params;
	size_t param_count;
	papago_kv_t *query;
	size_t query_count;
	int status;
	struct timeval start_time;
	struct MHD_Connection *connection;
	void *user_data;
};

/**
 * Response structure 
 */
struct papago_response {
	papago_status_t status;
	char *body;
	size_t body_length;
	papago_kv_t *headers;
	size_t header_count;
	bool sent;
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
 * Server structure
 */
struct papago_server {
	struct MHD_Daemon *mhd_daemon;
	struct lws_context *lws_context;
	papago_config_t config;
	FILE *log_output_dst;
	papago_route_t routes[PAPAGO_MAX_ROUTES];
	size_t route_count;
	papago_middleware_t	middleware[PAPAGO_MAX_MIDDLEWARE];
	size_t middleware_count;
	papago_ws_endpoint_t ws_endpoints[PAPAGO_MAX_WS_ENDPOINTS];
	size_t ws_endpoint_count;
	pthread_t lws_thread;
	volatile bool running;
	mp_context_t *maple_ctx;
	papago_ws_connection_t *ws_connections[MAX_WS_CONNECTIONS]; // websocket connection tracking
	size_t ws_connection_count;
	pthread_mutex_t ws_mutex;
	pthread_mutex_t shutdown_mutex; // shutdown synchronization
	pthread_cond_t shutdown_cond;
    char *error_message;
};

/**
 * Global server for signal handling
 */
static volatile papago_t *g_server = NULL;

/**
 * Get the current server instance
 */
papago_t*
papago_get_current_server(void)
{
	return (papago_t *)g_server;
}

// utility Functions

static char*
papago_strdup(const char *str)
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
papago_free_kv_array(papago_kv_t *arr, size_t count)
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
papago_add_kv(papago_kv_t **arr, size_t *count, const char *key,
              const char *value)
{
	if (arr == NULL || count == NULL || key == NULL || value == NULL) {
		return -1;
	}

	papago_kv_t *new_arr = realloc(*arr, (*count + 1) * sizeof(papago_kv_t));
	if (new_arr == NULL) {
		return -1;
    }

	new_arr[*count].key = papago_strdup(key);
	new_arr[*count].value = papago_strdup(value);

	if (new_arr[*count].key == NULL || new_arr[*count].value == NULL) {
		free(new_arr[*count].key);
		free(new_arr[*count].value);
		free(new_arr);
		return -1;
	}

	*arr = new_arr;
	(*count)++;

	return 0;
}

static const char*
papago_find_kv(const papago_kv_t *arr, size_t count, const char *key)
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
	config.enable_logging = false;
	config.enable_template_rendering = false;
	config.cert_file = NULL;
	config.key_file = NULL;
	config.static_dir = NULL;
	config.thread_pool_size = 4;
	config.max_body_size = DEFAULT_BODY_SIZE;
	config.enable_cors = false;
	config.log_output_dst = stdout;

	return config;
}

// path parameter matching

static bool
papago_match_route(const char *pattern, const char *path, papago_kv_t **params,
                   size_t *param_count)
{
	if (pattern == NULL || path == NULL) {
        return false;
    }

	// simple comparison if no parameters
	if (strchr(pattern, ':') == NULL) {
		printf("XXX - simple match: pattern=%s, path=%s\n", pattern, path);
		return strcmp(pattern, path) == 0;
    }

	// parse path with parameters
	const char *pattern_copy = papago_strdup(pattern);
	const char *path_copy = papago_strdup(path);

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
		printf("pattern_token: %s, path_token: %s\n", pattern_token, path_token);
		if (pattern_token[0] == ':') {
			// extract parameter
			const char *param_name = pattern_token + 1;
			if (params != NULL && param_count != NULL) {
				papago_add_kv(params, param_count, param_name, path_token);
            }
		} else if (strcmp(pattern_token, path_token) != 0) {
			match = false;
			break;
		}

		pattern_token = strtok_r(NULL, "/", &pattern_saveptr);
		path_token = strtok_r(NULL, "/", &path_saveptr);
	}

	if (pattern_token != NULL || path_token != NULL) {
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
	return papago_find_kv(req->headers, req->header_count, key);
}

const char*
papago_req_param(papago_request_t *req, const char *key)
{
	return papago_find_kv(req->params, req->param_count, key);
}

const char*
papago_req_query(papago_request_t *req, const char *key)
{
	return papago_find_kv(req->query, req->query_count, key);
}

const char*
papago_req_body(const papago_request_t *req)
{
	return req->body;
}

const char*
papago_req_method(const papago_request_t *req)
{
	switch (req->method) {
	case 0:
		return "GET";
	case 1:
		return "POST";
	case 2:
		return "PUT";
	case 3:
		return "DELETE";
	case 4:
		return "PATCH";
	case 5:
		return "HEAD";
	case 6:
		return "OPTIONS";
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

void
papago_res_status(papago_response_t *res, papago_status_t status)
{
	res->status = status;
}

void
papago_res_header(papago_response_t *res, const char *key, const char *value)
{
	papago_add_kv(&res->headers, &res->header_count, key, value);
}

int
papago_res_send(papago_response_t *res, const char *body)
{
	if (res->body != NULL) {
		free(res->body);
    }

	res->body = papago_strdup(body);
	res->body_length = (body != NULL) ? strlen(body) : 0;
	res->sent = true;

	return 0;
}

int
papago_res_json(papago_response_t *res, const char *json)
{
	papago_res_header(res, "Content-Type", "application/json");
	return papago_res_send(res, json);
}

int
papago_res_sendfile(papago_response_t *res, const char *filepath)
{
	FILE *f = fopen(filepath, "rb");
	if (f == NULL) {
		return -1;
	}

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *content = malloc(size + 1);
	if (content == NULL) {
		fclose(f);
		return -1;
	}

	size_t ret = fread(content, 1, size, f);
    if (ret != (size_t)size) {
        free(content);
        fclose(f);
        return -1;
    }

	content[size] = '\0';
	fclose(f);

	if (res->body != NULL) {
		free(res->body);
    }

	res->body = content;
	res->body_length = size;
	res->sent = true;

	return 0;
}

static void
papago_log_request(papago_t *server, struct MHD_Connection *connection,
                   const char *url, const char *method, papago_status_t status,
                   const struct timeval *start_time, const char *version)
{
	PAPAGO_UNUSED(server);
	
	const union MHD_ConnectionInfo *conn_info = MHD_get_connection_info(
		connection, MHD_CONNECTION_INFO_CLIENT_ADDRESS);
	
	char client_ip[INET6_ADDRSTRLEN] = "unknown";
	if (conn_info != NULL) {
		const struct sockaddr *addr = conn_info->client_addr;
		if (addr->sa_family == AF_INET) {
			struct sockaddr_in *addr_in = (struct sockaddr_in *)addr;
			inet_ntop(AF_INET, &addr_in->sin_addr, client_ip,
				sizeof(client_ip));
		} else if (addr->sa_family == AF_INET6) {
			struct sockaddr_in6 *addr_in6 = (struct sockaddr_in6 *)addr;
			inet_ntop(AF_INET6, &addr_in6->sin6_addr, client_ip,
			    sizeof(client_ip));
		}
	}
 
	const char *user_agent = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
	    "User-Agent");
	const char *host = MHD_lookup_connection_value(connection, MHD_HEADER_KIND,
		"Host");

	struct timeval end;
	gettimeofday(&end, NULL);
	long duration_ms = (end.tv_sec - start_time->tv_sec) * 1000L +
		(end.tv_usec - start_time->tv_usec) / 1000L;
 
	if (server->config.enable_logging) {
		s_log(S_LOG_INFO,
			s_log_string("remote", client_ip),
			s_log_string("method", method),
			s_log_string("path", url),
			s_log_string("version", version ? version : "-"),
			s_log_string("host", host ? host : "-"),
			s_log_string("user_agent", user_agent ? user_agent : "-"),
			s_log_int("status", (int)status),
			s_log_int64("duration_ms", (int64_t)duration_ms));
	}
}

// libmicrohttpd Request Handler

static enum MHD_Result
papago_mhd_handler(void *cls, struct MHD_Connection *connection,
                   const char *url, const char *method, const char *version,
                   const char *upload_data, size_t *upload_data_size,
                   void **con_cls)
{
	papago_t *server = (papago_t *)cls;
	papago_request_t *req;
	struct MHD_Response *mhd_response;
	enum MHD_Result ret;
	bool route_found;

	// first call - allocate request structure
	if (*con_cls == NULL) {
		req = calloc(1, sizeof(papago_request_t));
		if (req == NULL) {
			return MHD_NO;
        }

		gettimeofday(&req->start_time, NULL);

		req->path = papago_strdup(url);
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

	// handle upload data (POST/PUT body)
	if (*upload_data_size > 0) {
		if (req->body == NULL) {
			req->body = malloc(*upload_data_size + 1);
			if (req->body != NULL) {
				memcpy(req->body, upload_data, *upload_data_size);
				req->body[*upload_data_size] = '\0';
				req->body_length = *upload_data_size;
			}
		}
		*upload_data_size = 0;

		return MHD_YES;
	}

	// create response
	papago_response_t *res = calloc(1, sizeof(papago_response_t));
	if (res == NULL) {
		free(req->path);
		free(req->body);

		papago_free_kv_array(req->headers, req->header_count);
		papago_free_kv_array(req->params, req->param_count);
		papago_free_kv_array(req->query, req->query_count);
    
		free(req);

		return MHD_NO;
	}
	res->status = PAPAGO_STATUS_OK; 

	// run middleware
	for (size_t i = 0; i < server->middleware_count; i++) {
		papago_middleware_t *mw = &server->middleware[i];

		if (mw->path != NULL) {
			if (strncmp(req->path, mw->path, strlen(mw->path)) != 0) 
				continue;
		}

		if (!mw->fn(req, res, mw->user_data)) {
			if (!res->sent) {
				papago_res_status(res, PAPAGO_STATUS_FORBIDDEN);
				papago_res_json(res, "{\"error\":\"Forbidden\"}");
			}
			goto send_response;
		}
	}

	// find matching route
	route_found = false;
	for (size_t i = 0; i < server->route_count; i++) {
		papago_route_t *route = &server->routes[i];
		printf("XXX - checking route %s (method %d) against %s (method %d)\n",
		    route->path, route->method, req->path, req->method);

		if (route->method != req->method) {
			continue;
		}

		if (papago_match_route(route->path, req->path, &req->params,
		    &req->param_count)) {
			route->handler(req, res, route->user_data);
			route_found = true;
			break;
		}
	}

	if (!route_found) {
		papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
		papago_res_json(res, "{\"error\":\"Not Found\"}");
	}

send_response:
	papago_log_request(server, connection, req->path, method, res->status,
		&req->start_time, version); 

	// create MHD response
	mhd_response = MHD_create_response_from_buffer(res->body_length,
	    res->body != NULL ? res->body : "", MHD_RESPMEM_MUST_COPY);

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

	papago_free_kv_array(req->headers, req->header_count);
	papago_free_kv_array(req->params, req->param_count);
	papago_free_kv_array(req->query, req->query_count);

	free(req);
	free(res->body);
	papago_free_kv_array(res->headers, res->header_count);
	free(res);

	return ret;
}

// libwebsockets Protocol Handler

static int
papago_lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                    void *user, void *in, size_t len)
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
        .name = "papgo-ws",
        .callback = papago_lws_callback,
        .per_session_data_size = sizeof(papago_ws_connection_t),
        .rx_buffer_size = 4096,
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
papago_lws_thread_func(void *arg)
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
papago_load_file(const char *filepath)
{
	FILE *f = fopen(filepath, "rb");
	if (f == NULL) {
		return NULL;
    }

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);

	char *content = malloc(size + 1);
	if (content == NULL) {
		fclose(f);
		return NULL;
	}

	size_t read_size = fread(content, 1, size, f);
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
	pthread_mutex_init(&server->shutdown_mutex, NULL);
	pthread_cond_init(&server->shutdown_cond, NULL);

	server->ws_connection_count = 0;

	return server;
}

const char*
papago_error(const papago_t *server)
{
	return server->error_message;
}

int
papago_configure(papago_t *server, const papago_config_t *config)
{
	if (server == NULL || config == NULL) {
		return -1;
    }
	server->config = *config;

	return 0;
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
papago_start(papago_t *server)
{
	if (server == NULL) {
		return -1;
    }

	g_server = server;
	server->running = true;

	char *cert_pem = NULL;
	char *key_pem = NULL;

	if (server->config.enable_logging) {
		s_log_init(stdout);
	}

	// determine libmicrohttpd flags
	unsigned int mhd_flags = MHD_USE_THREAD_PER_CONNECTION
		| MHD_USE_INTERNAL_POLLING_THREAD;

	if (server->config.enable_template_rendering) {
		server->maple_ctx = mp_init();
		if (server->maple_ctx == NULL) {
			server->error_message = "failed to initialize Maple template engine";

			return -1;
		}
	}

	// start libmicrohttpd daemon with optional SSL
	if (server->config.enable_ssl) {
		if (server->config.cert_file == NULL ||
		    server->config.key_file == NULL) {
            server->error_message = "SSL enabled but cert_file or key_file not set";

			return -1;
		}

		// load certificate and key into memory
		cert_pem = papago_load_file(server->config.cert_file);
		key_pem = papago_load_file(server->config.key_file);

		if (cert_pem == NULL || key_pem == NULL) {
			fprintf(stderr, "Failed to load certificate files\n");
			fprintf(stderr, "  Certificate: %s\n", server->config.cert_file);
			fprintf(stderr, "  Key: %s\n", server->config.key_file);
			free(cert_pem);
			free(key_pem);

			return -1;
		}

		mhd_flags |= MHD_USE_TLS;

		server->mhd_daemon = MHD_start_daemon(
			mhd_flags,
			server->config.port,
		    NULL, NULL,
			&papago_mhd_handler, server,
			MHD_OPTION_HTTPS_MEM_KEY, key_pem,
			MHD_OPTION_HTTPS_MEM_CERT, cert_pem,
			MHD_OPTION_END);

		// free certificate memory after daemon starts
		free(cert_pem);
		free(key_pem);
	} else {
		server->mhd_daemon = MHD_start_daemon(
			mhd_flags,
			server->config.port,
            NULL, NULL,
			&papago_mhd_handler, server,
			MHD_OPTION_END);
	}

	if (server->mhd_daemon == NULL) {
		if (errno == EADDRINUSE) {
			server->error_message = "port already in use";
		} else if (errno == EACCES) {
			server->error_message = "insufficient permissions to start HTTP server";
		} else {
			perror("Failed to start HTTP server");
		}

		return -1;
	}

    struct lws_context_creation_info info = {0};

	// start libwebsockets context if we have websocket endpoints
	if (server->ws_endpoint_count > 0) {
		info.port = server->config.port + 1; // WS on different port
		info.protocols = papago_lws_protocols;
		info.gid = -1;
		info.uid = -1;
		info.user = server;

		// SSL/TLS for websocket
		if (server->config.enable_ssl) {
			if (server->config.cert_file == NULL ||
			    server->config.key_file == NULL) {
				fprintf(stderr, "SSL enabled but cert/key files missing\n");
				MHD_stop_daemon(server->mhd_daemon);

				return -1;
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
			fprintf(stderr, "Failed to create libwebsockets context\n");
			MHD_stop_daemon(server->mhd_daemon);
	
			return -1;
		}

		// start websocket service thread
		pthread_create(&server->lws_thread, NULL,
		papago_lws_thread_func, server);
	}

	if (server->config.enable_logging) {
		s_log(S_LOG_INFO, 
			s_log_string("msg", "server started"),
			s_log_int("port", server->config.port),
			s_log_string("type", server->config.enable_ssl ? "HTTPS" : "HTTP"));
	}
	

	if (server->ws_endpoint_count > 0) {
		if (server->config.enable_logging) {
			s_log(S_LOG_INFO, 
				s_log_string("msg", "websocket server started"),
				s_log_int("port", server->config.port+1),
				s_log_string("type", server->config.enable_ssl ? "WSS" : "WS"));
		}
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
	for (size_t i = 0; i < server->middleware_count; i++) {
		free(server->middleware[i].path);
    }

	// free websocket endpoint data
	for (size_t i = 0; i < server->ws_endpoint_count; i++) {
		free(server->ws_endpoints[i].path);
    }

	// destroy synchronization primitives
	pthread_mutex_destroy(&server->ws_mutex);
	pthread_mutex_destroy(&server->shutdown_mutex);
	pthread_cond_destroy(&server->shutdown_cond);

	// free template engine memory
	if (server->maple_ctx != NULL) {
		mp_free(server->maple_ctx);
	}

	free(server);
	g_server = NULL;
}

// routing 

int
papago_route(papago_t *server, papago_method_t method,
             const char *path, papago_handler_t handler, void *user_data)
{
	if (server == NULL || path == NULL || handler == NULL ||
	    server->route_count >= PAPAGO_MAX_ROUTES) {
		return -1;
    }

	papago_route_t *route = &server->routes[server->route_count];
	route->method = method;
	route->path = papago_strdup(path);
	route->path_pattern = papago_strdup(path);
	route->handler = handler;
	route->has_params = (strchr(path, ':') != NULL);
    route->user_data = user_data;

	server->route_count++;

	return 0;
}

int
papago_get(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data)
{
	return papago_route(server, PAPAGO_GET, path, handler, user_data); 
}

int
papago_post(papago_t *server, const char *path, papago_handler_t handler,
            void *user_data)
{
	return papago_route(server, PAPAGO_POST, path, handler, user_data); 
}

int
papago_put(papago_t *server, const char *path, papago_handler_t handler,
           void *user_data)
{
	return papago_route(server, PAPAGO_PUT, path, handler, user_data);
}

int
papago_delete(papago_t *server, const char *path, papago_handler_t handler,
              void *user_data)
{
	return papago_route(server, PAPAGO_DELETE, path, handler, user_data);
}

int
papago_patch(papago_t *server, const char *path, papago_handler_t handler,
             void *user_data)
{
	return papago_route(server, PAPAGO_PATCH, path, handler, user_data);
}

// middleware

int
papago_middleware_add(papago_t *server, papago_middleware_fn_t middleware)
{
	return papago_middleware_path_add(server, NULL, middleware);
}

int
papago_middleware_path_add(papago_t *server, const char *path,
                           papago_middleware_fn_t middleware)
{
	if (server == NULL || middleware == NULL ||
	    server->middleware_count >= PAPAGO_MAX_MIDDLEWARE) {
		return -1;
    }

	papago_middleware_t *mw = &server->middleware[server->middleware_count];
	mw->path = (path != NULL) ? papago_strdup(path) : NULL;
	mw->fn = middleware;

	server->middleware_count++;

	return 0;
}

// static Files

int
papago_static(papago_t *server, const char *directory)
{
	if (server == NULL || directory == NULL) {
		return -1;
    }

	server->config.static_dir = papago_strdup(directory);

	return 0;
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
		return -1;
    }

	endpoint = &server->ws_endpoints[server->ws_endpoint_count];
	endpoint->path = papago_strdup(path);
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
		return -1;
    }

	size_t len = strlen(message);
	unsigned char *buf = malloc(LWS_PRE + len);
	if (buf == NULL) {
		return -1;
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
		return -1;
    }

	unsigned char *buf = malloc(LWS_PRE + length);
	if (buf == NULL) {
		return -1;
    }

	memcpy(&buf[LWS_PRE], data, length);
	lws_write(conn->wsi, &buf[LWS_PRE], length, LWS_WRITE_BINARY);

	free(buf);

	return 0;
}

int
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

	int count = 0;
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

typedef struct {
    char *data;
    size_t size;
    size_t capacity;
    size_t pos;
} memstream_t;

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

#if defined(__APPLE__) || defined(__FreeBSD__)
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

int
papago_render_file(const char *tmpl_path, char *output,
                   size_t output_size, ...)
{
	if (tmpl_path == NULL) {
		return -1;
	}
 
	va_list args;
	va_start(args, output_size);
	
	const char *key;
	while ((key = va_arg(args, const char *)) != NULL) {
		const char *value = va_arg(args, const char *);
		if (value != NULL) {
			mp_set_var(g_server->maple_ctx, key, value);
		}
	}
	va_end(args);

	memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
	uint8_t ret = mp_render_file(g_server->maple_ctx, buf, tmpl_path, ".");
	if (ret != 0) {
		return 4;
	}

	fseek(buf, 0, SEEK_SET);
	memcpy(output, memstream_data(mem), memstream_size(mem));
	fclose(buf);
 
	return 0;
}
 
uint8_t
papago_render_template(const char *tmpl, char *output, size_t output_size, ...)
{
	if (tmpl == NULL) {
		return 1;
	}

	if (g_server->maple_ctx == NULL) {
		return 2;
	}

	if (output == NULL || output_size == 0) {
		return 3;
	}
 
	va_list args;
	va_start(args, output_size);

	const char *key;
	while ((key = va_arg(args, const char*)) != NULL && strcmp(key, "") != 0) {
		const char *value = va_arg(args, const char*);
		if (value != NULL) {
			mp_set_var(g_server->maple_ctx, key, value);
		}
	}
	va_end(args);

	memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
	uint8_t ret = mp_render_segment(g_server->maple_ctx, buf, tmpl, NULL, ".");
	if (ret != 0) {
		return 4;
	}

	fseek(buf, 0, SEEK_SET);
	memcpy(output, memstream_data(mem), memstream_size(mem));
	fclose(buf);
 
	return 0;
}
 
int
papago_res_render(papago_response_t *res, const char *tmpl, char *output,
                  size_t output_size, ...)
{
	if (res == NULL || tmpl == NULL) {
		return -1;
	}

	va_list args;
	va_start(args, output_size);

	const char *key;
	while ((key = va_arg(args, const char *)) != NULL) {
		const char *value = va_arg(args, const char *);
		if (value != NULL) {
			mp_set_var(g_server->maple_ctx, key, value);
		}
	}
	va_end(args);
 
	memstream_t *mem;
    FILE *buf = _fmemopen(&mem);
	uint8_t ret = mp_render_segment(g_server->maple_ctx, buf, tmpl, NULL, ".");
	if (ret != 0) {
		return 4;
	}

	fseek(buf, 0, SEEK_SET);
	memcpy(output, memstream_data(mem), memstream_size(mem));
	fclose(buf);
 
	// send as HTML response
	papago_res_header(res, "Content-Type", "text/html; charset=utf-8");
 
	return papago_res_send(res, output);
}
