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

static const char   USERS_COLL_STRING[] = "users";
static const size_t USERS_COLL_STRLEN = sizeof(USERS_COLL_STRING) - 1;

__unused static const char   UNAME_KEY_STRING[] = "user";
__unused static const size_t UNAME_KEY_STRLEN = sizeof(UNAME_KEY_STRING) - 1;
__unused static const char   PWORD_KEY_STRING[] = "password";
__unused static const size_t PWORD_KEY_STRLEN = sizeof(PWORD_KEY_STRING) - 1;

static const char   RSPMSG_REQ_ERROR_STRING[] = "{\"message\":"
                    " \"Problem parsing user auth request\"}";
static const size_t RSPMSG_REQ_ERROR_STRLEN = sizeof(RSPMSG_REQ_ERROR_STRING) - 1;

static const char   RSPMSG_USR_EXISTS_STRING[] = "{\"message\":"
                    " \"User already exists\"}";
static const size_t RSPMSG_USR_EXISTS_STRLEN = sizeof(RSPMSG_USR_EXISTS_STRING) - 1;

static const char   RSPMSG_USR_INS_FAIL_STRING[] = "{\"message\":"
                    " \"User insert failed\"}";
static const size_t RSPMSG_USR_INS_FAIL_STRLEN = sizeof(RSPMSG_USR_INS_FAIL_STRING) - 1;

static const char   RSPMSG_USR_INS_JSON_BAD_STRING[] = "{\"message\":"
                    " \"User insert succeeded but error creating response JSON\"}";
static const size_t RSPMSG_USR_INS_JSON_BAD_STRLEN = sizeof(RSPMSG_USR_INS_JSON_BAD_STRING) - 1;

static const char   RSPMSG_USR_DNEXIST_STRING[] = "{\"message\":"
                    " \"User data not found\"}";
static const size_t RSPMSG_USR_DNEXIST_STRLEN = sizeof(RSPMSG_USR_DNEXIST_STRING) - 1;

static const char   RSPMSG_USR_PWD_FAIL_STRING[] = "{\"message\":"
                    " \"Failed to get user data\"}";
static const size_t RSPMSG_USR_PWD_FAIL_STRLEN = sizeof(RSPMSG_USR_PWD_FAIL_STRING) - 1;

static const char   RSPMSG_USR_BAD_PWD_STRING[] = "{\"message\":"
                    " \"Password does not match\"}";
static const size_t RSPMSG_USR_BAD_PWD_STRLEN = sizeof(RSPMSG_USR_BAD_PWD_STRING) - 1;

static const char   RSPMSG_USR_PWD_JSON_BAD_STRING[] = "{\"message\":"
                    " \"Password matched but error creating response JSON\"}";
static const size_t RSPMSG_USR_PWD_JSON_BAD_STRLEN = sizeof(RSPMSG_USR_PWD_JSON_BAD_STRING) - 1;

typedef struct tcblcb_UserAuthParams {
    char *tenant;
    char *username;
    char *password;
} tcblcb_UserAuthParams;

typedef struct tcblcb_UserPasswordResult {
    lcb_STATUS status;
    char *password;
} tcblcb_UserPasswordResult;

// get user params from the request.
static tcblcb_UserAuthParams *get_user_params(struct http_request *req)
{
    tcblcb_UserAuthParams *auth_params = calloc(1, sizeof(tcblcb_UserAuthParams));

    struct kore_buf *http_body_buf = NULL;
    cJSON *request_body_json = NULL;

    // grab a copy of the path to tokenize the path parameters
    size_t path_strlen = strlen(req->path);
    char path_string[path_strlen + 1];
    strcpy(path_string, req->path);

    // route matches: ^/api/tenants/[^\?\/]+/user/login$
    // which is: /api/tenants/{tenant}/user/login
    // or
    // route matches: ^/api/tenants/[^\?\/]+/user/signup$
    // which is: /api/tenants/{tenant}/user/signup
    //
    // note that we don't need to guard segment count because of route match
    char *path_segments[3];
    char *path_segment = strtok(path_string, "/");
    int path_segment_num = 0;
    while (path_segment && path_segment_num < 3) {
        path_segments[path_segment_num++] = path_segment;
        path_segment = strtok(NULL, "/");
    }

    IfBadKoreResultGotoDone(
        http_argument_urldecode(path_segments[2]),
        "Failed to URL decode 'tenant' parameter"
    );
    char *tenant_string_ref = path_segments[2];
    to_lower_case(tenant_string_ref);

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

    char *user_param = cJSON_GetStringValue(cJSON_GetObjectItem(request_body_json, UNAME_KEY_STRING));
    IfNULLGotoDone(
        user_param,
        "Failed to get 'user' param from request"
    );
    to_lower_case(user_param);

    char *pass_param = cJSON_GetStringValue(cJSON_GetObjectItem(request_body_json, PWORD_KEY_STRING));
    IfNULLGotoDone(
        pass_param,
        "Failed to get 'password' param from request"
    );

    auth_params->tenant = strdup(tenant_string_ref);
    auth_params->username = strdup(user_param);
    auth_params->password = strdup(pass_param);

    LogDebug("User Auth Params: tenant=%s user=%s", auth_params->tenant, auth_params->username);

done:
    if (http_body_buf != NULL) {
        kore_buf_free(http_body_buf);
    }

    if (request_body_json != NULL) {
        cJSON_Delete(request_body_json);
    }

    return auth_params;
}

