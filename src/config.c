/*
 * Copyright(C) 2014 Sureshkumar Nedunchezhian. All rights reserved.
 *
 */ 

/*
 * Copyright (c) 2009-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "prez.h"
#include "cluster.h"

static struct {
    const char     *name;
    const int       value;
} validSyslogFacilities[] = {
    {"user",    LOG_USER},
    {"local0",  LOG_LOCAL0},
    {"local1",  LOG_LOCAL1},
    {"local2",  LOG_LOCAL2},
    {"local3",  LOG_LOCAL3},
    {"local4",  LOG_LOCAL4},
    {"local5",  LOG_LOCAL5},
    {"local6",  LOG_LOCAL6},
    {"local7",  LOG_LOCAL7},
    {NULL, 0}
};

clientBufferLimitsConfig clientBufferLimitsDefaults[PREZ_CLIENT_TYPE_COUNT] = {
    {0, 0, 0}, /* normal */
};

/*-----------------------------------------------------------------------------
 * Config file parsing
 *----------------------------------------------------------------------------*/

int yesnotoi(char *s) {
    if (!strcasecmp(s,"yes")) return 1;
    else if (!strcasecmp(s,"no")) return 0;
    else return -1;
}

void loadServerConfigFromString(char *config) {
    char *err = NULL;
    int linenum = 0, totlines, i;
    //int slaveof_linenum = 0;
    sds *lines;

    lines = sdssplitlen(config,strlen(config),"\n",1,&totlines);

    for (i = 0; i < totlines; i++) {
        sds *argv;
        int argc;

        linenum = i+1;
        lines[i] = sdstrim(lines[i]," \t\r\n");

        /* Skip comments and blank lines */
        if (lines[i][0] == '#' || lines[i][0] == '\0') continue;

        /* Split into arguments */
        argv = sdssplitargs(lines[i],&argc);
        if (argv == NULL) {
            err = "Unbalanced quotes in configuration line";
            goto loaderr;
        }

        /* Skip this line if the resulting command vector is empty. */
        if (argc == 0) {
            sdsfreesplitres(argv,argc);
            continue;
        }
        sdstolower(argv[0]);

        /* Execute config directives */
        if (!strcasecmp(argv[0],"timeout") && argc == 2) {
            server.maxidletime = atoi(argv[1]);
            if (server.maxidletime < 0) {
                err = "Invalid timeout value"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0], "name") && argc == 2) {
            zfree(server.name);
            server.name = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"tcp-keepalive") && argc == 2) {
            server.tcpkeepalive = atoi(argv[1]);
            if (server.tcpkeepalive < 0) {
                err = "Invalid tcp-keepalive value"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"port") && argc == 2) {
            server.port = atoi(argv[1]);
            if (server.port < 0 || server.port > 65535) {
                err = "Invalid port"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"tcp-backlog") && argc == 2) {
            server.tcp_backlog = atoi(argv[1]);
            if (server.tcp_backlog < 0) {
                err = "Invalid backlog value"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"bind") && argc >= 2) {
            int j, addresses = argc-1;

            if (addresses > PREZ_BINDADDR_MAX) {
                err = "Too many bind addresses specified"; goto loaderr;
            }
            for (j = 0; j < addresses; j++)
                server.bindaddr[j] = zstrdup(argv[j+1]);
            server.bindaddr_count = addresses;
        } else if (!strcasecmp(argv[0],"unixsocket") && argc == 2) {
            server.unixsocket = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"unixsocketperm") && argc == 2) {
            errno = 0;
            server.unixsocketperm = (mode_t)strtol(argv[1], NULL, 8);
            if (errno || server.unixsocketperm > 0777) {
                err = "Invalid socket file permissions"; goto loaderr;
            }
#if 0
        } else if (!strcasecmp(argv[0],"save")) {
            if (argc == 3) {
                int seconds = atoi(argv[1]);
                int changes = atoi(argv[2]);
                if (seconds < 1 || changes < 0) {
                    err = "Invalid save parameters"; goto loaderr;
                }
                appendServerSaveParams(seconds,changes);
            } else if (argc == 2 && !strcasecmp(argv[1],"")) {
                resetServerSaveParams();
            }
#endif
        } else if (!strcasecmp(argv[0],"dir") && argc == 2) {
            if (chdir(argv[1]) == -1) {
                prezLog(PREZ_WARNING,"Can't chdir to '%s': %s",
                    argv[1], strerror(errno));
                exit(1);
            }
        } else if (!strcasecmp(argv[0],"loglevel") && argc == 2) {
            if (!strcasecmp(argv[1],"debug")) server.verbosity = PREZ_DEBUG;
            else if (!strcasecmp(argv[1],"verbose")) server.verbosity = PREZ_VERBOSE;
            else if (!strcasecmp(argv[1],"notice")) server.verbosity = PREZ_NOTICE;
            else if (!strcasecmp(argv[1],"warning")) server.verbosity = PREZ_WARNING;
            else {
                err = "Invalid log level. Must be one of debug, notice, warning";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"logfile") && argc == 2) {
            FILE *logfp;

            zfree(server.logfile);
            server.logfile = zstrdup(argv[1]);
            if (server.logfile[0] != '\0') {
                /* Test if we are able to open the file. The server will not
                 * be able to abort just for this problem later... */
                logfp = fopen(server.logfile,"a");
                if (logfp == NULL) {
                    err = sdscatprintf(sdsempty(),
                        "Can't open the log file: %s", strerror(errno));
                    goto loaderr;
                }
                fclose(logfp);
            }
        } else if (!strcasecmp(argv[0],"syslog-enabled") && argc == 2) {
            if ((server.syslog_enabled = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"syslog-ident") && argc == 2) {
            if (server.syslog_ident) zfree(server.syslog_ident);
            server.syslog_ident = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"syslog-facility") && argc == 2) {
            int i;

            for (i = 0; validSyslogFacilities[i].name; i++) {
                if (!strcasecmp(validSyslogFacilities[i].name, argv[1])) {
                    server.syslog_facility = validSyslogFacilities[i].value;
                    break;
                }
            }

            if (!validSyslogFacilities[i].name) {
                err = "Invalid log facility. Must be one of USER or between LOCAL0-LOCAL7";
                goto loaderr;
            }
#if 0
        } else if (!strcasecmp(argv[0],"databases") && argc == 2) {
            server.dbnum = atoi(argv[1]);
            if (server.dbnum < 1) {
                err = "Invalid number of databases"; goto loaderr;
            }
#endif
        } else if (!strcasecmp(argv[0],"include") && argc == 2) {
            loadServerConfig(argv[1],NULL);
        } else if (!strcasecmp(argv[0],"maxclients") && argc == 2) {
            server.maxclients = atoi(argv[1]);
            if (server.maxclients < 1) {
                err = "Invalid max clients limit"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"maxmemory") && argc == 2) {
            server.maxmemory = memtoll(argv[1],NULL);
        } else if (!strcasecmp(argv[0],"maxmemory-policy") && argc == 2) {
            if (!strcasecmp(argv[1],"volatile-lru")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_LRU;
            } else if (!strcasecmp(argv[1],"volatile-random")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_RANDOM;
            } else if (!strcasecmp(argv[1],"volatile-ttl")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_TTL;
            } else if (!strcasecmp(argv[1],"allkeys-lru")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_ALLKEYS_LRU;
            } else if (!strcasecmp(argv[1],"allkeys-random")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_ALLKEYS_RANDOM;
            } else if (!strcasecmp(argv[1],"noeviction")) {
                server.maxmemory_policy = PREZ_MAXMEMORY_NO_EVICTION;
            } else {
                err = "Invalid maxmemory policy";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"maxmemory-samples") && argc == 2) {
            server.maxmemory_samples = atoi(argv[1]);
            if (server.maxmemory_samples <= 0) {
                err = "maxmemory-samples must be 1 or greater";
                goto loaderr;
            }
#if 0
        } else if (!strcasecmp(argv[0],"slaveof") && argc == 3) {
            slaveof_linenum = linenum;
            server.masterhost = sdsnew(argv[1]);
            server.masterport = atoi(argv[2]);
            server.repl_state = PREZ_REPL_CONNECT;
        } else if (!strcasecmp(argv[0],"repl-ping-slave-period") && argc == 2) {
            server.repl_ping_slave_period = atoi(argv[1]);
            if (server.repl_ping_slave_period <= 0) {
                err = "repl-ping-slave-period must be 1 or greater";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"repl-timeout") && argc == 2) {
            server.repl_timeout = atoi(argv[1]);
            if (server.repl_timeout <= 0) {
                err = "repl-timeout must be 1 or greater";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"repl-disable-tcp-nodelay") && argc==2) {
            if ((server.repl_disable_tcp_nodelay = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"repl-backlog-size") && argc == 2) {
            long long size = memtoll(argv[1],NULL);
            if (size <= 0) {
                err = "repl-backlog-size must be 1 or greater.";
                goto loaderr;
            }
            resizeReplicationBacklog(size);
        } else if (!strcasecmp(argv[0],"repl-backlog-ttl") && argc == 2) {
            server.repl_backlog_time_limit = atoi(argv[1]);
            if (server.repl_backlog_time_limit < 0) {
                err = "repl-backlog-ttl can't be negative ";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"masterauth") && argc == 2) {
        	server.masterauth = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"slave-serve-stale-data") && argc == 2) {
            if ((server.repl_serve_stale_data = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"slave-read-only") && argc == 2) {
            if ((server.repl_slave_ro = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"rdbcompression") && argc == 2) {
            if ((server.rdb_compression = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"rdbchecksum") && argc == 2) {
            if ((server.rdb_checksum = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"activerehashing") && argc == 2) {
            if ((server.activerehashing = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
#endif
        } else if (!strcasecmp(argv[0],"daemonize") && argc == 2) {
            if ((server.daemonize = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"hz") && argc == 2) {
            server.hz = atoi(argv[1]);
            if (server.hz < PREZ_MIN_HZ) server.hz = PREZ_MIN_HZ;
            if (server.hz > PREZ_MAX_HZ) server.hz = PREZ_MAX_HZ;
#if 0
        } else if (!strcasecmp(argv[0],"appendonly") && argc == 2) {
            int yes;

            if ((yes = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
            server.aof_state = yes ? PREZ_AOF_ON : PREZ_AOF_OFF;
        } else if (!strcasecmp(argv[0],"appendfilename") && argc == 2) {
            if (!pathIsBaseName(argv[1])) {
                err = "appendfilename can't be a path, just a filename";
                goto loaderr;
            }
            zfree(server.aof_filename);
            server.aof_filename = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"no-appendfsync-on-rewrite")
                   && argc == 2) {
            if ((server.aof_no_fsync_on_rewrite= yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"appendfsync") && argc == 2) {
            if (!strcasecmp(argv[1],"no")) {
                server.aof_fsync = AOF_FSYNC_NO;
            } else if (!strcasecmp(argv[1],"always")) {
                server.aof_fsync = AOF_FSYNC_ALWAYS;
            } else if (!strcasecmp(argv[1],"everysec")) {
                server.aof_fsync = AOF_FSYNC_EVERYSEC;
            } else {
                err = "argument must be 'no', 'always' or 'everysec'";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"auto-aof-rewrite-percentage") &&
                   argc == 2)
        {
            server.aof_rewrite_perc = atoi(argv[1]);
            if (server.aof_rewrite_perc < 0) {
                err = "Invalid negative percentage for AOF auto rewrite";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"auto-aof-rewrite-min-size") &&
                   argc == 2)
        {
            server.aof_rewrite_min_size = memtoll(argv[1],NULL);
        } else if (!strcasecmp(argv[0],"aof-rewrite-incremental-fsync") &&
                   argc == 2)
        {
            if ((server.aof_rewrite_incremental_fsync = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"requirepass") && argc == 2) {
            if (strlen(argv[1]) > PREZ_AUTHPASS_MAX_LEN) {
                err = "Password is longer than PREZ_AUTHPASS_MAX_LEN";
                goto loaderr;
            }
            server.requirepass = zstrdup(argv[1]);
#endif
        } else if (!strcasecmp(argv[0],"pidfile") && argc == 2) {
            zfree(server.pidfile);
            server.pidfile = zstrdup(argv[1]);
#if 0
        } else if (!strcasecmp(argv[0],"dbfilename") && argc == 2) {
            if (!pathIsBaseName(argv[1])) {
                err = "dbfilename can't be a path, just a filename";
                goto loaderr;
            }
            zfree(server.rdb_filename);
            server.rdb_filename = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"hash-max-ziplist-entries") && argc == 2) {
            server.hash_max_ziplist_entries = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"hash-max-ziplist-value") && argc == 2) {
            server.hash_max_ziplist_value = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"list-max-ziplist-entries") && argc == 2){
            server.list_max_ziplist_entries = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"list-max-ziplist-value") && argc == 2) {
            server.list_max_ziplist_value = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"set-max-intset-entries") && argc == 2) {
            server.set_max_intset_entries = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"zset-max-ziplist-entries") && argc == 2) {
            server.zset_max_ziplist_entries = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"zset-max-ziplist-value") && argc == 2) {
            server.zset_max_ziplist_value = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"hll-sparse-max-bytes") && argc == 2) {
            server.hll_sparse_max_bytes = memtoll(argv[1], NULL);
        } else if (!strcasecmp(argv[0],"rename-command") && argc == 3) {
            struct prezCommand *cmd = lookupCommand(argv[1]);
            int retval;

            if (!cmd) {
                err = "No such command in rename-command";
                goto loaderr;
            }

            /* If the target command name is the empty string we just
             * remove it from the command table. */
            retval = dictDelete(server.commands, argv[1]);
            prezAssert(retval == DICT_OK);

            /* Otherwise we re-add the command under a different name. */
            if (sdslen(argv[2]) != 0) {
                sds copy = sdsdup(argv[2]);

                retval = dictAdd(server.commands, copy, cmd);
                if (retval != DICT_OK) {
                    sdsfree(copy);
                    err = "Target command name already exists"; goto loaderr;
                }
            }
        } else if (!strcasecmp(argv[0],"cluster-enabled") && argc == 2) {
            if ((server.cluster_enabled = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
#endif
        } else if (!strcasecmp(argv[0],"cluster-port") && argc == 2) {
            server.cport = atoi(argv[1]);
            if (server.cport < 0 || server.port > 65535) {
                err = "Invalid port"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"cluster-config-file") && argc == 2) {
            zfree(server.cluster_configfile);
            server.cluster_configfile = zstrdup(argv[1]);
        } else if (!strcasecmp(argv[0],"cluster-election-timeout") && argc == 2) {
            server.cluster->election_timeout = strtoll(argv[1],NULL,10);
            if (server.cluster->election_timeout <= 0) {
                err = "cluster election timeout must be 1 or greater"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"cluster-heartbeat-interval") && argc == 2) {
            server.cluster->heartbeat_interval = strtoll(argv[1],NULL,10);
            if (server.cluster->heartbeat_interval <= 0) {
                err = "cluster heartbeat interval must be 1 or greater"; goto loaderr;
            }
#if 0
        } else if (!strcasecmp(argv[0],"cluster-node-timeout") && argc == 2) {
            server.cluster_node_timeout = strtoll(argv[1],NULL,10);
            if (server.cluster_node_timeout <= 0) {
                err = "cluster node timeout must be 1 or greater"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"cluster-migration-barrier")
                   && argc == 2)
        {
            server.cluster_migration_barrier = atoi(argv[1]);
            if (server.cluster_migration_barrier < 0) {
                err = "cluster migration barrier must zero or positive";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"cluster-slave-validity-factor")
                   && argc == 2)
        {
            server.cluster_slave_validity_factor = atoi(argv[1]);
            if (server.cluster_slave_validity_factor < 0) {
                err = "cluster slave validity factor must be zero or positive";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"lua-time-limit") && argc == 2) {
            server.lua_time_limit = strtoll(argv[1],NULL,10);
        } else if (!strcasecmp(argv[0],"slowlog-log-slower-than") &&
                   argc == 2)
        {
            server.slowlog_log_slower_than = strtoll(argv[1],NULL,10);
        } else if (!strcasecmp(argv[0],"latency-monitor-threshold") &&
                   argc == 2)
        {
            server.latency_monitor_threshold = strtoll(argv[1],NULL,10);
            if (server.latency_monitor_threshold < 0) {
                err = "The latency threshold can't be negative";
                goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"slowlog-max-len") && argc == 2) {
            server.slowlog_max_len = strtoll(argv[1],NULL,10);
#endif
        } else if (!strcasecmp(argv[0],"client-output-buffer-limit") &&
                   argc == 5)
        {
            int class = getClientTypeByName(argv[1]);
            unsigned long long hard, soft;
            int soft_seconds;

            if (class == -1) {
                err = "Unrecognized client limit class";
                goto loaderr;
            }
            hard = memtoll(argv[2],NULL);
            soft = memtoll(argv[3],NULL);
            soft_seconds = atoi(argv[4]);
            if (soft_seconds < 0) {
                err = "Negative number of seconds in soft limit is invalid";
                goto loaderr;
            }
            server.client_obuf_limits[class].hard_limit_bytes = hard;
            server.client_obuf_limits[class].soft_limit_bytes = soft;
            server.client_obuf_limits[class].soft_limit_seconds = soft_seconds;
#if 0
        } else if (!strcasecmp(argv[0],"stop-writes-on-bgsave-error") &&
                   argc == 2) {
            if ((server.stop_writes_on_bgsave_err = yesnotoi(argv[1])) == -1) {
                err = "argument must be 'yes' or 'no'"; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"slave-priority") && argc == 2) {
            server.slave_priority = atoi(argv[1]);
        } else if (!strcasecmp(argv[0],"min-slaves-to-write") && argc == 2) {
            server.repl_min_slaves_to_write = atoi(argv[1]);
            if (server.repl_min_slaves_to_write < 0) {
                err = "Invalid value for min-slaves-to-write."; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"min-slaves-max-lag") && argc == 2) {
            server.repl_min_slaves_max_lag = atoi(argv[1]);
            if (server.repl_min_slaves_max_lag < 0) {
                err = "Invalid value for min-slaves-max-lag."; goto loaderr;
            }
        } else if (!strcasecmp(argv[0],"notify-keyspace-events") && argc == 2) {
            int flags = keyspaceEventsStringToFlags(argv[1]);

            if (flags == -1) {
                err = "Invalid event class character. Use 'g$lshzxeA'.";
                goto loaderr;
            }
            server.notify_keyspace_events = flags;
        } else if (!strcasecmp(argv[0],"sentinel")) {
            /* argc == 1 is handled by main() as we need to enter the sentinel
             * mode ASAP. */
            if (argc != 1) {
                if (!server.sentinel_mode) {
                    err = "sentinel directive while not in sentinel mode";
                    goto loaderr;
                }
                err = sentinelHandleConfiguration(argv+1,argc-1);
                if (err) goto loaderr;
            }
#endif
        } else {
            err = "Bad directive or wrong number of arguments"; goto loaderr;
        }
        sdsfreesplitres(argv,argc);
    }
#if 0
    /* Sanity checks. */
    if (server.cluster_enabled && server.masterhost) {
        linenum = slaveof_linenum;
        i = linenum-1;
        err = "slaveof directive not allowed in cluster mode";
        goto loaderr;
    }
#endif
    sdsfreesplitres(lines,totlines);
    return;

loaderr:
    fprintf(stderr, "\n*** FATAL CONFIG FILE ERROR ***\n");
    fprintf(stderr, "Reading the configuration file, at line %d\n", linenum);
    fprintf(stderr, ">>> '%s'\n", lines[i]);
    fprintf(stderr, "%s\n", err);
    exit(1);
}

/* Load the server configuration from the specified filename.
 * The function appends the additional configuration directives stored
 * in the 'options' string to the config file before loading.
 *
 * Both filename and options can be NULL, in such a case are considered
 * empty. This way loadServerConfig can be used to just load a file or
 * just load a string. */
void loadServerConfig(char *filename, char *options) {
    sds config = sdsempty();
    char buf[PREZ_CONFIGLINE_MAX+1];

    /* Load the file content */
    if (filename) {
        FILE *fp;

        if (filename[0] == '-' && filename[1] == '\0') {
            fp = stdin;
        } else {
            if ((fp = fopen(filename,"r")) == NULL) {
                prezLog(PREZ_WARNING,
                    "Fatal error, can't open config file '%s'", filename);
                exit(1);
            }
        }
        while(fgets(buf,PREZ_CONFIGLINE_MAX+1,fp) != NULL)
            config = sdscat(config,buf);
        if (fp != stdin) fclose(fp);
    }
    /* Append the additional options */
    if (options) {
        config = sdscat(config,"\n");
        config = sdscat(config,options);
    }
    loadServerConfigFromString(config);
    sdsfree(config);
}

/*-----------------------------------------------------------------------------
 * CONFIG SET implementation
 *----------------------------------------------------------------------------*/

void configSetCommand(prezClient *c) {
    robj *o;
    long long ll;
    prezAssertWithInfo(c,c->argv[2],sdsEncodedObject(c->argv[2]));
    prezAssertWithInfo(c,c->argv[3],sdsEncodedObject(c->argv[3]));
    o = c->argv[3];

    if (!strcasecmp(c->argv[2]->ptr,"dbfilename")) {
        if (!pathIsBaseName(o->ptr)) {
            addReplyError(c, "dbfilename can't be a path, just a filename");
            return;
        }
#if 0
        zfree(server.rdb_filename);
        server.rdb_filename = zstrdup(o->ptr);
    } else if (!strcasecmp(c->argv[2]->ptr,"requirepass")) {
        if (sdslen(o->ptr) > REDIS_AUTHPASS_MAX_LEN) goto badfmt;
        zfree(server.requirepass);
        server.requirepass = ((char*)o->ptr)[0] ? zstrdup(o->ptr) : NULL;
    } else if (!strcasecmp(c->argv[2]->ptr,"masterauth")) {
        zfree(server.masterauth);
        server.masterauth = ((char*)o->ptr)[0] ? zstrdup(o->ptr) : NULL;
    } else if (!strcasecmp(c->argv[2]->ptr,"maxmemory")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.maxmemory = ll;
        if (server.maxmemory) {
            if (server.maxmemory < zmalloc_used_memory()) {
                prezLog(PREZ_WARNING,"WARNING: the new maxmemory value set via CONFIG SET is smaller than the current memory usage. This will result in keys eviction and/or inability to accept new write commands depending on the maxmemory-policy.");
            }
            freeMemoryIfNeeded();
        }
#endif
    } else if (!strcasecmp(c->argv[2]->ptr,"maxclients")) {
        int orig_value = server.maxclients;

        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 1) goto badfmt;

        /* Try to check if the OS is capable of supporting so many FDs. */
        server.maxclients = ll;
        if (ll > orig_value) {
            adjustOpenFilesLimit();
            if (server.maxclients != ll) {
                addReplyErrorFormat(c,"The operating system is not able to handle the specified number of clients, try with %d", server.maxclients);
                server.maxclients = orig_value;
                return;
            }
            if ((unsigned int) aeGetSetSize(server.el) <
                server.maxclients + PREZ_EVENTLOOP_FDSET_INCR)
            {
                if (aeResizeSetSize(server.el,
                    server.maxclients + PREZ_EVENTLOOP_FDSET_INCR) == AE_ERR)
                {
                    addReplyError(c,"The event loop API used by Prez is not able to handle the specified number of clients");
                    server.maxclients = orig_value;
                    return;
                }
            }
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"hz")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.hz = ll;
        if (server.hz < PREZ_MIN_HZ) server.hz = PREZ_MIN_HZ;
        if (server.hz > PREZ_MAX_HZ) server.hz = PREZ_MAX_HZ;
#if 0
    } else if (!strcasecmp(c->argv[2]->ptr,"maxmemory-policy")) {
        if (!strcasecmp(o->ptr,"volatile-lru")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_LRU;
        } else if (!strcasecmp(o->ptr,"volatile-random")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_RANDOM;
        } else if (!strcasecmp(o->ptr,"volatile-ttl")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_VOLATILE_TTL;
        } else if (!strcasecmp(o->ptr,"allkeys-lru")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_ALLKEYS_LRU;
        } else if (!strcasecmp(o->ptr,"allkeys-random")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_ALLKEYS_RANDOM;
        } else if (!strcasecmp(o->ptr,"noeviction")) {
            server.maxmemory_policy = PREZ_MAXMEMORY_NO_EVICTION;
        } else {
            goto badfmt;
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"maxmemory-samples")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll <= 0) goto badfmt;
        server.maxmemory_samples = ll;
#endif
    } else if (!strcasecmp(c->argv[2]->ptr,"timeout")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0 || ll > LONG_MAX) goto badfmt;
        server.maxidletime = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"tcp-keepalive")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0 || ll > INT_MAX) goto badfmt;
        server.tcpkeepalive = ll;
