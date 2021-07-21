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

#include <math.h>

#include "try-cb-lcb.h"
#include "util.h"

typedef struct tcblcb_FlightPathResults {
  char *from_airport;
  char *to_airport;
} tcblcb_FlightPathResults;

static void fpaths_query_callback(__unused lcb_INSTANCE *instance, __unused int type, const lcb_RESPQUERY *resp)
{
    cJSON *row_json = NULL;

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
        tcblcb_FlightPathResults *flight_path_results = NULL;
        IfLCBFailGotoDone(
            lcb_respquery_cookie(resp, (void**)(&flight_path_results)),
            "Failed to get query response cookie"
        );

        LogDebug("Row Data: %.*s", (int)nrow, row);

        // this will be deleted because we're extracting JSON string copies
        row_json = cJSON_ParseWithLength(row, nrow);
        IfNULLGotoDone(row_json, "Failed to parse row result");

        // this helper can iterate arrays or object entries
        const cJSON *row_object = NULL;
        cJSON_ArrayForEach(row_object, row_json) {
            const char *current_key = row_object->string;
            if (current_key != NULL && cJSON_IsString(row_object)) {
                if (strcmp(current_key, "fromAirport") == 0) {
                    flight_path_results->from_airport = create_json_string_param(row_object->valuestring);
                } else if (strcmp(current_key, "toAirport") == 0) {
                    flight_path_results->to_airport = create_json_string_param(row_object->valuestring);
                }
            } 
        }
    }

done:
    if (row_json != NULL) {
        cJSON_Delete(row_json);
    }
}

static void routes_query_callback(__unused lcb_INSTANCE *instance, __unused int type, const lcb_RESPQUERY *resp)
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
        cJSON *response_json_data_array;
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

        double flight_time = ceil(rand() / (double)RAND_MAX * 8000.0);
        double flight_price = ceil(flight_time / 8.0 * 100.0) / 100.0;

        cJSON_AddNumberToObject(row_json, "flighttime", flight_time);
        cJSON_AddNumberToObject(row_json, "price", flight_price);

        IfFalseGotoDone(
            cJSON_AddItemToArray(response_json_data_array, row_json),
            "Failed to add row JSON to response data array"
        );
    }

done:
    // no clean up to do in this block
    return;
}

