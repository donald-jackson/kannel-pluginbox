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

/*
	Attempt at generic Kannel database support.
*/


#include "gwlib/gwlib.h"
#include "db.h"
#include "db_mysql.h"
#include "db_mssql.h"
#include "db_oracle.h"
#include "db_pgsql.h"
#include "db_sdb.h"
#include "db_sqlite.h"
#include "db_sqlite3.h"

#define DB_TRACE

typedef struct {
	DBPool *pool;
	int count;
} SharedPool;

static Dict *shared_pools = NULL;

DBPool *db_init(Cfg *cfg, Octstr *config_id)
{
    DBPool *res;

    res = db_init_mysql(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_mssql(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_oracle(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_pgsql(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_sdb(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_sqlite(cfg, config_id);
    if (res) {
        return res;
    }
    res = db_init_sqlite3(cfg, config_id);
    if (res) {
        return res;
    }
    if (NULL == res) {
	debug("db.c", 0, "No connnection found with id %s", octstr_get_cstr(config_id));
	return NULL;
    }
    return res;
}

DBPool *db_init_shared(Cfg *cfg, Octstr *config_id)
{
	SharedPool *s_pool;
	if (NULL == shared_pools) {
		shared_pools = dict_create(10, NULL);
	}
	s_pool = dict_get(shared_pools, config_id);
	if (NULL == s_pool) {
		s_pool = gw_malloc(sizeof(SharedPool));
		s_pool->pool = db_init(cfg, config_id);
		s_pool->count = 1;
		dict_put(shared_pools, config_id, s_pool);
	}
	else {
		s_pool->count++;
	}
	return s_pool->pool;
}

void db_shutdown(DBPool *pool)
{
	List *keys;
	SharedPool *s_pool;
	int i;
	Octstr *key;

	/* check if this pool was one ouf of a shared pool */
	if (NULL != shared_pools) {
		keys = dict_keys(shared_pools);
		for (i = 0; i < gwlist_len(keys); i++) {
			key = gwlist_get(keys, i);
			s_pool = dict_get(shared_pools, key);
			if (NULL != s_pool && s_pool->pool == pool) {
				if ((--(s_pool->count)) > 0) {
					return;
				}
				dict_put(shared_pools, key, NULL);
				/* destroy the dict if not needed anymore */
				if (dict_key_count(shared_pools) == 0) {
					dict_destroy(shared_pools);
					shared_pools = NULL;
				}
				return;
			}
		}
	}
	dbpool_destroy(pool);
}

void db_table_destroy_item(void *ptr)
{
	gwlist_destroy((List *)ptr, octstr_destroy_item);
}

Octstr *db_fetch_pivot (DBPool *pool, Octstr *query, List *binds)
{
	Octstr *result = NULL, *pivot;
	List *table = db_fetch_list(pool, query, binds);
	List *record;
	if (NULL == table) {
		return NULL;
	}
	if (gwlist_len(table) > 0) {
		record = db_get_record(table, 0);
		if (gwlist_len(record) > 0) {
			pivot = gwlist_get(record, 0);
			if (pivot) {
				result = octstr_duplicate(pivot);
			}
		}
	}
	gwlist_destroy(table, db_table_destroy_item);
	return result;
}

List *db_fetch_list (DBPool *pool, Octstr *query, List *binds)
{
	List *result;
	DBPoolConn *conn;
	int ret;

	if (NULL == pool->db_ops->select) {
		panic(0, "sql_fetch not implemented for this database type.");
	}
#ifdef DB_TRACE
	debug("db,c", 0, "SQL: %s", octstr_get_cstr(query));
#endif
	conn = dbpool_conn_consume(pool);
	ret = dbpool_conn_select(conn, query, binds, &result);
	dbpool_conn_produce(conn);
	return result;
}

List *db_fetch_record (DBPool *pool, Octstr *query, List *binds)
{
	List *table = db_fetch_list(pool, query, binds);
	List *result;
	if (NULL == table) {
		return NULL;
	}
	if (gwlist_len(table) < 1) {
		return NULL;
	}
	result = gwlist_get(table, 0);
	gwlist_delete(table, 0, 1);
	db_table_destroy_item(table);
	return result;
}

Dict *db_fetch_dict (DBPool *pool, Octstr *query, List *binds)
{
	List *table = db_fetch_list(pool, query, binds);
	Dict *result = dict_create(gwlist_len(table), db_table_destroy_item);
	Octstr *field;
	int i;
	for (i = 0; i < gwlist_len(table); i++) {
		/* todo: check if nrfields > 0? */
		field = db_get_field_at(table, 0, i);
		if (NULL != field) {
			dict_put(result, field, gwlist_get(table, i));
		}
	}
	gwlist_destroy(table, NULL);
	return result;
}

int db_update(DBPool *pool, Octstr *query, List *binds)
{
	DBPoolConn *conn;
	int ret;

	if (NULL == pool->db_ops->update) {
		panic(0, "sql_update not implemented for this database type.");
	}
#ifdef DB_TRACE
	debug("db,c", 0, "SQL: %s", octstr_get_cstr(query));
#endif
	conn = dbpool_conn_consume(pool);
	ret = dbpool_conn_update(conn, query, binds);
	dbpool_conn_produce(conn);
	return ret;
}

List *db_get_record(List *table, int index)
{
	return gwlist_get(table, index);
}

Octstr *db_get_field(List *record, int fieldindex)
{
	return gwlist_get(record, fieldindex);
}

Octstr *db_get_field_at(List *table, int fieldindex, int index)
{
	return db_get_field(gwlist_get(table, index), fieldindex);
}

