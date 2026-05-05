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

/*
 * Papago Feature Demo - Real-time Chat Application
 * - HTTP routing (GET, POST, PUT, DELETE)
 * - Path parameters (/user/:id)
 * - Query parameters (?page=1)
 * - Middleware (logging, auth)
 * - websocket (real-time chat)
 * - JSON responses
 * - Static file serving
 *
 * Test:
 *   Open http://localhost:8484 in browser
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <logger.h>

#include "../papago.h"

static papago_t *server = NULL;

typedef struct {
	int id;
	char username[64];
	char email[128];
	time_t created_at;
} user_t;

static user_t users[100];
static size_t user_count = 0;

typedef struct {
	char username[64];
	time_t connected_at;
	int message_count;
} chat_user_t;

/**
 * Signal handler for graceful shutdown
 */
static void
signal_handler(int sig)
{
	PAPAGO_UNUSED(sig);

	s_log(S_LOG_INFO, "shutting down gracefully...");
	if (server != NULL) {
		papago_stop(server);
	}
}

/**
 * middleware simple API key auth
 */
bool
auth_middleware(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

	const char *api_key = papago_req_header(req, "X-API-Key");

	if (api_key == NULL || strcmp(api_key, "secret123") != 0) {
		papago_res_status(res, PAPAGO_STATUS_UNAUTHORIZED);
		papago_res_json(res, "{\"error\":\"Unauthorized\","
		    "\"message\":\"Valid X-API-Key header required\"}");

		return false;
	}

	return true;
}

// HTTP route handlers

/**
 * index page
 */
