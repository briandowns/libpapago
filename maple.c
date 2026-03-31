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
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "maple.h"

#define PATH_MAX_LEN      512
#define MAX_CACHED        64
#define MAX_INCLUDE_STACK 32 

/**
 * cached_template_t 
 */
typedef struct {
    char path[PATH_MAX_LEN];
    char *content;
    time_t mtime;
} cached_template_t;

static cached_template_t cache[MAX_CACHED];
static function_registry_t builtin_func_registry = {0};
static int cache_count = 0;
static char include_stack[MAX_INCLUDE_STACK][PATH_MAX_LEN];
static int include_depth = 0;

/**
 * trim removes unnecessary whitespace.
 */
inline static void
trim(char *s)
{
    while (isspace(*s)) {
        memmove(s, s + 1, strlen(s));
    }

    char *e = s + strlen(s) - 1;
    while (e >= s && isspace(*e)) {
        *e-- = '\0';
    }
}

char*
mp_upper(char *s)
{
    if (strlen(s) < 1) {
        return s;
    }

    for (uint64_t i = 0; s[i]; i++) {
        s[i] = toupper(s[i]);
    }

    return s;
}

char*
mp_lower(char *s)
{
    if (strlen(s) < 1) {
        return s;
    }

    for(uint64_t i = 0; s[i]; i++) {
        s[i] = tolower(s[i]);
    }

    return s;
}

char*
mp_title(char *s)
{
    if (strlen(s) < 1) {
        return s;
    }

    uint64_t i;
    uint64_t cap = 1;
    
    for (i = 0; s[i]; i++) {
        s[i] = cap ? toupper(s[i]) : tolower(s[i]); cap = (s[i] == ' ');
    }
    s[i] = 0;

    return s;
}

char*
mp_reverse(char *s)
{
    if (strlen(s) < 1) {
        return s;
    }

    for (uint64_t i = 0, j = strlen(s)-1; i < j; i++, j--) {
        int c = s[i];
        s[i] = s[j];
        s[j] = c;
    }

    return s;
}

/**
 * register_builtin_func registers builtin functions to the builtin registry.
 */
static void
register_builtin_func(const char *name, mp_func fn)
{
    if (builtin_func_registry.count < MAX_FUNCS) {
        strncpy(builtin_func_registry.funcs[builtin_func_registry.count].name,
            name, MAX_VAR_LEN);
        builtin_func_registry.funcs[builtin_func_registry.count].fn = fn;
        builtin_func_registry.count++;
    }
}

mp_context_t*
mp_init()
{
    mp_context_t *ctx = (mp_context_t*)calloc(1, sizeof(mp_context_t));
    ctx->var_count = 0;

    register_builtin_func("upper", mp_upper); 
    register_builtin_func("lower", mp_lower);
    register_builtin_func("title", mp_title);
    register_builtin_func("reverse", mp_reverse);

    return ctx;
}

void
mp_free(mp_context_t *ctx)
{
    if (ctx != NULL) {
        free(ctx);
    }
}

void
mp_set_var(mp_context_t *ctx, const char *name, const char *val)
{
    for (uint64_t i = 0; i < ctx->var_count; i++) {
        if (!strcmp(ctx->vars[i].key, name)) {
            strncpy(ctx->vars[i].value, val, 255);
            return;
        }
    }
        
    if (ctx->var_count < MAX_VARS) {
        strncpy(ctx->vars[ctx->var_count].key, name, MAX_VAR_LEN);
        strncpy(ctx->vars[ctx->var_count].value, val, 255);
        ctx->var_count++;
    }
}

char*
get_var(mp_context_t *ctx, const char *key)
{
    for (uint64_t i = 0; i < ctx->var_count; i++) {
        if (!strcmp(ctx->vars[i].key, key)) {
            return ctx->vars[i].value;
        }
    }

    return "";
}

void
mp_register_func(mp_context_t *ctx, const char* name, mp_func fn)
{
    if (ctx->user_func_registry.count < MAX_FUNCS) {
        strncpy(ctx->user_func_registry.funcs[ctx->user_func_registry.count].name,
            name, MAX_VAR_LEN);
        ctx->user_func_registry.funcs[ctx->user_func_registry.count].fn = fn;
        ctx->user_func_registry.count++;
    }
}

/**
 * find_func looks up the given function by name on the context if it a user
 * defined function or in the builtin registty and returns it. If nothing is
 * found, NULL is returned.
 */
