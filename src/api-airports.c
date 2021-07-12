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

#include "try-cb-lcb.h"
#include "util.h"

static void airports_query_callback(__unused lcb_INSTANCE *instance, __unused int type, const lcb_RESPQUERY *resp)
{
    IfLCBFailGotoDone(
        lcb_respquery_status(resp),
        "Failed to execute query"
    );

    const char *row;
    size_t nrow;
    IfLCBFailGotoDone(
        lcb_respquery_row(resp, &row, &nrow),
        "Failed to get query response row"
    );

    if (lcb_respquery_is_final(resp)) {
        LogDebug("Query Metadata:\n%.*s", (int)nrow, row);
    } else {
        cJSON *response_json_data_array = NULL;
        IfLCBFailGotoDone(
            lcb_respquery_cookie(resp, (void**)(&response_json_data_array)),
            "Failed to get query response cookie"
        );
        IfFalseGotoDone(
            cJSON_IsArray(response_json_data_array),
            "Query response cookie is not a JSON array"
        );

        LogDebug("Row Data: %.*s", (int)nrow, row);

        // we will not delete this here because it will be added to the array
        cJSON *row_json = cJSON_ParseWithLength(row, nrow);
        IfNULLGotoDone(row_json, "Failed to parse row result");
        IfFalseGotoDone(
            cJSON_IsObject(row_json),
            "Row data is not a JSON object"
        );

        IfFalseGotoDone(
            cJSON_AddItemToArray(response_json_data_array, row_json),
            "Failed to add row JSON to response data array"
        );
    }

done:
    // no clean up to do in this block
    return;
}

int tcblcb_api_airports(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);
    
    http_populate_qs(req);
    
    struct kore_buf *query_buf = NULL;
    struct kore_buf *context_buf = NULL;
    char *param_string = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    char *search_string = NULL;
    IfBadKoreResultGotoDone(
        http_argument_get_string(req, "search", &search_string),
        "search query param was not found"
    );

    size_t search_strlen = strlen(search_string);
    bool same_case = is_same_case(search_string);

    query_buf = kore_buf_alloc(BUFSIZ);
    kore_buf_appendf(query_buf, "SELECT airportname FROM `travel-sample`.inventory.airport WHERE ");

    if (same_case) {
        if (search_strlen == 3) {
            kore_buf_appendf(query_buf, "faa=$1");
            to_upper_case(search_string);
        } else if (search_strlen == 4) {
            kore_buf_appendf(query_buf, "icao=$1");
            to_upper_case(search_string);
        }
    } else {
        kore_buf_appendf(query_buf, "POSITION(LOWER(airportname), $1) = 0");
        to_lower_case(search_string);
    }

    param_string = create_json_string_param(search_string);

    size_t query_strlen;
    char *query_string = kore_buf_stringify(query_buf, &query_strlen);

    context_buf = kore_buf_alloc(BUFSIZ);
    kore_buf_appendf(context_buf, "N1QL query - scoped to inventory: %s", query_string);

    size_t context_strlen;
    char *context_string = kore_buf_stringify(context_buf, &context_strlen);

    LogDebug("Query Request:\n(%s)\nQuery Params: [%s]", query_string, param_string);

    // prepare JSON response early so we can accumulate query results
	response_json = cJSON_CreateObject();
    cJSON *resp_json_data_array = cJSON_AddArrayToObject(response_json, "data");
    IfNULLGotoDone(resp_json_data_array, "Failed to create response data array");
    cJSON *resp_json_context_array = cJSON_AddArrayToObject(response_json, "context");
    IfNULLGotoDone(resp_json_context_array, "Failed to create response context array");
    IfFalseGotoDone(
        cJSON_AddItemToArray(resp_json_context_array, cJSON_CreateStringReference(context_string)),
        "Failed to add response context string to array"
    );

    // execute the Couchbase airport query command and wait for results
    lcb_CMDQUERY *cmd;
    IfLCBFailGotoDone(
        lcb_cmdquery_create(&cmd),
        "Failed to create query command"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_statement(cmd, query_string, query_strlen),
        "Failed to set query command statement"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_positional_param(cmd, param_string, strlen(param_string)),
        "Failed to set query command parameter"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_option(cmd, "pretty", strlen("pretty"), "false", strlen("false")),
        "Failed to set query command pretty option"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_adhoc(cmd, false),
        "Failed to disable adhoc query (enable prepared statement)")
    ;
    IfLCBFailGotoDone(
        lcb_cmdquery_callback(cmd, airports_query_callback),
        "Failed to set airports query command callback"
    );
    DebugQueryPayload(cmd);
    IfLCBFailGotoDone(
        lcb_query(_tcblcb_lcb_instance, resp_json_data_array, cmd),
        "Failed to schedule query command"
    );
    IfLCBFailLogWarningMsg(
        lcb_cmdquery_destroy(cmd),
        "Failed to destroy query command statement"
    );
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Failed while waiting for query operation to complete"
    );

    // query results are complete so we can get the JSON response 
    response_string = cJSON_PrintBuffered(response_json, BUFSIZ, FMT_RESPONSE);
    response_strlen = strlen(response_string);

done:
    http_response(req, 200, response_string, response_strlen);

    if (param_string != NULL) {
        free(param_string);
    }

    if (query_buf != NULL) {
	    kore_buf_free(query_buf);
    }

    if (context_buf != NULL) {
        kore_buf_free(context_buf);
    }

    if (response_string != NULL) {
        free(response_string);
    }
    
    if (response_json != NULL) {
        cJSON_Delete(response_json);
    }

    return (KORE_RESULT_OK);
}