#if 0
    } else if (!strcasecmp(c->argv[2]->ptr,"appendfsync")) {
        if (!strcasecmp(o->ptr,"no")) {
            server.aof_fsync = AOF_FSYNC_NO;
        } else if (!strcasecmp(o->ptr,"everysec")) {
            server.aof_fsync = AOF_FSYNC_EVERYSEC;
        } else if (!strcasecmp(o->ptr,"always")) {
            server.aof_fsync = AOF_FSYNC_ALWAYS;
        } else {
            goto badfmt;
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"no-appendfsync-on-rewrite")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.aof_no_fsync_on_rewrite = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"appendonly")) {
        int enable = yesnotoi(o->ptr);

        if (enable == -1) goto badfmt;
        if (enable == 0 && server.aof_state != PREZ_AOF_OFF) {
            stopAppendOnly();
        } else if (enable && server.aof_state == PREZ_AOF_OFF) {
            if (startAppendOnly() == PREZ_ERR) {
                addReplyError(c,
                    "Unable to turn on AOF. Check server logs.");
                return;
            }
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"auto-aof-rewrite-percentage")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.aof_rewrite_perc = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"auto-aof-rewrite-min-size")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.aof_rewrite_min_size = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"aof-rewrite-incremental-fsync")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.aof_rewrite_incremental_fsync = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"aof-load-truncated")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.aof_load_truncated = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"save")) {
        int vlen, j;
        sds *v = sdssplitlen(o->ptr,sdslen(o->ptr)," ",1,&vlen);

        /* Perform sanity check before setting the new config:
         * - Even number of args
         * - Seconds >= 1, changes >= 0 */
        if (vlen & 1) {
            sdsfreesplitres(v,vlen);
            goto badfmt;
        }
        for (j = 0; j < vlen; j++) {
            char *eptr;
            long val;

            val = strtoll(v[j], &eptr, 10);
            if (eptr[0] != '\0' ||
                ((j & 1) == 0 && val < 1) ||
                ((j & 1) == 1 && val < 0)) {
                sdsfreesplitres(v,vlen);
                goto badfmt;
            }
        }
        /* Finally set the new config */
        resetServerSaveParams();
        for (j = 0; j < vlen; j += 2) {
            time_t seconds;
            int changes;

            seconds = strtoll(v[j],NULL,10);
            changes = strtoll(v[j+1],NULL,10);
            appendServerSaveParams(seconds, changes);
        }
        sdsfreesplitres(v,vlen);
    } else if (!strcasecmp(c->argv[2]->ptr,"slave-serve-stale-data")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.repl_serve_stale_data = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"slave-read-only")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.repl_slave_ro = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"dir")) {
        if (chdir((char*)o->ptr) == -1) {
            addReplyErrorFormat(c,"Changing directory: %s", strerror(errno));
            return;
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"hash-max-ziplist-entries")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.hash_max_ziplist_entries = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"hash-max-ziplist-value")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.hash_max_ziplist_value = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"list-max-ziplist-entries")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.list_max_ziplist_entries = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"list-max-ziplist-value")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.list_max_ziplist_value = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"set-max-intset-entries")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.set_max_intset_entries = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"zset-max-ziplist-entries")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.zset_max_ziplist_entries = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"zset-max-ziplist-value")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.zset_max_ziplist_value = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"hll-sparse-max-bytes")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.hll_sparse_max_bytes = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"lua-time-limit")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.lua_time_limit = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"slowlog-log-slower-than")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR) goto badfmt;
        server.slowlog_log_slower_than = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"slowlog-max-len")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.slowlog_max_len = (unsigned)ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"latency-monitor-threshold")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.latency_monitor_threshold = ll;
