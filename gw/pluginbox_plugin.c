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

#include <dlfcn.h>
#include "gwlib/gwlib.h"
#include "gw/smsc/smpp_pdu.h"
#include "pluginbox_plugin.h"

static List *smsbox_inbound_plugins;
static List *bearerbox_inbound_plugins;
static List *all_plugins;

PluginBoxMsg *pluginbox_msg_create() {
    PluginBoxMsg *pluginbox_msg = gw_malloc(sizeof (PluginBoxMsg));
    pluginbox_msg->callback = NULL;
    pluginbox_msg->done = NULL;
    pluginbox_msg->chain = 0;
    pluginbox_msg->context = NULL;
    pluginbox_msg->msg = NULL;
    pluginbox_msg->status = 0;

    return pluginbox_msg;
}

void pluginbox_msg_destroy(PluginBoxMsg *pluginbox_msg) {
    /* We do not destroy our msg as this will be passed along to the return chain */
    gw_free(pluginbox_msg);
}

int pluginbox_plugin_compare(const PluginBoxPlugin *a, const PluginBoxPlugin *b) {
    return a->priority - b->priority;
}

PluginBoxPlugin *pluginbox_plugin_create() {
    PluginBoxPlugin *pluginbox_plugin = gw_malloc(sizeof (PluginBoxPlugin));
    pluginbox_plugin->process = NULL;
    pluginbox_plugin->path = NULL;
    pluginbox_plugin->args = NULL;
    pluginbox_plugin->shutdown = NULL;
    return pluginbox_plugin;
}

void pluginbox_plugin_destroy(PluginBoxPlugin *pluginbox_plugin) {
    if(pluginbox_plugin->shutdown) {
        pluginbox_plugin->shutdown(pluginbox_plugin);
    }
    
    octstr_destroy(pluginbox_plugin->path);
    octstr_destroy(pluginbox_plugin->args);
    
    gw_free(pluginbox_plugin);
}

void pluginbox_plugins_done(PluginBoxMsg *pluginbox_msg) {
    if(pluginbox_msg->done) {
        pluginbox_msg->done(pluginbox_msg->context, pluginbox_msg->msg, pluginbox_msg->status);
    }
    pluginbox_msg_destroy(pluginbox_msg);
}

void pluginbox_plugins_next(PluginBoxMsg *pluginbox_msg) {
    List *target = NULL;
    if (pluginbox_msg->type & PLUGINBOX_MESSAGE_FROM_SMSBOX) {
        target = smsbox_inbound_plugins;
    }
    if (pluginbox_msg->type & PLUGINBOX_MESSAGE_FROM_BEARERBOX) {
        target = bearerbox_inbound_plugins;
    }
    
    if((target != NULL) && (pluginbox_msg->status != PLUGINBOX_MESSAGE_REJECT)) {
        long len = gwlist_len(target);
        if(pluginbox_msg->chain < len) {
            /* We're OK */
            PluginBoxPlugin *plugin = gwlist_get(target, pluginbox_msg->chain);
            if(plugin != NULL) {
                ++pluginbox_msg->chain;
                plugin->process(plugin, pluginbox_msg);
                return;
            }
        }
    }
    pluginbox_plugins_done(pluginbox_msg);
}

void pluginbox_plugins_start(void (*done)(void *context, Msg *msg, int status), void *context, Msg *msg, long type) {

    PluginBoxMsg *pluginbox_msg = pluginbox_msg_create();
    pluginbox_msg->done = done;
    pluginbox_msg->callback = pluginbox_plugins_next;
    pluginbox_msg->msg = msg;
    pluginbox_msg->context = context;
    pluginbox_msg->type = type;
    pluginbox_plugins_next(pluginbox_msg);

}

int pluginbox_plugins_init(Cfg *cfg) {
    smsbox_inbound_plugins = gwlist_create();
    bearerbox_inbound_plugins = gwlist_create();
    all_plugins = gwlist_create();

    gw_prioqueue_t *prioqueue = gw_prioqueue_create((int(*)(const void *, const void *))pluginbox_plugin_compare);

    char *error_str;
    void *lib;
    Octstr *tmp_str, *path;
    long tmp_long;
    List *grplist = cfg_get_multi_group(cfg, octstr_imm("pluginbox-plugin"));
    PluginBoxPlugin *plugin;
    CfgGroup *grp;
    while (grplist && (grp = gwlist_extract_first(grplist)) != NULL) {
        if (cfg_get_integer(&tmp_long, grp, octstr_imm("priority")) == -1) {
            tmp_long = 0;
        }

        plugin = pluginbox_plugin_create();
        plugin->path = cfg_get(grp, octstr_imm("path"));
        plugin->priority = tmp_long;
        plugin->running_configuration = cfg;

        if (!octstr_len(plugin->path)) {
            panic(0, "No 'path' specified for pluginbox-plugin group");
        }

        lib = dlopen(octstr_get_cstr(plugin->path), RTLD_NOW | RTLD_GLOBAL);

        if (!lib) {
            error_str = dlerror();
            panic(0, "Error opening plugin '%s' (%s)", octstr_get_cstr(plugin->path), error_str);
        }

        tmp_str = cfg_get(grp, octstr_imm("init"));
        if (octstr_len(tmp_str)) {
            plugin->init = dlsym(lib, octstr_get_cstr(tmp_str));
            if (!plugin->init) {
                panic(0, "init-function %s unable to load from %s", octstr_get_cstr(tmp_str), octstr_get_cstr(plugin->path));
            }
            plugin->args = cfg_get(grp, octstr_imm("args"));
            if (!plugin->init(plugin)) {
                panic(0, "Plugin %s initialization failed", octstr_get_cstr(plugin->path));
            } else {
                info(0, "Plugin %s initialized priority %ld", octstr_get_cstr(plugin->path), plugin->priority);
                gw_prioqueue_produce(prioqueue, plugin);
            }
        } else {
            panic(0, "No initialization 'init' function specified, cannot continue (%s)", octstr_get_cstr(plugin->path));
        }
        octstr_destroy(tmp_str);
    }

    while ((plugin = gw_prioqueue_consume(prioqueue)) != NULL) {
        gwlist_produce(all_plugins, plugin);

        if (plugin->direction & PLUGINBOX_MESSAGE_FROM_SMSBOX) {
            debug("pluginbox.plugin.init", 0, "Adding plugin %s to from smsbox process queue", octstr_get_cstr(plugin->path));
            gwlist_produce(smsbox_inbound_plugins, plugin);
        }
        if (plugin->direction & PLUGINBOX_MESSAGE_FROM_BEARERBOX) {
            debug("pluginbox.plugin.init", 0, "Adding plugin %s to from bearerbox process queue", octstr_get_cstr(plugin->path));
            gwlist_produce(bearerbox_inbound_plugins, plugin);
        }
    }
    
    gwlist_destroy(grplist, NULL);
    
    gw_prioqueue_destroy(prioqueue, NULL);
}

void pluginbox_plugin_shutdown() {
    gwlist_destroy(smsbox_inbound_plugins, NULL);
    gwlist_destroy(bearerbox_inbound_plugins, NULL);
    
    gwlist_destroy(all_plugins, (void (*)(void *))pluginbox_plugin_destroy);
}
