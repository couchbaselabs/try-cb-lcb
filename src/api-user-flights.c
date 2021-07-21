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

#include <jwt.h>

#include "try-cb-lcb.h"
#include "util.h"

static const unsigned char JWT_SECRET_STRING[] = "cbtravelsample";
static const size_t        JWT_SECRET_STRLEN = sizeof(JWT_SECRET_STRING) - 1;

static const char   RSPMSG_REQ_ERROR_STRING[] = "{\"message\":"
                    " \"Problem parsing user flight request\"}";
static const size_t RSPMSG_REQ_ERROR_STRLEN = sizeof(RSPMSG_REQ_ERROR_STRING) - 1;

static const char   RSPMSG_USR_NO_MATCH_STRING[] = "{\"message\":"
                    " \"Username does not match token\"}";
static const size_t RSPMSG_USR_NO_MATCH_STRLEN = sizeof(RSPMSG_USR_NO_MATCH_STRING) - 1;

static const char   RSPMSG_UPSERT_FAILED_STRING[] = "{\"message\":"
                    " \"Failed to upsert new flight to bookings\"}";
static const size_t RSPMSG_UPSERT_FAILED_STRLEN = sizeof(RSPMSG_UPSERT_FAILED_STRING) - 1;

static const char   RSPMSG_APPEND_FAILED_STRING[] = "{\"message\":"
                    " \"Failed to append new flight to user bookings\"}";
static const size_t RSPMSG_APPEND_FAILED_STRLEN = sizeof(RSPMSG_APPEND_FAILED_STRING) - 1;

static const char   RSPMSG_RESP_JSON_BAD_STRING[] = "{\"message\":"
                    " \"Flights added but error creating response JSON\"}";
static const size_t RSPMSG_RESP_JSON_BAD_STRLEN = sizeof(RSPMSG_RESP_JSON_BAD_STRING) - 1;

static const char   RSPMSG_USR_BOOKING_ERROR_STRING[] = "{\"message\":"
                    " \"Failed to get user bookings\"}";
static const size_t RSPMSG_USR_BOOKING_ERROR_STRLEN = sizeof(RSPMSG_USR_BOOKING_ERROR_STRING) - 1;

static const char   BOOKINGS_COLL_STRING[] = "bookings";
static const size_t BOOKINGS_COLL_STRLEN = sizeof(BOOKINGS_COLL_STRING) - 1;

static const char   USERS_COLL_STRING[] = "users";
static const size_t USERS_COLL_STRLEN = sizeof(USERS_COLL_STRING) - 1;

static const char   BOOKINGS_PATH_STRING[] = "bookings";
static const size_t BOOKINGS_PATH_STRLEN = sizeof(BOOKINGS_PATH_STRING) - 1;

typedef struct tcblcb_UserFlightsParams {
    char *tenant;
    char *username;
} tcblcb_UserFlightsParams;

typedef struct tcblcb_UserBookingDelegateParams {
    lcb_STATUS status;
    const char *tenant;
    cJSON *json;
} tcblcb_UserBookingDelegateParams;

// get user params from the request.
static tcblcb_UserFlightsParams *get_user_params(struct http_request *req)
{
    tcblcb_UserFlightsParams *user_params = calloc(1, sizeof(tcblcb_UserFlightsParams));

    // grab a copy of the path to tokenize the path parameters
    size_t path_strlen = strlen(req->path);
    char path_string[path_strlen + 1];
    strcpy(path_string, req->path);

    // route matches: ^/api/tenants/[^\?\/]+/user/[^\?\/]+/flights$
    // which is: /api/tenants/{tenant}/user/{username}/flights
    //
    // note that we don't need to guard segment count because of route match
    char *path_segments[6];
    char *path_segment = strtok(path_string, "/");
    int path_segment_num = 0;
    while (path_segment && path_segment_num < 6) {
        path_segments[path_segment_num++] = path_segment;
        path_segment = strtok(NULL, "/");
    }

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[2]),
        "Failed to URL decode 'tenant' parameter"
    );
    char *tenant_string_ref = path_segments[2];
    to_lower_case(tenant_string_ref);

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[4]),
        "Failed to URL decode 'username' parameter"
    );
    char *username_string_ref = path_segments[4];
    to_lower_case(username_string_ref);

    user_params->tenant = strdup(tenant_string_ref);
    user_params->username = strdup(username_string_ref);

    LogDebug("User Flight Params: tenant=%s user=%s", user_params->tenant, user_params->username);