#endif
    } else if (!strcasecmp(c->argv[2]->ptr,"loglevel")) {
        if (!strcasecmp(o->ptr,"warning")) {
            server.verbosity = PREZ_WARNING;
        } else if (!strcasecmp(o->ptr,"notice")) {
            server.verbosity = PREZ_NOTICE;
        } else if (!strcasecmp(o->ptr,"verbose")) {
            server.verbosity = PREZ_VERBOSE;
        } else if (!strcasecmp(o->ptr,"debug")) {
            server.verbosity = PREZ_DEBUG;
        } else {
            goto badfmt;
        }
    } else if (!strcasecmp(c->argv[2]->ptr,"client-output-buffer-limit")) {
        int vlen, j;
        sds *v = sdssplitlen(o->ptr,sdslen(o->ptr)," ",1,&vlen);

        /* We need a multiple of 4: <class> <hard> <soft> <soft_seconds> */
        if (vlen % 4) {
            sdsfreesplitres(v,vlen);
            goto badfmt;
        }

        /* Sanity check of single arguments, so that we either refuse the
         * whole configuration string or accept it all, even if a single
         * error in a single client class is present. */
        for (j = 0; j < vlen; j++) {
            char *eptr;
            long val;

            if ((j % 4) == 0) {
                if (getClientTypeByName(v[j]) == -1) {
                    sdsfreesplitres(v,vlen);
                    goto badfmt;
                }
            } else {
                val = strtoll(v[j], &eptr, 10);
                if (eptr[0] != '\0' || val < 0) {
                    sdsfreesplitres(v,vlen);
                    goto badfmt;
                }
            }
        }
        /* Finally set the new config */
        for (j = 0; j < vlen; j += 4) {
            int class;
            unsigned long long hard, soft;
            int soft_seconds;

            class = getClientTypeByName(v[j]);
            hard = strtoll(v[j+1],NULL,10);
            soft = strtoll(v[j+2],NULL,10);
            soft_seconds = strtoll(v[j+3],NULL,10);

            server.client_obuf_limits[class].hard_limit_bytes = hard;
            server.client_obuf_limits[class].soft_limit_bytes = soft;
            server.client_obuf_limits[class].soft_limit_seconds = soft_seconds;
        }
        sdsfreesplitres(v,vlen);
#if 0
    } else if (!strcasecmp(c->argv[2]->ptr,"stop-writes-on-bgsave-error")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.stop_writes_on_bgsave_err = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-ping-slave-period")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll <= 0) goto badfmt;
        server.repl_ping_slave_period = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-timeout")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll <= 0) goto badfmt;
        server.repl_timeout = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-backlog-size")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll <= 0) goto badfmt;
        resizeReplicationBacklog(ll);
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-backlog-ttl")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        server.repl_backlog_time_limit = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"watchdog-period")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR || ll < 0) goto badfmt;
        if (ll)
            enableWatchdog(ll);
        else
            disableWatchdog();
    } else if (!strcasecmp(c->argv[2]->ptr,"rdbcompression")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.rdb_compression = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"notify-keyspace-events")) {
        int flags = keyspaceEventsStringToFlags(o->ptr);

        if (flags == -1) goto badfmt;
        server.notify_keyspace_events = flags;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-disable-tcp-nodelay")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.repl_disable_tcp_nodelay = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-diskless-sync")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.repl_diskless_sync = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"repl-diskless-sync-delay")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.repl_diskless_sync_delay = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"slave-priority")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.slave_priority = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"min-slaves-to-write")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.repl_min_slaves_to_write = ll;
        refreshGoodSlavesCount();
    } else if (!strcasecmp(c->argv[2]->ptr,"min-slaves-max-lag")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.repl_min_slaves_max_lag = ll;
        refreshGoodSlavesCount();
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-require-full-coverage")) {
        int yn = yesnotoi(o->ptr);

        if (yn == -1) goto badfmt;
        server.cluster_require_full_coverage = yn;
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-node-timeout")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll <= 0) goto badfmt;
        server.cluster_node_timeout = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-migration-barrier")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.cluster_migration_barrier = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-slave-validity-factor")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll < 0) goto badfmt;
        server.cluster_slave_validity_factor = ll;
