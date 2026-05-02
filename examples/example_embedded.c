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

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "../papago.h"

static papago_t *server = NULL;

/**
 * Signal handler for graceful shutdown.
 */
static void
signal_handler(int sig)
{
	PAPAGO_UNUSED(sig);

	printf("\nShutting down...\n");
	if (server != NULL) {
		papago_stop(server);
	}
}

static const char index_html[] = 
	"<!DOCTYPE html>\n"
	"<html>\n"
	"<head>\n"
	"  <meta charset=\"UTF-8\">\n"
	"  <title>Static File Serving Demo</title>\n"
	"  <style>\n"
	"    body { font-family: sans-serif; margin: 40px; background: #f0f0f0; }\n"
	"    .container { background: white; padding: 30px; border-radius: 8px; max-width: 800px; }\n"
	"    h1 { color: #4CAF50; }\n"
	"    .badge { background: #2196F3; color: white; padding: 4px 8px; border-radius: 3px; font-size: 12px; }\n"
	"    code { background: #eee; padding: 2px 6px; border-radius: 3px; }\n"
	"  </style>\n"
	"</head>\n"
	"<body>\n"
	"  <div class=\"container\">\n"
	"    <h1>Static File Serving Demo</h1>\n"
	"    <p><span class=\"badge\">EMBEDDED</span> This file is compiled into the binary!</p>\n"
	"    <h2>Two Methods:</h2>\n"
	"    <h3>1. Embedded Files (this page)</h3>\n"
	"    <ul>\n"
	"      <li>Compiled into binary</li>\n"
	"      <li>No external dependencies</li>\n"
	"      <li>Single file deployment</li>\n"
	"    </ul>\n"
	"    <h3>2. Directory-based Static Files</h3>\n"
	"    <ul>\n"
	"      <li>Traditional file serving</li>\n"
	"      <li>Update without recompile</li>\n"
	"      <li>Automatic MIME types</li>\n"
	"    </ul>\n"
	"    <h2>Try:</h2>\n"
	"    <ul>\n"
	"      <li><a href=\"/app.js\">app.js</a> (embedded)</li>\n"
	"      <li><a href=\"/static/disk.html\">disk.html</a> (from /tmp/static/)</li>\n"
	"    </ul>\n"
	"    <script src=\"/app.js\"></script>\n"
	"  </div>\n"
	"</body>\n"
	"</html>";
 
static const char app_js[] = 
	"console.log('Embedded JavaScript loaded!');\n"
	"console.log('This file is compiled into the binary.');\n"
	"\n"
	"// Add some interactivity\n"
	"document.addEventListener('DOMContentLoaded', function() {\n"
	"  const p = document.createElement('p');\n"
	"  p.innerHTML = '<strong>JavaScript Active:</strong> This message was added by embedded JS!';\n"
	"  p.style.background = '#e8f5e9';\n"
	"  p.style.padding = '15px';\n"
	"  p.style.borderRadius = '5px';\n"
	"  document.querySelector('.container').appendChild(p);\n"
	"});\n";
 
/**
 * Embedded files array
 */
static const papago_embedded_file_t embedded_files[] = {
    {
        "/",
        "text/html; charset=utf-8", 
        (const unsigned char *)index_html, sizeof(index_html)-1
	},
    {
		"/index.html",
		"text/html; charset=utf-8", 
    	(const unsigned char *)index_html, sizeof(index_html)-1
	},
    {
		"/app.js",
		"application/javascript; charset=utf-8", 
    	(const unsigned char *)app_js, sizeof(app_js)-1
	},
    {
		NULL, NULL, NULL, 0
	}
};

int
main(void)
{
	// setup signal handling
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	// create server
	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	papago_config_t config = papago_default_config();
	config.port = 8282;
	config.enable_logging = true;
	papago_configure(server, &config);

    papago_register_embedded_files(server, embedded_files);
    papago_get(server, "/api/*", papago_serve_embedded_handler, NULL);

	// start server (blocking)
	if (papago_start(server) != 0) {
        fprintf(stderr, "%s\n", papago_error(server));
        papago_destroy(server);

        return 1;
    }

	// cleanup
	papago_destroy(server);

	return 0;
}