done:
    return user_params;
}

static void delete_user_params(tcblcb_UserFlightsParams *user_params)
{
    if (user_params->tenant != NULL) {
        free(user_params->tenant);
    }
    if (user_params->username != NULL) {
        free(user_params->username);
    }
    free(user_params);
}

// called from a global callback and should not reference any other locals
static void flight_upsert_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPSTORE *resp)
{
    lcb_STATUS *status = (lcb_STATUS *)cookie;
    IfNULLGotoDone(
        status,
        "Status result cookie was NULL"
    );

    *status = lcb_respstore_status(resp);
    LogDebug("Flight upsert status result: %s", lcb_strerror_long(*status));

done:
    // no clean up to do in this block
    return;
}

static lcb_STATUS upsert_new_flight(const char *tenant, const char *flight_uuid_string, const char *flight_string)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    lcb_CMDSTORE *cmd = NULL;
    tcblcb_RESPDELEGATE *store_delegate = NULL;
    bool cmd_scheduled = false;

    // insert or update the new flight
    IfLCBFailGotoDone(
        lcb_cmdstore_create(&cmd, LCB_STORE_UPSERT),
        "Failed to create store insert command"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_collection(
            cmd,
            tenant, strlen(tenant),
            BOOKINGS_COLL_STRING, BOOKINGS_COLL_STRLEN
        ),
        "Failed to set store insert scope and collection"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_key(cmd, flight_uuid_string, strlen(flight_uuid_string)),
        "Failed to set store command flight key"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_value(cmd, flight_string, strlen(flight_string)),
        "Failed to set store insert flight document"
    );

    LogDebug("Add new flight booking: (%s) %s", flight_uuid_string, flight_string);

    // receiver is responsible for freeing this memory if command is scheduled
    store_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    store_delegate->cookie = (void**)&rc;
    store_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)flight_upsert_callback;
    IfLCBFailGotoDone(
        lcb_store(_tcblcb_lcb_instance, store_delegate, cmd),
        "Failed to schedule user insert command"
    );

    cmd_scheduled = true;

done:
    if (cmd != NULL) {
        IfLCBFailLogWarningMsg(
            lcb_cmdstore_destroy(cmd),
            "Failed to destroy subdoc command"
        );
    }

    if (cmd_scheduled) {
        IfLCBFailGotoDone(
            lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
            "Failed to complete insert command"
        );
    } else {
        // free memory if command was not scheduled
        if (store_delegate != NULL) {
            free(store_delegate);
        }
    }

    return rc;
}

// called from a global callback and should not reference any other locals
static void booking_subdoc_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPSUBDOC *resp)
{
    lcb_STATUS *status = (lcb_STATUS *)cookie;
    IfNULLGotoDone(
        status,
        "Status result cookie was NULL"
    );

    *status = lcb_respsubdoc_status(resp);
    LogDebug("User booking subdoc append status result: %s", lcb_strerror_long(*status));

done:
    // no clean up to do in this block
    return;
}

