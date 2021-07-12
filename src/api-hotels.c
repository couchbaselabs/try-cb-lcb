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

#define             NUM_SUBDOC_PATHS 6
static const char   NAME_PATH_STRING[] = "name";
static const size_t NAME_PATH_STRLEN = sizeof(NAME_PATH_STRING) - 1;
static const char   ADDRESS_PATH_STRING[] = "address";
static const size_t ADDRESS_PATH_STRLEN = sizeof(ADDRESS_PATH_STRING) - 1;
static const char   CITY_PATH_STRING[] = "city";
static const size_t CITY_PATH_STRLEN = sizeof(CITY_PATH_STRING) - 1;
static const char   STATE_PATH_STRING[] = "state";
static const size_t STATE_PATH_STRLEN = sizeof(STATE_PATH_STRING) - 1;
static const char   COUNTRY_PATH_STRING[] = "country";
static const size_t COUNTRY_PATH_STRLEN = sizeof(COUNTRY_PATH_STRING) - 1;
static const char   DESCRIPTION_PATH_STRING[] = "description";
static const size_t DESCRIPTION_PATH_STRLEN = sizeof(DESCRIPTION_PATH_STRING) - 1;

// called from a global callback and should not reference any other locals
static void hotels_subdoc_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPSUBDOC *resp)
{
    IfLCBFailGotoDone(
        lcb_respsubdoc_status(resp),
        "Subdoc operation failed"
    );

    char *result_values[NUM_SUBDOC_PATHS] = {NULL};
    struct kore_buf *address_buf = NULL;

    cJSON *hotel_json = (cJSON *)cookie;
    IfNULLGotoDone(
        hotel_json,
        "Hotel JSON cookie was NULL"
    );

    // populate: name, description, address
    if (lcb_respsubdoc_result_size(resp) > 0) {
        for (size_t i=0; i < NUM_SUBDOC_PATHS; i++) {
            result_values[i] = extract_string_value_from_subdoc_resp(resp, i);
            LogDebug("Hotels subdoc [%zu] value: %s", i, result_values[i]);
        }

        IfNULLGotoDone(
            cJSON_AddStringToObject(hotel_json, "name", result_values[0]),
            "Failed to add `name` to hotel JSON document"
        );
        IfNULLGotoDone(
            cJSON_AddStringToObject(hotel_json, "description", result_values[5]),
            "Failed to add `description` to hotel JSON document"
        );

        address_buf = kore_buf_alloc(512);
        if (result_values[1] != NULL && *result_values[1] != '\0') {
            kore_buf_appendf(address_buf, "%s", result_values[1]);
        }
        if (result_values[2] != NULL && *result_values[2] != '\0') {
            if (address_buf->offset > 0) {
                kore_buf_appendf(address_buf, ", ");
            }
            kore_buf_appendf(address_buf, "%s", result_values[2]);
        }
        if (result_values[3] != NULL && *result_values[3] != '\0') {
            if (address_buf->offset > 0) {
                kore_buf_appendf(address_buf, ", ");
            }
            kore_buf_appendf(address_buf, "%s", result_values[3]);
        }
        if (result_values[4] != NULL && *result_values[4] != '\0') {
            if (address_buf->offset > 0) {
                kore_buf_appendf(address_buf, ", ");
            }
            kore_buf_appendf(address_buf, "%s", result_values[4]);
        }

        size_t address_strlen;
        char *address_string = kore_buf_stringify(address_buf, &address_strlen);

        IfNULLGotoDone(
            cJSON_AddStringToObject(hotel_json, "address", address_string),
            "Failed to add `address` to hotel JSON document"
        );

    } else {
        LogDebug("Hotels subdoc result was EMPTY", NULL);
    }

done:
    for (size_t i=0; i < NUM_SUBDOC_PATHS; i++) {
        if (result_values[i] != NULL) {
            free(result_values[i]);
        }
    }

    if (address_buf != NULL) {
	    kore_buf_free(address_buf);
    }
}

