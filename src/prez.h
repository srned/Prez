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

#ifndef __PREZ_H
#define __PREZ_H

#include "fmacros.h"
#include "config.h"

#if defined(__sun)
#include "solarisfixes.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <syslog.h>
#include <netinet/in.h>
#include <signal.h>

typedef long long mstime_t; /* millisecond time type. */

#include "ae.h"      
#include "sds.h"     
#include "dict.h"    
#include "adlist.h"  
#include "zmalloc.h" 
#include "anet.h"    
#include "version.h"
#include "util.h"

#define PREZ_OK               0
#define PREZ_ERR              -1

#define PREZ_MAX_LOGMSG_LEN   1024

/* Client flags */
//FIXME: Some are not applicable to Prez. Remove them.
#define PREZ_SLAVE (1<<0)   /* This client is a slave server */
#define PREZ_MASTER (1<<1)  /* This client is a master server */
#define PREZ_MONITOR (1<<2) /* This client is a slave monitor, see MONITOR */
#define PREZ_MULTI (1<<3)   /* This client is in a MULTI context */
#define PREZ_BLOCKED (1<<4) /* The client is waiting in a blocking operation */
#define PREZ_DIRTY_CAS (1<<5) /* Watched keys modified. EXEC will fail. */
#define PREZ_CLOSE_AFTER_REPLY (1<<6) /* Close after writing entire reply. */
#define PREZ_UNBLOCKED (1<<7) /* This client was unblocked and is stored in
                                  server.unblocked_clients */
#define PREZ_LUA_CLIENT (1<<8) /* This is a non connected client used by Lua */
#define PREZ_ASKING (1<<9)     /* Client issued the ASKING command */
#define PREZ_CLOSE_ASAP (1<<10)/* Close this client ASAP */
#define PREZ_UNIX_SOCKET (1<<11) /* Client connected via Unix domain socket */
#define PREZ_DIRTY_EXEC (1<<12)  /* EXEC will fail for errors while queueing */
#define PREZ_MASTER_FORCE_REPLY (1<<13)  /* Queue replies even if is master */
#define PREZ_FORCE_AOF (1<<14)   /* Force AOF propagation of current cmd. */
#define PREZ_FORCE_REPL (1<<15)  /* Force replication of current cmd. */
#define PREZ_PRE_PSYNC (1<<16)   /* Instance don't understand PSYNC. */
#define PREZ_READONLY (1<<17)    /* Cluster client is in read-only state. */
#define PREZ_PUBSUB (1<<18)      /* Client is in Pub/Sub mode. */

/* Client request types */
#define PREZ_REQ_INLINE 1
#define PREZ_REQ_MULTIBULK 2

/* Log levels */
#define PREZ_DEBUG 0
#define PREZ_VERBOSE 1
#define PREZ_NOTICE 2
#define PREZ_WARNING 3
#define PREZ_LOG_RAW (1<<10) /* Modifier to log without timestamp */
#define PREZ_DEFAULT_VERBOSITY PREZ_DEBUG

#define PREZ_NOTUSED(V) ((void) V)

#define prezPanic(_e) _prezPanic(#_e,__FILE__,__LINE__),_exit(1)

