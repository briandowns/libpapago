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

/**
 * papago_wsc — simple WebSocket client for libpapago servers.
 *
 * Mirrors the server-side papago_ws_* API so that client and server code
 * share the same callback shapes and naming conventions.  Built on
 * libwebsockets, which is already a project dependency.
 *
 * Typical usage:
 *
 *   papago_wsc_t *c = papago_wsc_new();
 *
 *   papago_wsc_config_t cfg = papago_wsc_default_config();
 *   cfg.host = "localhost";
 *   cfg.port = 8485;
 *   cfg.path = "/ws";
 *
 *   papago_wsc_connect(c, &cfg, on_connect, on_message, on_close, on_error);
 *   papago_wsc_run(c);   // blocks until closed or papago_wsc_stop()
 *   papago_wsc_destroy(c);
 *
 * papago_wsc_send() is thread-safe and may be called from any thread while
 * papago_wsc_run() is executing.
 */

#ifndef __PAPAGO_WSC_H
#define __PAPAGO_WSC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#define PAPAGO_WSC_UNUSED(x) (void)(x)

/** 
 * WebSocket client struct.
 */
typedef struct papago_wsc papago_wsc_t;

/**
 * Client connection configuration. Initialized via papago_wsc_default_config()
 * then can be overridden by user assigned data.
 */
typedef struct {
    const char *host;
    int port;
    const char *path;
    bool use_ssl;
} papago_wsc_config_t;

/**
 * Called when the WebSocket handshake is complete and the connection is ready
 * to exchange messages.
 */
typedef void (*papago_wsc_on_connect_t)(papago_wsc_t *client);

/**
 * Called for every complete incoming message frame. Text frames are provided
 * as null-terminated strings; binary frames are provided as raw bytes. The
 * is_binary flag indicates how the message was sent by the peer, but the
 * client is free to send messages in either format regardless of the received
 * message type.
 */
typedef void (*papago_wsc_on_message_t)(papago_wsc_t *client,
                                        const char *message,
                                        size_t length,
                                        bool is_binary);

/**
 * Called when the connection is cleanly closed by either side.
 */
typedef void (*papago_wsc_on_close_t)(papago_wsc_t *client);

/**
 * Called on connection or protocol errors.  The connection may already be
 * closed when this is called.
 */
typedef void (*papago_wsc_on_error_t)(papago_wsc_t *client, const char *error);

// lifecycle 

/**
 * Create a new WebSocket client.
 *
 * @return Heap-allocated client handle, or NULL on allocation failure.
 *         The caller must eventually pass it to papago_wsc_destroy().
 */
papago_wsc_t*
papago_wsc_new(void);

/**
 * Return a config struct pre-filled with defaults.
 *
 *   host = "127.0.0.1"
 *   path = "/ws"
 *   use_ssl = false
 */
papago_wsc_config_t
papago_wsc_default_config(void);

/**
 * Configure and connect to a papago WebSocket server. Must be called before
 * papago_wsc_run(). The four callback pointers are stored by the client. NULL
 * can be passed for any callback that is unneeded. Returns 0 on success or 1
 * on failure (call papago_wsc_error() for error string).
 */
int
papago_wsc_connect(papago_wsc_t *client, const papago_wsc_config_t *config,
                   papago_wsc_on_connect_t on_connect,
                   papago_wsc_on_message_t on_message,
                   papago_wsc_on_close_t on_close,
                   papago_wsc_on_error_t on_error);

/**
 * Run the WebSocket event loop (blocking). Returns when the connection is
 * closed or papago_wsc_stop() is called from another thread. Returns 0 on
 * successful exit or1 on error.
 */
int
papago_wsc_run(papago_wsc_t *client);

/**
 * Signal the run loop to exit on its next iteration. Safe to call from any
 * thread.
 */
void
papago_wsc_stop(papago_wsc_t *client);

/**
 * Close the connection (if open), destroy the lws context, and free all
 * memory owned by the client.  Do not use the handle after this call.
 */
void
papago_wsc_destroy(papago_wsc_t *client);

/* ------------------------------------------------------------------ */
/* Sending                                                             */
/* ------------------------------------------------------------------ */

/**
 * Enqueue a UTF-8 text message for delivery to the server.
 *
 * Thread-safe.  The message is copied internally; the caller may free or
 * reuse @p message immediately after this returns.
 *
 * @return 0 on success, 1 if the message could not be enqueued.
 */
int
papago_wsc_send(papago_wsc_t *client, const char *message);

/**
 * Enqueue a binary message for delivery to the server.
 *
 * Thread-safe.  @p data is copied internally.
 *
 * @return 0 on success, 1 if the message could not be enqueued.
 */
int
papago_wsc_send_binary(papago_wsc_t *client, const void *data, size_t length);

// user data

/**
 * Attach application-specific data to the client. Accessible from any
 * callback via papago_wsc_get_userdata().
 */
void
papago_wsc_set_userdata(papago_wsc_t *client, void *data);

/**
 * Retrieve the application-specific data previously set with
 * papago_wsc_set_userdata(). Returns NULL if none was set.
 */
void*
papago_wsc_get_userdata(papago_wsc_t *client);

/**
 * Return the last error message recorded for the client. Returns an empty
 * string if no error has occurred. The returned pointer is valid until the
 * next call that modifies the client's error state, or until
 * papago_wsc_destroy().
 */
const char*
papago_wsc_error(papago_wsc_t *client);

#ifdef __cplusplus
}
#endif
#endif /** end __PAPAGO_WSC_H */