static cJSON *get_hotel_json(lcb_INSTANCE *instance, const char *hotel_id)
{
    lcb_CMDSUBDOC *cmd = NULL;
    lcb_SUBDOCSPECS *ops = NULL;
    tcblcb_RESPDELEGATE *subdoc_delegate = NULL;
    bool cmd_scheduled = false;

    cJSON *hotel_json = cJSON_CreateObject();
    IfNULLGotoDone(
        hotel_json,
        "Failed to create hotel JSON object"
    );

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_create(&cmd),
        "Failed to create subdoc command"
    );
    IfLCBFailGotoDone(
        lcb_cmdsubdoc_key(cmd, hotel_id, strlen(hotel_id)),
        "Failed to set subdoc key"
    );

    IfLCBFailGotoDone(
        lcb_subdocspecs_create(&ops, NUM_SUBDOC_PATHS),
        "Failed to create subdoc operation specs"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 0, 0, NAME_PATH_STRING, NAME_PATH_STRLEN),
        "Failed to create get operation for 'name' path"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 1, 0, ADDRESS_PATH_STRING, ADDRESS_PATH_STRLEN),
        "Failed to create get operation for 'address' path"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 2, 0, CITY_PATH_STRING, CITY_PATH_STRLEN),
        "Failed to create get operation for 'city' path"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 3, 0, STATE_PATH_STRING, STATE_PATH_STRLEN),
        "Failed to create get operation for 'state' path"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 4, 0, COUNTRY_PATH_STRING, COUNTRY_PATH_STRLEN),
        "Failed to create get operation for 'country' path"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 5, 0, DESCRIPTION_PATH_STRING, DESCRIPTION_PATH_STRLEN),
        "Failed to create get operation for 'description' path"
    );

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_specs(cmd, ops),
        "Failed to add subspec operations for command"
    );

    LogDebug("Get JSON via subdoc for hotel: %s", hotel_id);

    // receiver is responsible for freeing this memory if command is scheduled
    subdoc_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    subdoc_delegate->cookie = (void**)hotel_json;
    subdoc_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)hotels_subdoc_callback;
    IfLCBFailGotoDone(
        lcb_subdoc(instance, subdoc_delegate, cmd),
        "Failed to schedule subdoc command"
    )

    cmd_scheduled = true;

done:
    if (ops != NULL) {
        IfLCBFailLogWarningMsg(
            lcb_subdocspecs_destroy(ops),
            "Failed to destroy subdoc operations"
        );
    }

    if (cmd != NULL) {
        IfLCBFailLogWarningMsg(
            lcb_cmdsubdoc_destroy(cmd),
            "Failed to destroy subdoc command"
        );
    }

    if (cmd_scheduled) {
        IfLCBFailLogWarningMsg(
            lcb_wait(instance, LCB_WAIT_DEFAULT),
            "Failed to complete subdoc command"
        );
    } else {
        // free memory if command was not scheduled
        if (subdoc_delegate != NULL) {
            free(subdoc_delegate);
        }
    }

    return hotel_json;
}

static void hotels_search_callback(__unused lcb_INSTANCE *instance, __unused int type, const lcb_RESPSEARCH *resp)
{
    cJSON *row_json = NULL;

    IfLCBFailGotoDone(
        lcb_respsearch_status(resp),
        "Failed to execute search"
    );

    const char *row;
    size_t nrow;
    IfLCBFailGotoDone(
        lcb_respsearch_row(resp, &row, &nrow),
        "Failed to get search response row"
    );

    if (lcb_respsearch_is_final(resp)) {
        LogDebug("Query Metadata:\n%.*s", (int)nrow, row);
    } else {
        cJSON *response_json_data_array = NULL;
        IfLCBFailGotoDone(
            lcb_respsearch_cookie(resp, (void**)(&response_json_data_array)),
            "Failed to get search response cookie"
        );
        IfFalseGotoDone(
            cJSON_IsArray(response_json_data_array),
            "Search response cookie is not a JSON array"
        );

        LogDebug("Row Data: %.*s", (int)nrow, row);

        row_json = cJSON_ParseWithLength(row, nrow);
        IfNULLGotoDone(row_json, "Failed to parse row result");
        IfFalseGotoDone(
            cJSON_IsObject(row_json),
            "Row data is not a JSON object"
        );

        char *hotel_id = cJSON_GetStringValue(cJSON_GetObjectItem(row_json, "id"));
        IfNULLGotoDone(row_json, "Failed to get hotel id from row data");
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                response_json_data_array,
                get_hotel_json(instance, hotel_id)
            ),
            "Failed to add row JSON to response data array"
        );
    }