static lcb_STATUS add_user_booking(tcblcb_UserFlightsParams *user_params, const char *flight_uuid_string)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    lcb_CMDSUBDOC *cmd = NULL;
    lcb_SUBDOCSPECS *ops = NULL;
    tcblcb_RESPDELEGATE *subdoc_delegate = NULL;
    char * flight_uuid_json_string = NULL;
    bool cmd_scheduled = false;

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_create(&cmd),
        "Failed to create subdoc command"
    );
    IfLCBFailGotoDone(
        lcb_cmdsubdoc_collection(
            cmd,
            user_params->tenant, strlen(user_params->tenant),
            USERS_COLL_STRING, USERS_COLL_STRLEN),
        "Failed to set subdoc get scope and collection"
    );
    IfLCBFailGotoDone(
        lcb_cmdsubdoc_key(cmd, user_params->username, strlen(user_params->username)),
        "Failed to set subdoc key"
    );

    IfLCBFailGotoDone(
        lcb_subdocspecs_create(&ops, 1),
        "Failed to create subdoc operation specs"
    );

    flight_uuid_json_string = create_json_string_param(flight_uuid_string);
    IfNULLGotoDone(
        flight_uuid_json_string,
        "Failed to create flight UUID JSON value string"
    );

    IfLCBFailGotoDone(
        lcb_subdocspecs_array_add_last(
            ops, 0, LCB_SUBDOCSPECS_F_MKINTERMEDIATES,
            BOOKINGS_PATH_STRING, BOOKINGS_PATH_STRLEN,
            flight_uuid_json_string, strlen(flight_uuid_json_string)
        ),
        "Failed to create array add operation for user 'bookings' path"
    );

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_specs(cmd, ops),
        "Failed to add subspec operations for command"
    );

    LogDebug("Add User Booking: (%s) %s", user_params->username, flight_uuid_json_string);

    // receiver is responsible for freeing this memory if command is scheduled
    subdoc_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    subdoc_delegate->cookie = (void**)&rc;
    subdoc_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)booking_subdoc_callback;
    IfLCBFailGotoDone(
        lcb_subdoc(_tcblcb_lcb_instance, subdoc_delegate, cmd),
        "Failed to schedule subdoc command"
    )

    cmd_scheduled = true;

done:
    if (flight_uuid_json_string != NULL) {
        free(flight_uuid_json_string);
    }
    
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
            lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
            "Failed to complete subdoc command"
        );
    } else {
        // free memory if command was not scheduled
        if (subdoc_delegate != NULL) {
            free(subdoc_delegate);
        }
    }

    return rc;
}

