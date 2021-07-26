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

#ifndef tcblcb_UTIL_HEADER_SEEN
#define tcblcb_UTIL_HEADER_SEEN

#include <stdbool.h>
#include <kore/kore.h>
#include <cjson/cJSON.h>
#include <libcouchbase/couchbase.h>

#define __unused __attribute__((__unused__))

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#ifdef DEBUG
#define FMT_RESPONSE true
#else
#define FMT_RESPONSE false
#endif

// debug log statements should be a no-op for release builds
#ifdef DEBUG
#define LogDebug(format, ...) kore_log(LOG_DEBUG, format, __VA_ARGS__);
#else
#define LogDebug(format, ...) // (no-op)
#endif

// use DebugQueryPayload instead so it's a no-op for release builds
void dump_query_payload(lcb_CMDQUERY *cmd);

// debug log statements should be a no-op for release builds
#ifdef DEBUG
#define DebugQueryPayload(cmd) dump_query_payload(cmd);
#else
#define DebugQueryPayload(cmd) // (no-op)
#endif

#define IfLCBFailGotoDone(res, msg) { \
lcb_STATUS xres = (res); \
if (xres != LCB_SUCCESS) { \
  kore_log(LOG_ERR, "<%s:%s:%d> %s (%s)", __FILENAME__, __func__, __LINE__, msg, lcb_strerror_long(xres)); \
  goto done; \
} }

#define IfLCBFailLogWarningMsg(res, msg) { \
lcb_STATUS xres = (res); \
if (xres != LCB_SUCCESS) { \
  kore_log(LOG_WARNING, "<%s:%s:%d> %s (%s)", __FILENAME__, __func__, __LINE__, msg, lcb_strerror_long(xres)); \
} }

#define IfLCBFailLogWarningMsgRef(res, msg, ref) { \
lcb_STATUS xres = (res); \
if (xres != LCB_SUCCESS) { \
  kore_log(LOG_WARNING, "<%s:%s:%d> %s [%s] (%s)", __FILENAME__, __func__, __LINE__, msg, ref, lcb_strerror_long(xres)); \
} }

#define IfNULLGotoDone(value, msg) \
if ((value) == NULL) { \
  kore_log(LOG_ERR, "<%s:%s:%d> %s", __FILENAME__, __func__, __LINE__, msg); \
  goto done; \
}

#define IfTrueGotoDone(value, msg) \
if (value) { \
  kore_log(LOG_ERR, "<%s:%s:%d> %s", __FILENAME__, __func__, __LINE__, msg); \
  goto done; \
}

#define IfFalseGotoDone(value, msg) IfTrueGotoDone((!(value)), msg)

#define IfBadKoreResultGotoDone(kres, msg) { \
int xkres = (kres); \
if (xkres != KORE_RESULT_OK) { \
  kore_log(LOG_WARNING, "<%s:%s:%d> %s (%d)", __FILENAME__, __func__, __LINE__, msg, xkres); \
  goto done; \
} }

#define IfBadErrnoGotoDone(res, msg) { \
int xres = (res); \
if (xres != 0) { \
  kore_log( \
    LOG_ERR, \
    "<%s:%s:%d> %s (%d) %s", \
    __FILENAME__, __func__, __LINE__, msg, xres, strerror(xres)); \
  goto done; \
} }

bool is_lower_string(const char *str);
bool is_upper_string(const char *str);
bool is_same_case(const char *str);
void to_lower_case(char *str);
void to_upper_case(char *str);

// return day of the week, where Monday == 0 ... Sunday == 6.
int weekday(const char *date_string);

// create a JSON array from an array of string references. caller must free.
// strings provided are referenced (not copied).
cJSON *create_string_array_param_json(char *strings[], int nstrings);

// create a serialized JSON array string from an array of string references. caller must free.
char *create_string_array_param_string(char *strings[], int nstrings);

// create a serialized JSON string param value. caller must free.
char *create_json_string_param(const char *value_string);

// create a serialized JSON string number value. caller must free.
char *create_json_number_param(const double value_number);

// create a UUID string. caller must free.
char *create_uuid_string();

// get a JSON doc from a SUBDOC response (decoded from JSON fragment). caller must free.
lcb_STATUS get_json_doc_from_subdoc_resp(const lcb_RESPSUBDOC *resp, size_t index, cJSON **json);

// extract the string value from a SUBDOC response. caller must free.
char *extract_string_value_from_subdoc_resp(const lcb_RESPSUBDOC *resp, size_t index);

// extract the string value from a raw JSON string value (decoding as needed). caller must free.
char *extract_string_value_from_json_string(const char *value, size_t nvalue);

// read the entire HTTP body into a buffer. caller must free.
struct kore_buf *get_http_body_buf(struct http_request *req);

// process Cross-Origin Resource Sharing (CORS) by adding response headers
void process_cors(struct http_request *req);

// process CORS response and exit early if preflight
#define ProcessCORSAndExitIfPreflight(req) \
process_cors(req); \
if (req->method == HTTP_METHOD_OPTIONS) { \
  http_response(req, 204, NULL, 0); \
  return (KORE_RESULT_OK); \
}

// HTTP response helper struct for methods that have more complex error handling paths
typedef struct tcblcb_HTTPResponse {
    int status;
    const char *string;
    size_t strlen;
} tcblcb_HTTPResponse;

#endif /* !tcblcb_UTIL_HEADER_SEEN */
