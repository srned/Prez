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

#include <time.h>
#include <signal.h>
#include <sys/wait.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/uio.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <locale.h>

/* Our shared "common" objects */

struct sharedObjectsStruct shared;

/* ===================== Globals ================================*/

struct prezServer server;
struct prezCommand *commandTable;

/* Return the UNIX time in microseconds */
long long ustime(void) {
    struct timeval tv;
    long long ust;

    gettimeofday(&tv, NULL);
    ust = ((long long)tv.tv_sec)*1000000;
    ust += tv.tv_usec;
    return ust;
}

/* Return the UNIX time in milliseconds */
long long mstime(void) {
    return ustime()/1000;
}

/*====================== Hash table type implementation  ==================== */

/* This is a hash table type that uses the SDS dynamic strings library as
 * keys and radis objects as values (objects can hold SDS strings,
 * lists, sets). */

int dictSdsKeyCompare(void *privdata, const void *key1,
        const void *key2)
{
    int l1,l2;
    DICT_NOTUSED(privdata);

    l1 = sdslen((sds)key1);
    l2 = sdslen((sds)key2);
    if (l1 != l2) return 0;
    return memcmp(key1, key2, l1) == 0;
}

/* A case insensitive version used for the command lookup table and other
 * places where case insensitive non binary-safe comparison is needed. */
int dictSdsKeyCaseCompare(void *privdata, const void *key1,
        const void *key2)
{
    DICT_NOTUSED(privdata);

    return strcasecmp(key1, key2) == 0;
}

void dictSdsDestructor(void *privdata, void *val)
{
    DICT_NOTUSED(privdata);

    sdsfree(val);
}

unsigned int dictSdsHash(const void *key) {
    return dictGenHashFunction((unsigned char*)key, sdslen((char*)key));
}

unsigned int dictSdsCaseHash(const void *key) {
    return dictGenCaseHashFunction((unsigned char*)key, sdslen((char*)key));
}

/* Cluster nodes hash table, mapping nodes addresses 1.2.3.4:6379 to
 * clusterNode structures. */
dictType clusterNodesDictType = {
    dictSdsHash,                /* hash function */
    NULL,                       /* key dup */
    NULL,                       /* val dup */
    dictSdsKeyCompare,          /* key compare */
    dictSdsDestructor,          /* key destructor */
    NULL                        /* val destructor */
};

/* Command table. sds string -> command struct pointer. */
dictType commandTableDictType = {
    dictSdsCaseHash,           /* hash function */
    NULL,                      /* key dup */
    NULL,                      /* val dup */
    dictSdsKeyCaseCompare,     /* key compare */
    dictSdsDestructor,         /* key destructor */
    NULL                       /* val destructor */
};

/* Low level logging. To use only for very big messages, otherwise
 * prezLog() is to prefer. */
void prezLogRaw(int level, const char *msg) {
    const int syslogLevelMap[] = { LOG_DEBUG, LOG_INFO, LOG_NOTICE, LOG_WARNING };
    const char *c = ".-*#";
    FILE *fp;
    char buf[64];
    int rawmode = (level & PREZ_LOG_RAW);
    int log_to_stdout = server.logfile[0] == '\0';

    level &= 0xff; /* clear flags */
    if (level < server.verbosity) return;

    fp = log_to_stdout ? stdout : fopen(server.logfile,"a");
    if (!fp) return;

    if (rawmode) {
        fprintf(fp,"%s",msg);
    } else {
        int off;
        struct timeval tv;

        gettimeofday(&tv,NULL);
        off = strftime(buf,sizeof(buf),"%d %b %H:%M:%S.",localtime(&tv.tv_sec));
        snprintf(buf+off,sizeof(buf)-off,"%03d",(int)tv.tv_usec/1000);
        fprintf(fp,"%d: %s %c %s\n",
            (int)getpid(),buf,c[level],msg);
    }
    fflush(fp);

    if (!log_to_stdout) fclose(fp);
    if (server.syslog_enabled) syslog(syslogLevelMap[level], "%s", msg);
}

/* Like prezLogRaw() but with printf-alike support. This is the function that
 * is used across the code. The raw version is only used in order to dump
 * the INFO output on crash. */
void prezLog(int level, const char *fmt, ...) {
    va_list ap;
    char msg[PREZ_MAX_LOGMSG_LEN];

    if ((level&0xff) < server.verbosity) return;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    prezLogRaw(level,msg);
}