void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

	static const char html[] =
	    "<!DOCTYPE html>\n"
	    "<html>\n"
	    "<head>\n"
	    "  <meta charset=\"UTF-8\">\n"
	    "  <title>Papago Demo - Chat</title>\n"
	    "  <style>\n"
	    "    * { margin: 0; padding: 0; box-sizing: border-box; }\n"
	    "    body { font-family: system-ui, sans-serif; background: #f5f5f5; }\n"
	    "    .container { max-width: 1200px; margin: 0 auto; padding: 20px; }\n"
	    "    h1 { color: #333; margin-bottom: 10px; }\n"
	    "    .subtitle { color: #666; margin-bottom: 30px; }\n"
	    "    .features { display: grid; grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); gap: 20px; margin-bottom: 30px; }\n"
	    "    .feature { background: white; padding: 20px; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
	    "    .feature h3 { color: #4f46e5; margin-bottom: 10px; }\n"
	    "    .feature code { background: #f0f0f0; padding: 2px 6px; border-radius: 3px; font-size: 0.9em; }\n"
	    "    #chat { background: white; border-radius: 8px; box-shadow: 0 2px 4px rgba(0,0,0,0.1); }\n"
	    "    #chat-header { background: #4f46e5; color: white; padding: 15px 20px; border-radius: 8px 8px 0 0; display: flex; align-items: center; gap: 10px; }\n"
	    "    #status { width: 10px; height: 10px; border-radius: 50%; background: #ef4444; }\n"
	    "    #status.connected { background: #22c55e; }\n"
	    "    #messages { height: 400px; overflow-y: auto; padding: 20px; border-bottom: 1px solid #e0e0e0; }\n"
	    "    .message { margin-bottom: 15px; }\n"
	    "    .message.system { color: #666; font-style: italic; text-align: center; font-size: 0.9em; }\n"
	    "    .message.user .username { font-weight: bold; color: #4f46e5; }\n"
	    "    .message.user .text { margin-top: 3px; }\n"
	    "    .message.user .time { font-size: 0.8em; color: #999; margin-left: 10px; }\n"
	    "    #input-area { padding: 20px; display: flex; gap: 10px; }\n"
	    "    #message-input { flex: 1; padding: 10px 15px; border: 1px solid #ddd; border-radius: 20px; outline: none; }\n"
	    "    #message-input:focus { border-color: #4f46e5; }\n"
	    "    button { padding: 10px 20px; background: #4f46e5; color: white; border: none; border-radius: 20px; cursor: pointer; }\n"
	    "    button:hover { background: #4338ca; }\n"
	    "    button:disabled { background: #ccc; cursor: not-allowed; }\n"
	    "  </style>\n"
	    "</head>\n"
	    "<body>\n"
	    "  <div class=\"container\">\n"
	    "    <h1>Papago Feature Demo</h1>\n"
	    "    <p class=\"subtitle\">Real-time chat with HTTP API + WebSocket</p>\n"
	    "\n"
	    "    <div class=\"features\">\n"
	    "      <div class=\"feature\">\n"
	    "        <h3>📡 HTTP API</h3>\n"
	    "        <p>Try: <code>curl http://localhost:8484/api/users</code></p>\n"
	    "      </div>\n"
	    "      <div class=\"feature\">\n"
	    "        <h3>🔐 Auth Middleware</h3>\n"
	    "        <p>Add header: <code>X-API-Key: secret123</code></p>\n"
	    "      </div>\n"
	    "      <div class=\"feature\">\n"
	    "        <h3>⚡ WebSocket</h3>\n"
	    "        <p>Real-time chat below (port 8485)</p>\n"
	    "      </div>\n"
	    "    </div>\n"
	    "\n"
	    "    <div id=\"chat\">\n"
	    "      <div id=\"chat-header\">\n"
	    "        <div id=\"status\"></div>\n"
	    "        <span>Live Chat</span>\n"
	    "        <span style=\"margin-left: auto; opacity: 0.8; font-size: 0.9em;\" id=\"username\">Guest</span>\n"
	    "      </div>\n"
	    "      <div id=\"messages\"></div>\n"
	    "      <div id=\"input-area\">\n"
	    "        <input type=\"text\" id=\"message-input\" placeholder=\"Type a message...\" disabled>\n"
	    "        <button id=\"send-btn\" onclick=\"sendMessage()\" disabled>Send</button>\n"
	    "      </div>\n"
	    "    </div>\n"
	    "  </div>\n"
	    "\n"
	    "  <script>\n"
	    "    let ws, myUsername = 'Guest';\n"
	    "    const messages = document.getElementById('messages');\n"
	    "    const input = document.getElementById('message-input');\n"
	    "    const sendBtn = document.getElementById('send-btn');\n"
	    "    const status = document.getElementById('status');\n"
	    "    const usernameEl = document.getElementById('username');\n"
	    "\n"
	    "    function connect() {\n"
	    "      ws = new WebSocket('ws://localhost:8485/ws');\n"
	    "\n"
	    "      ws.onopen = () => {\n"
	    "        status.className = 'connected';\n"
	    "        input.disabled = false;\n"
	    "        sendBtn.disabled = false;\n"
	    "        addMessage('Connected to server', 'system');\n"
	    "      };\n"
	    "\n"
	    "      ws.onclose = () => {\n"
	    "        status.className = '';\n"
	    "        input.disabled = true;\n"
	    "        sendBtn.disabled = true;\n"
	    "        addMessage('Disconnected. Reconnecting...', 'system');\n"
	    "        setTimeout(connect, 3000);\n"
	    "      };\n"
	    "\n"
	    "      ws.onmessage = (e) => {\n"
	    "        try {\n"
	    "          const msg = JSON.parse(e.data);\n"
	    "          if (msg.type === 'welcome') {\n"
	    "            myUsername = msg.username;\n"
	    "            usernameEl.textContent = myUsername;\n"
	    "            addMessage('Welcome, ' + myUsername + '!', 'system');\n"
	    "          } else if (msg.type === 'message') {\n"
	    "            addMessage(msg.username + ': ' + msg.text, 'user', msg.time);\n"
	    "          } else if (msg.type === 'join') {\n"
	    "            addMessage(msg.username + ' joined the chat', 'system');\n"
	    "          } else if (msg.type === 'leave') {\n"
	    "            addMessage(msg.username + ' left the chat', 'system');\n"
	    "          }\n"
	    "        } catch (e) {\n"
	    "          console.error('Invalid JSON:', e);\n"
	    "        }\n"
	    "      };\n"
	    "    }\n"
	    "\n"
	    "    function addMessage(text, type, time) {\n"
	    "      const div = document.createElement('div');\n"
	    "      div.className = 'message ' + type;\n"
	    "\n"
	    "      if (type === 'user') {\n"
	    "        const parts = text.split(': ', 2);\n"
	    "        div.innerHTML = '<div class=\"username\">' + parts[0] + \n"
	    "                       (time ? '<span class=\"time\">' + time + '</span>' : '') +\n"
	    "                       '</div><div class=\"text\">' + parts[1] + '</div>';\n"
	    "      } else {\n"
	    "        div.textContent = text;\n"
	    "      }\n"
	    "\n"
	    "      messages.appendChild(div);\n"
	    "      messages.scrollTop = messages.scrollHeight;\n"
	    "    }\n"
	    "\n"
	    "    function sendMessage() {\n"
	    "      const text = input.value.trim();\n"
	    "      if (!text || ws.readyState !== WebSocket.OPEN) return;\n"
	    "\n"
	    "      ws.send(JSON.stringify({ type: 'message', text: text }));\n"
	    "      input.value = '';\n"
	    "      input.focus();\n"
	    "    }\n"
	    "\n"
	    "    input.addEventListener('keypress', (e) => {\n"
	    "      if (e.key === 'Enter') sendMessage();\n"
	    "    });\n"
	    "\n"
	    "    connect();\n"
	    "  </script>\n"
	    "</body>\n"
	    "</html>\n";

	papago_res_header(res, "Content-Type", "text/html; charset=utf-8");
	papago_res_send(res, html);
}