static void tcblcb_api_user_flights_put(struct http_request *req, tcblcb_UserFlightsParams *user_params)
{
    tcblcb_HTTPResponse hresp;
    hresp.status = 500;
    hresp.string = RSPMSG_REQ_ERROR_STRING;
    hresp.strlen = RSPMSG_REQ_ERROR_STRLEN;

    struct kore_buf *http_body_buf = NULL;
    cJSON *request_body_json = NULL;

    char *flight_string = NULL;
    char *flight_uuid_string = NULL;

    struct kore_buf *context_buf = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    context_buf = kore_buf_alloc(BUFSIZ);
    kore_buf_appendf(context_buf, "KV update - scoped to %s.user: for bookings field in document %s", user_params->tenant, user_params->username);

    size_t context_strlen;
    char *context_string = kore_buf_stringify(context_buf, &context_strlen);

    http_body_buf = get_http_body_buf(req);
    IfNULLGotoDone(
        http_body_buf,
        "Failed to read request body data"
    );

    request_body_json = cJSON_Parse(kore_buf_stringify(http_body_buf, NULL));
    IfNULLGotoDone(
        request_body_json,
        "Failed to parse request body JSON"
    );

    cJSON *flights_json_array = cJSON_GetObjectItem(request_body_json, "flights");
    IfNULLGotoDone(
        flights_json_array,
        "Failed to get flights param from request"
    );
    IfFalseGotoDone(
        cJSON_IsArray(flights_json_array),
        "Flights param was not an array"
    );

    cJSON *flight_json = cJSON_GetArrayItem(flights_json_array, 0);
    flight_string = cJSON_PrintBuffered(flight_json, BUFSIZ, false);
    IfNULLGotoDone(
        flight_string,
        "Failed to get flight JSON as string"
    );

    flight_uuid_string = create_uuid_string();
    IfNULLGotoDone(
        flight_uuid_string,
        "Failed to get flight UUID as string"
    );

    hresp.status = 500;
    hresp.string = RSPMSG_UPSERT_FAILED_STRING;
    hresp.strlen = RSPMSG_UPSERT_FAILED_STRLEN;
    IfLCBFailGotoDone(
        upsert_new_flight(user_params->tenant, flight_uuid_string, flight_string),
        "Failed to add new flight to bookings collection"
    );

    hresp.status = 500;
    hresp.string = RSPMSG_APPEND_FAILED_STRING;
    hresp.strlen = RSPMSG_APPEND_FAILED_STRLEN;
    IfLCBFailGotoDone(
        add_user_booking(user_params, flight_uuid_string),
        "Failed to add new flight to user bookings"
    );

    // catch-all response if any operations fail while preparing response
    hresp.status = 500;
    hresp.string = RSPMSG_RESP_JSON_BAD_STRING;
    hresp.strlen = RSPMSG_RESP_JSON_BAD_STRLEN;

    // create main response object
    response_json = cJSON_CreateObject();
    // add 'data' object to response object
    cJSON *data_json = cJSON_AddObjectToObject(response_json, "data");
    IfNULLGotoDone(
        data_json,
        "Failed to create response 'data' object"
    );
    // add 'added' array to the 'data' object
    cJSON *added_array = cJSON_AddArrayToObject(data_json, "added");
    IfNULLGotoDone(added_array, "Failed to create response 'added'' array");
    IfFalseGotoDone(
        cJSON_AddItemReferenceToArray(added_array, flight_json),
        "Failed to add response flight JSON to 'added' array"
    );
    // add 'context' object to response object
    cJSON *context_array = cJSON_AddArrayToObject(response_json, "context");
    IfNULLGotoDone(context_array, "Failed to create response 'context' array");
    IfFalseGotoDone(
        cJSON_AddItemToArray(context_array, cJSON_CreateStringReference(context_string)),
        "Failed to add response context string to array"
    );

    response_string = cJSON_PrintBuffered(response_json, BUFSIZ, FMT_RESPONSE);
    response_strlen = strlen(response_string);
    IfTrueGotoDone(
        (response_string == NULL || response_strlen == 0),
        "Unable to create response JSON string"
    );

    hresp.status = 200;
    hresp.string = response_string;
    hresp.strlen = response_strlen;

done:
    http_response(req, hresp.status, hresp.string, hresp.strlen);

    if (flight_uuid_string != NULL) {
        free(flight_uuid_string);
    }
    
    if (flight_string != NULL) {
        free(flight_string);
    }

    if (http_body_buf != NULL) {
        kore_buf_free(http_body_buf);
    }

    if (request_body_json != NULL) {
        cJSON_Delete(request_body_json);
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
}

// called from a global callback and should not reference any other locals
static void get_flight_booking_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPGET *resp)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    cJSON *booking_json_array = (cJSON *)cookie;
    IfNULLGotoDone(
        booking_json_array,
        "Booking JSON array cookie was NULL"
    );

    IfLCBFailGotoDone(
        (rc = lcb_respget_status(resp)),
        "Flight booking get command failed"
    );

    const char *key = NULL;
    size_t nkey = 0;
    IfLCBFailGotoDone(
        (rc = lcb_respget_key(resp, &key, &nkey)),
        "Failed to get key from get response"
    );

    const char *value = NULL;
    size_t nvalue = 0;
    IfLCBFailGotoDone(
        (rc = lcb_respget_value(resp, &value, &nvalue)),
        "Failed to get value from get response"
    );

    LogDebug("Received get flight booking response: [%.*s] %.*s", (int)nkey, key, (int)nvalue, value);

    // accumulate the responses in the JSON array as they arrive
    if (!cJSON_AddItemToArray(booking_json_array, cJSON_ParseWithLength(value, nvalue))) {
        kore_log(LOG_WARNING, "Failed to add booking json to array for: %.*s", (int)nkey, key);
    }