/* Static server configuration */
#define PREZ_DEFAULT_HZ        10      /* Time interrupt calls/sec. */
#define PREZ_MIN_HZ            1
#define PREZ_MAX_HZ            500
#define PREZ_SERVERPORT   7981
#define PREZ_TCP_BACKLOG  511
#define PREZ_RUN_ID_SIZE  40
#define PREZ_BINDADDR_MAX 16
#define PREZ_DEFAULT_UNIX_SOCKET_PERM 0
#define PREZ_MAXIDLETIME       0       /* default client timeout: infinite */
#define PREZ_CONFIGLINE_MAX    1024
#define PREZ_MAX_WRITE_PER_EVENT (1024*64)
#define PREZ_SHARED_SELECT_CMDS 10
#define PREZ_SHARED_INTEGERS 10000
#define PREZ_SHARED_BULKHDR_LEN 32
#define PREZ_MAX_LOGMSG_LEN    1024 /* Default maximum length of syslog messages */
#define PREZ_MAX_CLIENTS 10000
#define PREZ_RUN_ID_SIZE 40
#define PREZ_DEFAULT_PID_FILE "/var/run/prez.pid"
#define PREZ_DEFAULT_NAME "prezserver"
#define PREZ_DEFAULT_SYSLOG_IDENT "prez"
#define PREZ_CLUSTERPORT 17981
#define PREZ_DEFAULT_CLUSTER_CONFIG_FILE "nodes.conf"
#define PREZ_DEFAULT_DAEMONIZE 0
#define PREZ_DEFAULT_UNIX_SOCKET_PERM 0
#define PREZ_DEFAULT_TCP_KEEPALIVE 0
#define PREZ_DEFAULT_LOGFILE ""
#define PREZ_DEFAULT_SYSLOG_ENABLED 0
#define PREZ_DEFAULT_MAXMEMORY 0
#define PREZ_DEFAULT_MAXMEMORY_SAMPLES 5
#define PREZ_IP_STR_LEN INET6_ADDRSTRLEN
#define PREZ_PEER_ID_LEN (PREZ_IP_STR_LEN+32) /* Must be enough for ip:port */
#define PREZ_BINDADDR_MAX 16
#define PREZ_MIN_RESERVED_FDS 32

/* Client classes for client limits, currently used only for
 * the max-client-output-buffer limit implementation. */
#define PREZ_CLIENT_TYPE_NORMAL 0 /* Normal req-reply clients */
#define PREZ_CLIENT_TYPE_COUNT 1

/* Protocol and I/O related defines */
#define PREZ_MAX_QUERYBUF_LEN  (1024*1024*1024) /* 1GB max query buffer. */
#define PREZ_IOBUF_LEN         (1024*16)  /* Generic I/O buffer size */
#define PREZ_REPLY_CHUNK_BYTES (16*1024) /* 16k output buffer */
#define PREZ_INLINE_MAX_SIZE   (1024*64) /* Max size of inline reads */
#define PREZ_MBULK_BIG_ARG     (1024*32)

/* When configuring the prez eventloop, we setup it so that the total number
 * of file descriptors we can handle are server.maxclients + RESERVED_FDS + FDSET_INCR
 * that is our safety margin. */
#define PREZ_EVENTLOOP_FDSET_INCR (PREZ_MIN_RESERVED_FDS+96)

/* Object types */
#define PREZ_STRING 0
#define PREZ_LIST 1
#define PREZ_SET 2
#define PREZ_ZSET 3
#define PREZ_HASH 4

/* Get the first bind addr or NULL */
#define PREZ_BIND_ADDR (server.bindaddr_count ? server.bindaddr[0] : NULL)

/* We can print the stacktrace, so our assert is defined this way: */
#define prezAssertWithInfo(_c,_o,_e) ((_e)?(void)0 : (_prezAssertWithInfo(_c,_o,#_e,__FILE__,__LINE__),_exit(1)))
#define prezAssert(_e) ((_e)?(void)0 : (_prezAssert(#_e,__FILE__,__LINE__),_exit(1)))
#define prezPanic(_e) _prezPanic(#_e,__FILE__,__LINE__),_exit(1)


/* Objects encoding. Some kind of objects like Strings and Hashes can be
 * internally represented in multiple ways. The 'encoding' field of the object
 * is set to one of this fields for this object. */
#define PREZ_ENCODING_RAW 0     /* Raw representation */
#define PREZ_ENCODING_INT 1     /* Encoded as integer */
#define PREZ_ENCODING_HT 2      /* Encoded as hash table */
#define PREZ_ENCODING_ZIPMAP 3  /* Encoded as zipmap */
#define PREZ_ENCODING_LINKEDLIST 4 /* Encoded as regular linked list */
#define PREZ_ENCODING_ZIPLIST 5 /* Encoded as ziplist */
#define PREZ_ENCODING_INTSET 6  /* Encoded as intset */
#define PREZ_ENCODING_SKIPLIST 7  /* Encoded as skiplist */
#define PREZ_ENCODING_EMBSTR 8  /* Embedded sds string encoding */

