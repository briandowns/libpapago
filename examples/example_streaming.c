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

#include <dirent.h>
#include <stdio.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../papago.h"

static papago_t *server = NULL;
static const char *files_dir = "/tmp";

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

/**
 * stream file handler
 */
void
serve_file_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(user_data);

	// get filename from path parameter
	const char *filename = papago_req_param(req, "filename");
	if (filename == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_send(res, "no filename specified");
		return;
	}

	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", files_dir, filename);

	// check if file exists
	struct stat st;
	if (stat(filepath, &st) != 0) {
		papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
		papago_res_send(res, "file not found");
		return;
	}

	// check if it's a regular file
	if (!S_ISREG(st.st_mode)) {
		papago_res_status(res, PAPAGO_STATUS_FORBIDDEN);
		papago_res_send(res, "not a regular file");
		return;
	}

	printf("streaming file: %s (%ld bytes)\n", filepath, st.st_size);

	// stream the file - automatically detects MIME type
	if (papago_res_sendfile(res, filepath) != 0) {
		papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
		papago_res_send(res, "failed to stream file");
	}
}

/**
 * force download handler
 */
void
download_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(user_data);

	const char *filename = papago_req_param(req, "filename");
	if (filename == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_send(res, "no filename specified");
		return;
	}

	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", files_dir, filename);

	struct stat st;
	if (stat(filepath, &st) != 0) {
		papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
		papago_res_send(res, "file not found");
		return;
	}

	// force download with Content-Disposition header
	char disposition[256];
	snprintf(disposition, sizeof(disposition), 
	    "attachment; filename=\"%s\"", filename);
	papago_res_header(res, PAPAGO_RESPONSE_HEADER_CONTENT_DISPOSITION,
		disposition);

	printf("forcing download: %s\n", filename);

	if (papago_res_sendfile(res, filepath) != 0) {
		papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
		papago_res_send(res, "failed to stream file");
	}
}

/**
 * video streaming handler
 */
void
video_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(user_data);

	const char *filename = papago_req_param(req, "filename");
	if (filename == NULL) {
		papago_res_status(res, PAPAGO_STATUS_BAD_REQUEST);
		papago_res_send(res, "no filename specified");
		return;
	}

	char filepath[1024];
	snprintf(filepath, sizeof(filepath), "%s/%s", files_dir, filename);

	struct stat st;
	if (stat(filepath, &st) != 0) {
		papago_res_status(res, PAPAGO_STATUS_NOT_FOUND);
		papago_res_send(res, "file not found");
		return;
	}

	// enable range requests for video seeking
	papago_res_header(res, PAPAGO_RESPONSE_HEADER_ACCEPT_RANGES, "bytes");

	printf("streaming video: %s\n", filename);

	if (papago_res_sendfile(res, filepath) != 0) {
		papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
		papago_res_send(res, "failed to stream file");
	}
}

/**
 * List files handler
 */
void
list_files_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(user_data);

	DIR *dir = opendir(files_dir);
	if (dir == NULL) {
		papago_res_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
		papago_res_send(res, "cannot open directory");
		return;
	}

	char html[8192];
	int len = 0;
	len += snprintf(html + len, sizeof(html) - len,
	    "<html><head><title>Files</title>"
	    "<style>body{font-family:sans-serif;margin:40px;}"
	    "table{border-collapse:collapse;width:100%%;}"
	    "th,td{border:1px solid #ddd;padding:8px;text-align:left;}"
	    "th{background:#4CAF50;color:white;}"
	    "a{color:#2196F3;text-decoration:none;}"
	    "a:hover{text-decoration:underline;}</style></head>"
	    "<body><h1>Files</h1><table>"
	    "<tr><th>Filename</th><th>Size</th><th>Actions</th></tr>");

	char filepath[1024];
	struct stat st;
	struct dirent *entry;

	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.') {
			continue;
		}

		snprintf(filepath, sizeof(filepath), "%s/%s", files_dir,
			entry->d_name);

		if (stat(filepath, &st) == 0 && S_ISREG(st.st_mode)) {
			long size_kb = st.st_size / 1024;
			
			len += snprintf(html + len, sizeof(html) - len,
			    "<tr><td>%s</td><td>%ld KB</td>"
			    "<td><a href='/files/%s'>View</a> | "
			    "<a href='/download/%s'>Download</a> | "
			    "<a href='/video/%s'>Stream</a></td></tr>",
			    entry->d_name, size_kb,
			    entry->d_name, entry->d_name, entry->d_name);
		}
	}

	closedir(dir);

	len += snprintf(html + len, sizeof(html) - len, "</table></body></html>");

	papago_res_send(res, html);
}

/**
 * index handler
 */
void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
	PAPAGO_UNUSED(req);
	PAPAGO_UNUSED(user_data);

	const char *html = 
	    "<html><head><title>Papago File Streaming Demo</title>"
	    "<style>body{font-family:sans-serif;margin:40px;max-width:800px;}"
	    "h1{color:#4CAF50;}</style></head>"
	    "<body>"
	    "<h1>Papago File Streaming Example</h1>"
	    "<p>This demo shows how Papago efficiently streams files of any size.</p>"
	    "<h2>Features:</h2>"
	    "<ul>"
	    "<li>Zero-copy file streaming</li>"
	    "<li>Automatic MIME type detection</li>"
	    "<li>No memory loading (handles GB files)</li>"
	    "<li>Force download support</li>"
	    "<li>Video streaming ready</li>"
	    "</ul>"
	    "<h2>Try it:</h2>"
	    "<ul>"
	    "<li><a href='/files'>List all files</a></li>"
	    "</ul>"
	    "</body></html>";

	papago_res_send(res, html);
}

int
main(void)
{
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	papago_config_t config;

	server = papago_new();
	if (server == NULL) {
		fprintf(stderr, "failed to create server\n");
		return 1;
	}

	config = papago_default_config();
	config.port = 8484;
	papago_configure(server, &config);

	// register routes
	papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
	papago_route(server, PAPAGO_GET, "/files", list_files_handler, NULL);
	papago_route(server, PAPAGO_GET, "/files/:filename", serve_file_handler, NULL);
	papago_route(server, PAPAGO_GET, "/download/:filename", download_handler, NULL);
	papago_route(server, PAPAGO_GET, "/video/:filename", video_handler, NULL);

	printf("Serving files from: %s\n\n", files_dir);
	printf("Server running on http://localhost:8484\n\n");

	papago_start(server);
	papago_destroy(server);

	return 0;
}
