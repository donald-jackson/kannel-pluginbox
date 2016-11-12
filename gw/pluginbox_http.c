/* ==================================================================== 
 * The Kannel Software License, Version 1.0 
 * 
 * Copyright (c) 2001-2014 Kannel Group  
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
 */ 

/*
 * plugin_http.c : pluginbox http adminstration commands
 *
 * NOTE: this is a special pluginbox module - it does call
 *   functions from core module! (other modules are fully
 *    encapsulated, and only called outside)
 *
 * Rene Kluwen <rene.kluwen@chimit.nl> 2016
 */

#include <errno.h>
#include <signal.h>
#include <unistd.h>

#include "gwlib/gwlib.h"
#include "pluginbox.h"

extern volatile sig_atomic_t plugin_status;

/* our own thingies */

static volatile sig_atomic_t httpadmin_running;

static long	ha_port;
static Octstr *ha_interface;
static Octstr *ha_password;
static Octstr *ha_allow_ip;
static Octstr *ha_deny_ip;


/*---------------------------------------------------------
 * static functions
 */

/*
 * check if the password matches. Return NULL if
 * it does (or is not required)
 */
static Octstr *httpd_check_authorization(List *cgivars, int status)
{
    Octstr *password;
    static double sleep = 0.01;

    password = http_cgi_variable(cgivars, "password");

    if (password == NULL || octstr_compare(password, ha_password)!=0) {
	    goto denied;
    }

    sleep = 0.0;
    return NULL;	/* allowed */
denied:
    gwthread_sleep(sleep);
    sleep += 1.0;		/* little protection against brute force
				 * password cracking */
    return octstr_create("Denied");
}

/*
 * check if we still have time to do things
 */
static Octstr *httpd_check_status(void)
{
    if (plugin_status == PLUGIN_SHUTDOWN || plugin_status == PLUGIN_DEAD)
	return octstr_create("Avalanche has already started, too late to "
	    	    	     "save the sheeps");
    return NULL;
}
    
static Octstr *httpd_status(List *cgivars, int status_type)
{
    Octstr *reply;
    if ((reply = httpd_check_authorization(cgivars, 1))!= NULL) return reply;
    return plugin_print_status(status_type);
}

static Octstr *httpd_loglevel(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *level;
    int new_loglevel;
    
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;
 
    /* check if new loglevel is given */
    level = http_cgi_variable(cgivars, "level");
    if (level) {
        new_loglevel = atoi(octstr_get_cstr(level));
        log_set_log_level(new_loglevel);
        return octstr_format("log-level set to %d", new_loglevel);
    }
    else {
        return octstr_create("New level not given");
    }
}

static Octstr *httpd_remove_plugin(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *plugin;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the plugin id is given */
    plugin = http_cgi_variable(cgivars, "plugin");
    if (plugin) {
        if (plugin_remove_plugin(plugin) == -1)
            return octstr_format("Could not remove plugin-id `%s'", octstr_get_cstr(plugin));
        else
            return octstr_format("PLUGIN `%s' removed", octstr_get_cstr(plugin));
    } else
        return octstr_create("PLUGIN id not given");
}


static Octstr *httpd_status_plugin(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *plugin;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the plugin id is given */
    plugin = http_cgi_variable(cgivars, "plugin");
    if (plugin) {
        return plugin_status_plugin(plugin, cgivars, status_type);
    } else {
        return octstr_create("PLUGIN id not given");
    }
}

static Octstr *httpd_add_plugin(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *plugin;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the plugin id is given */
    plugin = http_cgi_variable(cgivars, "plugin");
    if (plugin) {
        if (plugin_add_plugin(plugin) == 0)
            return octstr_format("Could not add plugin-id `%s'", octstr_get_cstr(plugin));
        else
            return octstr_format("PLUGIN `%s' added", octstr_get_cstr(plugin));
    } else
        return octstr_create("PLUGIN id not given");
}

static Octstr *httpd_restart_plugin(List *cgivars, int status_type)
{
    Octstr *reply;
    Octstr *plugin;
    if ((reply = httpd_check_authorization(cgivars, 0))!= NULL) return reply;
    if ((reply = httpd_check_status())!= NULL) return reply;

    /* check if the plugin id is given */
    plugin = http_cgi_variable(cgivars, "plugin");
    if (plugin) {
        if (plugin_restart_plugin(plugin) == -1)
            return octstr_format("Could not re-start plugin-id `%s'", octstr_get_cstr(plugin));
        else
            return octstr_format("PLUGIN `%s' re-started", octstr_get_cstr(plugin));
    } else
        return octstr_create("PLUGIN id not given");
}

