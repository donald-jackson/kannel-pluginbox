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
 * pluginbox.h
 *
 * General typedefs and functions for pluginbox.
 *
 * Rene Kluwen <rene.kluwen@chimit.nl>
 */

#include "gwlib/gwlib.h"

/* passed from pluginbox core */

int httpadmin_start(Cfg *cfg);
void httpadmin_stop(void);

enum {
    PLUGIN_RUNNING = 0,
    PLUGIN_ISOLATED = 1,	/* do not receive new messgaes from UDP/SMSC */
    PLUGIN_SUSPENDED = 2,	/* do not transfer any messages */
    PLUGIN_SHUTDOWN = 3,
    PLUGIN_DEAD = 4,
    PLUGIN_FULL = 5         /* message queue too long, do not accept new messages */
};

/* type of output given by various status functions */
enum {
    PLUGINSTATUS_HTML = 0,
    PLUGINSTATUS_TEXT = 1,
    PLUGINSTATUS_WML = 2,
    PLUGINSTATUS_XML = 3
};

Octstr *plugin_print_status(int status_type);
int plugin_stop_plugin(Octstr *plugin);
int plugin_start_plugin(Octstr *plugin);
int plugin_remove_plugin(Octstr *plugin);
int plugin_add_plugin(Octstr *plugin);
int plugin_restart_plugin(Octstr *plugin);
Octstr *plugin_status_plugin(Octstr *plugin, List *cgivars, int status_type);

/*----------------------------------------------------------------
 * common function to all (in pluginbox.c)
 */

/* return linebreak for given output format, or NULL if format
 * not supported */

char *plugin_status_linebreak(int status_type);
