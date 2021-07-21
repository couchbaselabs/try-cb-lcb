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

#include <ctype.h>
#include <string.h>
#include <uuid/uuid.h>
#include <kore/kore.h>
#include <kore/http.h>

#include "util.h"

void dump_query_payload(lcb_CMDQUERY *cmd)
{
    const char *payload = NULL;
    size_t payload_len = 0;
    lcb_cmdquery_encoded_payload(cmd, &payload, &payload_len);
    kore_log(LOG_DEBUG, "--- encoded payload: %.*s", (int)payload_len, payload);
}

bool is_lower_string(const char *str)
{
    while (*str) {
        if (!islower(*str++)) {
            return false;
        } 
    }
    return true;
}

bool is_upper_string(const char *str)
{
    while (*str) {
        if (!isupper(*str++)) {
            return false;
        } 
    }
    return true;
}

bool is_same_case(const char *str)
{
    if (str == NULL || *str == '\0') {
        return false;
    }

    // note that not all characters are lower or upper (e.g., digits)

    if (islower(*str)) {
        return is_lower_string(str);
    }
    
    if (isupper(*str)) {
        return is_upper_string(str);
    }

    return false;
}

void to_lower_case(char *str)
{
    while (*str) {
        *str = tolower(*str);
        str++;
    }
}

void to_upper_case(char *str)
{
    while (*str) {
        *str = toupper(*str);
        str++;
    }
}

int weekday(const char *date_string)
{
    struct tm date_tm;
    strptime(date_string, "%d/%m/%Y", &date_tm);

    // return day of the week, where Monday == 0 ... Sunday == 6.
    return (date_tm.tm_wday + 6) % 7;
}

// creates an array of cJSON_String stopping at the NULL terminator but cannot store JSON NULL values.
cJSON *create_string_array_param_json(char *strings[], int nstrings)
{
    cJSON * array_json = cJSON_CreateArray();

    for (int i=0; i<nstrings; i++) {
        char *string = strings[i];
        if (string == NULL) {
            cJSON_AddItemToArray(array_json, cJSON_CreateNull());
        } else {
            cJSON_AddItemToArray(array_json, cJSON_CreateStringReference(string));
        }
    }

    return array_json;
}

char *create_string_array_param_string(char *strings[], int nstrings)
{
    char *array_string = NULL;

    cJSON *array_json = create_string_array_param_json(strings, nstrings);
    if (array_json != NULL) {
        array_string = cJSON_PrintBuffered(array_json, BUFSIZ, false);
        cJSON_Delete(array_json);
    }
    
    return array_string;
}

char *create_json_string_param(const char *value_string)
{
    cJSON *json_string = NULL;
    if (value_string != NULL) {
        json_string = cJSON_CreateStringReference(value_string);
    }
    return json_string == NULL ? NULL : cJSON_PrintUnformatted(json_string);
}

char *create_json_number_param(const double value_number)
{
    cJSON *json_number = cJSON_CreateNumber(value_number);
    return json_number == NULL ? NULL : cJSON_PrintUnformatted(json_number);
}

char *create_uuid_string()
{
    uuid_t uuid;
    uuid_generate(uuid);

    char *uuid_str = malloc(37);
    uuid_unparse(uuid, uuid_str);

    return uuid_str;
}

lcb_STATUS get_json_doc_from_subdoc_resp(const lcb_RESPSUBDOC *resp, size_t index, cJSON **json)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    
    IfLCBFailGotoDone(
        (rc = lcb_respsubdoc_result_status(resp, index)),
        "Subdoc result not available"
    );

    const char *value = NULL;
    size_t nvalue = 0;
    IfLCBFailGotoDone(
        (rc = lcb_respsubdoc_result_value(resp, index, &value, &nvalue)),
        "Failed to get subdoc result value"
    );

    if (value != NULL && nvalue > 0) {
        *json = cJSON_ParseWithLength(value, nvalue);
    }

done:
    return rc;
}

char *extract_string_value_from_subdoc_resp(const lcb_RESPSUBDOC *resp, size_t index)
{
    char *result_value = NULL;
    cJSON *result_value_json = NULL;
    IfLCBFailGotoDone(
        get_json_doc_from_subdoc_resp(resp, index, &result_value_json),
        "Failed to get subdoc result as JSON doc"
    );

    char *parsed_result_value = cJSON_GetStringValue(result_value_json);
    if (parsed_result_value != NULL) {
        result_value = strdup(parsed_result_value);
    }

done:
    if (result_value_json != NULL) {
        cJSON_Delete(result_value_json);
    }

    return result_value;
}

char *extract_string_value_from_json_string(const char *value, size_t nvalue)
{
    char *result_value = NULL;

    // we will delete this here because we only need the extracted value
    cJSON *result_value_json = cJSON_ParseWithLength(value, nvalue);
    if (result_value_json != NULL) {
        char *parsed_result_value = cJSON_GetStringValue(result_value_json);
        if (parsed_result_value != NULL) {
            result_value = strdup(parsed_result_value);
        }
        cJSON_Delete(result_value_json);
    }

    return result_value;
}

struct kore_buf *get_http_body_buf(struct http_request *req)
{
    u_int8_t data[BUFSIZ];
	struct kore_buf *http_body_buf = kore_buf_alloc(BUFSIZ);
    ssize_t	last_bytes_read = 0;
    do {
		last_bytes_read = http_body_read(req, data, sizeof(data));
		if (last_bytes_read < 0) {
            LogDebug("Failed to read HTTP body @ offset:%zu", http_body_buf->offset);
            kore_buf_free(http_body_buf);
            return NULL;
		}
        
        if (last_bytes_read > 0) {
		    kore_buf_append(http_body_buf, data, last_bytes_read);
        }
    } while(last_bytes_read > 0);

    return http_body_buf;
}

void process_cors(struct http_request *req)
{
    // allow the origin (cannot use wildcard with credentials)
    const char *origin = NULL;
    if (http_request_header(req, "Origin", &origin) == KORE_RESULT_OK) {
		http_response_header(req, "Access-Control-Allow-Origin", origin);
    }

    // always allow credentials and the methods we support
    http_response_header(req, "Access-Control-Allow-Credentials", "true");
    http_response_header(req, "Access-Control-Allow-Methods", "POST, GET, PUT, OPTIONS");

    // allow whatever controls are requested
    const char *cors_headers = NULL;
    if (http_request_header(req, "Access-Control-Request-Headers", &cors_headers) == KORE_RESULT_OK) {
		http_response_header(req, "Access-Control-Allow-Headers", cors_headers);
    }
}
