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

#include <ctype.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "papago.h"
#include "papago_client.h"

_Thread_local static char hc_err[512];

static void
hc_set_err(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(hc_err, sizeof(hc_err), fmt, ap);
    va_end(ap);
}

// global curl init ref-count (process-wide, guarded by mutex)
static pthread_mutex_t ref_count_mu = PTHREAD_MUTEX_INITIALIZER;
static int refs = 0;

static int
ref_inc(void)
{
    pthread_mutex_lock(&ref_count_mu);
    int ret = 0;
    if (refs == 0) {
        ret = (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) ? -1 : 0;
    }
    if (ret == 0) {
        refs++;
    }
    pthread_mutex_unlock(&ref_count_mu);

    return ret;
}

static void
ref_dec(void)
{
    pthread_mutex_lock(&ref_count_mu);
    if (refs > 0 && --refs == 0) {
        curl_global_cleanup();
    }
    pthread_mutex_unlock(&ref_count_mu);
}

struct papago_http_client {
    // reserved for future options 
    // (proxy, auth, pool size)
    int pad;
};

typedef struct {
    char   *data;
    size_t  len;
    size_t  cap;
} req_body_t;

static size_t
write_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
    req_body_t *b = (req_body_t *)ud;
    size_t n = size * nmemb;

    if (b->len + n + 1 > b->cap) {
        size_t nc = b->cap == 0 ? 8192 : b->cap * 2;
        while (nc < b->len + n + 1) {
            nc *= 2;
        }

        char *tmp = realloc(b->data, nc);
        if (tmp == NULL) {
            // signals CURLE_WRITE_ERROR and causes
            //curl to abort the request
            return 0;
        }
        b->data = tmp;
        b->cap  = nc;
    }

    memcpy(b->data + b->len, ptr, n);
    b->len        += n;
    b->data[b->len] = '\0';

    return n;
}

/**
 * response header parser 
 */
static size_t
header_cb(char *ptr, size_t size, size_t nmemb, void *ud)
{
    papago_http_header_t **list = (papago_http_header_t**)ud;
    size_t len = size * nmemb;

    if (len < 3 || strncmp(ptr, "HTTP/", 5) == 0 ||
        ptr[0] == '\r' || ptr[0] == '\n') {
        return len;
    }

    char *colon = memchr(ptr, ':', len);
    if (colon == NULL) {
        return len;
    }

    size_t klen = (size_t)(colon - ptr);
    char  *key  = malloc(klen + 1);
    if (key == NULL) {
        return len;
    }
    memcpy(key, ptr, klen);
    key[klen] = '\0';

    const char *vs = colon + 1;
    while (*vs == ' ' || *vs == '\t') {
        vs++;
    }

    const char *ve = ptr + len;
    while (ve > vs && (*(ve-1) == '\r' || *(ve-1) == '\n')) {
        ve--;
    }

    size_t vlen = (size_t)(ve - vs);
    char *val = malloc(vlen + 1);
    if (val == NULL) { 
        free(key); return len;
    }
    memcpy(val, vs, vlen);
    val[vlen] = '\0';

    papago_http_header_t *node = malloc(sizeof(*node));
    if (node == NULL) {
        free(key); free(val); return len;
    }
    node->key   = key;
    node->value = val;
    node->next  = *list;
    *list = node;

    return len;
}

papago_http_client_t*
papago_client_new(void)
{
    if (ref_inc() != 0) {
        hc_set_err("curl_global_init failed");
        return NULL;
    }

    papago_http_client_t *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        ref_dec();
        return NULL;
    }

    return c;
}

void
papago_client_destroy(papago_http_client_t *client)
{
    if (client == NULL) {
        return;
    }

    ref_dec();
    free(client);
}

papago_http_request_t
papago_request_default(void)
{
    return (papago_http_request_t){
        .url = NULL,
        .method = PAPAGO_GET,
        .body = NULL,
        .body_len = 0,
        .content_type = NULL,
        .headers = NULL,
        .timeout_ms = 30000,
        .follow_redirects = true,
        .verify_ssl = true,
    };
}

