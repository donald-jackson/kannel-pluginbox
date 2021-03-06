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
	Currently only mysql is supported.
*/


#ifndef DB_H
#define DB_H

#include "gwlib/gwlib.h"
#include "gwlib/dbpool.h"
#include "gwlib/dbpool_p.h"

/* db_init initialized a database connection pool from the config with given id 'config_id'.
   all available database types are tried, until a suitable config is found. */
DBPool *db_init(Cfg *cfg, Octstr *config_id);

/* db_init_shared also initializes a database connection pool, but returns a previously found pointer,
   in case a given database is already initialized before. This allows for different plugins to use
   a common database pool. */
DBPool *db_init_shared(Cfg *cfg, Octstr *config_id);

/* db_shutdown destroys a database pool or decreases it's counter in case it is a shared pool */
void db_shutdown(DBPool *pool);

/* db_fetch_pivot executes an sql query and returns the first field of the first record as an Octstr */
Octstr *db_fetch_pivot (DBPool *pool, Octstr *query, List *binds);

/* db_fetch_list returns a record set as a list-of-fields of a list-of-records */
List *db_fetch_list (DBPool *pool, Octstr *query, List *binds);

/* db_fetch_record returns the first record of a given query as a list-of-fields */
List *db_fetch_record (DBPool *pool, Octstr *query, List *binds);

/* db_fetch_dict returns a dictionary made of result set records. The dict key is the primary key.
   The primary key should be the first field selected in the select query. */
Dict *db_fetch_dict (DBPool *pool, Octstr *query, List *binds);

/* db_update exexcutes an sql query that doesn't return a record set (e.g. update/insert/delete). */
int db_update(DBPool *pool, Octstr *query, List *binds);

/* db_get_field_at returns the field at fieldindex of the record at index as an Octstr */
Octstr *db_get_field_at(List *table, int fieldindex, int index);

/* db_get_record returns the record as a list-of-fields at the given result set "index" */
List *db_get_record(List *table, int index);

/* db_table_destroy_item can be used to destroy a table, returned by db_fetch_list .
   Example: gwlist_destroy(table, db_table_destroy_item); */
void db_table_destroy_item(void *ptr);

#endif