done:
    if (row_json != NULL) {
        cJSON_Delete(row_json);
    }
}

static cJSON *create_match_phrase_json(const char *match_phrase_ref, const char *field_ref)
{
    bool valid = false;

    cJSON *fts_json_match_phrase = cJSON_CreateObject();
    cJSON *fts_json_mp_value_ref = cJSON_CreateStringReference(match_phrase_ref);
    IfFalseGotoDone(
        cJSON_AddItemToObject(
            fts_json_match_phrase,
            "match_phrase",
            fts_json_mp_value_ref
        ),
        "Failed to add match phrase value to object"
    );
    cJSON *fts_json_mp_field_ref = cJSON_CreateStringReference(field_ref);
    IfFalseGotoDone(
        cJSON_AddItemToObject(
            fts_json_match_phrase,
            "field",
            fts_json_mp_field_ref
        ),
        "Failed to add field value to object"
    );

    valid = true;

done:
    return valid ? fts_json_match_phrase : NULL;
}

int tcblcb_api_hotels(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);
    
    cJSON *fts_json_payload = NULL;
    char *fts_json_payload_string = NULL;
    size_t fts_json_payload_strlen = 0;

    struct kore_buf *context_buf = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    // grab a copy of the path to tokenize the path parameters
    size_t path_strlen = strlen(req->path);
    char path_string[path_strlen + 1];
    strcpy(path_string, req->path);

    // route matches: ^/api/hotels/[^\?\/]+/[^\?\/]+/?$
    // which is: /api/hotels/{description}/{location}/
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
        "Failed to URL decode 'description' parameter"
    );
    char *description_string_ref = path_segments[2];

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[3]),
        "Failed to URL decode 'location' parameter"
    );
    char *location_string_ref = path_segments[3];

    // create the Full Text Search payload
	fts_json_payload = cJSON_CreateObject();
    IfNULLGotoDone(
        cJSON_AddStringToObject(fts_json_payload, "indexName", "hotels-index"),
        "Failed to add limit number to FTS payload"
    );
    IfFalseGotoDone(
        cJSON_AddNumberToObject(fts_json_payload, "limit", 100),
        "Failed to add limit number to FTS payload"
    );
    cJSON *fts_json_query = cJSON_AddObjectToObject(fts_json_payload, "query");
    IfNULLGotoDone(fts_json_query, "Failed to create query object");
    cJSON *fts_json_conjuncts = cJSON_AddArrayToObject(fts_json_query, "conjuncts");
    IfNULLGotoDone(fts_json_conjuncts, "Failed to create main conjuncts object");

    // don't include a location search if nothing was provided
    if (location_string_ref[0] != '\0' && strcmp(location_string_ref, "*") != 0) {
        cJSON *fts_json_loc_disjuncts = cJSON_CreateObject();
        IfNULLGotoDone(fts_json_loc_disjuncts, "Failed to create location disjuncts object");
        IfFalseGotoDone(
            cJSON_AddItemToArray(fts_json_conjuncts, fts_json_loc_disjuncts),
            "Failed to add location disjuncts to conjuncts array"
        );
        cJSON *fts_json_loc_disjuncts_array = cJSON_AddArrayToObject(fts_json_loc_disjuncts, "disjuncts");
        IfNULLGotoDone(fts_json_loc_disjuncts_array, "Failed to create location disjuncts array");
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_loc_disjuncts_array,
                create_match_phrase_json(location_string_ref, "country")
            ),
            "Failed to add location country match phrase"
        );
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_loc_disjuncts_array,
                create_match_phrase_json(location_string_ref, "city")
            ),
            "Failed to add location city match phrase"
        );
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_loc_disjuncts_array,
                create_match_phrase_json(location_string_ref, "state")
            ),
            "Failed to add location state match phrase"
        );
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_loc_disjuncts_array,
                create_match_phrase_json(location_string_ref, "address")
            ),
            "Failed to add location address match phrase"
        );
    }

    // don't include a description search if nothing was provided
    if (description_string_ref[0] != '\0' && strcmp(description_string_ref, "*") != 0) {
        cJSON *fts_json_desc_disjuncts = cJSON_CreateObject();
        IfNULLGotoDone(fts_json_desc_disjuncts, "Failed to create description disjuncts object");
        IfFalseGotoDone(
            cJSON_AddItemToArray(fts_json_conjuncts, fts_json_desc_disjuncts),
            "Failed to add description disjuncts to conjuncts array"
        );
        cJSON *fts_json_desc_disjuncts_array = cJSON_AddArrayToObject(fts_json_desc_disjuncts, "disjuncts");
        IfNULLGotoDone(fts_json_desc_disjuncts_array, "Failed to create description disjuncts object");
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_desc_disjuncts_array,
                create_match_phrase_json(description_string_ref, "description")
            ),
            "Failed to add description string match phrase"
        );
        IfFalseGotoDone(
            cJSON_AddItemToArray(
                fts_json_desc_disjuncts_array,
                create_match_phrase_json(description_string_ref, "name")
            ),
            "Failed to add description name match phrase"
        );
    }

    // if we don't have any search terms - then revert to a "match all" search
    if (cJSON_GetArraySize(fts_json_conjuncts) == 0) {
        cJSON *fts_json_match_all = cJSON_CreateObject();
        IfNULLGotoDone(
            fts_json_match_all,
            "Failed to create match_all query object"
        );
        IfNULLGotoDone(
            cJSON_AddObjectToObject(
                fts_json_match_all,
                "match_all"
            ),
            "Failed to add match_all query to conjuncts array"
        );
        IfFalseGotoDone(
            cJSON_AddItemToArray(fts_json_conjuncts, fts_json_match_all),
            "Failed to add match_all obejct to conjuncts array"
        );
    }

    fts_json_payload_string = cJSON_PrintBuffered(fts_json_payload, BUFSIZ, false);
    fts_json_payload_strlen = strlen(fts_json_payload_string);

    context_buf = kore_buf_alloc(BUFSIZ);
    kore_buf_appendf(context_buf, "FTS search - scoped to: %s", fts_json_payload_string);

    size_t context_strlen;
    char *context_string = kore_buf_stringify(context_buf, &context_strlen);

    LogDebug("Search Payload: (%s)", fts_json_payload_string);

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

    // execute the Couchbase hotel search command and wait for results
    lcb_CMDSEARCH *cmd;
    IfLCBFailGotoDone(
        lcb_cmdsearch_create(&cmd),
        "Failed to create search command"
    );
    IfLCBFailGotoDone(
        lcb_cmdsearch_callback(cmd, hotels_search_callback),
        "Failed to set search command callback"
    );
    IfLCBFailGotoDone(
        lcb_cmdsearch_payload(cmd, fts_json_payload_string, fts_json_payload_strlen),
        "Failed to set search payload"
    );
    IfLCBFailGotoDone(
        lcb_search(_tcblcb_lcb_instance, resp_json_data_array, cmd),
        "Failed to schedule search command"
    );
    IfLCBFailLogWarningMsg(
        lcb_cmdsearch_destroy(cmd),
        "Failed to destroy search command statement"
    );
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Failed while waiting for search operation to complete"
    );

    // query results are complete so we can get the JSON response 
    response_string = cJSON_PrintBuffered(response_json, BUFSIZ, FMT_RESPONSE);
    response_strlen = strlen(response_string);

done:
    http_response(req, 200, response_string, response_strlen);

    if (fts_json_payload_string != NULL) {
        free(fts_json_payload_string);
    }

    if (fts_json_payload != NULL) {
        cJSON_Delete(fts_json_payload);
    }

    if (response_string != NULL) {
        free(response_string);
    }

    if (response_json != NULL) {
        cJSON_Delete(response_json);
    }

    return (KORE_RESULT_OK);
}
