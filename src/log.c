/*
 * Copyright (c) 2014, Sureshkumar Nedunchezhian.
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
 *   * Neither the name of prez nor the names of its contributors may be used
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

#include <signal.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>

/* Log file loading */

/* Load the log file and read the log entries 
 * */
int loadLogFile(void) {
    FILE *fp;
    struct prez_stat sb;

    server.cluster->log_fd = open(server.cluster->log_filename,
            O_WRONLY|O_APPEND|O_CREAT,0644);
    if (server.cluster->log_fd == -1) {
        prezLog(PREZ_WARNING,"Prez can't open the log file: %s",strerror(errno));
        return PREZ_ERR;
    }
    fp = fopen(server.cluster->log_filename, "r");
    if (fp && prez_fstat(fileno(fp),&sb) != -1 && sb.st_size == 0) {
        server.cluster->log_current_size = 0;
        fclose(fp);
        return PREZ_ERR;
    }

    if (fp == NULL) {
        prezLog(PREZ_WARNING,"Fatal error: can't open log file for reading: %s",strerror(errno));
        exit(1);
    }

    while(1) {
        int argc, j, ok;
        unsigned long len;
        char buf[128];
        sds argsds;
        logEntryNode *entry;

        if (fgets(buf,sizeof(buf),fp) == NULL) {
            if (feof(fp))
                break;
            else
                goto readerr;
        }
        if (buf[0] != '*') goto fmterr;
        argc = atoi(buf+1);
        if (argc < 1) goto fmterr;

        entry = zmalloc(sizeof(entry));
        for (j = 0; j < argc; j++) {
            if (fgets(buf,sizeof(buf),fp) == NULL) goto readerr;
            if (buf[0] != '$') goto fmterr;
            len = strtol(buf+1,NULL,10);
            argsds = sdsnewlen(NULL,len);
            if (len && fread(argsds,len,1,fp) == 0) goto fmterr;

            switch(j) {
                case LOG_TYPE_INDEX:
                    ok = string2ll(argsds,len,&entry->log_entry.index);
                    if (!ok) goto readerr;
                    break;
                case LOG_TYPE_TERM:
                    ok = string2ll(argsds,len,&entry->log_entry.term);
                    if (!ok) goto readerr;
                    break;
                case LOG_TYPE_COMMANDNAME:
                    memcpy(entry->log_entry.commandName, argsds, sizeof(entry->log_entry.commandName));
                    break;
                case LOG_TYPE_COMMAND:
                    memcpy(entry->log_entry.command, argsds, sizeof(entry->log_entry.command));
                    break;
                default:
                    goto fmterr;
                    break;
            }
            if (fread(buf,2,1,fp) == 0) goto fmterr; /* discard CRLF */
        }
        //FIXME: fill Position
        listAddNodeTail(server.cluster->log_entries, entry);
    }

    fclose(fp);
    return PREZ_OK;

readerr:
    if (feof(fp)) {
        prezLog(PREZ_WARNING,"Unexpected end of file reading the prez log file");
    } else {
        prezLog(PREZ_WARNING,"Unrecoverable error reading the prez log file: %s", strerror(errno));
    }
    exit(1);
fmterr:
    prezLog(PREZ_WARNING,"Bad file format reading the prez log file");
    exit(1);
}

int logTruncate(long long index, long long term) {
    logEntryNode *entry;
    listNode *ln;
    listIter li;

    if (index < server.cluster->commit_index) {
        prezLog(PREZ_NOTICE, "Index is already committed. "
                "committed:%lld index:%lld term:%lld", 
                server.cluster->commit_index, index, term);
        return PREZ_ERR;
    }

    if (index > server.cluster->start_index + listLength(server.cluster->log_entries)) {
        prezLog(PREZ_NOTICE, "Index doesn't exist. length:%lu index:%lld term:%lld", 
                listLength(server.cluster->log_entries), index, term);
        return PREZ_ERR;
    }

    if (index == server.cluster->start_index) {
        prezLog(PREZ_DEBUG, "Clear log");
        ftruncate(server.cluster->log_fd, 0);
        listRelease(server.cluster->log_entries);
        server.cluster->log_entries = listCreate();
        server.cluster->log_current_size = 0;
    } else {
        entry = listNodeValue(listIndex(server.cluster->log_entries, 
                    index - server.cluster->start_index-1));
        if (entry->log_entry.term != term) {
            prezLog(PREZ_NOTICE, "Entry at index doesn't match term. index %lld term %lld",
                    index, term);
            return PREZ_ERR;
        }

        entry = listNodeValue(listIndex(server.cluster->log_entries, 
                    index - server.cluster->start_index));
        ftruncate(server.cluster->log_fd, entry->position);
        server.cluster->log_current_size = entry->position;
        listRewind(server.cluster->log_entries, &li);
        li.next = listIndex(server.cluster->log_entries,
                index - server.cluster->start_index - 1);
        while ((ln = listNext(&li)) != NULL) {
            logEntryNode *le = listNodeValue(ln);
            listDelNode(server.cluster->log_entries,ln);
            zfree(le);
        }
    }
    return PREZ_OK;
}