/* We take a cached value of the unix time in the global state because with
 * virtual memory and aging there is to store the current time in objects at
 * every object access, and accuracy is not needed. To access a global var is
 * a lot faster than calling time(NULL) */
void updateCachedTime(void) {
    server.unixtime = time(NULL);
    server.mstime = mstime();
}

/*====================== Cron Handling  ==================== */

/* This is our timer interrupt, called server.hz times per second.
 * Here is where we do a number of things that need to be done asynchronously.
 * For instance:
 *
 * - Cluster Cron (Heartbeat, Election timeout etc.)
 *
 * Everything directly called here will be called server.hz times per second,
 * so in order to throttle execution of things we want to do less frequently
 * a macro is used: run_with_period(milliseconds) { .... }
 */

int serverCron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    PREZ_NOTUSED(eventLoop);
    PREZ_NOTUSED(id);
    PREZ_NOTUSED(clientData);

    /* Update the time cache. */
    updateCachedTime();

    /* Run the Prez Cluster cron. */
    clusterCron();

    return 1000/server.hz;
}

void createPidFile(void) {
    /* Try to write the pid file in a best-effort way. */
    FILE *fp = fopen(server.pidfile,"w");
    if (fp) {
        fprintf(fp,"%d\n",(int)getpid());
        fclose(fp);
    }
}

void daemonize(void) {
    int fd;

    if (fork() != 0) exit(0); /* parent exits */
    setsid(); /* create a new session */

    /* Every output goes to /dev/null. If Prez is daemonized but
     * the 'logfile' is set to 'stdout' in the configuration file
     * it will not log at all. */
    if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) close(fd);
    }
}

void version() {
    printf("Prez server v=%s malloc=%s bits=%d \n",
        PREZ_VERSION,
        ZMALLOC_LIB,
        sizeof(long) == 4 ? 32 : 64);
    exit(0);
}

void usage() {
    fprintf(stderr,"Usage: ./prez-server [/path/to/prez.conf] [options]\n");
    fprintf(stderr,"       ./prez-server - (read config from stdin)\n");
    fprintf(stderr,"       ./prez-server -v or --version\n");
    fprintf(stderr,"       ./prez-server -h or --help\n");
    fprintf(stderr,"Examples:\n");
    fprintf(stderr,"       ./prez-server (run the server with default conf)\n");
    fprintf(stderr,"       ./prez-server /etc/prez/7981.conf\n");
    fprintf(stderr,"       ./prez-server --port 7777\n");
    fprintf(stderr,"       ./prez-server /etc/myprez.conf --loglevel verbose\n\n");
    exit(1);
}

static void sigtermHandler(int sig) {
    PREZ_NOTUSED(sig);

    //prezLogFromHandler(PREZ_WARNING,"Received SIGTERM, scheduling shutdown...");
    server.shutdown_asap = 1;
}

void setupSignalHandlers(void) {
    struct sigaction act;

    /* When the SA_SIGINFO flag is set in sa_flags then sa_sigaction is used.
     * Otherwise, sa_handler is used. */
    sigemptyset(&act.sa_mask);
    act.sa_flags = 0;
    act.sa_handler = sigtermHandler;
    sigaction(SIGTERM, &act, NULL);

#ifdef HAVE_BACKTRACE
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_RESETHAND | SA_SIGINFO;
    act.sa_sigaction = sigsegvHandler;
    sigaction(SIGSEGV, &act, NULL);
    sigaction(SIGBUS, &act, NULL);
    sigaction(SIGFPE, &act, NULL);
    sigaction(SIGILL, &act, NULL);
#endif
    return;
}

/* Initialize a set of file descriptors to listen to the specified 'port'
 * binding the addresses specified in the Redis server configuration.
 *
 * The listening file descriptors are stored in the integer array 'fds'
 * and their number is set in '*count'.
 *
 * The addresses to bind are specified in the global server.bindaddr array
 * and their number is server.bindaddr_count. If the server configuration
 * contains no specific addresses to bind, this function will try to
 * bind * (all addresses) for both the IPv4 and IPv6 protocols.
 *
 * On success the function returns PREZ_OK.
 *
 * On error the function returns PREZ_ERR. For the function to be on
 * error, at least one of the server.bindaddr addresses was
 * impossible to bind, or no bind addresses were specified in the server
 * configuration but the function is not able to bind * for at least
 * one of the IPv4 or IPv6 protocols. */
