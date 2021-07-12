/**
 * Copyright (C) 2021 Couchbase, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALING
 * IN THE SOFTWARE.
 */

#ifndef tcblcb_MAIN_HEADER_SEEN
#define tcblcb_MAIN_HEADER_SEEN

#include <stdbool.h>
#include <strings.h>
#include <kore/kore.h>
#include <kore/http.h>
#include <cjson/cJSON.h>
#include <libcouchbase/couchbase.h>

// thread local instance
extern _Thread_local lcb_INSTANCE *_tcblcb_lcb_instance;

// global callbacks use a response delegate for component logic (e.g., to aggregate responses)
typedef void (*tcblcb_RESPDELEGATE_CALLBACK)(lcb_INSTANCE *instance, void *cookie, const lcb_RESPBASE *resp);

// global callbacks use a response delegate for component logic (e.g., to aggregate responses)
typedef struct tcblcb_RESPDELEGATE {
    void *cookie;
    tcblcb_RESPDELEGATE_CALLBACK callback;
} tcblcb_RESPDELEGATE;

#endif /* !tcblcb_MAIN_HEADER_SEEN */