/* Redis maxmemory strategies */
#define PREZ_MAXMEMORY_VOLATILE_LRU 0
#define PREZ_MAXMEMORY_VOLATILE_TTL 1
#define PREZ_MAXMEMORY_VOLATILE_RANDOM 2
#define PREZ_MAXMEMORY_ALLKEYS_LRU 3
#define PREZ_MAXMEMORY_ALLKEYS_RANDOM 4
#define PREZ_MAXMEMORY_NO_EVICTION 5
#define PREZ_DEFAULT_MAXMEMORY_POLICY PREZ_MAXMEMORY_NO_EVICTION

/*-----------------------------------------------------------------------------
 * Data types
 *----------------------------------------------------------------------------*/

/* A prez object, that is a type able to hold a string / list / set */

/* The actual prez Object */
#define PREZ_LRU_BITS 24
#define PREZ_LRU_CLOCK_MAX ((1<<PREZ_LRU_BITS)-1) /* Max value of obj->lru */
#define PREZ_LRU_CLOCK_RESOLUTION 1000 /* LRU clock resolution in ms */
typedef struct prezObject {
    unsigned type:4;
    unsigned encoding:4;
    unsigned lru:PREZ_LRU_BITS; /* lru time (relative to server.lruclock) */
    int refcount;
    void *ptr;
} robj;

/* Macro used to obtain the current LRU clock.
 * If the current resolution is lower than the frequency we refresh the
 * LRU clock (as it should be in production servers) we return the
 * precomputed value, otherwise we need to resort to a function call. */
#define LRU_CLOCK() ((1000/server.hz <= PREZ_LRU_CLOCK_RESOLUTION) ? server.lruclock : getLRUClock())

/* Macro used to initialize a prez object allocated on the stack.
 * Note that this macro is taken near the structure definition to make sure
 * we'll update it when the structure is changed, to avoid bugs like
 * bug #85 introduced exactly in this way. */
#define initStaticStringObject(_var,_ptr) do { \
    _var.refcount = 1; \
    _var.type = PREZ_STRING; \
    _var.encoding = PREZ_ENCODING_RAW; \
    _var.ptr = _ptr; \
} while(0);

/* To improve the quality of the LRU approximation we take a set of keys
 * that are good candidate for eviction across freeMemoryIfNeeded() calls.
 *
 * Entries inside the eviciton pool are taken ordered by idle time, putting
 * greater idle times to the right (ascending order).
 *
 * Empty entries have the key pointer set to NULL. */
#define PREZ_EVICTION_POOL_SIZE 16
struct evictionPoolEntry {
    unsigned long long idle;    /* Object idle time. */
    sds key;                    /* Key name. */
};


/*-----------------------------------------------------------------------------
 * Global server state
 *----------------------------------------------------------------------------*/
typedef struct clientBufferLimitsConfig {
    unsigned long long hard_limit_bytes;
    unsigned long long soft_limit_bytes;
    time_t soft_limit_seconds;
} clientBufferLimitsConfig;

/* With multiplexing we need to take per-client state.
 * Clients are taken in a liked list. */
typedef struct prezClient {
    uint64_t id;            /* Client incremental unique ID. */
    int fd;
    robj *name;             /* As set by CLIENT SETNAME */
    sds querybuf;
    size_t querybuf_peak;   /* Recent (100ms or more) peak of querybuf size */
    int argc;
    robj **argv;
    struct prezCommand *cmd, *lastcmd;
    int reqtype;
    int multibulklen;       /* number of multi bulk arguments left to read */
    long bulklen;           /* length of bulk argument in multi bulk request */
    list *reply;
    unsigned long reply_bytes; /* Tot bytes of objects in reply list */
    int sentlen;            /* Amount of bytes already sent in the current
                               buffer or object being sent. */
    time_t ctime;           /* Client creation time */
    time_t lastinteraction; /* time of the last interaction, used for timeout */
    time_t obuf_soft_limit_reached_time;
    int flags;              
    int authenticated;      /* when requirepass is non-NULL */
    sds peerid;             /* Cached peer ID. */

    /* Response buffer */
    int bufpos;
    char buf[PREZ_REPLY_CHUNK_BYTES];
} prezClient;