/**
 * List all users
 */
void
api_users_list(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(res);
	PAPAGO_UNUSED(user_data);

    char json[4096];
	char *p = json;
	size_t remaining = sizeof(json);

	p += snprintf(p, remaining, "{\"users\":[");
	remaining = sizeof(json) - (p - json);

	for (size_t i = 0; i < user_count; i++) {
		p += snprintf(p, remaining,
		    "%s{\"id\":%d,\"username\":\"%s\",\"email\":\"%s\"}",
		    i > 0 ? "," : "", users[i].id, users[i].username,
		    users[i].email);
		remaining = sizeof(json) - (p - json);
	}

	snprintf(p, remaining, "],\"total\":%zu}", user_count);
	papago_res_json(res, json);
}

/**
 * Create new user
 */
void
api_users_create(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

	if (user_count >= 100) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_json(res, "{\"error\":\"User limit reached\"}");

		return;
	}

	const char *body = papago_req_body(req);
	if (body == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_json(res, "{\"error\":\"Missing request body\"}");

		return;
	}

	// simple JSON parsing - in production use a real JSON library
	user_t *user = &users[user_count];
	user->id = user_count + 1;
	user->created_at = time(NULL);

	// extract username (simple substring search)
	const char *username_start = strstr(body, "\"username\":");
	if (username_start != NULL) {
		username_start = strchr(username_start, ':') + 1;

		while (*username_start == ' ' || *username_start == '"') {
			username_start++;
        }

		const char *username_end = strchr(username_start, '"');
		if (username_end != NULL) {
			size_t len = username_end - username_start;
			if (len < sizeof(user->username)) {
				memcpy(user->username, username_start, len);
				user->username[len] = '\0';
			}
		}
	}

	// extract email
	const char *email_start = strstr(body, "\"email\":");
	if (email_start != NULL) {
		email_start = strchr(email_start, ':') + 1;

		while (*email_start == ' ' || *email_start == '"') {
			email_start++;
        }

		const char *email_end = strchr(email_start, '"');

		if (email_end != NULL) {
			size_t len = email_end - email_start;
			if (len < sizeof(user->email)) {
				memcpy(user->email, email_start, len);
				user->email[len] = '\0';
			}
		}
	}

	user_count++;

    char response[256];
	snprintf(response, sizeof(response),
	    "{\"id\":%d,\"username\":\"%s\",\"email\":\"%s\"}",
	    user->id, user->username, user->email);

	papago_res_status(res, PAPAGO_STATUS_CREATED);
	papago_res_json(res, response);
}

/**
 * Retrieve specific user
 */