done:
    // no clean up to do in this block
    return;
}

// called from a global callback and should not reference any other locals
static lcb_STATUS get_flight_booking(lcb_INSTANCE *instance, const char *tenant, const char *flight_booking_id, cJSON *booking_json_array)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    lcb_CMDGET *cmd;
    tcblcb_RESPDELEGATE *get_delegate = NULL;
    bool cmd_scheduled = false;

    IfLCBFailGotoDone(
        (rc = lcb_cmdget_create(&cmd)),
        "Failed to create get command"
    );
    IfLCBFailGotoDone(
        (rc = lcb_cmdget_collection(
            cmd,
            tenant, strlen(tenant),
            BOOKINGS_COLL_STRING, BOOKINGS_COLL_STRLEN)),
        "Failed to set the get command scope and collection"
    );
    IfLCBFailGotoDone(
        (rc = lcb_cmdget_key(cmd, flight_booking_id, strlen(flight_booking_id))),
        "Failed to set key for get command"
    );
    
    LogDebug("Get flight booking for: %s", flight_booking_id);

    // receiver is responsible for freeing this memory if command is scheduled
    get_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    get_delegate->cookie = (void*)booking_json_array;
    get_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)get_flight_booking_callback;
    IfLCBFailGotoDone(
        (rc = lcb_get(instance, get_delegate, cmd)),
        "Failed to schedule subdoc command"
    )

    cmd_scheduled = true;

done:
    if (cmd != NULL) {
        IfLCBFailLogWarningMsg(
            lcb_cmdget_destroy(cmd),
            "Failed to destroy get command"
        );
    }

    if (cmd_scheduled) {
        IfLCBFailLogWarningMsg(
            (rc = lcb_wait(instance, LCB_WAIT_DEFAULT)),
            "Failed to complete get command"
        );
    } else {
        // free memory if command was not scheduled
        if (get_delegate != NULL) {
            free(get_delegate);
        }
    }

    return rc;
}

// called from a global callback and should not reference any other locals
static void user_bookings_subdoc_callback(lcb_INSTANCE *instance, void *cookie, const lcb_RESPSUBDOC *resp)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    cJSON *booking_ids_json = NULL;

    tcblcb_UserBookingDelegateParams *bparams = (tcblcb_UserBookingDelegateParams *)cookie;
    IfNULLGotoDone(
        bparams,
        "Booking delegate params cookie was NULL"
    );

    bparams->json = cJSON_CreateArray();

    IfLCBFailGotoDone(
        (rc = lcb_respsubdoc_status(resp)),
        "Subdoc operation failed"
    );

    if (lcb_respsubdoc_result_size(resp) > 0) {
        IfLCBFailGotoDone(
            (rc = get_json_doc_from_subdoc_resp(resp, 0, &booking_ids_json)),
            "Failed to get JSON doc from subdoc response"
        );
        IfFalseGotoDone(
            cJSON_IsArray(booking_ids_json),
            "Unexpected JSON type for subdoc array"
        );
        
        const cJSON *booking_id = NULL;
        cJSON_ArrayForEach(booking_id, booking_ids_json) {
            const char *booking_id_string = cJSON_GetStringValue(booking_id);
            IfLCBFailLogWarningMsgRef(
                get_flight_booking(instance, bparams->tenant, booking_id_string, bparams->json),
                "Failed to get flight booking JSON",
                booking_id_string
            );
        }
    } else {
        LogDebug("User bookings subdoc result was EMPTY", NULL);
    }

done:
    if (booking_ids_json != NULL) {
        cJSON_Delete(booking_ids_json);
    }

    bparams->status = rc;
}