sds catLogEntry(sds dst, int argc, robj **argv) {
    char buf[32];
    int len, j;
    robj *o;

    buf[0] = '*';
    len = 1+ll2string(buf+1,sizeof(buf)-1,argc);
    buf[len++] = '\r';
    buf[len++] = '\n';
    dst = sdscatlen(dst,buf,len);

    for (j = 0; j < argc; j++) {
        o = getDecodedObject(argv[j]);
        buf[0] = '$';
        len = 1+ll2string(buf+1,sizeof(buf)-1,sdslen(o->ptr));
        buf[len++] = '\r';
        buf[len++] = '\n';
        dst = sdscatlen(dst,buf,len);
        dst = sdscatlen(dst,o->ptr,sdslen(o->ptr));
        dst = sdscatlen(dst,"\r\n",2);
        decrRefCount(o);
    }
    return dst;
}

int logWriteEntry(logEntry *e) {
    ssize_t nwritten;
    robj *argv[4];
    sds buf = sdsempty();
    logEntryNode *en;

    if (listLength(server.cluster->log_entries) > 0) {
        en = listNodeValue(listIndex(server.cluster->log_entries,-1));
        if (e->term < en->log_entry.term) {
            prezLog(PREZ_NOTICE, "Cannot append entry with earlier term." 
                    "term:%lld index:%lld, last term:%lld index:%lld",
                    e->term, e->index, en->log_entry.term, en->log_entry.index);
            return PREZ_ERR;
        } else if (e->term == en->log_entry.term && e->index <= en->log_entry.index) {
            prezLog(PREZ_NOTICE, "Cannot append entry with earlier index." 
                    "term:%lld index:%lld, last term:%lld index:%lld",
                    e->term, e->index, en->log_entry.term, en->log_entry.index);
            return PREZ_ERR;
        }
    }

    argv[0] = createStringObjectFromLongLong(ntohl(e->index));
    argv[1] = createStringObjectFromLongLong(ntohl(e->term));
    argv[2] = createStringObject(e->commandName, PREZ_COMMAND_NAMELEN);
    argv[3] = createStringObject(e->command, PREZ_COMMAND_NAMELEN);
    buf = catLogEntry(buf, 4, argv);
    decrRefCount(argv[0]);
    decrRefCount(argv[1]);
    decrRefCount(argv[2]);
    decrRefCount(argv[3]);
    nwritten = write(server.cluster->log_fd,buf,sdslen(buf));
    if (nwritten != (signed)sdslen(buf)) {
        prezLog(PREZ_NOTICE,"log write incomplete");
    }
    
    en = zmalloc(sizeof(en));
    memcpy(&(en->log_entry),e,sizeof(logEntry));
    en->position = server.cluster->log_current_size;
    server.cluster->log_current_size += sdslen(buf);
    listAddNodeTail(server.cluster->log_entries,en);

    return PREZ_OK;
}

int logAppendEntries(clusterMsgDataAppendEntries entries) {
    int i=0;
    logEntry *e;

    e = (logEntry*) entries.log_entries;
    for(i=0;i<ntohs(entries.log_entries_count);i++) {
        if(logWriteEntry(e)) {
            prezLog(PREZ_NOTICE, "log write error");
            return PREZ_ERR;
        }
        e++;
    }
    if(fsync(server.cluster->log_fd) == -1) return PREZ_ERR;
    return PREZ_OK;
}

int logCommitIndex(long long index) {
    int i;

    if (index > server.cluster->start_index+
            listLength(server.cluster->log_entries)) {
        prezLog(PREZ_DEBUG,"commit index %lld set back to %lu",
                index, listLength(server.cluster->log_entries));
        index = server.cluster->start_index+
            listLength(server.cluster->log_entries);
    }
    if (index < server.cluster->commit_index) return PREZ_OK;

    for (i=server.cluster->commit_index+1;i<=index;i++) {
        int eindex = i-1-server.cluster->start_index;
        logEntryNode *entry = listNodeValue(listIndex(
                    server.cluster->log_entries, eindex));
        server.cluster->commit_index = entry->log_entry.index;

        /* Process command which is what commit is really about */
        /* return if join command */
    }
    return PREZ_OK;
}

int logSync(void) {
    if(fsync(server.cluster->log_fd) == -1) return PREZ_ERR;
    return PREZ_OK;
}
