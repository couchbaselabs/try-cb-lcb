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

_Thread_local lcb_INSTANCE *_tcblcb_lcb_instance = NULL;

// See `docker-compose.yml` for the `db` alias that resolves to the couchbase-server docker hostname.
static const char   DEFAULT_CONN_STRING[]   = "couchbase://db";
static const size_t DEFAULT_CONN_STRLEN     = sizeof(DEFAULT_CONN_STRING) - 1;
static const char   DEFAULT_USER_STRING[]   = "Administrator";
static const size_t DEFAULT_USER_STRLEN     = sizeof(DEFAULT_USER_STRING) - 1;
static const char   DEFAULT_PSWD_STRING[]   = "password";
static const size_t DEFAULT_PSWD_STRLEN     = sizeof(DEFAULT_PSWD_STRING) - 1;
static const char   TRAVEL_BUCKET_STRING[]  = "travel-sample";
static const size_t TRAVEL_BUCKET_STRLEN    = sizeof(TRAVEL_BUCKET_STRING) - 1;

static const char   ENV_CB_CONN[]   = "CB_CONN";
static const char   ENV_CB_USER[]   = "CB_USER";
static const char   ENV_CB_PSWD[]   = "CB_PSWD";

static const char  *_cb_conn_string = DEFAULT_CONN_STRING;
static size_t       _cb_conn_strlen = DEFAULT_CONN_STRLEN;
static const char  *_cb_user_string = DEFAULT_USER_STRING;
static size_t       _cb_user_strlen = DEFAULT_USER_STRLEN;
static const char  *_cb_pswd_string = DEFAULT_PSWD_STRING;
static size_t       _cb_pswd_strlen = DEFAULT_PSWD_STRLEN;

static void open_callback(__unused lcb_INSTANCE *instance, lcb_STATUS rc)
{
    kore_log(LOG_NOTICE, "Open bucket callback result was: %s", lcb_strerror_short(rc));
}

static void get_callback(lcb_INSTANCE *instance, __unused int cbtype, const lcb_RESPGET *resp)
{
    tcblcb_RESPDELEGATE *resp_delegate = NULL;

    IfLCBFailGotoDone(
        lcb_respget_cookie(resp, (void**)&resp_delegate),
        "Failed to get response delegate cookie"
    );
    IfNULLGotoDone(
        resp_delegate,
        "Response delegate is NULL"
    );
    IfNULLGotoDone(
        resp_delegate->callback,
        "Response delegate callback is NULL"
    );

    resp_delegate->callback(instance, resp_delegate->cookie, (lcb_RESPBASE *)resp);

done:
    // receiver is responsible for freeing this memory if command is scheduled
    if (resp_delegate != NULL) {
        free(resp_delegate);
    }
}

static void store_callback(lcb_INSTANCE *instance, __unused int cbtype, const lcb_RESPSTORE *resp)
{
    tcblcb_RESPDELEGATE *resp_delegate = NULL;

    IfLCBFailGotoDone(
        lcb_respstore_cookie(resp, (void**)&resp_delegate),
        "Failed to get response delegate cookie"
    );
    IfNULLGotoDone(
        resp_delegate,
        "Response delegate is NULL"
    );
    IfNULLGotoDone(
        resp_delegate->callback,
        "Response delegate callback is NULL"
    );

    resp_delegate->callback(instance, resp_delegate->cookie, (lcb_RESPBASE *)resp);

done:
    // receiver is responsible for freeing this memory if command is scheduled
    if (resp_delegate != NULL) {
        free(resp_delegate);
    }
}

static void subdoc_callback(lcb_INSTANCE *instance, __unused int cbtype, const lcb_RESPSUBDOC *resp)
{
    tcblcb_RESPDELEGATE *resp_delegate = NULL;

    IfLCBFailGotoDone(
        lcb_respsubdoc_cookie(resp, (void**)&resp_delegate),
        "Failed to get response delegate cookie"
    );
    IfNULLGotoDone(
        resp_delegate,
        "Response delegate is NULL"
    );
    IfNULLGotoDone(
        resp_delegate->callback,
        "Response delegate callback is NULL"
    );

    resp_delegate->callback(instance, resp_delegate->cookie, (lcb_RESPBASE *)resp);

done:
    // receiver is responsible for freeing this memory if command is scheduled
    if (resp_delegate != NULL) {
        free(resp_delegate);
    }
}

static void destroy_cb_instance()
{
    if (_tcblcb_lcb_instance != NULL) {
        lcb_destroy(_tcblcb_lcb_instance);
        _tcblcb_lcb_instance = NULL;
    }
}

