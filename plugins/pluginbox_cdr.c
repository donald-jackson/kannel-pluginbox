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
#include "cdr/cdr_sql.inc"

#define SLEEP_BETWEEN_EMPTY_SELECTS 1.0

typedef struct {
	Octstr *id;
	Octstr *global_sender;
	int save_mo, save_mt, save_dlr;
	DBPool *pool;
	long insert_thread;
	long limit_per_cycle;
	Octstr *logtable;
	Octstr *inserttable;
	volatile int running;
} PluginCdr;

#define PLUGINBOX_LOG_PREFIX "[CDR-PLUGIN] "

PluginCdr *pluginbox_cdr_plugin_create() {
    PluginCdr *plugin_cdr = gw_malloc(sizeof(PluginCdr));
    plugin_cdr->id = NULL;
    plugin_cdr->pool = NULL;
    plugin_cdr->global_sender = NULL;
    plugin_cdr->limit_per_cycle = 0;
    plugin_cdr->logtable = NULL;
    plugin_cdr->inserttable = NULL;
    return plugin_cdr;
}

void pluginbox_cdr_plugin_destroy(PluginCdr *plugin_cdr) {
    if (plugin_cdr->id) octstr_destroy(plugin_cdr->id);
    if (plugin_cdr->global_sender) octstr_destroy(plugin_cdr->global_sender);
    if (plugin_cdr->logtable) octstr_destroy(plugin_cdr->logtable);
    if (plugin_cdr->inserttable) octstr_destroy(plugin_cdr->inserttable);
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
    #include "cdr/cdr-cfg.def"

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
    #include "cdr/cdr-cfg.def"
    return 0;
}

int pluginbox_cdr_configure(PluginCdr *plugin_cdr, Cfg *cfg)
{
    CfgGroup *grp;
    Octstr *db_id;

    grp = cfg_get_single_group(cfg, octstr_imm("plugin-cdr"));

    if (cfg_get_integer(&plugin_cdr->limit_per_cycle, grp, octstr_imm("limit-per-cycle")) == -1) {
        plugin_cdr->limit_per_cycle = 0;
	info(0, PLUGINBOX_LOG_PREFIX "No limit-per-cycle configured. Disabling send thread.");
    }

    /* set up save parameters */
    if (cfg_get_bool(&plugin_cdr->save_mo, grp, octstr_imm("save-mo")) == -1)
        plugin_cdr->save_mo = 1;

    if (cfg_get_bool(&plugin_cdr->save_mt, grp, octstr_imm("save-mt")) == -1)
        plugin_cdr->save_mt = 1;

    if (cfg_get_bool(&plugin_cdr->save_dlr, grp, octstr_imm("save-dlr")) == -1)
        plugin_cdr->save_dlr = 1;

    plugin_cdr->logtable = cfg_get(grp, octstr_imm("sql-log-table"));
    plugin_cdr->inserttable = cfg_get(grp, octstr_imm("sql-insert-table"));

    if (NULL == (db_id = cfg_get(grp, octstr_imm("db-id")))) {
    	if (NULL == (db_id = cfg_get(grp, octstr_imm("id")))) {
		debug("plugin_cdr.c", 0, "No db-id set in configuration file.");
		return 0;
	}
    }
    plugin_cdr->pool = db_init_shared(cfg, db_id);
    if (NULL == plugin_cdr->pool) {
	debug("plugin_cdr.c", 0, "Cannot create database connection pool.");
	return 0;
    }
    sql_init_tables(plugin_cdr->pool, plugin_cdr->logtable, plugin_cdr->inserttable);
    plugin_cdr->global_sender = cfg_get(grp, octstr_imm("global-sender"));
    octstr_destroy(db_id);
    return 1;
}

void pluginbox_cdr_injected_callback(ack_status_t ack_status, void *context) {
	Octstr *id = (Octstr *)context;
	/* here we need to sql_delete the message */
	octstr_destroy(id);
}

/****************************************************************************
 * Character convertion.
 * 
 * The 'msgdata' is read from the DB table as URL-encoded byte stream, 
 * which we need to URL-decode to get the orginal message. We use this
 * approach to get rid of the table character dependancy of the DB systems.
 * The URL-encoded chars as a subset of ASCII which is typicall no problem
 * for any of the supported DB systems.
 */

static int charset_processing(Msg *msg) 
{
    gw_assert(msg->type == sms);

    /* URL-decode first */
    if (octstr_url_decode(msg->sms.msgdata) == -1)
        return -1;
    if (octstr_url_decode(msg->sms.udhdata) == -1)
        return -1;
        
    /* If a specific character encoding has been indicated by the
     * user, then make sure we convert to our internal representations. */
    if (msg->sms.charset && octstr_len(msg->sms.charset)) {
    
        if (msg->sms.coding == DC_7BIT) {
            /* For 7 bit, convert to UTF-8 */
            if (charset_convert(msg->sms.msgdata, octstr_get_cstr(msg->sms.charset), "UTF-8") < 0)
                return -1;
        } 
        else if (msg->sms.coding == DC_UCS2) {
            /* For UCS-2, convert to UTF-16BE */
            if (charset_convert(msg->sms.msgdata, octstr_get_cstr(msg->sms.charset), "UTF-16BE") < 0) 
                return -1;
        }
    }
    
    return 0;
}