struct sharedObjectsStruct {
    robj *crlf, *ok, *err, *emptybulk, *czero, *cone, *cnegone, *pong, *space,
    *colon, *nullbulk, *nullmultibulk, *queued,
    *emptymultibulk, *wrongtypeerr, *nokeyerr, *syntaxerr, *sameobjecterr,
    *outofrangeerr, *noscripterr, *loadingerr, *slowscripterr, *bgsaveerr,
    *masterdownerr, *roslaveerr, *execaborterr, *noautherr, *noreplicaserr,
    *busykeyerr, *oomerr, *plus, *messagebulk, *pmessagebulk, *subscribebulk,
    *unsubscribebulk, *psubscribebulk, *punsubscribebulk, *del, *rpop, *lpop,
    *lpush, *emptyscan, *minstring, *maxstring,
    *select[PREZ_SHARED_SELECT_CMDS],
    *integers[PREZ_SHARED_INTEGERS],
    *mbulkhdr[PREZ_SHARED_BULKHDR_LEN], /* "*<value>\r\n" */
    *bulkhdr[PREZ_SHARED_BULKHDR_LEN];  /* "$<value>\r\n" */
};

extern clientBufferLimitsConfig clientBufferLimitsDefaults[PREZ_CLIENT_TYPE_COUNT];

struct clusterState;

struct prezServer {
    /* General */
    pid_t pid;                  /* Main process pid. */
    char *name;                 /* node name */
    char *configfile;           /* Absolute config file path, or NULL */
    int hz;                     /* call frequency in hertz */
    dict *commands;             /* Command table */
    aeEventLoop *el;
    unsigned lruclock:PREZ_LRU_BITS; /* Clock for LRU eviction */
    int shutdown_asap;          /* SHUTDOWN needed ASAP */
    char *pidfile;              /* PID file path */
    int arch_bits;              /* 32 or 64 depending on sizeof(long) */
    char runid[PREZ_RUN_ID_SIZE+1];  /* ID always different at every exec. */
    /* Networking */
    int port;                   /* TCP listening port */
    int tcp_backlog;            /* TCP listen() backlog */
    char *bindaddr[PREZ_BINDADDR_MAX]; /* Addresses we should bind to */
    int bindaddr_count;         /* Number of addresses in server.bindaddr[] */
    char *unixsocket;           /* UNIX socket path */
    mode_t unixsocketperm;      /* UNIX socket permission */
    int ipfd[PREZ_BINDADDR_MAX]; /* TCP socket file descriptors */
    int ipfd_count;             /* Used slots in ipfd[] */
    int sofd;                   /* Unix socket file descriptor */
    int cport;                  /* Cluster listening port */
    int cfd[PREZ_BINDADDR_MAX]; /* Cluster bus listening socket */
    int cfd_count;              /* Used slots in cfd[] */
    list *clients;              /* List of active clients */
    list *clients_to_close;     /* Clients to close asynchronously */
    prezClient *current_client; /* Current client, only used on crash report */
    char neterr[ANET_ERR_LEN];   /* Error buffer for anet.c */
    dict *migrate_cached_sockets;/* MIGRATE cached sockets */
    uint64_t next_client_id;    /* Next client unique ID. Incremental. */
     /* Fields used only for stats */
    time_t stat_starttime;          /* Server start time */
    long long stat_numcommands;     /* Number of processed commands */
    long long stat_numconnections;  /* Number of connections received */
    long long stat_rejected_conn;   /* Clients rejected because of maxclients */
    /* Configuration */
    int verbosity;                  /* Loglevel in prez.conf */
    int maxidletime;                /* Client timeout in seconds */
    int tcpkeepalive;               /* Set SO_KEEPALIVE if non-zero. */
    size_t client_max_querybuf_len; /* Limit for client query buffer length */
    int daemonize;                  /* True if running as a daemon */
    clientBufferLimitsConfig client_obuf_limits[PREZ_CLIENT_TYPE_COUNT];
    /* Logging */
    char *logfile;                  /* Path of log file */
    int syslog_enabled;             /* Is syslog enabled? */
    char *syslog_ident;             /* Syslog ident */
    int syslog_facility;            /* Syslog facility */
     /* Limits */
    int maxclients;                 /* Max number of simultaneous clients */
    unsigned long long maxmemory;   /* Max number of memory bytes to use */
    int maxmemory_policy;           /* Policy for key eviction */
    int maxmemory_samples;          /* Pricision of random sampling */

