/* ====================================================================
 * The Kannel Software License, Version 1.0
 *
 * Copyright (c) 2001-2016 Kannel Group
 * Copyright (c) 1998-2001 WapIT Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. The end-user documentation included with the redistribution,
 *    if any, must include the following acknowledgment:
 *       "This product includes software developed by the
 *        Kannel Group (http://www.kannel.org/)."
 *    Alternately, this acknowledgment may appear in the software itself,
 *    if and wherever such third-party acknowledgments normally appear.
 *
 * 4. The names "Kannel" and "Kannel Group" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please
 *    contact org@kannel.org.
 *
 * 5. Products derived from this software may not be called "Kannel",
 *    nor may "Kannel" appear in their name, without prior written
 *    permission of the Kannel Group.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE KANNEL GROUP OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This software consists of voluntary contributions made by many
 * individuals on behalf of the Kannel Group.  For more information on
 * the Kannel Group, please see <http://www.kannel.org/>.
 *
 * Portions of this software are based upon software originally written at
 * WapIT Ltd., Helsinki, Finland for the Kannel project.
 *
 *
 * @author Donald Jackson <djackson@kannel.org>
 */

#include "gwlib/gwlib.h"
#include "gw/pluginbox_plugin.h"

#define PLUGINBOX_LOG_PREFIX "[HTTP-PLUGIN] "
#define PLUGINBOX_HTTP_DEFAULT_MAX_PENDING 100

typedef struct {
    Semaphore *max_pending_requests;
    long request_thread;
    long receive_thread;
    List *queue;
    HTTPCaller *caller;
    PluginBoxPlugin *plugin;
    Octstr *id;
    Octstr *url;
} PluginHttp;

PluginHttp *pluginbox_http_plugin_create() {
    PluginHttp *plugin_http = gw_malloc(sizeof(PluginHttp));
    plugin_http->max_pending_requests = NULL;
    plugin_http->queue = NULL;
    plugin_http->receive_thread = -1;
    plugin_http->request_thread = -1;
    plugin_http->caller = NULL;
    plugin_http->id = NULL;
    plugin_http->plugin = NULL;
    return plugin_http;
}

void pluginbox_http_plugin_destroy(PluginHttp *plugin_http) {
    /* TODO */ gwlist_destroy(plugin_http->queue, NULL);
    octstr_destroy(plugin_http->id);
    semaphore_destroy(plugin_http->max_pending_requests);
    http_caller_destroy(plugin_http->caller);
    octstr_destroy(plugin_http->url);
    gw_free(plugin_http);
}


List *pluginbox_http_get_headers_for_msg(Msg *msg);
void pluginbox_http_modify_with_headers(Msg *msg, List *headers);


