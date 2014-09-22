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

#ifndef __PREZ_CLUSTER_H
#define __PREZ_CLUSTER_H

/*-----------------------------------------------------------------------------
 * prez cluster data structures, defines, exported API.
 *----------------------------------------------------------------------------*/

#define PREZ_CLUSTER_OK 0          /* Everything looks ok */
#define PREZ_CLUSTER_FAIL 1        /* The cluster can't work */
#define PREZ_CLUSTER_NAMELEN 40    /* sha1 hex length */
#define PREZ_COMMAND_NAMELEN 40
#define PREZ_COMMAND_LEN 40
#define PREZ_CLUSTER_PORT_INCR 10000 /* Cluster port = baseport + PORT_INCR */
#define PREZ_CLUSTER_ELECTION_TIMEOUT  5000 /* cluster election timeout of 150 ms */
#define PREZ_CLUSTER_HEARTBEAT_INTERVAL 1500 /* cluster node heartbeat interval of 50 ms */ 
#define PREZ_DEFAULT_LOG_FILENAME "prezstore.log"
#define PREZ_LOG_MAX_ENTRIES_PER_REQUEST 10

#define PREZ_FOLLOWER 0
#define PREZ_CANDIDATE 1
#define PREZ_LEADER 2

/* Cluster node flags and macros. */
#define PREZ_NODE_FAIL 8       /* The node is believed to be malfunctioning */
#define PREZ_NODE_MYSELF 16    /* This node is myself */
#define PREZ_NODE_HANDSHAKE 32 /* We have still to exchange the first ping */
#define PREZ_NODE_NOADDR   64  /* We don't know the address of this node */

#define CLUSTERMSG_TYPE_VOTEREQUEST 1
#define CLUSTERMSG_TYPE_VOTEREQUEST_RESP 2
#define CLUSTERMSG_TYPE_APPENDENTRIES 3
#define CLUSTERMSG_TYPE_APPENDENTRIES_RESP 4

#define DENY_VOTE 0
#define GRANT_VOTE 1

/*-----------------------------------------------------------------------------
 * Log Replication
 *----------------------------------------------------------------------------*/
#define LOG_TYPE_INDEX 0
#define LOG_TYPE_TERM 1
#define LOG_TYPE_COMMANDNAME 2
#define LOG_TYPE_COMMAND 3
#define LOG_TYPE_MAX 4

typedef struct logEntry {
    long long index;
    long long term;
    char commandName[PREZ_COMMAND_NAMELEN];
    char command[PREZ_COMMAND_LEN];
} logEntry;

typedef struct logEntryNode {
    logEntry log_entry;
    long position;
} logEntryNode;

struct clusterNode;

/* clusterLink encapsulates everything needed to talk with a remote node. */
typedef struct clusterLink {
    mstime_t ctime;             /* Link creation time */
    int fd;                     /* TCP socket file descriptor */
    sds sndbuf;                 /* Packet send buffer */
    sds rcvbuf;                 /* Packet reception buffer */
    struct clusterNode *node;   /* Node related to this link if any, or NULL */
} clusterLink;

struct clusterNode {
    mstime_t ctime; /* Node object creation time. */
    char name[PREZ_CLUSTER_NAMELEN]; /* Node name, hex string, sha1-size */
    int flags;
    mstime_t voted_time;     /* Last time we voted for a slave of this master */
    mstime_t last_activity_time; 
    long long prev_log_index;
    logEntryNode *last_sent_entry;
    long long last_sent_term;
    char ip[PREZ_IP_STR_LEN];  /* Latest known IP address of this node */
    int port;                   /* Latest known port of this node */
    clusterLink *link;          /* TCP/IP link with this node */
};
typedef struct clusterNode clusterNode;