    time_t unixtime;        /* Unix time sampled every cron cycle. */
    long long mstime;       /* Like 'unixtime' but with milliseconds resolution. */
 
     /* Cluster */
    mstime_t cluster_node_timeout; /* Cluster node timeout. */
    char *cluster_configfile; /* Cluster auto-generated config file name. */
    struct clusterState *cluster;  /* State of the cluster */

     /* Assert & bug reporting */
    char *assert_failed;
    char *assert_file;
    int assert_line;
    int bug_report_start; /* True if bug report header was already logged. */
    int watchdog_period;  /* Software watchdog period in ms. 0 = off */
};

typedef void prezCommandProc(prezClient *c);
typedef int *prezGetKeysProc(struct prezCommand *cmd, robj **argv, int argc, int *numkeys);
struct prezCommand {
    char *name;
    prezCommandProc *proc;
    int arity;
    int flags;    /* The actual flags, obtained from the 'sflags' field. */
    /* Use a function to determine keys arguments in a command line.
     * Used for prez Cluster redirect. */
    prezGetKeysProc *getkeys_proc;
    /* What keys should be loaded in background when calling this command? */
    int firstkey; /* The first argument that's a key (0 = no keys) */
    int lastkey;  /* The last argument that's a key */
    int keystep;  /* The step between first and last key */
    long long microseconds, calls;
};

/*-----------------------------------------------------------------------------
 * Extern declarations
 *----------------------------------------------------------------------------*/

extern struct prezServer server;
extern struct sharedObjectsStruct shared;
extern dictType clusterNodesDictType;
extern dictType clusterProcClientsDictType;

/*-----------------------------------------------------------------------------
 * Functions prototypes
 *----------------------------------------------------------------------------*/

/* Utils */
long long ustime(void);
long long mstime(void);
void getRandomHexChars(char *p, unsigned int len);
uint64_t crc64(uint64_t crc, const unsigned char *s, uint64_t l);
void exitFromChild(int retcode);

void loadServerConfig(char *filename, char *options);

/* networking.c -- Networking and Client related operations */
prezClient *createClient(int fd);
void closeTimedoutClients(void);
void freeClient(prezClient *c);
void freeClientAsync(prezClient *c);
void resetClient(prezClient *c);
void sendReplyToClient(aeEventLoop *el, int fd, void *privdata, int mask);
void addReply(prezClient *c, robj *obj);
void *addDeferredMultiBulkLength(prezClient *c);
void setDeferredMultiBulkLength(prezClient *c, void *node, long length);
void addReplySds(prezClient *c, sds s);
void processInputBuffer(prezClient *c);
void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void acceptUnixHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void readQueryFromClient(aeEventLoop *el, int fd, void *privdata, int mask);
void addReplyBulk(prezClient *c, robj *obj);
void addReplyBulkCString(prezClient *c, char *s);
void addReplyBulkCBuffer(prezClient *c, void *p, size_t len);
void addReplyBulkLongLong(prezClient *c, long long ll);
void acceptHandler(aeEventLoop *el, int fd, void *privdata, int mask);
void addReply(prezClient *c, robj *obj);
void addReplySds(prezClient *c, sds s);
void addReplyError(prezClient *c, char *err);
void addReplyStatus(prezClient *c, char *status);
void addReplyDouble(prezClient *c, double d);
void addReplyLongLong(prezClient *c, long long ll);
void addReplyMultiBulkLen(prezClient *c, long length);
void copyClientOutputBuffer(prezClient *dst, prezClient *src);
void *dupClientReplyValue(void *o);
void getClientsMaxBuffers(unsigned long *longest_output_list,
                          unsigned long *biggest_input_buffer);