void
api_users_get(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

	const char *id_str = papago_req_param(req, "id");
	if (id_str == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_json(res, "{\"error\":\"Missing user ID\"}");
		return;
	}

	int id = atoi(id_str);
    char response[256];

	for (size_t i = 0; i < user_count; i++) {
		if (users[i].id == id) {
			snprintf(response, sizeof(response),
			    "{\"id\":%d,\"username\":\"%s\",\"email\":\"%s\"}",
			    users[i].id, users[i].username, users[i].email);
			papago_res_json(res, response);
			return;
		}
	}

	papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
	papago_res_json(res, "{\"error\":\"User not found\"}");
}

/**
 * Delete user
 */
void
api_users_delete(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

	const char *id_str = papago_req_param(req, "id");
	if (id_str == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_json(res, "{\"error\":\"Missing user ID\"}");

		return;
	}

	int id = atoi(id_str);

	for (size_t i = 0; i < user_count; i++) {
		if (users[i].id == id) {
			// shift remaining users
			for (size_t j = i; j < user_count - 1; j++) {
				users[j] = users[j + 1];
            }
			user_count--;

			papago_res_status(res, PAPAGO_STATUS_NO_CONTENT);
			papago_res_send(res, "");

			return;
		}
	}

	papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
	papago_res_json(res, "{\"error\":\"User not found\"}");
}

/**
 * Retrieve server statistics
 */
void
api_stats(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    char response[256];
	snprintf(response, sizeof(response),
	    "{\"users\":%zu,\"uptime\":\"running\"}", user_count);

	papago_res_json(res, response);
}

// websocket handlers

void
ws_on_connect(papago_ws_connection_t *conn)
{
	chat_user_t *user = calloc(1, sizeof(chat_user_t));
	if (user == NULL) {
		return;
    }

	time_t now = time(NULL);

	snprintf(user->username, sizeof(user->username), "User%04d",
        (int)(now % 10000));
	user->connected_at = now;
	user->message_count = 0;

	papago_ws_set_userdata(conn, user);

	s_log(S_LOG_INFO, "client connected",
		s_log_string("username", user->username),
		s_log_string("ip", papago_ws_get_client_ip(conn)));

	// send welcome
    char welcome[256];
	snprintf(welcome, sizeof(welcome),
	    "{\"type\":\"welcome\",\"username\":\"%s\"}", user->username);
	papago_ws_send(conn, welcome);

	// announce to others
	snprintf(welcome, sizeof(welcome),
	    "{\"type\":\"join\",\"username\":\"%s\"}", user->username);
	papago_ws_broadcast(papago_get_current_server(), welcome);
}

void
ws_on_message(papago_ws_connection_t *conn, const char *message, size_t length,
              bool is_binary)
{
    PAPAGO_UNUSED(length);

	chat_user_t *user = papago_ws_get_userdata(conn);
	if (user == NULL) {
		return;
	}

	if (is_binary) {
		papago_ws_send(conn, "{\"type\":\"error\","
		    "\"message\":\"Binary messages not supported\"}");

		return;
	}

	s_log(S_LOG_INFO, "received message",
		s_log_string("username", user->username),
		s_log_string("message", message));

	// parse message type
	const char *type_start = strstr(message, "\"type\":");
	if (type_start == NULL) {
		papago_ws_send(conn, "{\"type\":\"error\"," \
            "\"message\":\"Invalid message format\"}");

		return;
	}

	if (strstr(type_start, "\"message\"") != NULL) {
		// extract message text
		const char *text_start = strstr(message, "\"text\":");
		if (text_start != NULL) {
			text_start = strchr(text_start, ':') + 1;

			while (*text_start == ' ' || *text_start == '"') {
				text_start++;
            }

			const char *text_end = strchr(text_start, '"');
			if (text_end != NULL) {
				size_t len = text_end - text_start;
                char text[512];

				if (len < sizeof(text)) {
					memcpy(text, text_start, len);
					text[len] = '\0';

					user->message_count++;

					// get timestamp
					time_t now = time(NULL);
					struct tm *tm_info = localtime(&now);

                    char timestamp[32];
					strftime(timestamp, sizeof(timestamp), "%H:%M", tm_info);

					// broadcast to all
                    char response[1024];
					snprintf(response, sizeof(response),
					    "{\"type\":\"message\","
					    "\"username\":\"%s\","
					    "\"text\":\"%s\","
					    "\"time\":\"%s\"}",
					    user->username, text, timestamp);

					papago_ws_broadcast(papago_get_current_server(), response);
				}
			}
		}
	}
}

