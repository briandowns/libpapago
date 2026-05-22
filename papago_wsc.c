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

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libwebsockets.h>

#include "papago_wsc.h"

/**
 * A single outbound message queued for delivery. buf is allocated as LWS_PRE
 * + payload so that lws_write() can use it directly without an extra copy.
 */
typedef struct wsc_msg {
    unsigned char *buf;
    size_t len;
    bool is_binary;
    struct wsc_msg *next;
} wsc_msg_t;

struct papago_wsc {
    struct lws_context*lws_ctx;
    struct lws *wsi;
    // copy of config so we can reconnect if needed
    char host[256];
    int port;
    char path[256];
    int lws_ssl_flags;
    papago_wsc_on_connect_t on_connect;
    papago_wsc_on_message_t on_message;
    papago_wsc_on_close_t on_close;
    papago_wsc_on_error_t on_error;
    wsc_msg_t *send_head;
    wsc_msg_t *send_tail;
    pthread_mutex_t send_mutex;
    volatile bool running;
    volatile bool connected;
    void *user_data;
    char errmsg[512];
};

/**
 * Records an error message on the client.
 */
static void
wsc_set_error(papago_wsc_t *client, const char *fmt, ...)
{
    if (client == NULL || fmt == NULL) {
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(client->errmsg, sizeof(client->errmsg), fmt, ap);
    va_end(ap);
}

/**
 * Drains one message off the send queue and writes it. This must be called
 * only from LWS_CALLBACK_CLIENT_WRITEABLE.
 */
static void
wsc_flush_one(papago_wsc_t *client)
{
    if (client == NULL || client->wsi == NULL) {
        return;
    }

    pthread_mutex_lock(&client->send_mutex);
    wsc_msg_t *msg = client->send_head;
    if (msg != NULL) {
        client->send_head = msg->next;
        if (client->send_head == NULL) {
            client->send_tail = NULL;
        }
    }
    pthread_mutex_unlock(&client->send_mutex);
    if (msg == NULL) {
        return;
    }

    enum lws_write_protocol wp =
        msg->is_binary ? LWS_WRITE_BINARY : LWS_WRITE_TEXT;
    int ret = lws_write(client->wsi, msg->buf + LWS_PRE, msg->len, wp);
    if (ret < 0) {
        wsc_set_error(client, "lws_write failed: %d", ret);
        free(msg->buf);
        free(msg);
        return;
    }

    free(msg->buf);
    free(msg);

    // if there are more messages, request another writable callback
    pthread_mutex_lock(&client->send_mutex);
    bool more = (client->send_head != NULL);
    pthread_mutex_unlock(&client->send_mutex);

    if (more) {
        lws_callback_on_writable(client->wsi);
    }
}

static int
wsc_lws_callback(struct lws *wsi, enum lws_callback_reasons reason,
                 void *user, void *in, size_t len)
{
    PAPAGO_WSC_UNUSED(user);

    papago_wsc_t *client =
        (papago_wsc_t *)lws_context_user(lws_get_context(wsi));

    if (client == NULL) {
        return 0;
    }

    switch (reason) {
    case LWS_CALLBACK_CLIENT_ESTABLISHED:
        client->connected = true;
        if (client->on_connect != NULL) {
            client->on_connect(client);
        }
        break;
    case LWS_CALLBACK_CLIENT_RECEIVE:
        if (client->on_message != NULL && in != NULL) {
            bool is_bin = (bool)lws_frame_is_binary(wsi);
            client->on_message(client, (const char *)in, len, is_bin);
        }
        break;
    case LWS_CALLBACK_CLIENT_CLOSED:
        client->connected = false;
        client->wsi       = NULL;
        if (client->on_close != NULL) {
            client->on_close(client);
        }
        // stop the run loop. caller can restart if they need reconnect
        client->running = false;
        break;
    case LWS_CALLBACK_CLIENT_CONNECTION_ERROR: {
        const char *msg = (in != NULL && len > 0)
            ? (const char *)in
            : "connection error";

        wsc_set_error(client, "%s", msg);
        client->connected = false;
        client->wsi       = NULL;

        if (client->on_error != NULL) {
            client->on_error(client, client->errmsg);
        }

        client->running = false;
        break;
    }
    case LWS_CALLBACK_CLIENT_WRITEABLE:
        wsc_flush_one(client);
        break;
    default:
        break;
    }

    return 0;
}

static const struct lws_protocols wsc_protocols[] = {
    {
        "papago-ws", //protocol name
        wsc_lws_callback, // callback 
        0, // per-session data size (we use context_user)
        65536, // rx buffer size
        0, NULL, 0
    },
    LWS_PROTOCOL_LIST_TERM
};

papago_wsc_t *
papago_wsc_new(void)
{
    papago_wsc_t *client = calloc(1, sizeof(papago_wsc_t));
    if (client == NULL) {
        return NULL;
    }

    if (pthread_mutex_init(&client->send_mutex, NULL) != 0) {
        free(client);
        return NULL;
    }

    return client;
}

papago_wsc_config_t
papago_wsc_default_config(void)
{
    papago_wsc_config_t config;
    memset(&config, 0, sizeof(config));
    config.host    = "127.0.0.1";
    config.port    = 8485;
    config.path    = "/ws";
    config.use_ssl = false;
    return config;
}

int
papago_wsc_connect(papago_wsc_t *client,
                   const papago_wsc_config_t *config,
                   papago_wsc_on_connect_t on_connect,
                   papago_wsc_on_message_t on_message,
                   papago_wsc_on_close_t on_close,
                   papago_wsc_on_error_t on_error)
{
    if (client == NULL || config == NULL) {
        return 1;
    }

    client->on_connect = on_connect;
    client->on_message = on_message;
    client->on_close = on_close;
    client->on_error = on_error;

    // copy config so we own the strings
    snprintf(client->host, sizeof(client->host), "%s",
        config->host != NULL ? config->host : "127.0.0.1");
    client->port = config->port > 0 ? config->port : 8485;
    snprintf(client->path, sizeof(client->path), "%s",
        config->path != NULL ? config->path : "/ws");
    client->lws_ssl_flags = config->use_ssl ? LCCSCF_USE_SSL : 0;

    struct lws_context_creation_info ctx_info;
    memset(&ctx_info, 0, sizeof(ctx_info));
    ctx_info.port = CONTEXT_PORT_NO_LISTEN;
    ctx_info.protocols = wsc_protocols;
    ctx_info.user = client;
    ctx_info.options = LWS_SERVER_OPTION_DO_SSL_GLOBAL_INIT;

    client->lws_ctx = lws_create_context(&ctx_info);
    if (client->lws_ctx == NULL) {
        wsc_set_error(client, "failed to create lws context");
        return 1;
    }

    struct lws_client_connect_info conn_info;
    memset(&conn_info, 0, sizeof(conn_info));
    conn_info.context = client->lws_ctx;
    conn_info.address = client->host;
    conn_info.port = client->port;
    conn_info.path = client->path;
    conn_info.host = client->host;
    conn_info.origin = client->host;
    conn_info.protocol = wsc_protocols[0].name;
    conn_info.ssl_connection = client->lws_ssl_flags;

    client->wsi = lws_client_connect_via_info(&conn_info);
    if (client->wsi == NULL) {
        wsc_set_error(client, "lws_client_connect_via_info failed for %s:%d%s",
            client->host, client->port, client->path);
        lws_context_destroy(client->lws_ctx);
        client->lws_ctx = NULL;

        return 1;
    }

    return 0;
}

int
papago_wsc_run(papago_wsc_t *client)
{
    if (client == NULL || client->lws_ctx == NULL) {
        return 1;
    }

    client->running = true;

    while (client->running) {
        if (lws_service(client->lws_ctx, 50) < 0) {
            wsc_set_error(client, "lws_service failed");
            return 1;
        }
    }

    client->running = false;
    return 0;
}

void
papago_wsc_stop(papago_wsc_t *client)
{
    if (client == NULL) {
        return;
    }

    client->running = false;

    if (client->lws_ctx != NULL) {
        lws_cancel_service(client->lws_ctx);
    }
}

void
papago_wsc_destroy(papago_wsc_t *client)
{
    if (client == NULL) {
        return;
    }

    client->running = false;

    if (client->wsi != NULL) {
        lws_close_reason(client->wsi, LWS_CLOSE_STATUS_NORMAL, NULL, 0);
        client->wsi = NULL;
    }

    if (client->lws_ctx != NULL) {
        lws_cancel_service(client->lws_ctx);
        lws_context_destroy(client->lws_ctx);
        client->lws_ctx = NULL;
    }

    // drain unsent messages
    pthread_mutex_lock(&client->send_mutex);
    wsc_msg_t *msg = client->send_head;
    wsc_msg_t *next;
    while (msg != NULL) {
        next = msg->next;
        free(msg->buf);
        free(msg);
        msg = next;
    }
    client->send_head = NULL;
    client->send_tail = NULL;
    pthread_mutex_unlock(&client->send_mutex);

    pthread_mutex_destroy(&client->send_mutex);

    free(client);
}

static int
wsc_enqueue(papago_wsc_t *client, const void *data, size_t len, bool is_binary)
{    
    if (client == NULL || client->wsi == NULL || data == NULL) {
        return 1;
    }

    wsc_msg_t *msg = malloc(sizeof(wsc_msg_t));
    if (msg == NULL) {
        return 1;
    }

    // LWS_PRE bytes of head-room + payload + null terminator for safety 
    unsigned char *buf = malloc(LWS_PRE+len+1);
    if (buf == NULL) {
        free(msg);
        return 1;
    }

    memcpy(buf+LWS_PRE, data, len);
    buf[LWS_PRE+len] = '\0';

    msg->buf = buf;
    msg->len = len;
    msg->is_binary = is_binary;
    msg->next = NULL;

    pthread_mutex_lock(&client->send_mutex);
    if (client->send_tail != NULL) {
        client->send_tail->next = msg;
    } else {
        client->send_head = msg;
    }
    client->send_tail = msg;
    bool more = true;
    pthread_mutex_unlock(&client->send_mutex);

    PAPAGO_WSC_UNUSED(more);

    // wake the lws event loop so it sends LWS_CALLBACK_CLIENT_WRITEABLE
    lws_callback_on_writable(client->wsi);

    return 0;
}

int
papago_wsc_send(papago_wsc_t *client, const char *message)
{
    if (client == NULL || message == NULL) {
        return 1;
    }
    return wsc_enqueue(client, message, strlen(message), false);
}

int
papago_wsc_send_binary(papago_wsc_t *client, const void *data, size_t length)
{
    if (client == NULL || data == NULL) {
        return 1;
    }
    return wsc_enqueue(client, data, length, true);
}

void
papago_wsc_set_userdata(papago_wsc_t *client, void *data)
{
    if (client != NULL) {
        client->user_data = data;
    }
}

void*
papago_wsc_get_userdata(papago_wsc_t *client)
{
    return client != NULL ? client->user_data : NULL;
}

const char*
papago_wsc_error(papago_wsc_t *client)
{
    if (client == NULL) {
        return "";
    }
    return client->errmsg;
}