int listenToPort(int port, int *fds, int *count) {
    int j;

    /* Force binding of 0.0.0.0 if no bind address is specified, always
     * entering the loop if j == 0. */
    if (server.bindaddr_count == 0) server.bindaddr[0] = NULL;
    for (j = 0; j < server.bindaddr_count || j == 0; j++) {
        if (server.bindaddr[j] == NULL) {
            /* Bind * for both IPv6 and IPv4, we enter here only if
             * server.bindaddr_count == 0. */
            fds[*count] = anetTcp6Server(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            fds[*count] = anetTcpServer(server.neterr,port,NULL,
                server.tcp_backlog);
            if (fds[*count] != ANET_ERR) {
                anetNonBlock(NULL,fds[*count]);
                (*count)++;
            }
            /* Exit the loop if we were able to bind * on IPv4 or IPv6,
             * otherwise fds[*count] will be ANET_ERR and we'll print an
             * error and return to the caller with an error. */
            if (*count) break;
        } else if (strchr(server.bindaddr[j],':')) {
            /* Bind IPv6 address. */
            fds[*count] = anetTcp6Server(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        } else {
            /* Bind IPv4 address. */
            fds[*count] = anetTcpServer(server.neterr,port,server.bindaddr[j],
                server.tcp_backlog);
        }
        if (fds[*count] == ANET_ERR) {
            prezLog(PREZ_WARNING,
                "Creating Server TCP listening socket %s:%d: %s",
                server.bindaddr[j] ? server.bindaddr[j] : "*",
                port, server.neterr);
            return PREZ_ERR;
        }
        anetNonBlock(NULL,fds[*count]);
        (*count)++;
    }
    return PREZ_OK;
}


void initServer() {
    int j;

    signal(SIGHUP, SIG_IGN);
    signal(SIGPIPE, SIG_IGN);
    setupSignalHandlers();

    if (server.syslog_enabled) {
        openlog(server.syslog_ident, LOG_PID | LOG_NDELAY | LOG_NOWAIT,
            server.syslog_facility);
    }

    server.pid = getpid();
    server.clients = listCreate();

    //FIXME: adjustOpenFilesLimit();
    server.el = aeCreateEventLoop(server.maxclients+PREZ_EVENTLOOP_FDSET_INCR);

    /* Open the TCP listening socket for the user commands. */
    if (server.port != 0 &&
        listenToPort(server.port,server.ipfd,&server.ipfd_count) == PREZ_ERR)
        exit(1);

    /* Open the listening Unix domain socket. */
    if (server.unixsocket != NULL) {
        unlink(server.unixsocket); /* don't care if this fails */
        server.sofd = anetUnixServer(server.neterr,server.unixsocket,
            server.unixsocketperm, server.tcp_backlog);
        if (server.sofd == ANET_ERR) {
            prezLog(PREZ_WARNING, "Opening socket: %s", server.neterr);
            exit(1);
        }
        anetNonBlock(NULL,server.sofd);
    }

    /* Abort if there are no listening sockets at all. */
    if (server.ipfd_count == 0 && server.sofd < 0) {
        prezLog(PREZ_WARNING, "Configured to not listen anywhere, exiting.");
        exit(1);
    }

    /* Create the serverCron() time event, that's our main way to process
     * background operations. */
    if(aeCreateTimeEvent(server.el, 1, serverCron, NULL, NULL) == AE_ERR) {
        prezPanic("Can't create the serverCron time event.");
        exit(1);
    }

    /* Create an event handler for accepting new connections in TCP and Unix
     * domain sockets. */
    for (j = 0; j < server.ipfd_count; j++) {
        if (aeCreateFileEvent(server.el, server.ipfd[j], AE_READABLE,
            acceptTcpHandler,NULL) == AE_ERR)
            {
                prezPanic(
                    "Unrecoverable error creating server.ipfd file event.");
            }
    }
    if (server.sofd > 0 && aeCreateFileEvent(server.el,server.sofd,AE_READABLE,
        acceptUnixHandler,NULL) == AE_ERR) prezPanic("Unrecoverable error creating server.sofd file event.");

    clusterInit();
}

void populateCommandTable(void) {
    return;
}

void initServerConfig() {
    int j;

    server.name = zstrdup(PREZ_DEFAULT_NAME);
    server.configfile = NULL;
    server.hz = PREZ_DEFAULT_HZ;
    server.arch_bits = (sizeof(long) == 8) ? 64 : 32;
    server.port = PREZ_SERVERPORT;
    server.tcp_backlog = PREZ_TCP_BACKLOG;
    server.bindaddr_count = 0;
    server.unixsocket = NULL;
    server.unixsocketperm = PREZ_DEFAULT_UNIX_SOCKET_PERM;
    server.ipfd_count = 0;
    server.sofd = -1;
    server.verbosity = PREZ_DEFAULT_VERBOSITY;
    server.maxidletime = PREZ_MAXIDLETIME;
    server.tcpkeepalive = PREZ_DEFAULT_TCP_KEEPALIVE;
    server.client_max_querybuf_len = PREZ_MAX_QUERYBUF_LEN;
    server.logfile = zstrdup(PREZ_DEFAULT_LOGFILE);
    server.syslog_enabled = PREZ_DEFAULT_SYSLOG_ENABLED;
    server.syslog_ident = zstrdup(PREZ_DEFAULT_SYSLOG_IDENT);
    server.syslog_facility = LOG_LOCAL0;
    server.daemonize = PREZ_DEFAULT_DAEMONIZE;
    server.pidfile = zstrdup(PREZ_DEFAULT_PID_FILE);
    server.maxclients = PREZ_MAX_CLIENTS;
    server.shutdown_asap = 0;
    server.cport = PREZ_CLUSTERPORT;
    server.cluster_configfile = zstrdup(PREZ_DEFAULT_CLUSTER_CONFIG_FILE);
    server.next_client_id = 1; /* Client IDs, start from 1 .*/

    /* Client output buffer limits */
    for (j = 0; j < PREZ_CLIENT_TYPE_COUNT; j++)
        server.client_obuf_limits[j] = clientBufferLimitsDefaults[j];

    /* Command table -- we initiialize it here as it is part of the
     * initial configuration, since command names may be changed via
     * prez.conf using the rename-command directive. */
    server.commands = dictCreate(&commandTableDictType,NULL);
    populateCommandTable();

    /* Debugging */
    server.assert_failed = "<no assertion failed>";
    server.assert_file = "<no file>";
    server.assert_line = 0;
    server.bug_report_start = 0;
    server.watchdog_period = 0;
}

unsigned int getLRUClock(void) {
    return (mstime()/PREZ_LRU_CLOCK_RESOLUTION) & PREZ_LRU_CLOCK_MAX;
}

void prezOutOfMemoryHandler(size_t allocation_size) {
    prezLog(PREZ_WARNING,"Out Of Memory allocating %zu bytes!",
        allocation_size);
    prezPanic("prez aborting for OUT OF MEMORY");
}

int main(int argc, char **argv) {
    zmalloc_enable_thread_safeness();
    zmalloc_set_oom_handler(prezOutOfMemoryHandler);
    initServerConfig();

    if (argc >= 2) {
        int j = 1; /* First option to parse in argv[] */
        sds options = sdsempty();
        char *configfile = NULL;

        /* Handle special options --help and --version */
        if (strcmp(argv[1], "-v") == 0 ||
            strcmp(argv[1], "--version") == 0) version();
        if (strcmp(argv[1], "--help") == 0 ||
            strcmp(argv[1], "-h") == 0) usage();

        /* First argument is the config file name? */
        if (argv[j][0] != '-' || argv[j][1] != '-')
            configfile = argv[j++];
        /* All the other options are parsed and conceptually appended to the
         * configuration file. For instance --port 6380 will generate the
         * string "port 6380\n" to be parsed after the actual file name
         * is parsed, if any. */
        while(j != argc) {
            if (argv[j][0] == '-' && argv[j][1] == '-') {
                /* Option name */
                if (sdslen(options)) options = sdscat(options,"\n");
                options = sdscat(options,argv[j]+2);
                options = sdscat(options," ");
            } else {
                /* Option argument */
                options = sdscatrepr(options,argv[j],strlen(argv[j]));
                options = sdscat(options," ");
            }
            j++;
        }
        if (configfile) server.configfile = getAbsolutePath(configfile);
        loadServerConfig(configfile,options);
        sdsfree(options);
    } else {
        prezLog(PREZ_WARNING, "Warning: no config file specified, using the default config. In order to specify a config file use %s /path/to/prez.conf", argv[0]);
    }
    if (server.daemonize) daemonize();
    initServer();

    prezLog(PREZ_WARNING,"Server started, Prez version " PREZ_VERSION);

    if (server.ipfd_count > 0)
        prezLog(PREZ_NOTICE,"The server is now ready to accept connections on port %d", server.port);
    if (server.sofd > 0)
        prezLog(PREZ_NOTICE,"The server is now ready to accept connections at %s", server.unixsocket);

    aeMain(server.el);
    aeDeleteEventLoop(server.el);
    return 0;
}