static void delete_user_params(tcblcb_UserAuthParams *auth_params)
{
    if (auth_params->tenant != NULL) {
        free(auth_params->tenant);
    }
    if (auth_params->username != NULL) {
        free(auth_params->username);
    }
    if (auth_params->password != NULL) {
        free(auth_params->password);
    }
    free(auth_params);
}

static char *gen_token(const char *username)
{
    jwt_t *jwt = NULL;
    cJSON *json_payload = NULL;
    char *jwt_token_string = NULL;
    char *json_grants_string = NULL;

    IfBadErrnoGotoDone(
        jwt_new(&jwt),
        "Failed to create new JWT object"
    );

    json_payload = cJSON_CreateObject();
    IfNULLGotoDone(
        cJSON_AddStringToObject(json_payload, UNAME_KEY_STRING, username),
        "Failed to add `user` to JWT payload"
    );
    json_grants_string = cJSON_PrintUnformatted(json_payload);

    IfBadErrnoGotoDone(
        jwt_add_grants_json(jwt, json_grants_string),
        "Failed to add JSON grants to JWT"
    );

    IfBadErrnoGotoDone(
        jwt_set_alg(jwt, JWT_ALG_HS256, JWT_SECRET_STRING, JWT_SECRET_STRLEN),
        "Failed to add algorithm to JWT"
    );

    jwt_token_string = jwt_encode_str(jwt);

done:
    if (json_grants_string != NULL) {
        free(json_grants_string);
    }

    if (json_payload != NULL) {
        cJSON_Delete(json_payload);
    }

    if (jwt != NULL) {
        jwt_free(jwt);
    }

    return jwt_token_string;
}

// called from a global callback and should not reference any other locals
static void user_insert_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPSTORE *resp)
{
    lcb_STATUS *insert_result = (lcb_STATUS *)cookie;
    IfNULLGotoDone(
        insert_result,
        "Insert result cookie was NULL"
    );

    *insert_result = lcb_respstore_status(resp);

done:
    // no clean up to do in this block
    return;
}

static lcb_STATUS insert_user(tcblcb_UserAuthParams *auth_params)
{
    lcb_STATUS rc = LCB_ERR_GENERIC;
    lcb_CMDSTORE *cmd = NULL;
    tcblcb_RESPDELEGATE *store_delegate = NULL;
    bool cmd_scheduled = false;

    cJSON *user_json = NULL;
    char *user_json_string = NULL;

    user_json= cJSON_CreateObject();
    IfNULLGotoDone(
        cJSON_AddStringToObject(user_json, UNAME_KEY_STRING, auth_params->username),
        "Failed to add `user` to JWT payload"
    );
    IfNULLGotoDone(
        cJSON_AddStringToObject(user_json, PWORD_KEY_STRING, auth_params->password),
        "Failed to add `password` to JWT payload"
    );
    user_json_string = cJSON_PrintUnformatted(user_json);

    // insert the user or indicate failure
    IfLCBFailGotoDone(
        lcb_cmdstore_create(&cmd, LCB_STORE_INSERT),
        "Failed to create store insert command"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_collection(
            cmd,
            auth_params->tenant, strlen(auth_params->tenant),
            USERS_COLL_STRING, USERS_COLL_STRLEN
        ),
        "Failed to set store insert scope and collection"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_key(cmd, auth_params->username, strlen(auth_params->username)),
        "Failed to set store command user key"
    );
    IfLCBFailGotoDone(
        lcb_cmdstore_value(cmd, user_json_string, strlen(user_json_string)),
        "Failed to set store command user document"
    );

    // receiver is responsible for freeing this memory if command is scheduled
    store_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    store_delegate->cookie = (void**)&rc;
    store_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)user_insert_callback;
    IfLCBFailGotoDone(
        lcb_store(_tcblcb_lcb_instance, store_delegate, cmd),
        "Failed to schedule user insert command"
    );

    cmd_scheduled = true;

