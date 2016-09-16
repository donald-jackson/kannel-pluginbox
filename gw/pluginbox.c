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

#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include "gwlib/gwlib.h"
#include "gw/msg.h"
#include "gw/sms.h"
#include "gw/shared.h"
#include "gw/bb.h"

#include "pluginbox_plugin.h"

/* our config */
static Cfg *cfg;
/* have we received restart cmd from bearerbox? */

static volatile sig_atomic_t restart_pluginbox = 0;
static volatile sig_atomic_t pluginbox_status;
#define PLUGIN_DEAD 0
#define PLUGIN_SHUTDOWN 1
#define PLUGIN_RUNNING 2
static long pluginbox_port;
static int pluginbox_port_ssl = 0;
static long bearerbox_port;
static Octstr *bearerbox_host;
static int bearerbox_port_ssl = 0;

Octstr *pluginbox_id;

#define SLEEP_BETWEEN_EMPTY_SELECTS 1.0
#define DEFAULT_LIMIT_PER_CYCLE 10

typedef struct _boxc {
    Connection *smsbox_connection;
    Connection *bearerbox_connection;
    time_t connect_time;
    Octstr *client_ip;
    volatile sig_atomic_t alive;
    Octstr *boxc_id; /* identifies the connected smsbox instance */
    List *smsbox_inbound_queue; /* The queue that contains messages from smsbox -> pluginbox */
    List *smsbox_outbound_queue; /* The queue that contains messages from pluginbox -> smsbox */
    List *bearerbox_inbound_queue; /* The queue that contains messages from bearerbox -> pluginbox */
    List *bearerbox_outbound_queue; /* The queue that contains messages from pluginbox -> bearerbox */
    Counter *pending_counter;
} Boxc;

/*
 * Adding hooks to kannel check config
 *
 * Martin Conte.
 */

static int pluginbox_is_allowed_in_group(Octstr *group, Octstr *variable) {
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
#include "pluginbox-cfg.def"

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
#include "pluginbox-cfg.def"
    return 0;
}

static Boxc *boxc_create(int fd, Octstr *ip, int ssl) {
    Boxc *boxc;

    boxc = gw_malloc(sizeof (Boxc));
    boxc->smsbox_connection = conn_wrap_fd(fd, ssl);
    boxc->bearerbox_connection = NULL;
    boxc->client_ip = ip;
    boxc->alive = 1;
    boxc->connect_time = time(NULL);
    boxc->boxc_id = NULL;
    boxc->smsbox_inbound_queue = gwlist_create();
    boxc->smsbox_outbound_queue = gwlist_create();
    boxc->bearerbox_inbound_queue = gwlist_create();
    boxc->bearerbox_outbound_queue = gwlist_create();
    boxc->pending_counter = counter_create();
    return boxc;
}

static void boxc_destroy(Boxc *boxc) {
    if (boxc == NULL)
        return;

    /* do nothing to the lists, as they are only references */

    if (boxc->smsbox_connection)
        conn_destroy(boxc->smsbox_connection);
    if (boxc->bearerbox_connection)
        conn_destroy(boxc->bearerbox_connection);
    octstr_destroy(boxc->client_ip);
    octstr_destroy(boxc->boxc_id);

    gwlist_destroy(boxc->smsbox_inbound_queue, (void(*)(void *))msg_destroy);
    gwlist_destroy(boxc->smsbox_outbound_queue, (void(*)(void *))msg_destroy);
    gwlist_destroy(boxc->bearerbox_inbound_queue, (void(*)(void *))msg_destroy);
    gwlist_destroy(boxc->bearerbox_outbound_queue, (void(*)(void *))msg_destroy);
    counter_destroy(boxc->pending_counter);
    gw_free(boxc);
}

