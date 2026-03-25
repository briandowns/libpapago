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

#ifdef __cplusplus
extern "C" {
#endif
 
#ifndef __MAPLE_H
#define __MAPLE_H

#include <stdint.h>
#include <stdio.h>

#define MAX_VARS    64
#define MAX_VAR_LEN 64
#define MAX_FUNCS   64

#define MP_ERR_FILE_NOT_FOUND         1
#define MP_ERR_INVALID_INCLUDE_SYNTAX 2
#define MP_ERR_MISSING_END_TAG        3
#define MP_ERR_UNABLE_TO_LOAD_INCLUDE 4

/**
 * var_t holds all variables stored in key/value pairs.
 */
typedef struct {
    char key[MAX_VAR_LEN];
    char value[256];
} var_t;

/**
 * map_func defines the signature for functions given to the template engine.
 */
typedef char *(*mp_func)(char*);

/**
 * function_t stores a template function.
 */
typedef struct {
    char name[MAX_VAR_LEN]; 
    mp_func fn;
} function_t;

/**
 * function_registry_t holds all function pointers and the total count.
 */
typedef struct {
    function_t funcs[MAX_FUNCS];
    int count;
} function_registry_t;

/**
 * mp_context_t holds all functions and variables to be used when rendering
 * a template.
 */
typedef struct {
    var_t vars[MAX_VARS];
    int var_count;
    function_registry_t user_func_registry;
} mp_context_t;

/**
 * mp_init setups the library.
 */
mp_context_t*
mp_init();

/**
 * mp_free frees the used memory for the fiven context.
 */
void
mp_free(mp_context_t *ctx);

/**
 * mp_set_var store the given key/value pair in the given context. The name is
 * the name of variable and val is the value to be stored.
 */
void
mp_set_var(mp_context_t *ctx, const char *name, const char *val);

/**
 * mp_register_func adds a user defined function to the context to be used
 * during template rendering.
 */
void
mp_register_func(mp_context_t *ctx, const char *name, mp_func fn);

/**
 * mp_render_segment renders the given template with the seeded values in the
 * provided context. The "base_dir" variable is the relative location of the 
 * template to the executable. If not in the same directory, populate this with
 * the relative path.
 */
uint8_t
mp_render_segment(mp_context_t *ctx, FILE *out, const char *tpl, 
                  const char *end, const char *base_dir);

/**
 * mp_render_file renders the given file with the seeded values in the provided
 * context.
 */
uint8_t
mp_render_file(mp_context_t *ctx, FILE *out, const char *filename,
               const char *caller_dir);

/**
 * mp_err_lookup gets the error string for the given error code.
 */
const char*
mp_err_lookup(const uint8_t code);

#endif /** end __MAPLE_H */
#ifdef __cplusplus
}
#endif
