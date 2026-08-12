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

#ifndef __PAPAGO_CLIENT_H
#define __PAPAGO_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "papago.h"

typedef struct papago_http_client papago_http_client_t;

/**
 * papago_http_header_t is used for both request and response headers.
 */
typedef struct papago_http_header {
    char *key;
    char *value;
    struct papago_http_header *next;
} papago_http_header_t;

/**
 * papago_http_request_t describes an outgoing HTTP request.
 * Initialise with papago_http_request_default() then override fields.
 */
typedef struct {
    const char *url; // required
    papago_method_t method; // default: GET
    const char *body; // request body, or NULL
    size_t body_len; // 0 to strlen(body)
    const char *content_type; // e.g. "application/json"
    papago_http_header_t *headers; // extra headers linked list
    long timeout_ms; // 0 to 30 000 ms
    bool follow_redirects; // default: true
} papago_http_request_t;

/**
 * papago_http_response_t holds the result of a completed request. Call
 * papago_http_response_free() when done.
 */
typedef struct {
    int status_code;
    char *body;
    size_t body_len;
    papago_http_header_t *headers;
} papago_http_response_t;

/**
 * Create a new HTTP client. Returns NULL on failure. One client per process is
 * sufficient as it is internally thread safe.
 */
papago_http_client_t*
papago_client_new(void);

/**
 * Enable or disable TLS peer/host certificate verification for requests.
 * Defaults to true. Only disable this for local development against
 * self-signed certificates.
 */
void
papago_client_set_ssl_verify(papago_http_client_t *client, bool verify);

/**
 * Load a CA bundle file (PEM) to validate server certificates against, in
 * addition to (or instead of) the system default trust store. Pass NULL to
 * clear a previously set file.
 */
int
papago_client_set_ca_file(papago_http_client_t *client, const char *path);

/**
 * Load a directory of CA certificates (OpenSSL c_rehash format) to validate
 * server certificates against. Pass NULL to clear a previously set path.
 */
int
papago_client_set_ca_path(papago_http_client_t *client, const char *path);

/**
 * Destroy client and release global resources.
 */
void
papago_client_destroy(papago_http_client_t *client);

/**
 * Return a default-initialised request struct.
 */
papago_http_request_t
papago_request_default(void);

/**
 * Sends a given request. Returns 0 on success or 1 on failure. Caller must
 * call papago_http_response_free(res) after use.
 */
int
papago_http_send(papago_http_client_t *client,
                 const papago_http_request_t *req,
                 papago_http_response_t *res);

/**
 * Free memory held by res.
 */
void
papago_http_response_free(papago_http_response_t *res);

int
papago_http_get(papago_http_client_t *client, const char *url,
                papago_http_response_t *res);

int
papago_http_post_json(papago_http_client_t *client, const char *url,
                      const char *json_body, papago_http_response_t *res);

int
papago_http_put_json(papago_http_client_t *client, const char *url,
                     const char *json_body, papago_http_response_t *res);

int
papago_http_delete(papago_http_client_t *client, const char *url,
                   papago_http_response_t *res);

/**
 * Create a new header. Returns NULL on failure.
 */
papago_http_header_t*
papago_header_new(const char *key, const char *value);

/**
 * Append a header. Returns new head or NULL on failure.
 */
papago_http_header_t*
papago_header_append(papago_http_header_t *list, const char *key,
                     const char *value);

/**
 * Case-insensitive lookup. Returns string or NULL.
 */
const char*
papago_header_get(const papago_http_header_t *list, const char *key);

/**
 * Free the list and all key/value strings.
 */
void
papago_header_free_all(papago_http_header_t *list);

#ifdef __cplusplus
}
#endif
#endif /* __PAPAGO_HTTP_CLIENT_H */