void
ws_on_close(papago_ws_connection_t *conn) 
{
	char leave[256];

	chat_user_t *user = papago_ws_get_userdata(conn);
	if (user == NULL) {
		return;
    }

	s_log(S_LOG_INFO, "client disconnected",
		s_log_string("username", user->username),
		s_log_int("messages_sent", user->message_count));

	// announce departure
	snprintf(leave, sizeof(leave), "{\"type\":\"leave\",\"username\":\"%s\"}",
	    user->username);
	papago_ws_broadcast(papago_get_current_server(), leave);

	free(user);
}

void
ws_on_error(papago_ws_connection_t *conn, const char *error)
{
	chat_user_t *user = papago_ws_get_userdata(conn);
	s_log(S_LOG_ERROR, "websocket error",
		s_log_string("username", user != NULL ? user->username : "unknown"),
		s_log_string("error", error));
}

int
main(void)
{
	// setup signal handling
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// create and configure server
	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	papago_config_t config = papago_default_config();
	config.port = 8484;
	config.enable_logging = true;
	papago_configure(server, &config);

	// add some demo users
	strcpy(users[0].username, "Jess");
	strcpy(users[0].email, "jess@ocean.com");
	users[0].id = 1;
	users[0].created_at = time(NULL);

	strcpy(users[1].username, "picklefish");
	strcpy(users[1].email, "picklefish@ocean.com");
	users[1].id = 2;
	users[1].created_at = time(NULL);

	user_count = 2;

	// register HTTP routes
	papago_get(server, "/", index_handler, NULL);
	papago_get(server, "/api/stats", api_stats, NULL);

	// protected API routes (require X-API-Key header)
	papago_middleware_path_add(server, "/api/users", auth_middleware);
	papago_get(server, "/api/users", api_users_list, NULL);
	papago_post(server, "/api/users", api_users_create, NULL);
	papago_get(server, "/api/users/:id", api_users_get, NULL);
	papago_delete(server, "/api/users/:id", api_users_delete, NULL);

	// register websocket endpoint
	papago_ws_endpoint(server, "/ws", ws_on_connect, ws_on_message,
	    ws_on_close, ws_on_error);

	printf("Features demo'd:\n");
	printf("  HTTP Routes (GET, POST, DELETE)\n");
	printf("  Path Parameters (/api/users/:id)\n");
	printf("  Middleware (logging, auth)\n");
	printf("  WebSocket (real-time chat)\n\n");

	printf("Server listening on:\n");
	printf("  HTTP:      http://localhost:8484\n");
	printf("  WebSocket: ws://localhost:8485/ws\n\n");
	printf("  curl -H 'X-API-Key: secret123' http://localhost:8484/api/users\n\n");

	printf("Create user\n");
	printf("  curl -X POST -H 'X-API-Key: secret123' \\\n");
	printf("       -d '{\"username\":\"charlie\",\"email\":\"charlie@example.com\"}' \\\n");
	printf("       http://localhost:8484/api/users\n\n");
	printf("  curl -H 'X-API-Key: secret123' http://localhost:8484/api/users/1\n\n");
	printf("  curl -X DELETE -H 'X-API-Key: secret123' http://localhost:8484/api/users/2\n\n");
	printf("  curl http://localhost:8484/api/stats\n\n");

	printf("Press Ctrl+C to stop\n");

	// start server (blocking)
	if (papago_start(server) != 0) {
		s_log(S_LOG_ERROR, "failed to start server",
			s_log_string("error", papago_error(server)));
        papago_destroy(server);

        return 1;
    }

	// cleanup
	papago_destroy(server);

	s_log(S_LOG_INFO, "server stopped");

	return 0;
}
