# papago

Modern web framework designed to be full featured and powerful all while being extremely simple to use.

## Features

- RESTful Routing - GET, POST, PUT, DELETE, PATCH support
- Path Parameters - Dynamic routes like `/users/:id`
- Wildcard URIs - Example: `/api/v1/*` 
- Query Parameters - Parse URL query strings
- HTML Templates - Easy dynamic content via server side templating
- Middleware System - Global and path-specific middleware
- File stream - video / audio / large files, zero-copy, automatic MIME type detection
- WebSocket Support - Real-time bidirectional communication
- Embedded File Support - Embed HTML, JS, CSS, etc into the application
- JSON Responses - Built-in JSON helpers
- Static Files - Serve files from directories
- Thread-Safe - Built on proven concurrent architecture
- Low Dependencies - Only requires libmicrohttpd + libwebsockets
- Rate limiting by IP
- Compression with Gzip
- Metrics collection and exposure via Prometheus endpoint

### MIME Types Supported
HTML, CSS, JS, JSON, XML, TXT
PNG, JPG, GIF, SVG, ICO, WebP
MP4, WebM, OGG
MP3, WAV, M4A
PDF, ZIP, TAR, GZ
WOFF, WOFF2, TTF

## Dependencies

* libnmicrohttpd
* libwebsockets
* jansson
* openssl
* libmaple - [Maple Template Engine](https://github.com/briandowns/libmaple)
* liblogger - [Structured(json) Logger](https://github.com/briandowns/liblogger)

### Build

```sh 
make
```

## Quick Start

Below is a very simple demonstration of how to create a handler for `GET` request. More examples can be found in the [examples](/examples) directory. Each example has a `Makefile` target or you can make every example with `make examples_all`.

```sh
make example
```

```sh
make example_ssl
```

```sh
make example_websocket
```

### Hello World

```c
#include <stdio.h>

#include "papago.h"

void
hello_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	papago_res_json(res, "{\"message\":\"Hello, World!\"}");
}

int
main(void)
{
	papago_t *server = papago_new();
	
    papago_config_t config = papago_default_config();
	config.port = 8080;
	papago_configure(server, &config);
	
	papago_route(server, PAPAGO_GET, "/hello", hello_handler, NULL);
	
	papago_start(server); // blocking
	
	papago_destroy(server);

	return 0;
}
```

Build and run:

```sh
cc -o hello hello.c logger.c maple.c papago.c -lwebsockets -lmicrohttpd -ljansson -lssl -lcrypto -lz -lm
./hello
```

Test:

```sh
curl http://localhost:8080/hello
# {"message":"Hello, World!"}
```

## Core Concepts

### HTTP Routes

```c
// basic routes
papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
papago_route(server, PAPAGO_POST, "/users", create_user, NULL);
papago_route(server, PAPAGO_PUT, "/users/:id", update_user, NULL);
papago_route(server, PAPAGO_DELETE, "/users/:id", delete_user, NULL);

// path parameters
void
user_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    const char *id = papago_req_param(req, "id");
    // use id...
}
papago_route(server, PAPAGO_GET, "/users/:id", user_handler, NULL);

// query parameters
void
search_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    const char *q = papago_req_query(req, "q");
    const char *page = papago_req_query(req, "page");
    // use q and page...
}
papago_route(server, PAPAGO_GET, "/search", search_handler, NULL);
```

### Middleware

```c
// auth middleware
bool
auth(papago_request_t *req, papago_response_t *res, void *user_data)
{
    const char *token = papago_req_header(req, "Authorization");

    if (token == NULL) {
        papago_res_status(res, PAPAGO_STATUS_UNAUTHORIZED);
        papago_res_json(res, "{\"error\":\"unauthorized\"}");
        return false;  // stop processing
    }

    return true;
}

// global middleware - runs on ALL routes
papago_middleware_add(server, logger);

// path-specific - runs only on /api/* routes
papago_middleware_path_add(server, "/api", auth);
```

### Websocket

```c
void
ws_on_connect(papago_ws_connection_t *conn)
{
    printf("Client connected: %s\n", papago_ws_get_client_ip(conn));
    papago_ws_send(conn, "{\"type\":\"welcome\"}");
}

void
ws_on_message(papago_ws_connection_t *conn, const char *message,
              size_t length, bool is_binary)
{
    printf("Received: %s\n", message);
    
    // echo back
    papago_ws_send(conn, message);
    
    // or broadcast to all
    papago_ws_broadcast(papago_get_current_server(), message);
}

void
ws_on_close(papago_ws_connection_t *conn)
{
    printf("client disconnected\n");
}

void
ws_on_error(papago_ws_connection_t *conn, const char *error)
{
    fprintf(stderr, "error: %s\n", error);
}

// register websocket endpoint
papago_ws_endpoint(server, "/ws", 
    ws_on_connect, ws_on_message, ws_on_close, ws_on_error);
```

Client-side JavaScript:

```javascript
const ws = new WebSocket('ws://localhost:8081/ws');

ws.onopen = () => ws.send('Hello!');
ws.onmessage = (e) => console.log('Received:', e.data);
```

### Request & Response

```c
void
handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    // read request
    const char *header = papago_req_header(req, "Content-Type");
    const char *id = papago_req_param(req, "id");
    const char *search = papago_req_query(req, "q");
    const char *body = papago_req_body(req);
    
    // send response
    papago_res_status(res, PAPAGO_STATUS_OK);
    papago_res_header(res, "X-Custom", "value");
    papago_res_json(res, "{\"status\":\"ok\"}");
}
```

## Metrics

To expose the Prometheus endpoint, register the metrics handler in your application.

```c
papago_route(server, PAPAGO_GET, "/metrics", papago_metrics_handler, NULL);
```

### Threading Model

- HTTP: Thread-per-connection (libmicrohttpd)
- WebSocket: Event loop in separate thread
- Broadcast: Thread-safe with mutex protection

## Contributing

Please feel free to open a PR!

## Contact

Brian Downs [@bdowns328](http://twitter.com/bdowns328)

## License

BSD 2 Clause [License](/LICENSE).