static int pluginbox_http_is_allowed_in_group(Octstr *group, Octstr *variable) {
    Octstr *groupstr;

    groupstr = octstr_imm("group");

#define OCTSTR(name) \
        if (octstr_compare(octstr_imm(#name), variable) == 0) \
        return 1;
#define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
#define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), group) == 0) { \
        if (octstr_compare(groupstr, variable) == 0) \
        return 1; \
        fields \
        return 0; \
    }
#include "pluginbox_http_cfg.def"

    return 0;
}

#undef OCTSTR
#undef SINGLE_GROUP
#undef MULTI_GROUP

static int pluginbox_is_single_group(Octstr *query) {
#define OCTSTR(name)
#define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
#define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
#include "pluginbox_http_cfg.def"
    return 0;
}


void pluginbox_http_shutdown(PluginBoxPlugin *pluginbox_plugin) {
    info(0, PLUGINBOX_LOG_PREFIX "Shutting down");


    PluginHttp *plugin_http = pluginbox_plugin->context;

    gwlist_remove_producer(plugin_http->queue);
    http_caller_signal_shutdown(plugin_http->caller);

    gwthread_join(plugin_http->request_thread);
    gwthread_join(plugin_http->receive_thread);

    pluginbox_http_plugin_destroy(plugin_http);

    info(0, PLUGINBOX_LOG_PREFIX "Shutdown complete");
}

void pluginbox_http_request_thread(void *arg) {
    info(0, PLUGINBOX_LOG_PREFIX "Starting request thread");

    PluginHttp *plugin_http = arg;
    PluginBoxMsg *pluginbox_msg;

    List *headers = NULL;

    while((pluginbox_msg = gwlist_consume(plugin_http->queue)) != NULL) {
        semaphore_up(plugin_http->max_pending_requests);
        headers = pluginbox_http_get_headers_for_msg(pluginbox_msg->msg);
        http_start_request(plugin_http->caller, HTTP_METHOD_GET, plugin_http->url, headers, NULL, 0, pluginbox_msg, NULL);
        http_destroy_headers(headers);
    }

}

void pluginbox_http_receive_thread(void *arg) {
    info(0, PLUGINBOX_LOG_PREFIX "Starting receive thread");
    PluginHttp *plugin_http = arg;
    Octstr *header_key, *header_value;
    int status;
    Octstr *final_url;
    List *response_headers;
    Octstr *response_body;
    PluginBoxMsg *pluginbox_msg;

    while((pluginbox_msg = http_receive_result(plugin_http->caller,&status,&final_url,&response_headers,&response_body)) != NULL) {
        semaphore_down(plugin_http->max_pending_requests);
        if((status >= HTTP_OK) && (status < HTTP_BAD_REQUEST)) {
            debug(PLUGINBOX_LOG_PREFIX"pluginbox.http.receive.thread", 0, "Upstream URL %s accepted message, processing changes", octstr_get_cstr(final_url));
            pluginbox_http_modify_with_headers(pluginbox_msg->msg, response_headers);
        } else {
            warning(0, PLUGINBOX_LOG_PREFIX"Upstream URL %s rejected msg with status %d (outside >= %d < %d), rejecting", octstr_get_cstr(final_url), status, HTTP_OK, HTTP_BAD_REQUEST);
            pluginbox_msg->status = PLUGINBOX_MESSAGE_REJECT;
        }
        pluginbox_msg->callback(pluginbox_msg);
        octstr_destroy(final_url);
        http_destroy_headers(response_headers);
        octstr_destroy(response_body);
    }
}

void pluginbox_http_process(PluginBoxPlugin *pluginbox_plugin, PluginBoxMsg *pluginbox_msg) {
    debug("pluginbox.http.process", 0, PLUGINBOX_LOG_PREFIX "Got plugin message chain %ld", pluginbox_msg->chain);

    PluginHttp *plugin_http = pluginbox_plugin->context;

    if(msg_type(pluginbox_msg->msg) == sms) {
        gwlist_produce(plugin_http->queue, pluginbox_msg);
    } else {
        debug("pluginbox.http.process", 0, PLUGINBOX_LOG_PREFIX "Ingoring non sms message type");
        pluginbox_msg->callback(pluginbox_msg);
    }
}

int pluginbox_http_configure(PluginHttp *plugin_http) {
    Octstr *tmp_str;
    long tmp_long;
    PluginBoxPlugin *pluginbox_plugin = plugin_http->plugin;

    if(!octstr_len(pluginbox_plugin->id)) {
        error(0, PLUGINBOX_LOG_PREFIX "An 'id' parameter must be specified in the pluginbox-plugin group for HTTP plugins");
        return 0;
    }

    Cfg *cfg = cfg_create(pluginbox_plugin->args);

    if(cfg_read(cfg) == -1) {
        error(0, PLUGINBOX_LOG_PREFIX "Couldn't load HTTP plugin %s configuration", octstr_get_cstr(pluginbox_plugin->id));
        cfg_destroy(cfg);
        return 0;
    }

    List *grplist = cfg_get_multi_group(cfg, octstr_imm("pluginbox-http"));
    CfgGroup *grp;
    Octstr *config_id;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        config_id = cfg_get(grp, octstr_imm("id"));

        if(octstr_len(config_id) && (octstr_compare(config_id, pluginbox_plugin->id) == 0)) {
            octstr_destroy(config_id);
            goto found;
        }
        octstr_destroy(config_id);
    }

    gwlist_destroy(grplist, NULL);

    goto error;

found:
    info(0, PLUGINBOX_LOG_PREFIX "Loading configuration for %s", octstr_get_cstr(pluginbox_plugin->id));

    plugin_http->url = cfg_get(grp, octstr_imm("url"));

    if(!octstr_len(plugin_http->url)) {
        error(0, PLUGINBOX_LOG_PREFIX "No 'url' specified in 'pluginbox-http' group id %s", octstr_get_cstr(pluginbox_plugin->id));
        return 0;
    }

    if(cfg_get_integer(&tmp_long,grp, octstr_imm("max-pending-requests")) == -1) {
        tmp_long = PLUGINBOX_HTTP_DEFAULT_MAX_PENDING;
    }

    info(0, PLUGINBOX_LOG_PREFIX "Max pending requests set to %ld", tmp_long);

    plugin_http->max_pending_requests = semaphore_create(tmp_long);

    gwlist_destroy(grplist, NULL);

    cfg_destroy(cfg);


    return 1;
error:
    error(0, PLUGINBOX_LOG_PREFIX "No matching 'pluginbox-http' group found for id %s, cannot initialize", octstr_get_cstr(pluginbox_plugin->id));
    cfg_destroy(cfg);
    return 0;
}

int pluginbox_http_init(PluginBoxPlugin *pluginbox_plugin) {
    PluginHttp *plugin_http = pluginbox_http_plugin_create();

    info(0, PLUGINBOX_LOG_PREFIX "Initializing HTTP plugin");

    cfg_add_hooks(pluginbox_http_is_allowed_in_group, pluginbox_is_single_group);

    pluginbox_plugin->process = pluginbox_http_process;
    pluginbox_plugin->direction = PLUGINBOX_MESSAGE_FROM_SMSBOX | PLUGINBOX_MESSAGE_FROM_BEARERBOX;
    pluginbox_plugin->shutdown = pluginbox_http_shutdown;
    plugin_http->plugin = pluginbox_plugin;

    if(!pluginbox_http_configure(plugin_http)) {
        pluginbox_http_plugin_destroy(plugin_http);
        return 0;
    }

    plugin_http->queue = gwlist_create();

    gwlist_add_producer(plugin_http->queue);

    plugin_http->caller = http_caller_create();

    plugin_http->request_thread = gwthread_create(pluginbox_http_request_thread, plugin_http);
    plugin_http->receive_thread = gwthread_create(pluginbox_http_receive_thread, plugin_http);

    pluginbox_plugin->context = plugin_http;



    return 1;
}

static char *type_as_str(Msg *msg)
{
    switch (msg->type) {
#define MSG(t, stmt) case t: return #t;
#include "gw/msg-decl.h"
        default:
            return "unknown type";
    }
}


void pluginbox_http_modify_with_headers(Msg *msg, List *headers)
{
    Octstr *prefix, *key, *value;


    prefix = octstr_format("X-Kannel-Plugin-Msg-%s", type_as_str(msg));

#define INTEGER(name) key = octstr_format("%S-%s", prefix, #name); \
     value = http_header_find_first(headers, octstr_get_cstr(key)); \
     if(value != NULL) { \
        octstr_parse_long(&p->name, value, 0, 10); \
        debug("pluginbox.http.modify.with.headers", 0, PLUGINBOX_LOG_PREFIX "Found header %s and set to value %ld ", octstr_get_cstr(key), p->name); \
    } \
    octstr_destroy(value); octstr_destroy(key);
#define OCTSTR(name) key = octstr_format("%S-%s", prefix, #name); \
     value = http_header_find_first(headers, octstr_get_cstr(key)); \
     if(value != NULL) { \
        octstr_destroy(p->name); \
        p->name = octstr_duplicate(value); \
        octstr_url_decode(p->name); \
        debug("pluginbox.http.modify.with.headers", 0, PLUGINBOX_LOG_PREFIX "Found header %s and set to value %s ", octstr_get_cstr(key), octstr_get_cstr(p->name)); \
    } else { \
        debug("pluginbox.http.modify.with.headers", 0, PLUGINBOX_LOG_PREFIX "No header found for %s", octstr_get_cstr(key)); \
    } \
    octstr_destroy(value); octstr_destroy(key);
#define UUID(name) key = octstr_format("%S-%s", prefix, #name); \
     value = http_header_find_first(headers, octstr_get_cstr(key)); \
     if(value != NULL) { \
        uuid_parse(octstr_get_cstr(value), p->name); \
        debug("pluginbox.http.modify.with.headers", 0, PLUGINBOX_LOG_PREFIX "Found header %s and set to value %s ", octstr_get_cstr(key), octstr_get_cstr(value)); \
    } \
    octstr_destroy(value); octstr_destroy(key);
#define VOID(name) ;
#define MSG(type, stmt) \
    case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "gw/msg-decl.h"
        default:
            panic(0, "Internal error: unknown message type: %d",
                  msg->type);
    }

    octstr_destroy(prefix);
}