static lcb_STATUS get_user_bookings(tcblcb_UserFlightsParams *user_params, cJSON **json_array)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    lcb_CMDSUBDOC *cmd = NULL;
    lcb_SUBDOCSPECS *ops = NULL;
    tcblcb_RESPDELEGATE *subdoc_delegate = NULL;
    bool cmd_scheduled = false;

    IfLCBFailGotoDone(
        (rc = lcb_cmdsubdoc_create(&cmd)),
        "Failed to create subdoc command"
    );
    IfLCBFailGotoDone(
        (rc = lcb_cmdsubdoc_collection(
            cmd,
            user_params->tenant, strlen(user_params->tenant),
            USERS_COLL_STRING, USERS_COLL_STRLEN)),
        "Failed to set subdoc get scope and collection"
    );
    IfLCBFailGotoDone(
        (rc = lcb_cmdsubdoc_key(cmd, user_params->username, strlen(user_params->username))),
        "Failed to set subdoc key"
    );

    IfLCBFailGotoDone(
        (rc = lcb_subdocspecs_create(&ops, 1)),
        "Failed to create subdoc operation specs"
    );
    IfLCBFailGotoDone(
        (rc = lcb_subdocspecs_get(ops, 0, 0, BOOKINGS_COLL_STRING, BOOKINGS_COLL_STRLEN)),
        "Failed to create get operation for 'bookings' path"
    );

    IfLCBFailGotoDone(
        (rc = lcb_cmdsubdoc_specs(cmd, ops)),
        "Failed to add subspec operations for command"
    );

    LogDebug("Get user bookings via subdoc for user: %s", user_params->username);

    tcblcb_UserBookingDelegateParams bparams;
    bparams.tenant = user_params->tenant;

    // receiver is responsible for freeing this memory if command is scheduled
    subdoc_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    subdoc_delegate->cookie = (void*)&bparams;
    subdoc_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)user_bookings_subdoc_callback;
    IfLCBFailGotoDone(
        (rc = lcb_subdoc(_tcblcb_lcb_instance, subdoc_delegate, cmd)),
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
            (rc = lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT)),
            "Failed to complete subdoc command"
        );
        if (rc == LCB_SUCCESS) {
            rc = bparams.status;
            *json_array = bparams.json;
        }
    } else {
        // free memory if command was not scheduled
        if (subdoc_delegate != NULL) {
            free(subdoc_delegate);
        }
    }

    return rc;
}

static void tcblcb_api_user_flights_get(struct http_request *req, tcblcb_UserFlightsParams *user_params)
{
    tcblcb_HTTPResponse hresp;
    hresp.status = 500;
    hresp.string = RSPMSG_USR_BOOKING_ERROR_STRING;
    hresp.strlen = RSPMSG_USR_BOOKING_ERROR_STRLEN;

    struct kore_buf *context_buf = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    cJSON *bookings_json_array = NULL;
    lcb_STATUS bookings_status = get_user_bookings(user_params, &bookings_json_array);

    if (bookings_status == LCB_SUCCESS || bookings_status == LCB_ERR_SUBDOC_PATH_NOT_FOUND) {
        context_buf = kore_buf_alloc(BUFSIZ);
        kore_buf_appendf(context_buf, "KV get - scoped to %s.users: for password field in document %s", user_params->tenant, user_params->username);

        size_t context_strlen;
        char *context_string = kore_buf_stringify(context_buf, &context_strlen);

        // create main response object
        response_json = cJSON_CreateObject();
        // add data to response object
        IfFalseGotoDone(
            cJSON_AddItemToObject(response_json, "data", bookings_json_array),
            "Failed to add data array to response"
        );
        // add context to response object
        cJSON *context_array = cJSON_AddArrayToObject(response_json, "context");
        IfNULLGotoDone(context_array, "Failed to create response context array");
        IfFalseGotoDone(
            cJSON_AddItemToArray(context_array, cJSON_CreateStringReference(context_string)),
            "Failed to add response context string to array"
        );

        response_string = cJSON_PrintBuffered(response_json, BUFSIZ, FMT_RESPONSE);
        response_strlen = strlen(response_string);
        IfTrueGotoDone(
            (response_string == NULL || response_strlen == 0),
            "Unable to create response JSON string"
        );

        hresp.status = 200;
        hresp.string = response_string;
        hresp.strlen = response_strlen;
    }

done:
    http_response(req, hresp.status, hresp.string, hresp.strlen);

    if (context_buf != NULL) {
        kore_buf_free(context_buf);
    }

    if (response_string != NULL) {
        free(response_string);
    }
    
    if (response_json != NULL) {
        cJSON_Delete(response_json);
    }
}