void pluginbox_cdr_insert_thread(void *arg)
{
    PluginCdr *plugin_cdr = arg;
    PluginBoxMsg *pluginbox_msg;
    List *fetched;
    Msg *msg;
    int consumed;
    void *context = NULL;

    if (0 == plugin_cdr->limit_per_cycle) {
	return;
    }
    info(0, PLUGINBOX_LOG_PREFIX "Starting insert thread");
    /* allow for the rest of the plugin chain to start up before sending messages */
    gwthread_sleep(5.0);
    fetched = gwlist_create();
    gwlist_add_producer(fetched);
    while (plugin_cdr->running) {
	if ( sql_fetch_msg_list(plugin_cdr->pool, fetched, plugin_cdr->limit_per_cycle, plugin_cdr->inserttable) > 0 ) {
	    while((gwlist_len(fetched)>0) && ((msg = gwlist_consume(fetched)) != NULL )) {
                if (charset_processing(msg) == -1) {
                    error(0, "Could not charset process message, dropping it!");
                    msg_destroy(msg);
                    continue;
                }
                if (plugin_cdr->global_sender != NULL && (msg->sms.sender == NULL || octstr_len(msg->sms.sender) == 0)) {
                    msg->sms.sender = octstr_duplicate(plugin_cdr->global_sender);
                }
                if (plugin_cdr->id != NULL && (msg->sms.boxc_id == NULL || octstr_len(msg->sms.boxc_id) == 0)) {
                    msg->sms.boxc_id = octstr_duplicate(plugin_cdr->id);
                }
#if 0
                /* convert validity and deferred to unix timestamp */
                if (msg->sms.validity != SMS_PARAM_UNDEFINED)
                    msg->sms.validity = time(NULL) + msg->sms.validity * 60;
                if (msg->sms.deferred != SMS_PARAM_UNDEFINED)
                    msg->sms.deferred = time(NULL) + msg->sms.deferred * 60;
#endif
                pluginbox_inject_message(PLUGINBOX_MESSAGE_FROM_SMSBOX, plugin_cdr->id, msg_duplicate(msg), pluginbox_cdr_injected_callback, (void *)octstr_duplicate(msg->sms.foreign_id));
    
                if (plugin_cdr->save_mt) {
#if 0
                    /* convert validity & deferred back to minutes
                    * TODO clarify why we fetched message from DB and then insert it back here???
                    */
                    if (msg->sms.validity != SMS_PARAM_UNDEFINED)
                        msg->sms.validity = (msg->sms.validity - time(NULL))/60;
                    if (msg->sms.deferred != SMS_PARAM_UNDEFINED)
                        msg->sms.deferred = (msg->sms.deferred - time(NULL))/60;
#endif
	            sql_save_msg(plugin_cdr->pool, msg, octstr_imm("MT"), plugin_cdr->logtable);
		    msg_destroy(msg);
                }
	    }
        }
        else {
            gwthread_sleep(SLEEP_BETWEEN_EMPTY_SELECTS);
        }
    }
    info(0, PLUGINBOX_LOG_PREFIX "Stopping insert thread");
    gwlist_remove_producer(fetched);
    gwlist_destroy(fetched, msg_destroy_item);
}

void pluginbox_cdr_shutdown(PluginBoxPlugin *pluginbox_plugin) {
    PluginCdr *plugin_cdr = pluginbox_plugin->context;

    plugin_cdr->running = 0;
    gwthread_join(plugin_cdr->insert_thread);
    if (plugin_cdr->pool) {
	db_shutdown(plugin_cdr->pool);
    }
    pluginbox_cdr_plugin_destroy(plugin_cdr);
    info(0, PLUGINBOX_LOG_PREFIX "Shutdown complete");
}