static Boxc *accept_boxc(int fd, int ssl) {
    Boxc *newconn;
    Octstr *ip;

    int newfd;
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;

    client_addr_len = sizeof (client_addr);

    newfd = accept(fd, (struct sockaddr *) &client_addr, &client_addr_len);
    if (newfd < 0)
        return NULL;

    ip = host_ip(client_addr);

    // if (is_allowed_ip(box_allow_ip, box_deny_ip, ip) == 0) {
    // info(0, "Box connection tried from denied host <%s>, disconnected",
    // octstr_get_cstr(ip));
    // octstr_destroy(ip);
    // close(newfd);
    // return NULL;
    // }
    newconn = boxc_create(newfd, ip, ssl);

    /*
     * check if the SSL handshake was successfull, otherwise
     * this is no valid box connection any more
     */
#ifdef HAVE_LIBSSL
    if (ssl && !conn_get_ssl(newconn->smsbox_connection))
        return NULL;
#endif

    if (ssl)
        info(0, "Client connected from <%s> using SSL", octstr_get_cstr(ip));
    else
        info(0, "Client connected from <%s>", octstr_get_cstr(ip));


    /* XXX TODO: do the hand-shake, baby, yeah-yeah! */

    return newconn;
}

static Msg *read_from_box(Connection *conn, Boxc *boxconn) {
    Msg *msg;

    while ((boxconn->alive) && (pluginbox_status == PLUGIN_RUNNING)) {
        switch (read_from_bearerbox_real(conn, &msg, 1.0)) {
            case -1:
                /* connection to bearerbox lost */
                return NULL;
                break;
            case 0:
                /* all is well */
                return msg;
                break;
            case 1:
                /* timeout */
                break;
        }
    }

    return NULL;
}

/* Plugin callback functions
 * 
 * 
 * 
 * 
 * 
 * */

static void bearerbox_inbound_queue_plugins_done(void *context, Msg *msg, int status) {

    debug("bearerbox.inbound.queue.plugins.done", 0, "Got bearerbox msg callback from plugin chain");
    Boxc *conn = context;
    counter_decrease(conn->pending_counter);

    if (conn->alive) {
        gwlist_produce(conn->smsbox_outbound_queue, msg);
    } else {
        /* Connection is shutting down just destroy */
        msg_destroy(msg);
    }
}

static void smsbox_inbound_queue_plugins_done(void *context, Msg *msg, int status) {

    debug("smsbox.inbound.queue.plugins.done", 0, "Got smsbox msg callback from plugin chain");
    Boxc *conn = context;
    counter_decrease(conn->pending_counter);
    Msg *ackmsg;

    if (conn->alive) {
        if (status == PLUGINBOX_MESSAGE_REJECT) {
            warning(0, "Plugin chain has asked us to reject this message");
            ackmsg = msg_create(ack);
            uuid_copy(ackmsg->ack.id, msg->sms.id);
            ackmsg->ack.time = msg->sms.time;
            ackmsg->ack.nack = ack_failed;
            msg_destroy(msg);
            gwlist_produce(conn->smsbox_outbound_queue, ackmsg);
        } else {
            gwlist_produce(conn->bearerbox_outbound_queue, msg);
        }
    } else {
        /* Connection is shutting down just destroy */
        msg_destroy(msg);
    }
}


/*
 *-------------------------------------------------
 *  sender thingies
 *-------------------------------------------------
 *
 */

/* send to either smsbox or bearerbox */

static int send_msg(Connection *conn, Boxc *boxconn, Msg *pmsg) {
    Octstr *pack;

    pack = msg_pack(pmsg);

    if (pack == NULL)
        return -1;

    if (conn_write_withlen(conn, pack) == -1) {
        error(0, "Couldn't write Msg to box <%s>, disconnecting",
                octstr_get_cstr(boxconn->client_ip));
        octstr_destroy(pack);
        return -1;
    }
    octstr_destroy(pack);
    return 0;
}

static void smsbox_outbound_queue_thread(void *arg) {
    Boxc *conn = arg;

    Msg *msg;

    while ((msg = gwlist_consume(conn->smsbox_outbound_queue)) != NULL) {
        debug("smsbox.outbound.queue.thread", 0, "Pluginbox -> smsbox forwarding message to smsbox %s", octstr_get_cstr(conn->boxc_id));
        send_msg(conn->smsbox_connection, conn, msg);
        msg_destroy(msg);
    }

}

static void bearerbox_outbound_queue_thread(void *arg) {
    Boxc *conn = arg;

    Msg *msg;

    while ((msg = gwlist_consume(conn->bearerbox_outbound_queue)) != NULL) {
        debug("bearerbox.outbound.queue.thread", 0, "Pluginbox -> bearerbox forwarding to bearerbox");
        send_msg(conn->bearerbox_connection, conn, msg);
        msg_destroy(msg);
    }
}