/* Known httpd commands and their functions */
static struct httpd_command {
    const char *command;
    Octstr * (*function)(List *cgivars, int status_type);
} httpd_commands[] = {
    { "status", httpd_status },
    { "log-level", httpd_loglevel },
    { "restart-plugin", httpd_restart_plugin },
    { "add-plugin", httpd_add_plugin },
    { "remove-plugin", httpd_remove_plugin },
    { "status-plugin", httpd_status_plugin },
    { NULL , NULL } /* terminate list */
};

static void httpd_serve(HTTPClient *client, Octstr *ourl, List *headers,
    	    	    	Octstr *body, List *cgivars)
{
    Octstr *reply, *final_reply, *url;
    char *content_type;
    char *header, *footer;
    int status_type;
    int i;
    long pos;

    reply = final_reply = NULL; /* for compiler please */
    url = octstr_duplicate(ourl);

    /* Set default reply format according to client
     * Accept: header */
    if (http_type_accepted(headers, "text/vnd.wap.wml")) {
	status_type = PLUGINSTATUS_WML;
	content_type = "text/vnd.wap.wml";
    }
    else if (http_type_accepted(headers, "text/html")) {
	status_type = PLUGINSTATUS_HTML;
	content_type = "text/html";
    }
    else if (http_type_accepted(headers, "text/xml")) {
	status_type = PLUGINSTATUS_XML;
	content_type = "text/xml";
    } else {
	status_type = PLUGINSTATUS_TEXT;
	content_type = "text/plain";
    }

    /* kill '/cgi-bin' prefix */
    pos = octstr_search(url, octstr_imm("/cgi-bin/"), 0);
    if (pos != -1)
        octstr_delete(url, pos, 9);
    else if (octstr_get_char(url, 0) == '/')
        octstr_delete(url, 0, 1);

    /* look for type and kill it */
    pos = octstr_search_char(url, '.', 0);
    if (pos != -1) {
        Octstr *tmp = octstr_copy(url, pos+1, octstr_len(url) - pos - 1);
        octstr_delete(url, pos, octstr_len(url) - pos);

        if (octstr_str_compare(tmp, "txt") == 0)
            status_type = PLUGINSTATUS_TEXT;
        else if (octstr_str_compare(tmp, "html") == 0)
            status_type = PLUGINSTATUS_HTML;
        else if (octstr_str_compare(tmp, "xml") == 0)
            status_type = PLUGINSTATUS_XML;
        else if (octstr_str_compare(tmp, "wml") == 0)
            status_type = PLUGINSTATUS_WML;

        octstr_destroy(tmp);
    }

    for (i=0; httpd_commands[i].command != NULL; i++) {
        if (octstr_str_compare(url, httpd_commands[i].command) == 0) {
            reply = httpd_commands[i].function(cgivars, status_type);
            break;
        }
    }

    /* check if command found */
    if (httpd_commands[i].command == NULL) {
        char *lb = plugin_status_linebreak(status_type);
	reply = octstr_format("Unknown command `%S'.%sPossible commands are:%s",
            ourl, lb, lb);
        for (i=0; httpd_commands[i].command != NULL; i++)
            octstr_format_append(reply, "%s%s", httpd_commands[i].command, lb);
    }

    gw_assert(reply != NULL);

    if (status_type == PLUGINSTATUS_HTML) {
	header = "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2//EN\">\n"
 	    "<html>\n<title>" GW_NAME "</title>\n<body>\n<p>";
	footer = "</p>\n</body></html>\n";
	content_type = "text/html";
    } else if (status_type == PLUGINSTATUS_WML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<!DOCTYPE wml PUBLIC \"-//WAPFORUM//DTD WML 1.1//EN\" "
            "\"http://www.wapforum.org/DTD/wml_1.1.xml\">\n"
            "\n<wml>\n <card>\n  <p>";
	footer = "  </p>\n </card>\n</wml>\n";
	content_type = "text/vnd.wap.wml";
    } else if (status_type == PLUGINSTATUS_XML) {
	header = "<?xml version=\"1.0\"?>\n"
            "<gateway>\n";
        footer = "</gateway>\n";
    } else {
	header = "";
	footer = "";
	content_type = "text/plain";
    }
    final_reply = octstr_create(header);
    octstr_append(final_reply, reply);
    octstr_append_cstr(final_reply, footer);
    
    /* debug("bb.http", 0, "Result: '%s'", octstr_get_cstr(final_reply));
     */
    http_destroy_headers(headers);
    headers = gwlist_create();
    http_header_add(headers, "Content-Type", content_type);

    http_send_reply(client, HTTP_OK, headers, final_reply);

    octstr_destroy(url);
    octstr_destroy(ourl);
    octstr_destroy(body);
    octstr_destroy(reply);
    octstr_destroy(final_reply);
    http_destroy_headers(headers);
    http_destroy_cgiargs(cgivars);
}