List *pluginbox_http_get_headers_for_msg(Msg *msg)
{
    List *headers = http_create_empty_headers();

    Octstr *prefix, *key, *value;


    prefix = octstr_format("X-Kannel-Plugin-Msg-%s", type_as_str(msg));

    key = octstr_create("X-Kannel-Plugin-Msg-Type");

    http_header_add(headers, octstr_get_cstr(key), type_as_str(msg));

    octstr_destroy(key);


    char uuid[UUID_STR_LEN+1];

#define INTEGER(name) value = octstr_format("%ld", p->name); key = octstr_format("%S-%s", prefix, #name); http_header_add(headers, octstr_get_cstr(key), octstr_get_cstr(value)); octstr_destroy(key); octstr_destroy(value);
#define OCTSTR(name) value = octstr_format("%E", p->name); key = octstr_format("%S-%s", prefix, #name); http_header_add(headers, octstr_get_cstr(key), octstr_get_cstr(value)); octstr_destroy(key); octstr_destroy(value);
#define UUID(name) uuid_unparse(p->name, uuid); value = octstr_format("%s", uuid); key = octstr_format("%S-%s", prefix, #name); http_header_add(headers, octstr_get_cstr(key), octstr_get_cstr(value)); octstr_destroy(key); octstr_destroy(value);
#define VOID(name) ;
#define MSG(type, stmt) \
    case type: { struct type *p = &msg->type; stmt } break;

    switch (msg->type) {
#include "gw/msg-decl.h"
        default:
            panic(0, PLUGINBOX_LOG_PREFIX"Internal error: unknown message type: %d",
                  msg->type);
    }

    octstr_destroy(prefix);

    return headers;
}


