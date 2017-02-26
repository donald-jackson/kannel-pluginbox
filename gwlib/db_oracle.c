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

#include "db_oracle.h"

DBPool *sqlbox_init_oracle(Cfg* cfg, Octstr *config_id)
{
    CfgGroup *grp;
    List *grplist;
    Octstr *oracle_user, *oracle_pass, *oracle_tnsname;
    Octstr *p = NULL;
    long pool_size;
    DBConf *db_conf = NULL;
    DBPool *pool;

    /*
     * now grap the required information from the 'oracle-connection' group
     * with the oracle-id we just obtained
     *
     * we have to loop through all available Oracle connection definitions
     * and search for the one we are looking for
     */

     grplist = cfg_get_multi_group(cfg, octstr_imm("oracle-connection"));
     while (grplist && (grp = (CfgGroup *)gwlist_extract_first(grplist)) != NULL) {
        p = cfg_get(grp, octstr_imm("id"));
        if (p != NULL && octstr_compare(p, config_id) == 0) {
            goto found;
        }
        if (p != NULL) octstr_destroy(p);
     }
     debug("db_oracle.c", 0, "SQLBOX: Oracle: connection settings for id '%s' are not specified!",
           octstr_get_cstr(config_id));
     return NULL;

found:
    octstr_destroy(p);
    gwlist_destroy(grplist, NULL);

    if (cfg_get_integer(&pool_size, grp, octstr_imm("max-connections")) == -1 || pool_size == 0)
        pool_size = 1;

    if (!(oracle_user = cfg_get(grp, octstr_imm("username")))) {
	debug("db_oracle.c", 0, "SQLBOX: Oracle: directive 'username' is not specified!");
	return NULL;
    }
    if (!(oracle_pass = cfg_get(grp, octstr_imm("password")))) {
	debug("db_oracle.c", 0, "SQLBOX: Oracle: directive 'password' is not specified!");
	return NULL;
    }
    if (!(oracle_tnsname = cfg_get(grp, octstr_imm("tnsname")))) {
	debug("db_oracle.c", 0, "SQLBOX: Oracle: directive 'tnsname' is not specified!");
	return NULL;
    }

    /*
     * ok, ready to connect to Oracle
     */
    db_conf = gw_malloc(sizeof(DBConf));
    gw_assert(db_conf != NULL);

    db_conf->oracle = gw_malloc(sizeof(OracleConf));
    gw_assert(db_conf->oracle != NULL);

    db_conf->oracle->username = oracle_user;
    db_conf->oracle->password = oracle_pass;
    db_conf->oracle->tnsname = oracle_tnsname;

    pool = dbpool_create(DBPOOL_ORACLE, db_conf, pool_size);
    gw_assert(pool != NULL);

    /*
     * XXX should a failing connect throw panic?!
     */
    if (dbpool_conn_count(pool) == 0) {
	debug("db_oracle.c", 0,"SQLBOX: Oracle: database pool has no connections!");
	return NULL;
    }

    return pool;
}