void kore_parent_configure(__unused int argc, __unused char *argv[])
{
    // use current time as the random number generator seed
    srand(time(NULL));

    // kore has it's own command line options processing so we'll use env variables instead
    char *cb_conn = getenv(ENV_CB_CONN);
    if (cb_conn != NULL && cb_conn[0] != '\0') {
        _cb_conn_string = cb_conn;
        _cb_conn_strlen = strlen(cb_conn);
    }

    char *cb_user = getenv(ENV_CB_USER);
    if (cb_user != NULL && cb_user[0] != '\0') {
        _cb_user_string = cb_user;
        _cb_user_strlen = strlen(cb_user);
    }

    char *cb_pswd = getenv(ENV_CB_PSWD);
    if (cb_pswd != NULL && cb_pswd[0] != '\0') {
        _cb_pswd_string = cb_pswd;
        _cb_pswd_strlen = strlen(cb_pswd);
    }

    kore_log(LOG_INFO, "Couchbase Connection (%s): %s", ENV_CB_CONN, _cb_conn_string);
    kore_log(LOG_INFO, "Couchbase Username (%s): %s", ENV_CB_USER, _cb_user_string);
}

void kore_worker_configure()
{
    bool connected = false;

    lcb_CREATEOPTS *create_options = NULL;
    lcb_createopts_create(&create_options, LCB_TYPE_CLUSTER);
    lcb_createopts_connstr(create_options, _cb_conn_string, _cb_conn_strlen);
    lcb_createopts_credentials(
        create_options,
        _cb_user_string, _cb_user_strlen,
        _cb_pswd_string, _cb_pswd_strlen
    );

    // Note that we're creating the instance as a thread local in the worker threads
    
    IfLCBFailGotoDone(
        lcb_create(&_tcblcb_lcb_instance, create_options),
        "Failed to create a libcouchbase instance"
    );
    IfLCBFailLogWarningMsg(
        lcb_createopts_destroy(create_options),
        "Failed to destroy libcouchbase create options"
    );
    IfNULLGotoDone(
        _tcblcb_lcb_instance,
        "libcouchbase instance is NULL"
    );

    // schedule the initial connect operation
    IfLCBFailGotoDone(
        lcb_connect(_tcblcb_lcb_instance),
        "Failed to schedule the Couchbase connect operation"
    );

    // wait for the initial connect operation to complete
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Failed to establish initial connection to Couchbase"
    );

    // confirm the resulting bootstrap status
    IfLCBFailGotoDone(
        lcb_get_bootstrap_status(_tcblcb_lcb_instance),
        "Couchbase bootstrap failed"
    );

    // install any global callbacks that are needed to delegate to other logic
    lcb_set_open_callback(_tcblcb_lcb_instance, open_callback);
    lcb_install_callback(_tcblcb_lcb_instance, LCB_CALLBACK_GET, (lcb_RESPCALLBACK)get_callback);
    lcb_install_callback(_tcblcb_lcb_instance, LCB_CALLBACK_STORE, (lcb_RESPCALLBACK)store_callback);
    lcb_install_callback(_tcblcb_lcb_instance, LCB_CALLBACK_SDLOOKUP, (lcb_RESPCALLBACK)subdoc_callback);
    lcb_install_callback(_tcblcb_lcb_instance, LCB_CALLBACK_SDMUTATE, (lcb_RESPCALLBACK)subdoc_callback);

    // schedule an open bucket operation
    IfLCBFailGotoDone(
        lcb_open(_tcblcb_lcb_instance, TRAVEL_BUCKET_STRING, TRAVEL_BUCKET_STRLEN),
        "Failed to schedule the open bucket operation"
    );

    // wait for the open bucket operation to complete
    IfLCBFailGotoDone(
        lcb_wait(_tcblcb_lcb_instance, LCB_WAIT_DEFAULT),
        "Open bucket operation failed"
    );

    connected = true;

done:
    if (!connected) {
        destroy_cb_instance();
    }
}

void kore_worker_teardown()
{
    destroy_cb_instance();
}

int tcblcb_page_index(struct http_request *req)
{
    const char body[] = "<h1> Kore.io Travel Sample API </h1>"
    "A sample API for getting started with Couchbase Server that demonstrates using the <a href=\"https://docs.couchbase.com/c-sdk/current/hello-world/start-using-sdk.html\">C SDK</a> with the <a href=\"https://kore.io/\">Kore.io</a> framework to create a RESTful service API."
    "<ul>"
    "<li> <a href = \"/apidocs\"> Learn the API with Swagger, interactively </a>"
    "<li> <a href = \"https://github.com/couchbaselabs/try-cb-lcb\"> GitHub </a>"
    "</ul>";

    http_response(req, 200, body, sizeof(body));
    return (KORE_RESULT_OK);
}