void formatPeerId(char *peerid, size_t peerid_len, char *ip, int port);
char *getClientPeerId(prezClient *client);
sds catClientInfoString(sds s, prezClient *client);
sds getAllClientsInfoString(void);
void rewriteClientCommandVector(prezClient *c, int argc, ...);
void rewriteClientCommandArgument(prezClient *c, int i, robj *newval);
unsigned long getClientOutputBufferMemoryUsage(prezClient *c);
void freeClientsInAsyncFreeQueue(void);
void asyncCloseClientOnOutputBufferLimitReached(prezClient *c);
int getClientType(prezClient *c);
int getClientTypeByName(char *name);
char *getClientTypeName(int class);

void disconnectSlaves(void);
int listenToPort(int port, int *fds, int *count);
void pauseClients(mstime_t duration);
int clientsArePaused(void);
int processEventsWhileBlocked(void);

/* object implementation */
void decrRefCount(robj *o);
void decrRefCountVoid(void *o);
void incrRefCount(robj *o);
robj *resetRefCount(robj *obj);
void freeStringObject(robj *o);
void freeListObject(robj *o);
void freeSetObject(robj *o);
void freeZsetObject(robj *o);
void freeHashObject(robj *o);
robj *createObject(int type, void *ptr);
robj *createStringObject(char *ptr, size_t len);
robj *createRawStringObject(char *ptr, size_t len);
robj *createEmbeddedStringObject(char *ptr, size_t len);
robj *dupStringObject(robj *o);
int isObjectRepresentableAsLongLong(robj *o, long long *llongval);
robj *tryObjectEncoding(robj *o);
robj *getDecodedObject(robj *o);
size_t stringObjectLen(robj *o);
robj *createStringObjectFromLongLong(long long value);
robj *createStringObjectFromLongDouble(long double value);
robj *createListObject(void);
robj *createZiplistObject(void);
robj *createSetObject(void);
robj *createIntsetObject(void);
robj *createHashObject(void);
robj *createZsetObject(void);
robj *createZsetZiplistObject(void);
int getLongFromObjectOrReply(prezClient *c, robj *o, long *target, const char *msg);
int checkType(prezClient *c, robj *o, int type);
int getLongLongFromObjectOrReply(prezClient *c, robj *o, long long *target, const char *msg);
int getDoubleFromObjectOrReply(prezClient *c, robj *o, double *target, const char *msg);
int getLongLongFromObject(robj *o, long long *target);
int getLongDoubleFromObject(robj *o, long double *target);
int getLongDoubleFromObjectOrReply(prezClient *c, robj *o, long double *target, const char *msg);
char *strEncoding(int encoding);
int compareStringObjects(robj *a, robj *b);
int collateStringObjects(robj *a, robj *b);
int equalStringObjects(robj *a, robj *b);
unsigned long long estimateObjectIdleTime(robj *o);
#define sdsEncodedObject(objptr) (objptr->encoding == PREZ_ENCODING_RAW || objptr->encoding == PREZ_ENCODING_EMBSTR)


#ifdef __GNUC__
void addReplyErrorFormat(prezClient *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void addReplyStatusFormat(prezClient *c, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void addReplyErrorFormat(prezClient *c, const char *fmt, ...);
void addReplyStatusFormat(prezClient *c, const char *fmt, ...);
#endif


#ifdef __GNUC__
void prezLog(int level, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
#else
void prezLog(int level, const char *fmt, ...);
#endif

/* Core */
void call(prezClient *c);
int processCommand(prezClient *c);
struct prezCommand *lookupCommand(sds name);

/* Command Prototypes */
void getCommand(prezClient *c);
void setCommand(prezClient *c);

/* Cluster */
void clusterInit(void);
void clusterCron(void);
void clusterProcessCommand(prezClient *c);

/* Debugging stuff */
void _prezAssertWithInfo(prezClient *c, robj *o, char *estr, char *file, int line);
void _prezAssert(char *estr, char *file, int line);
void _prezPanic(char *msg, char *file, int line);
void bugReportStart(void);
void prezLogObjectDebugInfo(robj *o);
void sigsegvHandler(int sig, siginfo_t *info, void *secret);

unsigned int getLRUClock(void);

#endif
