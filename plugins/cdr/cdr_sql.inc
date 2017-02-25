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
#include "gw/sms.h"
#include "gwlib/db.h"

#define SQLBOX_MYSQL_SELECT_QUERY "SELECT sql_id, momt%s FROM %S LIMIT 0,1"

#define SQLBOX_MYSQL_SELECT_LIST_QUERY "SELECT sql_id, momt%s FROM %S LIMIT 0,%ld"

#define SQLBOX_MYSQL_INSERT_QUERY "INSERT INTO %s ( momt%s, sql_id) VALUES ( \
?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, NULL)"

#define SQLBOX_MYSQL_DELETE_QUERY "DELETE FROM %S WHERE sql_id = %S"

static Octstr *msg_fields()
{
	Octstr *res = octstr_create("");

#define INTEGER(name) octstr_format_append(res, ", f_%s", #name);
#define OCTSTR(name) octstr_format_append(res, ", f_%s", #name);
#define UUID(name) octstr_format_append(res, ", f_%s", #name);
#define VOID(name) /**/
#define MSG(type, stmt) { if (type == sms) { stmt } }
#include "gw/msg-decl.h"
#undef OCTSTR
#undef INTEGER
#undef UUID
#undef VOID
	return res;
}

static Octstr *column_defs(Octstr *tablename)
{
	Octstr *res = octstr_format("CREATE TABLE IF NOT EXISTS %s (sql_id BIGINT(20) NOT NULL AUTO_INCREMENT PRIMARY KEY, momt ENUM('MO', 'MT') NULL", octstr_get_cstr(tablename));

#define INTEGER(name) octstr_format_append(res, ", f_%s BIGINT(20) NULL", #name);
#define OCTSTR(name) octstr_format_append(res, ", f_%s VARCHAR(1024) NULL", #name);
#define UUID(name) octstr_format_append(res, ", f_%s VARCHAR(255) NULL", #name);
#define VOID(name) /**/
#define MSG(type, stmt) { if (type == sms) { stmt } }
#include "gw/msg-decl.h"
#undef OCTSTR
#undef INTEGER
#undef UUID
#undef VOID
	octstr_append(res, octstr_imm(")"));
	return res;
}

/* save a message in the sent sms table */
static void sql_save_msg(DBPool *pool, Msg *msg, Octstr *momt, Octstr *sql_logtable)
{
    Octstr *sql, *fields;
    List *binds = gwlist_create();
    char uuidstr[UUID_STR_LEN + 1];

    gwlist_append(binds, octstr_duplicate(momt));
#define INTEGER(name) gwlist_append(binds, octstr_format("%ld", p->name));
#define OCTSTR(name) gwlist_append(binds, (p->name == NULL ? NULL : octstr_duplicate(p->name)));
#define UUID(name) uuid_unparse(p->name, uuidstr); gwlist_append(binds, octstr_create(uuidstr));
#define VOID(name) /**/
#define MSG(type, stmt) { if (type == sms) { struct type *p = &msg->type; stmt } }
#include "gw/msg-decl.h"
#undef OCTSTR
#undef INTEGER
#undef UUID
#undef VOID
    fields = msg_fields();
    sql = octstr_format(SQLBOX_MYSQL_INSERT_QUERY, octstr_get_cstr(sql_logtable), octstr_get_cstr(fields));
    octstr_destroy(fields);
    db_update(pool, sql, binds);
    octstr_destroy(sql);
    gwlist_destroy(binds, octstr_destroy_item);
}

/* save a list of messages and delete them from the insert table */
static void sql_save_list(DBPool *pool, List *qlist, Octstr *momt, int save_mt, Octstr *sql_log_table)
{
    Msg *msg;
    Octstr *sql;

    while (gwlist_len(qlist) > 0 && (msg = gwlist_consume(qlist)) != NULL) {
        if (save_mt) {
	    sql_save_msg(pool, msg, momt, sql_log_table);
	}
    }
}

static Msg *record_to_msg(List *record)
{
	int fcnt = 2; /* skip sql_id & momt fields */
	Msg *msg = msg_create(sms);
#define INTEGER(name) { Octstr *f = gwlist_get(record, fcnt++); p->name = (f && octstr_len(f) > 0) ? atoi(octstr_get_cstr(f)) : SMS_PARAM_UNDEFINED; }
#define OCTSTR(name) { Octstr *f = gwlist_get(record, fcnt++); p->name = (f && octstr_len(f) > 0) ? octstr_duplicate(f) : NULL; }
#define UUID(name) uuid_parse(octstr_get_cstr(gwlist_get(record, fcnt++)), p->name);
#define VOID(name) /**/
#define MSG(type, stmt) { if (type == sms) { struct type *p = &msg->type; stmt } }
#include "gw/msg-decl.h"
#undef OCTSTR
#undef INTEGER
#undef UUID
#undef VOID
	msg_dump(msg, 0);
	return msg;
}

static void sql_delete(DBPool *pool, Octstr *sql_insert_table, Octstr *id)
{
	Octstr *sql;

	sql = octstr_format(SQLBOX_MYSQL_DELETE_QUERY, sql_insert_table, id);
	db_update(pool, sql, NULL);
	octstr_destroy(sql);
}

static Msg *sql_fetch_msg(DBPool *pool, Octstr *sql_insert_table)
{
	Msg *msg = NULL;
	List *recordset, *record;
	Octstr *sql, *id, *fields;

	fields = msg_fields();
	sql = octstr_format(SQLBOX_MYSQL_SELECT_QUERY, octstr_get_cstr(fields), sql_insert_table);
	octstr_destroy(fields);
	record = db_fetch_record(pool, sql, NULL);
	octstr_destroy(sql);
	if (record == NULL) {
        	return NULL;
	}
	id = gwlist_get(record, 0);
	msg = record_to_msg(record);
	if (NULL != msg->sms.foreign_id) {
		octstr_destroy(msg->sms.foreign_id);
	}
	// we abuse the foreign id value to save the sql id of this message.
	msg->sms.foreign_id = octstr_duplicate(id);
	/* delete current row */
	sql = octstr_format(SQLBOX_MYSQL_DELETE_QUERY, sql_insert_table, id);
	db_update(pool, sql, NULL);
	octstr_destroy(sql);
	return msg;
}

static int sql_fetch_msg_list(DBPool *pool, List *qlist, long limit, Octstr *sql_insert_table)
{
    int ret = 0, i;
    Octstr *sql, *id, *fields;
    List *table, *record;

    fields = msg_fields();
    sql = octstr_format(SQLBOX_MYSQL_SELECT_LIST_QUERY, octstr_get_cstr(fields), sql_insert_table, limit);
    octstr_destroy(fields);
    table = db_fetch_list(pool, sql, NULL);
    if (table == NULL) {
        return 0;
    }
    ret = gwlist_len(table);
    for (i = 0; i < ret; i++) {
	record = gwlist_get(table, i);
	id = gwlist_get(record, 0);
	gwlist_append(qlist, record_to_msg(record));
	sql = octstr_format(SQLBOX_MYSQL_DELETE_QUERY, sql_insert_table, id);
	db_update(pool, sql, NULL);
	octstr_destroy(sql);
    }
    return ret;
}

static void sql_init_tables(DBPool *pool, Octstr *logtable, Octstr *inserttable)
{
	Octstr *sql;
	if (NULL != logtable) {
		sql = column_defs(logtable);
		db_update(pool, sql, NULL);
		octstr_destroy(sql);
	}
	if (NULL != inserttable) {
		sql = column_defs(inserttable);
		db_update(pool, sql, NULL);
		octstr_destroy(sql);
	}
}