static void smsbox_inbound_queue_thread(void *arg) {
    Boxc *conn = arg;

    Msg *msg;

    gwlist_add_producer(conn->bearerbox_outbound_queue);

    long outbound_thread = gwthread_create(bearerbox_outbound_queue_thread, conn);

    while ((msg = gwlist_consume(conn->smsbox_inbound_queue)) != NULL) {
        debug("smsbox.inbound.queue.thread", 0, "Smsbox -> Pluginbox got message from %s to process", octstr_get_cstr(conn->boxc_id));
        counter_increase(conn->pending_counter);
        pluginbox_plugins_start(smsbox_inbound_queue_plugins_done, conn, msg, PLUGINBOX_MESSAGE_FROM_SMSBOX);
        //gwlist_produce(conn->bearerbox_outbound_queue, msg);
    }

    gwlist_remove_producer(conn->bearerbox_outbound_queue);

    gwthread_join(outbound_thread);
}

static void bearerbox_inbound_queue_thread(void *arg) {
    Boxc *conn = arg;

    Msg *msg;

    gwlist_add_producer(conn->smsbox_outbound_queue);

    long outbound_thread = gwthread_create(smsbox_outbound_queue_thread, conn);

    while ((msg = gwlist_consume(conn->bearerbox_inbound_queue)) != NULL) {
        debug("bearerbox.inbound.queue.thread", 0, "Bearerbox -> Pluginbox got message from bearerbox %s", octstr_get_cstr(conn->boxc_id));
        counter_increase(conn->pending_counter);
        pluginbox_plugins_start(bearerbox_inbound_queue_plugins_done, conn, msg, PLUGINBOX_MESSAGE_FROM_BEARERBOX);
    }

    gwlist_remove_producer(conn->smsbox_outbound_queue);

    gwthread_join(outbound_thread);
}

static void bearerbox_to_smsbox(void *arg) {
    Msg *msg, *msg_dupe;
    Boxc *conn = arg;

    gwlist_add_producer(conn->bearerbox_inbound_queue);

    long inbound_thread = gwthread_create(bearerbox_inbound_queue_thread, conn);

    while (pluginbox_status == PLUGIN_RUNNING && conn->alive) {

        msg = read_from_box(conn->bearerbox_connection, conn);

        if (msg == NULL) {
            /* tell pluginbox to die */
            conn->alive = 0;
            debug("pluginbox", 0, "bearerbox_to_smsbox: connection to bearerbox died.");
            break;
        }
        if (msg_type(msg) == admin) {
            if (msg->admin.command == cmd_shutdown || msg->admin.command == cmd_restart) {
                /* tell pluginbox to die */
                conn->alive = 0;
                debug("pluginbox", 0, "bearerbox_to_smsbox: Bearerbox told us to shutdown.");
                break;
            }
        }

        if (msg_type(msg) == heartbeat) {
            // todo
            debug("pluginbox", 0, "bearerbox_to_smsbox: catch an heartbeat - we are alive");
            msg_destroy(msg);
            continue;
        }
        if (!conn->alive) {
            msg_destroy(msg);
            break;
        }

        msg_dupe = msg_duplicate(msg);

        if (msg_type(msg) == sms) {

        }

        gwlist_produce(conn->bearerbox_inbound_queue, msg_dupe);
        //        send_msg(conn->smsbox_connection, conn, msg);
        msg_destroy(msg);
    }
    /* the client closes the connection, after that die in receiver */
    conn->alive = 0;

    gwlist_remove_producer(conn->bearerbox_inbound_queue);

    gwthread_join(inbound_thread);

    /* Join inbound queue processor thread */
}