static mp_func
find_func(mp_context_t *ctx, const char *name)
{
    for (uint8_t i = 0; i < ctx->user_func_registry.count; i++) {
        if (!strcmp(ctx->user_func_registry.funcs[i].name, name)) {
            return ctx->user_func_registry.funcs[i].fn;
        }
    }

    for (uint8_t i = 0; i < builtin_func_registry.count; i++) {
        if (!strcmp(builtin_func_registry.funcs[i].name, name)) {
            return builtin_func_registry.funcs[i].fn;
        }
    }

    return NULL;
}

/* file & cache handling */

/**
 * dirname_from_path gets the directory name from the given path.
 */
static void
dirname_from_path(const char *path, char *dir)
{
    const char *sl = strrchr(path, '/');

    if (sl) {
        size_t len = sl - path;
        strncpy(dir, path, len);
        dir[len] = '\0';
    } else {
        strcpy(dir, ".");
    }
}

/**
 * join_path returns a full path for the given file.
 */
static void
join_path(char *out, const char *base, const char *rel)
{
    strcpy(out, base);

    size_t len = strlen(out);
    if (len > 0 && out[len - 1] != '/') {
        strcat(out, "/");
    }

    strcat(out, rel);
}

/**
 * file_mtime gets the modified time for the given file. 
 */
static time_t
file_mtime(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? st.st_mtime : 0;
}

/**
 * cache_lookup checks if the given file is in cache.
 */
static cached_template_t*
cache_lookup(const char *path)
{
    for (uint8_t i = 0; i < cache_count; i++) {
        if (!strcmp(cache[i].path, path)) {
            return &cache[i];
        }
    }

    return NULL;
}

/**
 * cache_load
 */
static char*
cache_load(const char *path)
{
    cached_template_t *cache_item = cache_lookup(path);

    time_t now = file_mtime(path);
    if (cache_item && cache_item->mtime == now) {
        return cache_item->content;
    }

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        // file not found. calling function will 
        // handle the NULL knowing there's no file.
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    rewind(fp);

    char *content = malloc(len + 1);
    size_t ret = fread(content, 1, len, fp);
    if (ret == 0) {
        return NULL;
    }

    content[len] = 0;
    fclose(fp);

    if (cache_item) {
        free(cache_item->content);
        cache_item->content = content;
        cache_item->mtime = now;
    } else if (cache_count < MAX_CACHED) {
        strncpy(cache[cache_count].path, path, PATH_MAX_LEN);
        cache[cache_count].content = content;
        cache[cache_count].mtime = now;
        cache_count++;
    }

    return content;
}

/* include guards */

/**
 * is_included checks to see if a template file at the given path has
 * already been included.
 */
static bool
is_included(const char *fullpath)
{
    for (uint8_t i = 0; i < include_depth; i++) {
        if (!strcmp(include_stack[i], fullpath)) {
            return true;
        }
    }

    return false;
}

/**
 * push_include stores the path of the included template file.
 */
static void
push_include(const char *fullpath)
{
    if (include_depth < MAX_INCLUDE_STACK) {
        strncpy(include_stack[include_depth++], fullpath, PATH_MAX_LEN);
    }
}

/**
 * pop_include essentially moves the stack pointer up (decreases). 
 */
static void
pop_include()
{
    if (include_depth > 0) {
        include_depth--;
    }
}

/* expression parser with comparisons */

// forward declaration for simplicity
static double
parse_expr(mp_context_t *ctx, const char **tr);

/**
 * parse_factor
 */
static double
parse_factor(mp_context_t *ctx, const char **str)
{
    while (isspace(**str)) {
        (*str)++;
    }

    if (**str == '(') {
        (*str)++;

        double v = parse_expr(ctx, str);

        if (**str == ')') {
            (*str)++;
            return v;
        }
    }

    if (**str == '!') {
        (*str)++;
        return !parse_factor(ctx, str);
    }

    char tok[64];
    uint64_t i = 0;

    if (isalpha(**str)) {
        while (isalnum(**str) || **str == '_') {
            tok[i++] = *(*str)++; tok[i] = 0;
        }
        return atof(get_var(ctx, tok));
    }

    if (isdigit(**str) || **str == '.') {
        return strtod(*str, (char**)str);
    }

    return 0;
}

/**
 * parse_term 
 */
static double
parse_term(mp_context_t *ctx, const char **str)
{
    double v = parse_factor(ctx, str);

    while (1) {
        while (isspace(**str)) {
            (*str)++;
        }

        if (**str == '*') {
            (*str)++;
            v *= parse_factor(ctx, str);
        } else if (**str == '/') {
            (*str)++;
            double d = parse_factor(ctx, str);
            v = d ? v / d : 0;
        } else {
            break;
        }
    }

    return v;
}

/**
 * parse_arith
 */
