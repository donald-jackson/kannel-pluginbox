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
 * @author Rene Kluwen <rene.kluwen@chimit.nl>
 */

#include "gwlib/gwlib.h"
#include "gw/pluginbox_plugin.h"
#include "sqlbox/sqlbox_sql.inc"
#include "sqlbox/sqlbox_mysql.inc"

typedef struct {
	Octstr *id;
	int save_mo, save_mt, save_dlr;
	struct server_type *backend;
} PluginCdr;

#define PLUGINBOX_LOG_PREFIX "[CDR-PLUGIN] "

PluginCdr *pluginbox_cdr_plugin_create() {
    PluginCdr *plugin_cdr = gw_malloc(sizeof(PluginCdr));
    plugin_cdr->id = NULL;
    plugin_cdr->backend = NULL;
    return plugin_cdr;
}

void pluginbox_cdr_plugin_destroy(PluginCdr *plugin_cdr) {
    if (plugin_cdr->id) octstr_destroy(plugin_cdr->id);
    if (plugin_cdr->backend && plugin_cdr->backend->sql_leave) {
	plugin_cdr->backend->sql_leave();
    }
    gw_free(plugin_cdr);
}

static int sqlbox_is_allowed_in_group(Octstr *group, Octstr *variable)
{
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
    #include "sqlbox/sqlbox-cfg.def"

    return 0;
}

#undef OCTSTR
#undef SINGLE_GROUP
#undef MULTI_GROUP

static int sqlbox_is_single_group(Octstr *query)
{
    #define OCTSTR(name)
    #define SINGLE_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 1;
    #define MULTI_GROUP(name, fields) \
        if (octstr_compare(octstr_imm(#name), query) == 0) \
        return 0;
    #include "sqlbox/sqlbox-cfg.def"
    return 0;
}

void pluginbox_cdr_configure(PluginCdr *plugin_cdr, Cfg *cfg)
{
    CfgGroup *grp;

    grp = cfg_get_single_group(cfg, octstr_imm("sqlbox"));
    /* set up save parameters */
    if (cfg_get_bool(&plugin_cdr->save_mo, grp, octstr_imm("save-mo")) == -1)
        plugin_cdr->save_mo = 1;

    if (cfg_get_bool(&plugin_cdr->save_mt, grp, octstr_imm("save-mt")) == -1)
        plugin_cdr->save_mt = 1;

    if (cfg_get_bool(&plugin_cdr->save_dlr, grp, octstr_imm("save-dlr")) == -1)
        plugin_cdr->save_dlr = 1;
}

void pluginbox_cdr_shutdown(PluginBoxPlugin *pluginbox_plugin) {
    PluginCdr *plugin_cdr = pluginbox_plugin->context;

    pluginbox_cdr_plugin_destroy(plugin_cdr);
    info(0, PLUGINBOX_LOG_PREFIX "Shutdown complete");
}

void pluginbox_cdr_process(PluginBoxPlugin *pluginbox_plugin, PluginBoxMsg *pluginbox_msg) {
    PluginCdr *plugin_cdr;
    Msg *msg_escaped;

    if (msg_type(pluginbox_msg->msg) == sms) {
	debug("pluginbox.http.process", 0, PLUGINBOX_LOG_PREFIX "Got plugin message chain %ld", pluginbox_msg->chain);
	plugin_cdr = (PluginCdr *)pluginbox_plugin->context;
	msg_escaped = msg_duplicate(pluginbox_msg->msg);
        switch (pluginbox_msg->msg->sms.sms_type) {
	case report_mo:
	    if (plugin_cdr->save_dlr) {
	        plugin_cdr->backend->sql_save_msg(msg_escaped, octstr_imm("DLR"));
	    }
	    break;
	case mo:
	    if (plugin_cdr->save_mo) {
	        plugin_cdr->backend->sql_save_msg(msg_escaped, octstr_imm("MO"));
	    }
	    break;
	case mt_reply:
	case mt_push:
	    if (plugin_cdr->save_mt) {
	        plugin_cdr->backend->sql_save_msg(msg_escaped, octstr_imm("MT"));
	    }
	    break;
	}
	msg_destroy(msg_escaped);
    }
    pluginbox_msg->callback(pluginbox_msg);
}

Octstr *pluginbox_cdr_status(PluginBoxPlugin *pluginbox_plugin, List *cgivars, int status_type)
{
	const char *fmt;
	Octstr *value;
	int intvalue;
	PluginCdr *plugin_cdr = (PluginCdr *)pluginbox_plugin->context;

	value = http_cgi_variable(cgivars, "save-mo");
	if (value) {
		plugin_cdr->save_mo = atoi(octstr_get_cstr(value)) ? 1 : 0;
	}
	value = http_cgi_variable(cgivars, "save-mt");
	if (value) {
		plugin_cdr->save_mt = atoi(octstr_get_cstr(value)) ? 1 : 0;
	}
	value = http_cgi_variable(cgivars, "save-dlr");
	if (value) {
		plugin_cdr->save_dlr = atoi(octstr_get_cstr(value)) ? 1 : 0;
	}
	switch (status_type) {
	case PLUGINSTATUS_HTML:
	case PLUGINSTATUS_WML:
		fmt = "Configuration:<br/>\n<br/>\nSave MO: %d.<br/>\nSave MT: %d.<br/>\nSave DLR: %d.<br/>\n";
		break;
	case PLUGINSTATUS_XML:
		fmt = "<plugin-cdr>\n    <save-mo>%d</save-mo>\n    <save-mt>%d</save-mt>\n    <save-dlr>%d</save-dlr>\n</plugin-cdr>\n";
		break;
	case PLUGINSTATUS_TEXT:
	default:
		fmt = "Configuration:\n\nSave MO: %d.\nSave MT: %d.\nSave DLR: %d.\n";
		break;
	}
	return octstr_format(fmt, plugin_cdr->save_mo, plugin_cdr->save_mt, plugin_cdr->save_dlr);
}

int pluginbox_cdr_init(PluginBoxPlugin *pluginbox_plugin) {
	info(0, PLUGINBOX_LOG_PREFIX "Using configuration from %s", octstr_get_cstr(pluginbox_plugin->args));
	cfg_add_hooks(sqlbox_is_allowed_in_group, sqlbox_is_single_group);

	Cfg *cfg = cfg_create(pluginbox_plugin->args);
	if(cfg_read(cfg) == -1) {
		error(0, PLUGINBOX_LOG_PREFIX "Couldn't load CDR plugin %s configuration", octstr_get_cstr(pluginbox_plugin->id));
		cfg_destroy(cfg);
		return 0;
	}
	pluginbox_plugin->context = pluginbox_cdr_plugin_create();
	pluginbox_cdr_configure(pluginbox_plugin->context, cfg);
	((PluginCdr *)pluginbox_plugin->context)->backend = sqlbox_init_sql(cfg);
	((PluginCdr *)pluginbox_plugin->context)->backend->sql_enter(cfg);
	cfg_destroy(cfg);
	pluginbox_plugin->direction = PLUGINBOX_MESSAGE_FROM_SMSBOX | PLUGINBOX_MESSAGE_FROM_BEARERBOX;
	pluginbox_plugin->process = pluginbox_cdr_process;
	pluginbox_plugin->shutdown = pluginbox_cdr_shutdown;
	pluginbox_plugin->status = pluginbox_cdr_status;
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