int tcblcb_api_user_flights(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);
    
    bool user_authorized = false;
    jwt_t *jwt = NULL;
    char *jwt_user_json = NULL;
    char *jwt_user_string = NULL;
    char *authorization_string = NULL;
    tcblcb_UserFlightsParams *user_params = NULL;

    tcblcb_HTTPResponse hresp;
    hresp.status = 401;
    hresp.string = RSPMSG_REQ_ERROR_STRING;
    hresp.strlen = RSPMSG_REQ_ERROR_STRLEN;

    //////////
    // get the username from the JWT
    //
    jwt = NULL;
    const char *authorization_header = NULL;
    IfBadKoreResultGotoDone(
        http_request_header(req, "Authorization", &authorization_header),
        "Authorization Bearer missing in request header"
    );

    authorization_string = strdup(authorization_header);
    char *auth_bearer_parts[3] = {0};
    IfTrueGotoDone(
        (kore_split_string(authorization_string, " ", auth_bearer_parts, 3) != 2),
        "Wrong number of string parts in Authorization Bearer"
    );

    char *auth_bearer_token = auth_bearer_parts[1];
    IfBadErrnoGotoDone(
        jwt_decode(&jwt, auth_bearer_token, JWT_SECRET_STRING, JWT_SECRET_STRLEN),
        "Failed to decode Authorization Bearer JWT"
    );

    IfTrueGotoDone(
        (jwt_get_alg(jwt) != JWT_ALG_HS256),
        "Unexpected algorithm used for the JWT"
    );

    jwt_user_json = jwt_get_grants_json(jwt, "user");
    IfNULLGotoDone(
        jwt_user_json,
        "Unable to get user JSON string from the JWT"
    );

    jwt_user_string = extract_string_value_from_json_string(jwt_user_json, strlen(jwt_user_json));
    IfNULLGotoDone(
        jwt_user_string,
        "Unable to get deserialized user string from the JWT"
    );

    //////////
    // get the username from the path parameters
    //
    user_params = get_user_params(req);
    IfNULLGotoDone(
        user_params,
        "Failed to get user flight params from request"
    );

    //////////
    // verify that the JWT user matches the path parameter user
    //
    LogDebug("User Flights User Auth: {param user: = %s} {JWT user: %s}", user_params->username, jwt_user_string);

    hresp.status = 401;
    hresp.string = RSPMSG_USR_NO_MATCH_STRING;
    hresp.strlen = RSPMSG_USR_NO_MATCH_STRLEN;
    IfTrueGotoDone(
        (strcmp(jwt_user_string, user_params->username) != 0),
        "Path param username does not match JWT username"
    );

    user_authorized = true;

    // these functions are now responsible for sending the response
    if (req->method == HTTP_METHOD_PUT) {
        tcblcb_api_user_flights_put(req, user_params);
    } else if (req->method == HTTP_METHOD_GET) {
        tcblcb_api_user_flights_get(req, user_params);
    }

done:
    // only send common user auth failure responses from this function
    if (!user_authorized) {
        http_response(req, hresp.status, hresp.string, hresp.strlen);
    }

    if (user_params != NULL) {
        delete_user_params(user_params);
    }

    if (jwt_user_string != NULL) {
        free(jwt_user_string);
    }

    if (jwt_user_json != NULL) {
        free(jwt_user_json);
    }

    if (jwt != NULL) {
        jwt_free(jwt);
    }

    if (authorization_string != NULL) {
        free(authorization_string);
    }
    
    return (KORE_RESULT_OK);
}