static double
parse_arith(mp_context_t *ctx, const char **str)
{
    double v = parse_term(ctx, str);

    while (1) {
        while (isspace(**str)) {
            (*str)++;
        }

        if (**str == '+') {
            (*str)++;
            v += parse_term(ctx, str);
        } else if (**str == '-') {
            (*str)++;
            v -= parse_term(ctx, str);
        }
        else {
            break;
        }
    }

    return v;
}

/**
 * parse_compare 
 */
static double
parse_compare(mp_context_t *ctx, const char **str)
{
    double v = parse_arith(ctx, str);

    while (1) {
        while (isspace(**str)) {
            (*str)++;
        }

        if (strncmp(*str, ">=", 2) == 0) {
            (*str) += 2;
            v = v >= parse_arith(ctx, str);
        } else if (strncmp(*str, "<=", 2) == 0) {
            (*str) += 2;
            v = v <= parse_arith(ctx, str);
        } else if (strncmp(*str, "==", 2) == 0) {
            (*str) += 2;
            v = fabs(v - parse_arith(ctx, str)) < 1e-9;
        } else if (strncmp(*str, "!=", 2) == 0) {
            (*str) += 2;
            v = fabs(v - parse_arith(ctx, str)) >= 1e-9;
        } else if (**str == '>') {
            (*str)++;
            v = v > parse_arith(ctx, str);
        } else if (**str == '<') {
            (*str)++;
            v = v < parse_arith(ctx, str);
        } else {
            break;
        }
    }

    return v;
}

/**
 * parse_logic
 */
static double
parse_logic(mp_context_t *ctx, const char **str)
{
    double v = parse_compare(ctx, str);

    while (1) {
        while (isspace(**str)) {
            (*str)++;
        }

        if (strncmp(*str, "&&", 2) == 0) {
            (*str) += 2;
            v = (v && parse_compare(ctx, str)); 
        } else if (strncmp(*str, "||", 2) == 0) {
            (*str) += 2;
            v = (v || parse_compare(ctx, str));
        } else {
            break;
        }
    }

    return v;
}

/**
 * parse_expr
 */
static double
parse_expr(mp_context_t *ctx, const char **str)
{
    return parse_logic(ctx, str);
}

/**
 * eval_expr
 */
static double
eval_expr(mp_context_t *ctx, const char *expr)
{
    const char* p = expr;
    return parse_expr(ctx, &p);
}

uint8_t
mp_render_file(mp_context_t *ctx, FILE *out, const char *filename,
               const char *caller_dir)
{
    char full_path[PATH_MAX_LEN];
    char abs_path[PATH_MAX_LEN];

    if (filename[0] == '/' || (strlen(filename) > 2 && filename[1] == ':')) {
        snprintf(full_path, sizeof(full_path), "%s", filename);
    } else {
        join_path(full_path, caller_dir ? caller_dir : ".", filename);
    }

    if (!realpath(full_path, abs_path)) {
        return MP_ERR_FILE_NOT_FOUND;
    }

    // guard against cyclic include
    if (is_included(abs_path)) {
        return 0;
    }

    push_include(abs_path);

    char *content = cache_load(abs_path);
    if (content == NULL) {
        pop_include();
        return MP_ERR_UNABLE_TO_LOAD_INCLUDE;
    }

    // derive relative dir for nested includes
    char dir[PATH_MAX_LEN];
    dirname_from_path(abs_path, dir);

    mp_render_segment(ctx, out, content, NULL, dir);

    pop_include();

    return 0;
}

/**
 * html_escape receives a string and performs HTML escaping and returns a
 * new string.
 */
static char*
html_escape(const char *s)
{
    static char buf[1024];
    char *p = buf;

    while (*s && (p - buf) < 1020) {
        switch (*s) {
            case '&':
                strcpy(p, "&amp;");
                p += 5; break;
            case '<':
                strcpy(p, "&lt;");
                p += 4; break;
            case '>':
                strcpy(p, "&gt;");
                p += 4; break;
            case '"':
                strcpy(p, "&quot;");
                p += 6; break;
            case '\'':
            strcpy(p, "&#39;");
                p += 5; break;
            default:
                *p++ = *s;
                break;
        }
        s++;
    }
    *p = 0;

    return buf;
}

/**
 * mp_render_segment
 */