void pluginbox_cdr_process(PluginBoxPlugin *pluginbox_plugin, PluginBoxMsg *pluginbox_msg) {
    PluginCdr *plugin_cdr;
    Msg *msg_escaped;

    if (msg_type(pluginbox_msg->msg) == sms) {
	debug("pluginbox.cdr.process", 0, PLUGINBOX_LOG_PREFIX "Got plugin message chain %ld", pluginbox_msg->chain);
	plugin_cdr = (PluginCdr *)pluginbox_plugin->context;
	msg_escaped = msg_duplicate(pluginbox_msg->msg);
        switch (pluginbox_msg->msg->sms.sms_type) {
	case report_mo:
	    if (plugin_cdr->save_dlr) {
	        sql_save_msg(plugin_cdr->pool, msg_escaped, octstr_imm("DLR"), plugin_cdr->logtable);
	    }
	    break;
	case mo:
	    if (plugin_cdr->save_mo) {
	        sql_save_msg(plugin_cdr->pool, msg_escaped, octstr_imm("MO"), plugin_cdr->logtable);
	    }
	    break;
	case mt_reply:
	case mt_push:
	    if (plugin_cdr->save_mt) {
	        sql_save_msg(plugin_cdr->pool, msg_escaped, octstr_imm("MT"), plugin_cdr->logtable);
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
	value = http_cgi_variable(cgivars, "limit-per-cycle");
	if (value) {
		plugin_cdr->limit_per_cycle = atoi(octstr_get_cstr(value));
	}
	value = http_cgi_variable(cgivars, "global-sender");
	if (value) {
		if (plugin_cdr->global_sender) octstr_destroy(plugin_cdr->global_sender);
		plugin_cdr->global_sender = octstr_duplicate(value);
	}
	value = http_cgi_variable(cgivars, "log-table");
	if (value) {
		if (plugin_cdr->logtable) octstr_destroy(plugin_cdr->logtable);
		plugin_cdr->logtable = octstr_duplicate(value);
	}
	value = http_cgi_variable(cgivars, "insert-table");
	if (value) {
		if (plugin_cdr->inserttable) octstr_destroy(plugin_cdr->inserttable);
		plugin_cdr->inserttable = octstr_duplicate(value);
	}
	switch (status_type) {
	case PLUGINSTATUS_HTML:
		fmt = "<table class=\"cdrtable\"><tr><td>Save MO</td><td>%d</td></tr><tr><td>Save MT</td><td>%d</td></tr><tr><td>Save DLR</td><td>%d</td></tr><tr><td>Global Sender</td><td>%s</td></tr><tr><td>Limit per cycle</td><td>%ld</td></tr><tr><td>Logtable</td><td>%s</td></tr><tr><td>Insert table</td><td>%s</td></tr></table>";
		break;
	case PLUGINSTATUS_WML:
		fmt = "Configuration:<br/>\n<br/>\nSave MO: %d.<br/>\nSave MT: %d.<br/>\nSave DLR: %d.<br/>Global Sender: %s<br/>Limit per cycle: %ld<br/>Logtable: %s<br/>Insert table: %s<br/>\n";
		break;
	case PLUGINSTATUS_XML:
		fmt = "<plugin-cdr>\n    <save-mo>%d</save-mo>\n    <save-mt>%d</save-mt>\n    <save-dlr>%d</save-dlr>\n</plugin-cdr>\n<global-sender>%s</global-sender>\n<limit-per-cycle>%ld</limit-per-cycle>\n<log-table>%s</log-table>\n<insert-table>%s</insert-table>\n";
		break;
	case PLUGINSTATUS_TEXT:
	default:
		fmt = "Configuration:\n\nSave MO: %d.\nSave MT: %d.\nSave DLR: %d.\nGlobal Sender: %s\nLimit per cycle: %ld\nLogtable: %s\nInsert table: %s\n";
		break;
	}
	return octstr_format(fmt, plugin_cdr->save_mo, plugin_cdr->save_mt, plugin_cdr->save_dlr, plugin_cdr->global_sender ? octstr_get_cstr(plugin_cdr->global_sender) : "(not configured)", plugin_cdr->limit_per_cycle, plugin_cdr->logtable ? octstr_get_cstr(plugin_cdr->logtable) : "(not configured)", plugin_cdr->inserttable ? octstr_get_cstr(plugin_cdr->inserttable) : "(not configured)");
}

int pluginbox_cdr_init(PluginBoxPlugin *pluginbox_plugin) {
	int success;

	info(0, PLUGINBOX_LOG_PREFIX "Using configuration from %s", octstr_get_cstr(pluginbox_plugin->args));
	cfg_add_hooks(sqlbox_is_allowed_in_group, sqlbox_is_single_group);

	Cfg *cfg = cfg_create(pluginbox_plugin->args);
	if(cfg_read(cfg) == -1) {
		error(0, PLUGINBOX_LOG_PREFIX "Couldn't load CDR plugin %s configuration", octstr_get_cstr(pluginbox_plugin->id));
		cfg_destroy(cfg);
		return 0;
	}
	pluginbox_plugin->context = pluginbox_cdr_plugin_create();
	success = pluginbox_cdr_configure(pluginbox_plugin->context, cfg);
	cfg_destroy(cfg);
        if (!success) {
		pluginbox_cdr_plugin_destroy((PluginCdr *)pluginbox_plugin->context);
		return 0;
	}
	pluginbox_plugin->direction = PLUGINBOX_MESSAGE_FROM_SMSBOX | PLUGINBOX_MESSAGE_FROM_BEARERBOX;
	pluginbox_plugin->process = pluginbox_cdr_process;
	pluginbox_plugin->shutdown = pluginbox_cdr_shutdown;
	pluginbox_plugin->status = pluginbox_cdr_status;
	((PluginCdr *)pluginbox_plugin->context)->running = 1;
	((PluginCdr *)pluginbox_plugin->context)->insert_thread = gwthread_create(pluginbox_cdr_insert_thread, pluginbox_plugin->context);
	return 1;
}