#endif
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-heartbeat-interval")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll <= 0) goto badfmt;
        server.cluster->heartbeat_interval = ll;
    } else if (!strcasecmp(c->argv[2]->ptr,"cluster-election-timeout")) {
        if (getLongLongFromObject(o,&ll) == PREZ_ERR ||
            ll <= 0) goto badfmt;
        server.cluster->election_timeout = ll;
    } else {
        addReplyErrorFormat(c,"Unsupported CONFIG parameter: %s",
            (char*)c->argv[2]->ptr);
        return;
    }
    addReply(c,shared.ok);
    return;

badfmt: /* Bad format errors */
    addReplyErrorFormat(c,"Invalid argument '%s' for CONFIG SET '%s'",
            (char*)o->ptr,
            (char*)c->argv[2]->ptr);
}

/*-----------------------------------------------------------------------------
 * CONFIG GET implementation
 *----------------------------------------------------------------------------*/

#define config_get_string_field(_name,_var) do { \
    if (stringmatch(pattern,_name,0)) { \
        addReplyBulkCString(c,_name); \
        addReplyBulkCString(c,_var ? _var : ""); \
        matches++; \
    } \
} while(0);

#define config_get_bool_field(_name,_var) do { \
    if (stringmatch(pattern,_name,0)) { \
        addReplyBulkCString(c,_name); \
        addReplyBulkCString(c,_var ? "yes" : "no"); \
        matches++; \
    } \
} while(0);

