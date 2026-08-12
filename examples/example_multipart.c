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

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../papago.h"

#define UPLOAD_DIR "./uploads"

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

static int
move_file(const char *src, const char *dst)
{
    if (rename(src, dst) == 0) {
        return 0;
    }

    if (errno != EXDEV) {
        return -1;
    }

    FILE *in = fopen(src, "rb");
    if (in == NULL) {
        return -1;
    }

    FILE *out = fopen(dst, "wb");
    if (out == NULL) {
        fclose(in);
        return -1;
    }

    char buf[65536];

    size_t n;
    bool ok = true;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            ok = false;
            break;
        }
    }

    if (ferror(in)) {
        ok = false;
    }

    fclose(in);
    if (fclose(out) != 0) {
        ok = false;
    }

    if (!ok) {
        unlink(dst);
        return -1;
    }

    unlink(src);
    return 0;
}

void
index_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(req);
    PAPAGO_UNUSED(user_data);

    papago_res_send(res,
        "<html>"
        "<body>"
        "<h1>Papago multipart/form-data example</h1>"
        "<h2>Single file + a text field</h2>"
        "<form action=\"/upload/avatar\" method=\"POST\" enctype=\"multipart/form-data\">"
        "  <input type=\"text\" name=\"display_name\" placeholder=\"display name\"><br>"
        "  <input type=\"file\" name=\"avatar\"><br>"
        "  <button type=\"submit\">Upload avatar</button>"
        "</form>"
        "<h2>Multiple files</h2>"
        "<form action=\"/upload/gallery\" method=\"POST\" enctype=\"multipart/form-data\">"
        "  <input type=\"file\" name=\"photos\" multiple><br>"
        "  <button type=\"submit\">Upload gallery</button>"
        "</form>"
        "</body>"
        "</html>");
}

/**
 * Single-file upload: an avatar image plus a text field, demonstrating
 * papago_req_file() (singular) alongside papago_req_form() for the
 * regular multipart text field.
 */
void
upload_avatar_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

    if (papago_req_body_too_large(req)) {
        papago_res_set_status(res, PAPAGO_STATUS_REQUEST_ENTITY_TOO_LARGE);
        papago_res_json(res, "{\"error\":\"upload exceeds max body size\"}");
        return;
    }

    const papago_file_upload_t *avatar = papago_req_file(req, "avatar");
    if (avatar == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_BAD_REQUEST);
        papago_res_json(res, "{\"error\":\"missing file field 'avatar'\"}");
        return;
    }

    const char *display_name = papago_req_form(req, "display_name");
    if (display_name == NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_BAD_REQUEST);
        papago_res_json(res, "{\"error\":\"missing form field 'display_name'\"}");
        return;
    }

    mkdir(UPLOAD_DIR, 0755);

    const char *filename = papago_multipart_filename(avatar);
    if (filename == NULL || filename[0] == '\0' ||
        strpbrk(filename, "/\\\"") != NULL || strstr(filename, "..") != NULL) {
        papago_res_set_status(res, PAPAGO_STATUS_BAD_REQUEST);
        papago_res_json(res, "{\"error\":\"invalid uploaded filename\"}");
        return;
    }

    char dest[512];
    snprintf(dest, sizeof(dest), "%s/%s", UPLOAD_DIR, filename);

    if (move_file(papago_multipart_tmp_path(avatar), dest) != 0) {
        papago_res_set_status(res, PAPAGO_STATUS_INTERNAL_ERROR);
        papago_res_json(res, "{\"error\":\"failed to store uploaded file\"}");
        return;
    }

    size_t size = papago_multipart_size(avatar);
    const char *content_type = papago_multipart_content_type(avatar);

    char json[512];
    snprintf(json, sizeof(json),
        "{\"status\":\"uploaded\",\"display_name\":\"%s\","
        "\"filename\":\"%s\",\"content_type\":\"%s\",\"size\":%zu}",
        display_name, filename, content_type, size);

    papago_res_set_status(res, PAPAGO_STATUS_CREATED);
    papago_res_json(res, json);
}

/**
 * Multi-file upload: an <input multiple> gallery field, demonstrating
 * papago_req_files() (plural).
 */
void
upload_gallery_handler(papago_request_t *req, papago_response_t *res, void *user_data)
{
    PAPAGO_UNUSED(user_data);

    if (papago_req_body_too_large(req)) {
        papago_res_set_status(res, PAPAGO_STATUS_REQUEST_ENTITY_TOO_LARGE);
        papago_res_json(res, "{\"error\":\"upload exceeds max body size\"}");
        return;
    }

    // first call with out=NULL just to get the count
    size_t total = papago_req_files(req, "photos", NULL, 0);
    if (total == 0) {
        papago_res_set_status(res, PAPAGO_STATUS_BAD_REQUEST);
        papago_res_json(res, "{\"error\":\"no files under field 'photos'\"}");
        return;
    }

#define MAX_GALLERY_FILES 32
    const papago_file_upload_t *files[MAX_GALLERY_FILES];
    size_t n = papago_req_files(req, "photos", files, MAX_GALLERY_FILES);

    if (total > MAX_GALLERY_FILES) {
        papago_res_set_status(res, PAPAGO_STATUS_BAD_REQUEST);
        papago_res_json(res, "{\"error\":\"too many files, max 32 per request\"}");
        return;
    }

    mkdir(UPLOAD_DIR, 0755);

    char body[4096];
    size_t off = 0;
    off += (size_t)snprintf(body + off, sizeof(body) - off,
        "{\"status\":\"uploaded\",\"count\":%zu,\"files\":[", n);

    for (size_t i = 0; i < n && off < sizeof(body) - 256; i++) {
        char dest[512];
        const char *filename = papago_multipart_filename(files[i]);
        snprintf(dest, sizeof(dest), "%s/%s", UPLOAD_DIR, filename);

        bool ok = (move_file(papago_multipart_tmp_path(files[i]), dest) == 0);

        off += (size_t)snprintf(body + off, sizeof(body) - off,
            "%s{\"filename\":\"%s\",\"size\":%zu,\"stored\":%s}",
            i > 0 ? "," : "", filename, papago_multipart_size(files[i]),
            ok ? "true" : "false");
    }

    snprintf(body + off, sizeof(body) - off, "]}");

    papago_res_set_status(res, PAPAGO_STATUS_CREATED);
    papago_res_json(res, body);
}

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

    // register HTTP routes
    papago_route(server, PAPAGO_GET, "/", index_handler, NULL);
    papago_route(server, PAPAGO_POST, "/upload/avatar", upload_avatar_handler, NULL);
    papago_route(server, PAPAGO_POST, "/upload/gallery", upload_gallery_handler, NULL);

    papago_config_t config = papago_default_config();
    config.max_body_size = 25 * 1024 * 1024; // 25MB cap for this example

    // start server (blocking)
    if (papago_start(server, &config) != 0) {
        fprintf(stderr, "%s\n", papago_error());
        papago_destroy(server);

        return 1;
    }

    // cleanup
    papago_destroy(server);

    return 0;
}