static void smsbox_to_bearerbox(void *arg) {
    Boxc *conn = arg;
    Msg *msg, *msg_dupe;

    gwlist_add_producer(conn->smsbox_inbound_queue);

    long inbound_thread = gwthread_create(smsbox_inbound_queue_thread, conn);

    /* remove messages from socket until it is closed */
    while (pluginbox_status == PLUGIN_RUNNING && conn->alive) {

        msg = read_from_box(conn->smsbox_connection, conn);

        if (msg == NULL) { /* garbage/connection lost */
            conn->alive = 0;
            break;
        }

        if (msg_type(msg) == sms) {
            debug("pluginbox", 0, "smsbox_to_bearerbox: sms received");
        }

        msg_dupe = msg_duplicate(msg);

        /* if this is an identification message from an smsbox instance */
        if (msg_type(msg) == admin && msg->admin.command == cmd_identify) {
            /*
             * any smsbox sends this command even if boxc_id is NULL,
             * but we will only consider real identified boxes
             */
            if (msg->admin.boxc_id != NULL) {

                /* and add the boxc_id into conn for boxc_status() output */
                conn->boxc_id = msg->admin.boxc_id;
                msg->admin.boxc_id = NULL;

                debug("pluginbox", 0, "smsbox_to_bearerbox: got boxc_id <%s> from <%s>",
                        octstr_get_cstr(conn->boxc_id),
                        octstr_get_cstr(conn->client_ip));
            }
        }

        gwlist_produce(conn->smsbox_inbound_queue, msg_dupe);

        msg_destroy(msg);
    }
    conn->alive = 0;

    gwlist_remove_producer(conn->smsbox_inbound_queue);

    gwthread_join(inbound_thread);
}

static void run_pluginbox(void *arg) {
    int fd;
    Boxc *newconn;
    long sender;
    
    int *fdarg = arg;

    fd = *fdarg;
    
    
    newconn = accept_boxc(fd, pluginbox_port_ssl);
    if (newconn == NULL) {
        panic(0, "Socket accept failed");
        return;
    }
    newconn->bearerbox_connection = connect_to_bearerbox_real(bearerbox_host, bearerbox_port, bearerbox_port_ssl, NULL /* bb_our_host */);
    /* XXX add our_host if required */


    sender = gwthread_create(bearerbox_to_smsbox, newconn);
    if (sender == -1) {
        error(0, "Failed to start a new thread, disconnecting client <%s>",
                octstr_get_cstr(newconn->client_ip));
        //goto cleanup;
    }
    smsbox_to_bearerbox(newconn);

    gwthread_wakeup(sender);
    gwthread_join(sender);

    debug("run.pluginbox", 0, "%s waiting for pending messages to be flushed by plugin chain %ld remaining", octstr_get_cstr(newconn->boxc_id), counter_value(newconn->pending_counter));
    while (counter_value(newconn->pending_counter) > 0) {
        gwthread_sleep(1);
    }

    boxc_destroy(newconn);
}

static void wait_for_connections(int fd, void (*function) (void *arg),
        List *waited) {
    int ret;
    int timeout = 10; /* 10 sec. */

    gw_assert(function != NULL);
    
    int *fdptr = gw_malloc(sizeof(int));
    *fdptr = fd;

    while (pluginbox_status == PLUGIN_RUNNING) {

        ret = gwthread_pollfd(fd, POLLIN, 1.0);
        if (pluginbox_status == PLUGIN_SHUTDOWN) {
            if (ret == -1 || !timeout)
                break;
            else
                timeout--;
        }

        if (ret > 0) {
            gwthread_create(function, fdptr);
            gwthread_sleep(1.0);
        } else if (ret < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) continue;
            error(errno, "wait_for_connections failed");
        }
    }
}

static void pluginboxc_run(void *arg) {
    int fd;
    long port;

    long *portarg = arg;
    port = *portarg;

    fd = make_server_socket(port, NULL);
    /* XXX add interface_name if required */

    if (fd < 0) {
        panic(0, "Could not open pluginbox port %ld", port);
    }
    
    info(0, "Waiting for connections on %ld", port);

    /*
     * infinitely wait for new connections;
     * to shut down the system, SIGTERM is send and then
     * select drops with error, so we can check the status
     */

    wait_for_connections(fd, run_pluginbox, NULL);

    /* close listen socket */
    close(fd);

    gwthread_join_every(run_pluginbox);
    
}

/***********************************************************************
 * Main program. Configuration, signal handling, etc.
 */