uint8_t
mp_render_segment(mp_context_t *ctx, FILE *out, const char *tpl, 
                  const char *end, const char *base_dir)
{
    const char *p = tpl;

    while (*p && (!end || strncmp(p, end, strlen(end)) != 0)) {
        if (*p == '{' && *(p + 1) == '{') {
            p += 2;
            char token[512];
            int i = 0;

            while (*p && !(*p == '}' && *(p + 1) == '}')) {
                token[i++] = *p++;
            }
            token[i] = 0; p += 2; trim(token);

            if (!strncmp(token, "if ", 3)) {
                char cond[256]; 
                strcpy(cond, token + 3);

                const char *if_start = p;
                const char *end_tag = NULL;
                const char *else_tag = NULL;
                uint8_t depth = 1;

                const char* scan = p;
                while (*scan && depth > 0) {
                    if (strncmp(scan, "{{ if", 5) == 0) {
                        depth++;
                    } else if (strncmp(scan, "{{ range", 8) == 0) {
                        depth++;
                    } else if (strncmp(scan, "{{ end }}", 8) == 0) {
                        depth--;
                        if (depth == 0) {
                            end_tag = scan;
                            break;
                        }
                    } else if (depth == 1 && !else_tag && strncmp(scan, "{{ else }}", 9) == 0) {
                        else_tag = scan;
                    }
                    scan++;
                }

                if (!end_tag) {
                    return MP_ERR_MISSING_END_TAG;
                }

                uint8_t truth = eval_expr(ctx, cond) != 0;
                uint8_t ret = 0;

                if (truth) {
                    ret = mp_render_segment(ctx, out, if_start, else_tag ? "{{ else }}" : "{{ end }}", base_dir);
                } else if (else_tag) {
                    ret = mp_render_segment(ctx, out, else_tag + strlen("{{ else }}"), "{{ end }}", base_dir);
                }
                if (ret != 0) {
                    return ret;
                }

                p = end_tag + strlen("{{ end }}");
                continue;
            }

            if (!strncmp(token, "range ", 6)) {
                char list[64];
                strcpy(list, token + 6);

                const char *range_start = p;
                uint8_t depth = 1;
                const char *end_tag = NULL;

                // match {{ end }} properly even inside nested if/range
                const char *scan = p;
                while (*scan && depth > 0) {
                    if (strncmp(scan, "{{ range", 8) == 0) {
                        depth++;
                    } else if (strncmp(scan, "{{ if", 5) == 0) {
                        depth++;
                    } else if (strncmp(scan, "{{ end }}", 8) == 0) {
                        depth--;
                    }
                    scan++;
                }

                end_tag = strstr(scan - 1, "{{ end }}");
                if (!end_tag) {
                    return MP_ERR_MISSING_END_TAG;
                }

                const char *items = get_var(ctx, list);
                char buf[512];
                strcpy(buf, items);
                char *it = strtok(buf, ",");

                while (it) {
                    trim(it);
                    mp_set_var(ctx, ".", it);
                    mp_render_segment(ctx, out, range_start, "{{ end }}", base_dir);
                    it = strtok(NULL, ",");
                }

                p = end_tag + strlen("{{ end }}");
                continue;
            }

            if (!strncmp(token, "include ", 8)) {
                char fname[256];
                
                if (sscanf(token + 8, "\"%255[^\"]\"", fname) == 1) {
                    uint8_t ret = mp_render_file(ctx, out, fname, base_dir);
                    if (ret != 0) {
                        return ret;
                    }
                } else {
                    return MP_ERR_INVALID_INCLUDE_SYNTAX;
                }
                continue;
            }

            char func[64];
            char arg[128];

            if (sscanf(token, "%63s %127[^\n]", func, arg) == 2) {
                mp_func f = find_func(ctx, func);
                if (f) {
                    fprintf(out, "%s", html_escape(f(get_var(ctx, arg))));
                    continue;
                }
            }

            if (strpbrk(token, "+-*/&|!()><=")) {
                const double result = eval_expr(ctx, token);

                // all numbers are doubles but there's no reason to print 
                // useless zeros after the decimal point so we determine 
                // the value is a whole number and cast to int
                if (floor(result) == ceil(result)) {
                    fprintf(out, "%d", (int)result);
                } else {
                    fprintf(out, "%.2f", result);
                }
            }
            
            if (!strncmp(token, "safe ", 5)) {
                const char *var = token + 5;

                while (isspace(*var)) {
                    var++;
                }
                fprintf(out, "%s", get_var(ctx, var));
            } else {
                const char *val = get_var(ctx, token);
                fprintf(out, "%s", html_escape(val));
            }
        } else {
            fputc(*p++, out);
        }
    }

    return 0;
}

const char*
mp_err_lookup(const uint8_t code)
{
    switch (code) {
        case MP_ERR_FILE_NOT_FOUND:
            return "file not found";
        case MP_ERR_INVALID_INCLUDE_SYNTAX:
            return "invalid include syntax";
        case MP_ERR_MISSING_END_TAG:
            return "missing end tag";
        case MP_ERR_UNABLE_TO_LOAD_INCLUDE:
            return "unable to load include";
        default:
            return "unknown error code";
    }
}