done:
    if (user_json_string != NULL) {
        free(user_json_string);
    }

    if (user_json != NULL) {
        cJSON_Delete(user_json);
    }

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
static void user_password_subdoc_callback(__unused lcb_INSTANCE *instance, void *cookie, const lcb_RESPSUBDOC *resp)
{
    tcblcb_UserPasswordResult *user_password_result = (tcblcb_UserPasswordResult *)cookie;
    IfNULLGotoDone(
        user_password_result,
        "User password result cookie was NULL"
    );

    IfLCBFailGotoDone(
        (user_password_result->status = lcb_respsubdoc_status(resp)),
        "User password subdoc operation failed"
    );

    if (lcb_respsubdoc_result_size(resp) > 0) {
        user_password_result->password = extract_string_value_from_subdoc_resp(resp, 0);
    } else {
        LogDebug("User password subdoc result was EMPTY", NULL);
    }

done:
    // no clean up to do in this block
    return;
}

static tcblcb_UserPasswordResult get_user_password(lcb_INSTANCE *instance, tcblcb_UserAuthParams *auth_params)
{
    tcblcb_UserPasswordResult user_password_result;
    user_password_result.status = LCB_ERR_GENERIC;
    user_password_result.password = NULL;

    lcb_CMDSUBDOC *cmd = NULL;
    lcb_SUBDOCSPECS *ops = NULL;
    tcblcb_RESPDELEGATE *subdoc_delegate = NULL;
    bool cmd_scheduled = false;

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_create(&cmd),
        "Failed to create subdoc command"
    );
    IfLCBFailGotoDone(
        lcb_cmdsubdoc_collection(
            cmd,
            auth_params->tenant, strlen(auth_params->tenant),
            USERS_COLL_STRING, USERS_COLL_STRLEN),
        "Failed to set subdoc get scope and collection"
    );
    IfLCBFailGotoDone(
        lcb_cmdsubdoc_key(cmd, auth_params->username, strlen(auth_params->username)),
        "Failed to set subdoc key"
    );

    IfLCBFailGotoDone(
        lcb_subdocspecs_create(&ops, 1),
        "Failed to create subdoc operation specs"
    );
    IfLCBFailGotoDone(
        lcb_subdocspecs_get(ops, 0, 0, PWORD_KEY_STRING, PWORD_KEY_STRLEN),
        "Failed to create get operation for 'password' path"
    );

    IfLCBFailGotoDone(
        lcb_cmdsubdoc_specs(cmd, ops),
        "Failed to add subspec operations for command"
    );

    LogDebug("Get password via subdoc for username: %s", auth_params->username);

    // receiver is responsible for freeing this memory if command is scheduled
    subdoc_delegate = malloc(sizeof(tcblcb_RESPDELEGATE));
    subdoc_delegate->cookie = (void*)&user_password_result;
    subdoc_delegate->callback = (tcblcb_RESPDELEGATE_CALLBACK)user_password_subdoc_callback;
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

    return user_password_result;
}