static void signal_handler(int signum) {
    /* On some implementations (i.e. linuxthreads), signals are delivered
     * to all threads.  We only want to handle each signal once for the
     * entire box, and we let the gwthread wrapper take care of choosing
     * one.
     */
    if (!gwthread_shouldhandlesignal(signum))
        return;

    switch (signum) {
        case SIGINT:
            if (pluginbox_status == PLUGIN_RUNNING) {
                error(0, "SIGINT received, aborting program...");
                pluginbox_status = PLUGIN_SHUTDOWN;
                gwthread_wakeup_all();
            }
            break;

        case SIGHUP:
            warning(0, "SIGHUP received, catching and re-opening logs");
            log_reopen();
            alog_reopen();
            break;
            /*
             * It would be more proper to use SIGUSR1 for this, but on some
             * platforms that's reserved by the pthread support.
             */
        case SIGQUIT:
            warning(0, "SIGQUIT received, reporting memory usage.");
            gw_check_leaks();
            break;
    }
}

static void setup_signal_handlers(void) {
    struct sigaction act;

    act.sa_handler = signal_handler;
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGQUIT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGPIPE, &act, NULL);
}

static void init_pluginbox(Cfg *cfg) {
    CfgGroup *grp;
    Octstr *logfile;
    long lvl;

    /* some default values */
    pluginbox_port_ssl = 0;
    bearerbox_port = BB_DEFAULT_SMSBOX_PORT;
    bearerbox_port_ssl = 0;
    logfile = NULL;
    lvl = 0;

    /*
     * first we take the port number in bearerbox and other values from the
     * core group in configuration file
     */

    grp = cfg_get_single_group(cfg, octstr_imm("pluginbox"));
    if (cfg_get_integer(&bearerbox_port, grp, octstr_imm("bearerbox-port")) == -1)
        panic(0, "Missing or bad 'bearerbox-port' in pluginbox group");
#ifdef HAVE_LIBSSL
    cfg_get_bool(&bearerbox_port_ssl, grp, octstr_imm("smsbox-port-ssl"));
    conn_config_ssl(grp);
#endif

    grp = cfg_get_single_group(cfg, octstr_imm("pluginbox"));
    if (grp == NULL)
        panic(0, "No 'pluginbox' group in configuration");

    bearerbox_host = cfg_get(grp, octstr_imm("bearerbox-host"));
    if (bearerbox_host == NULL)
        bearerbox_host = octstr_create(BB_DEFAULT_HOST);

    if (cfg_get_integer(&pluginbox_port, grp, octstr_imm("smsbox-port")) == -1)
        pluginbox_port = 13005;

    /* setup logfile stuff */
    logfile = cfg_get(grp, octstr_imm("log-file"));

    cfg_get_integer(&lvl, grp, octstr_imm("log-level"));

    if (logfile != NULL) {
        info(0, "Starting to log to file %s level %ld",
                octstr_get_cstr(logfile), lvl);
        log_open(octstr_get_cstr(logfile), lvl, GW_NON_EXCL);
        octstr_destroy(logfile);
    }

    pluginbox_plugins_init(cfg);

    pluginbox_status = PLUGIN_RUNNING;
}

static int check_args(int i, int argc, char **argv) {
    if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--tryhttp") == 0) {

    } else {
        return -1;
    }

    return 0;
}

int main(int argc, char **argv) {
    int cf_index;
    Octstr *filename;

    gwlib_init();

    cf_index = get_and_set_debugs(argc, argv, check_args);
    setup_signal_handlers();

    if (argv[cf_index] == NULL) {
        filename = octstr_create("pluginbox.conf");
    } else {
        filename = octstr_create(argv[cf_index]);
    }

    cfg = cfg_create(filename);

    /* Adding cfg-checks to core */

    cfg_add_hooks(pluginbox_is_allowed_in_group, pluginbox_is_single_group);

    if (cfg_read(cfg) == -1)
        panic(0, "Couldn't read configuration from `%s'.", octstr_get_cstr(filename));

    octstr_destroy(filename);

    report_versions("pluginbox");

    init_pluginbox(cfg);
    
    long *pluginbox_port_ptr = gw_malloc(sizeof(long));
    *pluginbox_port_ptr = pluginbox_port;

    pluginboxc_run(pluginbox_port_ptr);

    cfg_destroy(cfg);
    if (restart_pluginbox) {
        gwthread_sleep(1.0);
    }

    pluginbox_plugin_shutdown();

    gwlib_shutdown();

    if (restart_pluginbox)
        execvp(argv[0], argv);
    return 0;
}