int tcblcb_api_fpaths(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);
    
    struct kore_buf *context_buf = NULL;

    lcb_CMDQUERY *query_cmd = NULL;

    char *params_string = NULL;

    char *from_faa_json_string = NULL;
    char *to_faa_json_string = NULL;
    char *leave_weekday_json_string = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    // grab a copy of the path to tokenize the path parameters
    size_t path_strlen = strlen(req->path);
    char path_string[path_strlen + 1];
    strcpy(path_string, req->path);

    // route matches: ^/api/flightPaths/[^\?\/]+/[^\?\/]+$
    // which is: /api/flightPaths/{fromloc}/{toloc}
    //
    // note that we don't need to guard segment count because of route match
    char *path_segments[4];
    char *path_segment = strtok(path_string, "/");
    int path_segment_num = 0;
    while (path_segment) {
        path_segments[path_segment_num++] = path_segment;
        path_segment = strtok(NULL, "/");
    }

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[2]),
        "Failed to URL decode 'from loc' parameter"
    );
    char *from_loc_param = path_segments[2];
    IfNULLGotoDone(from_loc_param, "Failed to get 'from loc' parameter from path segment");

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[3]),
        "Failed to URL decode 'to loc' parameter"
    );
    char *to_loc_param = path_segments[3];
    IfNULLGotoDone(to_loc_param, "Failed to get 'to loc' parameter from path segment");

    // parse the leave query parameter
    http_populate_qs(req);

    char *leave_date_string = NULL;
    IfBadKoreResultGotoDone(
        http_argument_get_string(req, "leave", &leave_date_string),
        "leave query param was not found"
    );

    // prepare the N1QL query command to get the flight paths
    char fpaths_query_string[] =
        "SELECT faa as fromAirport FROM `travel-sample`.inventory.airport "
        "WHERE airportname = $1 "
        "UNION "
        "SELECT faa as toAirport FROM `travel-sample`.inventory.airport "
        "WHERE airportname = $2";
    size_t fpaths_query_strlen = sizeof(fpaths_query_string) - 1;

    char *params[2] = {from_loc_param, to_loc_param};
    params_string = create_string_array_param_string(params, 2);

    LogDebug(
        "Flight Paths Query Request:\n%s\nQuery Params: %s",
        fpaths_query_string,
        params_string
    );

    // add the query to the response context
    context_buf = kore_buf_alloc(BUFSIZ);
    kore_buf_appendf(context_buf, "N1QL query - scoped to inventory: %s", fpaths_query_string);

    size_t context_strlen;
    char *context_string = kore_buf_stringify(context_buf, &context_strlen);

    // prepare JSON response early so we can accumulate the results
	response_json = cJSON_CreateObject();
    cJSON *resp_json_data_array = cJSON_AddArrayToObject(response_json, "data");
    IfNULLGotoDone(resp_json_data_array, "Failed to create response data array");
    cJSON *resp_json_context_array = cJSON_AddArrayToObject(response_json, "context");
    IfNULLGotoDone(resp_json_context_array, "Failed to create response context array");
    IfFalseGotoDone(
        cJSON_AddItemToArray(resp_json_context_array, cJSON_CreateStringReference(context_string)),
        "Failed to add response fpaths context string to array"
    );

    // execute the N1QL query command to get the flight paths and wait for results
    tcblcb_FlightPathResults flight_path_results = {0};
    IfLCBFailGotoDone(
        lcb_cmdquery_create(&query_cmd),
        "Failed to create query command"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_statement(query_cmd, fpaths_query_string, fpaths_query_strlen),
        "Failed to set fpaths query command statement"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_positional_param(query_cmd, params_string, strlen(params_string)),
        "Failed to set query command positional parameters"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_option(query_cmd, "pretty", strlen("pretty"), "false", strlen("false")),
        "Failed to set query command pretty option"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_adhoc(query_cmd, false),
        "Failed to disable adhoc query (enable prepared statement)"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_callback(query_cmd, fpaths_query_callback),
        "Failed to set fpaths query command callback"
    );
    DebugQueryPayload(query_cmd);
    IfLCBFailGotoDone(
        lcb_query(_tcblcb_lcb_instance, &flight_path_results, query_cmd),
        "Failed to schedule fpaths query command"
    );
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Failed while waiting for fpaths query operation to complete"
    );

    // prepare the N1QL query command to get the routes
    char routes_query_string[] =
        "SELECT a.name, s.flight, s.utc, r.sourceairport, r.destinationairport, r.equipment "
        "FROM `travel-sample`.inventory.route AS r "
        "UNNEST r.schedule AS s "
        "JOIN `travel-sample`.inventory.airline AS a ON KEYS r.airlineid "
        "WHERE r.sourceairport = $fromfaa AND r.destinationairport = $tofaa AND s.day = $dayofweek "
        "ORDER BY a.name ASC";
    size_t routes_query_strlen = sizeof(routes_query_string) - 1;

    from_faa_json_string = flight_path_results.from_airport;
    IfNULLGotoDone(from_faa_json_string, "Failed to get 'fromfaa' parameter JSON string value");
    to_faa_json_string = flight_path_results.to_airport;
    IfNULLGotoDone(to_faa_json_string, "Failed to get 'tofaa' parameter JSON string value");
    int leave_weekday = weekday(leave_date_string);
    leave_weekday_json_string = create_json_number_param(leave_weekday);

    const char from_param_string[] = "fromfaa";
    const size_t from_param_strlen = sizeof(from_param_string) - 1;
    const char to_param_string[] = "tofaa";
    const size_t to_param_strlen = sizeof(to_param_string) - 1;
    const char dayofweek_param_string[] = "dayofweek";
    const size_t dayofweek_param_strlen= sizeof(dayofweek_param_string) - 1;

    LogDebug(
        "Routes Query Request:\n(%s)\n%s:(%s)  %s:(%s)  %s:(%s)",
        routes_query_string,
        from_param_string,
        from_faa_json_string,
        to_param_string,
        to_faa_json_string,
        dayofweek_param_string,
        leave_weekday_json_string
    );

    // add the query to the response context
    kore_buf_reset(context_buf);
    kore_buf_appendf(context_buf, "N1QL query - scoped to inventory: %s", routes_query_string);

    context_string = kore_buf_stringify(context_buf, &context_strlen);
    IfFalseGotoDone(
        cJSON_AddItemToArray(resp_json_context_array, cJSON_CreateStringReference(context_string)),
        "Failed to add response routes context string to array"
    );

    // execute the N1QL query command to get the routes and wait for results
    IfLCBFailGotoDone(
        lcb_cmdquery_reset(query_cmd),
        "Failed to reset query command"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_create(&query_cmd),
        "Failed to create routes query command"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_statement(query_cmd, routes_query_string, routes_query_strlen),
        "Failed to set routes query command statement"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_named_param(
            query_cmd,
            from_param_string,
            from_param_strlen,
            from_faa_json_string,
            strlen(from_faa_json_string)
        ),
        "Failed to set '$fromfaa' query command parameter"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_named_param(
            query_cmd,
            to_param_string,
            to_param_strlen,
            to_faa_json_string,
            strlen(to_faa_json_string)
        ),
        "Failed to set '$tofaa' query command parameter"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_named_param(
            query_cmd,
            dayofweek_param_string,
            dayofweek_param_strlen,
            leave_weekday_json_string,
            strlen(leave_weekday_json_string)
        ),
        "Failed to set '$dayofweek' query command parameter"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_option(query_cmd, "pretty", strlen("pretty"), "false", strlen("false")),
        "Failed to set query command pretty option"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_adhoc(query_cmd, false),
        "Failed to disable adhoc query (enable prepared statement)"
    );
    IfLCBFailGotoDone(
        lcb_cmdquery_callback(query_cmd, routes_query_callback),
        "Failed to set routes query command callback"
    );
    DebugQueryPayload(query_cmd);
    IfLCBFailGotoDone(
        lcb_query(_tcblcb_lcb_instance, resp_json_data_array, query_cmd),
        "Failed to schedule routes query command"
    );
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Failed while waiting for routes query operation to complete"
    );

    // query results are complete so we can get the JSON response 
    response_string = cJSON_PrintBuffered(response_json, BUFSIZ, FMT_RESPONSE);
    response_strlen = strlen(response_string);

done:
    http_response(req, 200, response_string, response_strlen);

    if (params_string != NULL) {
        free(params_string);
    }

    if (from_faa_json_string != NULL) {
        free(from_faa_json_string);
    }

    if (to_faa_json_string != NULL) {
        free(to_faa_json_string);
    }
    
    if (leave_weekday_json_string != NULL) {
        free(leave_weekday_json_string);
    }

    if (response_string != NULL) {
        free(response_string);
    }
    
    if (response_json != NULL) {
        cJSON_Delete(response_json);
    }
    
    if (query_cmd != NULL) {
        IfLCBFailLogWarningMsg(
            lcb_cmdquery_destroy(query_cmd),
            "Failed to destroy fpaths query command statement"
        );
    }

    return (KORE_RESULT_OK);
}