#define config_get_numerical_field(_name,_var) do { \
    if (stringmatch(pattern,_name,0)) { \
        ll2string(buf,sizeof(buf),_var); \
        addReplyBulkCString(c,_name); \
        addReplyBulkCString(c,buf); \
        matches++; \
    } \
} while(0);

void configGetCommand(prezClient *c) {
    robj *o = c->argv[2];
    void *replylen = addDeferredMultiBulkLength(c);
    char *pattern = o->ptr;
    char buf[128];
    int matches = 0;
    prezAssertWithInfo(c,o,sdsEncodedObject(o));

    /* String values */
#if 0
    config_get_string_field("dbfilename",server.rdb_filename);
    config_get_string_field("requirepass",server.requirepass);
    config_get_string_field("masterauth",server.masterauth);
#endif
    config_get_string_field("unixsocket",server.unixsocket);
    config_get_string_field("logfile",server.logfile);
    config_get_string_field("pidfile",server.pidfile);

    /* Numerical values */
#if 0
    config_get_numerical_field("maxmemory",server.maxmemory);
    config_get_numerical_field("maxmemory-samples",server.maxmemory_samples);
#endif
    config_get_numerical_field("timeout",server.maxidletime);
    config_get_numerical_field("tcp-keepalive",server.tcpkeepalive);
#if 0
    config_get_numerical_field("auto-aof-rewrite-percentage",
            server.aof_rewrite_perc);
    config_get_numerical_field("auto-aof-rewrite-min-size",
            server.aof_rewrite_min_size);
    config_get_numerical_field("hash-max-ziplist-entries",
            server.hash_max_ziplist_entries);
    config_get_numerical_field("hash-max-ziplist-value",
            server.hash_max_ziplist_value);
    config_get_numerical_field("list-max-ziplist-entries",
            server.list_max_ziplist_entries);
    config_get_numerical_field("list-max-ziplist-value",
            server.list_max_ziplist_value);
    config_get_numerical_field("set-max-intset-entries",
            server.set_max_intset_entries);
    config_get_numerical_field("zset-max-ziplist-entries",
            server.zset_max_ziplist_entries);
    config_get_numerical_field("zset-max-ziplist-value",
            server.zset_max_ziplist_value);
    config_get_numerical_field("hll-sparse-max-bytes",
            server.hll_sparse_max_bytes);
    config_get_numerical_field("lua-time-limit",server.lua_time_limit);
    config_get_numerical_field("slowlog-log-slower-than",
            server.slowlog_log_slower_than);
    config_get_numerical_field("latency-monitor-threshold",
            server.latency_monitor_threshold);
    config_get_numerical_field("slowlog-max-len",
            server.slowlog_max_len);
#endif
    config_get_numerical_field("port",server.port);
    config_get_numerical_field("tcp-backlog",server.tcp_backlog);
#if 0
    config_get_numerical_field("databases",server.dbnum);
    config_get_numerical_field("repl-ping-slave-period",server.repl_ping_slave_period);
    config_get_numerical_field("repl-timeout",server.repl_timeout);
    config_get_numerical_field("repl-backlog-size",server.repl_backlog_size);
    config_get_numerical_field("repl-backlog-ttl",server.repl_backlog_time_limit);
#endif
    config_get_numerical_field("maxclients",server.maxclients);
#if 0
    config_get_numerical_field("watchdog-period",server.watchdog_period);
    config_get_numerical_field("slave-priority",server.slave_priority);
    config_get_numerical_field("min-slaves-to-write",server.repl_min_slaves_to_write);
    config_get_numerical_field("min-slaves-max-lag",server.repl_min_slaves_max_lag);
#endif
    config_get_numerical_field("hz",server.hz);
    config_get_numerical_field("cluster-election-timeout",server.cluster->election_timeout);
    config_get_numerical_field("cluster-heartbeat-interval",server.cluster->heartbeat_interval);
#if 0
    config_get_numerical_field("cluster-node-timeout",server.cluster_node_timeout);
    config_get_numerical_field("cluster-migration-barrier",server.cluster_migration_barrier);
    config_get_numerical_field("cluster-slave-validity-factor",server.cluster_slave_validity_factor);
    config_get_numerical_field("repl-diskless-sync-delay",server.repl_diskless_sync_delay);

    /* Bool (yes/no) values */
    config_get_bool_field("cluster-require-full-coverage",
            server.cluster_require_full_coverage);
    config_get_bool_field("no-appendfsync-on-rewrite",
            server.aof_no_fsync_on_rewrite);
    config_get_bool_field("slave-serve-stale-data",
            server.repl_serve_stale_data);
    config_get_bool_field("slave-read-only",
            server.repl_slave_ro);
    config_get_bool_field("stop-writes-on-bgsave-error",
            server.stop_writes_on_bgsave_err);
    config_get_bool_field("daemonize", server.daemonize);
    config_get_bool_field("rdbcompression", server.rdb_compression);
    config_get_bool_field("rdbchecksum", server.rdb_checksum);
    config_get_bool_field("activerehashing", server.activerehashing);
    config_get_bool_field("repl-disable-tcp-nodelay",
            server.repl_disable_tcp_nodelay);
    config_get_bool_field("repl-diskless-sync",
            server.repl_diskless_sync);
    config_get_bool_field("aof-rewrite-incremental-fsync",
            server.aof_rewrite_incremental_fsync);
    config_get_bool_field("aof-load-truncated",
            server.aof_load_truncated);

    /* Everything we can't handle with macros follows. */

    if (stringmatch(pattern,"appendonly",0)) {
        addReplyBulkCString(c,"appendonly");
        addReplyBulkCString(c,server.aof_state == PREZ_AOF_OFF ? "no" : "yes");
        matches++;
    }
    if (stringmatch(pattern,"dir",0)) {
        char buf[1024];

        if (getcwd(buf,sizeof(buf)) == NULL)
            buf[0] = '\0';

        addReplyBulkCString(c,"dir");
        addReplyBulkCString(c,buf);
        matches++;
    }
    if (stringmatch(pattern,"maxmemory-policy",0)) {
        char *s;

        switch(server.maxmemory_policy) {
        case PREZ_MAXMEMORY_VOLATILE_LRU: s = "volatile-lru"; break;
        case PREZ_MAXMEMORY_VOLATILE_TTL: s = "volatile-ttl"; break;
        case PREZ_MAXMEMORY_VOLATILE_RANDOM: s = "volatile-random"; break;
        case PREZ_MAXMEMORY_ALLKEYS_LRU: s = "allkeys-lru"; break;
        case PREZ_MAXMEMORY_ALLKEYS_RANDOM: s = "allkeys-random"; break;
        case PREZ_MAXMEMORY_NO_EVICTION: s = "noeviction"; break;
        default: s = "unknown"; break; /* too harmless to panic */
        }
        addReplyBulkCString(c,"maxmemory-policy");
        addReplyBulkCString(c,s);
        matches++;
    }
    if (stringmatch(pattern,"appendfsync",0)) {
        char *policy;

        switch(server.aof_fsync) {
        case AOF_FSYNC_NO: policy = "no"; break;
        case AOF_FSYNC_EVERYSEC: policy = "everysec"; break;
        case AOF_FSYNC_ALWAYS: policy = "always"; break;
        default: policy = "unknown"; break; /* too harmless to panic */
        }
        addReplyBulkCString(c,"appendfsync");
        addReplyBulkCString(c,policy);
        matches++;
    }
    if (stringmatch(pattern,"save",0)) {
        sds buf = sdsempty();
        int j;

        for (j = 0; j < server.saveparamslen; j++) {
            buf = sdscatprintf(buf,"%jd %d",
                    (intmax_t)server.saveparams[j].seconds,
                    server.saveparams[j].changes);
            if (j != server.saveparamslen-1)
                buf = sdscatlen(buf," ",1);
        }
        addReplyBulkCString(c,"save");
        addReplyBulkCString(c,buf);
        sdsfree(buf);
        matches++;
    }
#endif
    if (stringmatch(pattern,"loglevel",0)) {
        char *s;

        switch(server.verbosity) {
        case PREZ_WARNING: s = "warning"; break;
        case PREZ_VERBOSE: s = "verbose"; break;
        case PREZ_NOTICE: s = "notice"; break;
        case PREZ_DEBUG: s = "debug"; break;
        default: s = "unknown"; break; /* too harmless to panic */
        }
        addReplyBulkCString(c,"loglevel");
        addReplyBulkCString(c,s);
        matches++;
    }
    if (stringmatch(pattern,"client-output-buffer-limit",0)) {
        sds buf = sdsempty();
        int j;

        for (j = 0; j < PREZ_CLIENT_TYPE_COUNT; j++) {
            buf = sdscatprintf(buf,"%s %llu %llu %ld",
                    getClientTypeName(j),
                    server.client_obuf_limits[j].hard_limit_bytes,
                    server.client_obuf_limits[j].soft_limit_bytes,
                    (long) server.client_obuf_limits[j].soft_limit_seconds);
            if (j != PREZ_CLIENT_TYPE_COUNT-1)
                buf = sdscatlen(buf," ",1);
        }
        addReplyBulkCString(c,"client-output-buffer-limit");
        addReplyBulkCString(c,buf);
        sdsfree(buf);
        matches++;
    }
    if (stringmatch(pattern,"unixsocketperm",0)) {
        char buf[32];
        snprintf(buf,sizeof(buf),"%o",server.unixsocketperm);
        addReplyBulkCString(c,"unixsocketperm");
        addReplyBulkCString(c,buf);
        matches++;
    }
#if 0
    if (stringmatch(pattern,"slaveof",0)) {
        char buf[256];

        addReplyBulkCString(c,"slaveof");
        if (server.masterhost)
            snprintf(buf,sizeof(buf),"%s %d",
                server.masterhost, server.masterport);
        else
            buf[0] = '\0';
        addReplyBulkCString(c,buf);
        matches++;
    }
    if (stringmatch(pattern,"notify-keyspace-events",0)) {
        robj *flagsobj = createObject(PREZ_STRING,
            keyspaceEventsFlagsToString(server.notify_keyspace_events));

        addReplyBulkCString(c,"notify-keyspace-events");
        addReplyBulk(c,flagsobj);
        decrRefCount(flagsobj);
        matches++;
    }
    if (stringmatch(pattern,"bind",0)) {
        sds aux = sdsjoin(server.bindaddr,server.bindaddr_count," ");

        addReplyBulkCString(c,"bind");
        addReplyBulkCString(c,aux);
        sdsfree(aux);
        matches++;
    }
#endif
    setDeferredMultiBulkLength(c,replylen,matches*2);
}