typedef struct clusterState {
    clusterNode *myself;  /* This node */
    //int state;          /* PREZ_CLUSTER_OK, PREZ_CLUSTER_FAIL, ... */
    int size;             /* Num of master nodes with at least one slot */
    dict *nodes;          /* Hash table of name -> clusterNode structures */

    // Prez Specific
    int state;            /* PREZ_FOLLOWER, PREZ_CANDIDATE, PREZ_LEADER */
    sds leader;
    sds voted_for;
    int votes_granted;
    long long current_term;
    mstime_t election_timeout;
    mstime_t heartbeat_interval;
    mstime_t last_activity_time; /* Time of previous AppendEntries or VoteRequest */
    dict *synced_nodes;   /* Hash table of synced nodes name -> 1/0 */

    // Log Specific
    char *log_filename;
    int log_fd;
    off_t log_current_size;
    list *log_entries; /* list of log_entries */
    long long log_max_entries_per_request;
    long long start_index;
    long long current_index;
    long long commit_index;
    
    int todo_before_sleep; /* Things to do in clusterBeforeSleep(). */
    long long stats_bus_messages_sent;  /* Num of msg sent via cluster bus. */
    long long stats_bus_messages_received; /* Num of msg rcvd via cluster bus.*/
} clusterState;

/* Prez cluster messages header  */
typedef struct {
    long long term;
    char candidateid[PREZ_CLUSTER_NAMELEN];
    long long lastLogIndex;
    long long lastLogTerm;
} clusterMsgDataRequestVote;

typedef struct {
    long long term;
    int vote_granted;
} clusterMsgDataResponseVote;

typedef struct {
    long long term;
    char leaderid[PREZ_CLUSTER_NAMELEN];
    long long prevLogIndex;
    long long prevLogTerm;
    long long leaderCommitIndex;
    uint16_t logEntriesCount;
    logEntry logEntries[1];    //Array of N logEntry structures
} clusterMsgDataAppendEntries;

typedef struct {
    long long term;
    long long index;
    long long commitIndex;
    int ok;
} clusterMsgDataResponseAppendEntries;

union clusterMsgData {
    /* VoteRequest */
    struct {
        clusterMsgDataRequestVote vote;
    } requestvote;

    /* AppendEntries */
    struct {
        clusterMsgDataAppendEntries entries;
    } appendentries;

    /* VoteResponse */
    struct {
        clusterMsgDataResponseVote vote;
    } responsevote;

    /* AppendEntries Response */
    struct {
        clusterMsgDataResponseAppendEntries entries;
    } responseappendentries;

    struct {
        unsigned char data[2048];
    } data;

};

typedef struct {
    char sig[4];        /* Siganture "RCmb" (prez Cluster message bus). */
    uint32_t totlen;    /* Total length of this message */
    uint16_t ver;       /* Protocol version, currently set to 0. */
    uint16_t notused0;  /* 2 bytes not used. */
    uint16_t type;      /* Message type */
    uint16_t count;     /* Only used for some kind of messages. */
    char sender[PREZ_CLUSTER_NAMELEN]; /* Name of the sender node */
    char notused1[32];  /* 32 bytes reserved for future usage. */
    uint16_t port;      /* Sender TCP base port */
    union clusterMsgData data;
} clusterMsg;

#define CLUSTERMSG_MIN_LEN (sizeof(clusterMsg)-sizeof(union clusterMsgData))

void clusterProcessRequestVote(clusterLink *link, clusterMsgDataRequestVote vote);
void clusterProcessResponseVote(clusterLink *link, clusterMsgDataResponseVote vote);
void clusterProcessAppendEntries(clusterLink *link, clusterMsgDataAppendEntries entries);
void clusterProcessResponseAppendEntries(clusterLink *link, 
        clusterMsgDataResponseAppendEntries entries);
void clusterSendHeartbeat(clusterLink *link);
void clusterSendResponseVote(clusterLink *link, int vote_granted);
void clusterSendRequestVote(void);
void clusterSendAppendEntries(clusterLink *link);
void clusterSendResponseAppendEntries(clusterLink *link, int ok);

/* Log replication */
int loadLogFile(void); 
int logTruncate(long long index, long long term);
sds catLogEntry(sds dst, int argc, robj **argv);
int logWriteEntry(logEntry *e);
int logAppendEntries(clusterMsgDataAppendEntries entries);
int logCommitIndex(long long index);
int logSync(void);

/* Functions as macros */
#define quorumSize ((dictSize(server.cluster->nodes) / 2) + 1)

#endif