int tcblcb_api_user_login(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);

    tcblcb_HTTPResponse hresp;
    hresp.status = 400;
    hresp.string = RSPMSG_REQ_ERROR_STRING;
    hresp.strlen = RSPMSG_REQ_ERROR_STRLEN;

    char *token_value_string = NULL;
    struct kore_buf *context_buf = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    tcblcb_UserAuthParams *auth_params = get_user_params(req);
    IfNULLGotoDone(
        auth_params,
        "Failed to get user auth params from request"
    );

    tcblcb_UserPasswordResult user_password_result = get_user_password(_tcblcb_lcb_instance, auth_params);
    lcb_STATUS pword_status = user_password_result.status;
    LogDebug("User Password Loookup Status: (%d) %s", pword_status, lcb_strerror_long(pword_status));
    if (pword_status == LCB_SUCCESS) {
        if (strcmp(user_password_result.password, auth_params->password) != 0) {
            hresp.status = 401;
            hresp.string = RSPMSG_USR_BAD_PWD_STRING;
            hresp.strlen = RSPMSG_USR_BAD_PWD_STRLEN;
            goto done;
        }

        // note that if we fail to prepare a JSON response we could have an edge case where
        // the insert actually succeeded but an error will still be returned 
        hresp.status = 500;
        hresp.string = RSPMSG_USR_PWD_JSON_BAD_STRING;
        hresp.strlen = RSPMSG_USR_PWD_JSON_BAD_STRLEN;

        context_buf = kore_buf_alloc(BUFSIZ);
        kore_buf_appendf(context_buf, "KV get - scoped to %s.users: for password field in document %s", auth_params->tenant, auth_params->username);

        size_t context_strlen;
        char *context_string = kore_buf_stringify(context_buf, &context_strlen);

        // create main response object
        response_json = cJSON_CreateObject();
        // add data to response object
        cJSON *data_json = cJSON_AddObjectToObject(response_json, "data");
        IfNULLGotoDone(
            data_json,
            "Failed to create response data object"
        );
        token_value_string = gen_token(auth_params->username);
        IfNULLGotoDone(
            token_value_string,
            "Failed to create token value string"
        );
        IfFalseGotoDone(
            cJSON_AddItemToObject(data_json, "token", cJSON_CreateStringReference(token_value_string)),
            "Failed to create response token object"
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
    } else if (pword_status == LCB_ERR_DOCUMENT_NOT_FOUND) {
        hresp.status = 401;
        hresp.string = RSPMSG_USR_DNEXIST_STRING;
        hresp.strlen = RSPMSG_USR_DNEXIST_STRLEN;
    } else {
        hresp.status = 500;
        hresp.string = RSPMSG_USR_PWD_FAIL_STRING;
        hresp.strlen = RSPMSG_USR_PWD_FAIL_STRLEN;
    }

done:
    http_response(req, hresp.status, hresp.string, hresp.strlen);

    if (auth_params != NULL) {
        delete_user_params(auth_params);
    }

    if (token_value_string != NULL) {
        free(token_value_string);
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

int tcblcb_api_user_signup(struct http_request *req)
{
    ProcessCORSAndExitIfPreflight(req);

    tcblcb_HTTPResponse hresp;
    hresp.status = 500;
    hresp.string = RSPMSG_REQ_ERROR_STRING;
    hresp.strlen = RSPMSG_REQ_ERROR_STRLEN;

    char *token_value_string = NULL;
    struct kore_buf *context_buf = NULL;

    cJSON *response_json = NULL;
    char *response_string = NULL;
    size_t response_strlen = 0;

    tcblcb_UserAuthParams *auth_params = get_user_params(req);
    IfNULLGotoDone(
        auth_params,
        "Failed to get user auth params from request"
    );

    // prepare the user JSON document using the updated strings
    lcb_STATUS insert_status = insert_user(auth_params);
    LogDebug("User Signup Insert Status: (%d) %s", insert_status, lcb_strerror_long(insert_status));
    if (insert_status == LCB_SUCCESS) {
        // note that if we fail to prepare a JSON response we could have an edge case where
        // the insert actually succeeded but an error will still be returned 
        hresp.status = 500;
        hresp.string = RSPMSG_USR_INS_JSON_BAD_STRING;
        hresp.strlen = RSPMSG_USR_INS_JSON_BAD_STRLEN;

        context_buf = kore_buf_alloc(BUFSIZ);
        kore_buf_appendf(context_buf, "KV insert - scoped to %s.users: document %s", auth_params->tenant, auth_params->username);

        size_t context_strlen;
        char *context_string = kore_buf_stringify(context_buf, &context_strlen);

        // create main response object
        response_json = cJSON_CreateObject();
        // add data to response object
        cJSON *data_json = cJSON_AddObjectToObject(response_json, "data");
        IfNULLGotoDone(
            data_json,
            "Failed to create response data object"
        );
        token_value_string = gen_token(auth_params->username);
        IfNULLGotoDone(
            token_value_string,
            "Failed to create token value string"
        );
        IfFalseGotoDone(
            cJSON_AddItemToObject(data_json, "token", cJSON_CreateStringReference(token_value_string)),
            "Failed to create response token object"
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

        hresp.status = 201;
        hresp.string = response_string;
        hresp.strlen = response_strlen;
    } else if (insert_status == LCB_ERR_DOCUMENT_EXISTS) {
        hresp.status = 409;
        hresp.string = RSPMSG_USR_EXISTS_STRING;
        hresp.strlen = RSPMSG_USR_EXISTS_STRLEN;
    } else {
        hresp.status = 500;
        hresp.string = RSPMSG_USR_INS_FAIL_STRING;
        hresp.strlen = RSPMSG_USR_INS_FAIL_STRLEN;
    }

done:
    http_response(req, hresp.status, hresp.string, hresp.strlen);

    if (auth_params != NULL) {
        delete_user_params(auth_params);
    }

    if (token_value_string != NULL) {
        free(token_value_string);
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