/*-----------------------------------------------------------------------------
 * CONFIG command entry point
 *----------------------------------------------------------------------------*/

void configCommand(prezClient *c, robj **argv, int argc) {
    if (!strcasecmp(c->argv[1]->ptr,"set")) {
        if (c->argc != 4) goto badarity;
        configSetCommand(c);
    } else if (!strcasecmp(c->argv[1]->ptr,"get")) {
        if (c->argc != 3) goto badarity;
        configGetCommand(c);
    } else if (!strcasecmp(c->argv[1]->ptr,"resetstat")) {
        if (c->argc != 2) goto badarity;
        resetServerStats();
        //resetCommandTableStats();
        addReply(c,shared.ok);
#if 0
    } else if (!strcasecmp(c->argv[1]->ptr,"rewrite")) {
        if (c->argc != 2) goto badarity;
        if (server.configfile == NULL) {
            addReplyError(c,"The server is running without a config file");
            return;
        }
        if (rewriteConfig(server.configfile) == -1) {
            redisLog(REDIS_WARNING,"CONFIG REWRITE failed: %s", strerror(errno));
            addReplyErrorFormat(c,"Rewriting config file: %s", strerror(errno));
        } else {
            redisLog(REDIS_WARNING,"CONFIG REWRITE executed with success.");
            addReply(c,shared.ok);
        }
#endif
    } else {
        addReplyError(c,
            "CONFIG subcommand must be one of GET, SET, RESETSTAT, REWRITE");
    }
    return;

badarity:
    addReplyErrorFormat(c,"Wrong number of arguments for CONFIG %s",
        (char*) c->argv[1]->ptr);
}