int
papago_http_send(papago_http_client_t *client,
                 const papago_http_request_t *req,
                 papago_http_response_t *res)
{
    if (client == NULL || req == NULL || req->url == NULL || res == NULL) {
        hc_set_err("papago_http_send: invalid arguments");
        return 1;
    }

    memset(res, 0, sizeof(*res));
    req_body_t body_buf = {0};

    CURL *curl = curl_easy_init();
    if (curl == NULL) {
        hc_set_err("curl_easy_init failed");
        return 1;
    }

    curl_easy_setopt(curl, CURLOPT_URL, req->url);

    size_t blen = req->body ?
        (req->body_len > 0 ? req->body_len : strlen(req->body)) : 0;

    switch (req->method) {
    case PAPAGO_POST:
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        if (req->body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)blen);
        } else {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
        }
        break;
    case PAPAGO_PUT:
    case PAPAGO_PATCH:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST,
            papago_req_method((papago_request_t*)req));
        if (req->body) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->body);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)blen);
        }
        break;
    case PAPAGO_DELETE:
    case PAPAGO_HEAD:
    case PAPAGO_OPTIONS:
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST,
            papago_req_method((papago_request_t*)req));
        break;
    default:
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        break;
    }

    struct curl_slist *slist = NULL;
    if (req->content_type) {
        char ct[256];
        snprintf(ct, sizeof(ct), "Content-Type: %s", req->content_type);
        slist = curl_slist_append(slist, ct);
    }

    for (papago_http_header_t *h = req->headers; h; h = h->next) {
        char hdr[1024];
        snprintf(hdr, sizeof(hdr), "%s: %s", h->key, h->value);
        slist = curl_slist_append(slist, hdr);
    }

    if (slist) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body_buf);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &res->headers);

    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
        req->timeout_ms > 0 ? req->timeout_ms : 30000L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,
        req->follow_redirects ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,
        req->verify_ssl ? 1L : 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST,
        req->verify_ssl ? 2L : 0L);

    CURLcode rc = curl_easy_perform(curl);
    if (rc != CURLE_OK) {
        hc_set_err("curl_easy_perform: %s", curl_easy_strerror(rc));
        free(body_buf.data);

        papago_header_free_all(res->headers);
        res->headers = NULL;

        curl_slist_free_all(slist);
        curl_easy_cleanup(curl);

        return 1;
    }

    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    res->status_code = (int)code;
    res->body = body_buf.data;
    res->body_len = body_buf.len;

    curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

    return 0;
}

void
papago_http_response_free(papago_http_response_t *res)
{
    if (res == NULL) {
        return;
    }

    free(res->body);
    papago_header_free_all(res->headers);
    memset(res, 0, sizeof(*res));
}

papago_http_header_t*
papago_header_new(const char *key, const char *value)
{
    if (key == NULL || value == NULL) {
        return NULL;
    }

    papago_http_header_t *h = malloc(sizeof(*h));
    if (h == NULL) {
        return NULL;
    }

    h->key   = strdup(key);
    h->value = strdup(value);
    h->next  = NULL;

    if (h->key == NULL || h->value == NULL) {
        free(h->key);
        free(h->value);
        free(h);
        return NULL;
    }

    return h;
}

papago_http_header_t*
papago_header_append(papago_http_header_t *list, const char *key,
                     const char *value)
{
    if (list == NULL) {
        return list;
    }

    papago_http_header_t *node = papago_header_new(key, value);
    if (node == NULL) {
        return list;
    }

    papago_http_header_t *tail = list;
    while (tail->next) {
        tail = tail->next;
    }
    tail->next = node;

    return list;
}

const char*
papago_header_get(const papago_http_header_t *list, const char *key)
{
    for (const papago_http_header_t *h = list; h; h = h->next) {
        if (strcasecmp(h->key, key) == 0) {
            return h->value;
        }
    }

    return NULL;
}

void
papago_header_free_all(papago_http_header_t *list)
{
    while (list != NULL) {
        papago_http_header_t *next = list->next;
        free(list->key);
        free(list->value);
        free(list);
        list = next;
    }
}

int
papago_http_get(papago_http_client_t *client, const char *url,
                papago_http_response_t *res)
{
    papago_http_request_t req = papago_request_default();
    req.url = url;
    req.method = PAPAGO_GET;

    return papago_http_send(client, &req, res);
}

int
papago_http_post_json(papago_http_client_t *client, const char *url,
                      const char *json_body, papago_http_response_t *res)
{
    papago_http_request_t req = papago_request_default();
    req.url = url;
    req.method = PAPAGO_POST;
    req.body = json_body;
    req.content_type = "application/json";

    return papago_http_send(client, &req, res);
}

int
papago_http_put_json(papago_http_client_t *client, const char *url,
                     const char *json_body, papago_http_response_t *res)
{
    papago_http_request_t req = papago_request_default();
    req.url = url;
    req.method = PAPAGO_PUT;
    req.body = json_body;
    req.content_type = "application/json";

    return papago_http_send(client, &req, res);
}

int
papago_http_delete(papago_http_client_t *client, const char *url,
                   papago_http_response_t *res)
{
    papago_http_request_t req = papago_request_default();
    req.url = url;
    req.method = PAPAGO_DELETE;

    return papago_http_send(client, &req, res);
}