static void httpadmin_run(void *arg)
{
    HTTPClient *client;
    Octstr *ip, *url, *body;
    List *headers, *cgivars;

    while(plugin_status != PLUGIN_DEAD) {
    	client = http_accept_request(ha_port, &ip, &url, &headers, &body, 
	    	    	    	     &cgivars);
	if (client == NULL)
	    break;
	if (is_allowed_ip(ha_allow_ip, ha_deny_ip, ip) == 0) {
	    info(0, "HTTP admin tried from denied host <%s>, disconnected",
		 octstr_get_cstr(ip));
	    http_close_client(client);
	    continue;
	}
        httpd_serve(client, url, headers, body, cgivars);
	octstr_destroy(ip);
    }

    httpadmin_running = 0;
}


/*-------------------------------------------------------------
 * public functions
 *
 */

int httpadmin_start(Cfg *cfg)
{
    CfgGroup *grp;
    int ssl = 0; 
#ifdef HAVE_LIBSSL
    Octstr *ssl_server_cert_file;
    Octstr *ssl_server_key_file;
#endif /* HAVE_LIBSSL */
    
    if (httpadmin_running) return -1;


    grp = cfg_get_single_group(cfg, octstr_imm("pluginbox"));
    if (cfg_get_integer(&ha_port, grp, octstr_imm("admin-port")) == -1)
	panic(0, "Missing admin-port variable, cannot start HTTP admin");

    ha_interface = cfg_get(grp, octstr_imm("admin-interface"));
    ha_password = cfg_get(grp, octstr_imm("admin-password"));
    if (ha_password == NULL)
	panic(0, "You MUST set HTTP admin-password");
    
    ha_allow_ip = cfg_get(grp, octstr_imm("admin-allow-ip"));
    ha_deny_ip = cfg_get(grp, octstr_imm("admin-deny-ip"));

#ifdef HAVE_LIBSSL
    cfg_get_bool(&ssl, grp, octstr_imm("admin-port-ssl"));
    
    /*
     * check if SSL is desired for HTTP servers and then
     * load SSL client and SSL server public certificates 
     * and private keys
     */    
    ssl_server_cert_file = cfg_get(grp, octstr_imm("ssl-server-cert-file"));
    ssl_server_key_file = cfg_get(grp, octstr_imm("ssl-server-key-file"));
    if (ssl_server_cert_file != NULL && ssl_server_key_file != NULL) {
        /* we are fine here, the following call is now in conn_config_ssl(),
         * so there is no reason to do this twice.

        use_global_server_certkey_file(ssl_server_cert_file, 
            ssl_server_key_file);
        */
    } else if (ssl) {
	   panic(0, "You MUST specify cert and key files within core group for SSL-enabled HTTP servers!");
    }

    octstr_destroy(ssl_server_cert_file);
    octstr_destroy(ssl_server_key_file);
#endif /* HAVE_LIBSSL */

    http_open_port_if(ha_port, ssl, ha_interface);

    if (gwthread_create(httpadmin_run, NULL) == -1)
	panic(0, "Failed to start a new thread for HTTP admin");

    httpadmin_running = 1;
    return 0;
}


void httpadmin_stop(void)
{
    http_close_all_ports();
    gwthread_join_every(httpadmin_run);
    octstr_destroy(ha_interface);    
    octstr_destroy(ha_password);
    octstr_destroy(ha_allow_ip);
    octstr_destroy(ha_deny_ip);
    ha_password = NULL;
    ha_allow_ip = NULL;
    ha_deny_ip = NULL;
}
