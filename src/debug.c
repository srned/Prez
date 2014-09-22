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
#include "signal.h"

#ifdef HAVE_BACKTRACE
#include <execinfo.h>
#include <ucontext.h>
#include <fcntl.h>
#endif

/* =========================== Crash handling  ============================== */

void _prezAssertPrintClientInfo(prezClient *c) {
    int j;

    bugReportStart();
    prezLog(PREZ_WARNING,"=== ASSERTION FAILED CLIENT CONTEXT ===");
    //prezLog(PREZ_WARNING,"client->flags = %d", c->flags);
    prezLog(PREZ_WARNING,"client->fd = %d", c->fd);
    prezLog(PREZ_WARNING,"client->argc = %d", c->argc);
    for (j=0; j < c->argc; j++) {
        char buf[128];
        char *arg;

        if (c->argv[j]->type == PREZ_STRING && sdsEncodedObject(c->argv[j])) {
            arg = (char*) c->argv[j]->ptr;
        } else {
            snprintf(buf,sizeof(buf),"Object type: %d, encoding: %d",
                c->argv[j]->type, c->argv[j]->encoding);
            arg = buf;
        }
        prezLog(PREZ_WARNING,"client->argv[%d] = \"%s\" (refcount: %d)",
            j, arg, c->argv[j]->refcount);
    }
}

void prezLogObjectDebugInfo(robj *o) {
    prezLog(PREZ_WARNING,"Object type: %d", o->type);
    prezLog(PREZ_WARNING,"Object encoding: %d", o->encoding);
    prezLog(PREZ_WARNING,"Object refcount: %d", o->refcount);
    if (o->type == PREZ_STRING && sdsEncodedObject(o)) {
        prezLog(PREZ_WARNING,"Object raw string len: %zu", sdslen(o->ptr));
        if (sdslen(o->ptr) < 4096) {
            sds repr = sdscatrepr(sdsempty(),o->ptr,sdslen(o->ptr));
            prezLog(PREZ_WARNING,"Object raw string content: %s", repr);
            sdsfree(repr);
        }
    } /*else if (o->type == PREZ_LIST) {
        prezLog(PREZ_WARNING,"List length: %d", (int) listTypeLength(o));
    } else if (o->type == PREZ_SET) {
        prezLog(PREZ_WARNING,"Set size: %d", (int) setTypeSize(o));
    } else if (o->type == PREZ_HASH) {
        prezLog(PREZ_WARNING,"Hash size: %d", (int) hashTypeLength(o));
    } else if (o->type == PREZ_ZSET) {
        prezLog(PREZ_WARNING,"Sorted set size: %d", (int) zsetLength(o));
        if (o->encoding == PREZ_ENCODING_SKIPLIST)
            prezLog(PREZ_WARNING,"Skiplist level: %d", (int) ((zset*)o->ptr)->zsl->level);
    } */
}

void _prezAssertPrintObject(robj *o) {
    bugReportStart();
    prezLog(PREZ_WARNING,"=== ASSERTION FAILED OBJECT CONTEXT ===");
    prezLogObjectDebugInfo(o);
}

void _prezAssertWithInfo(prezClient *c, robj *o, char *estr, char *file, int line) {
    if (c) _prezAssertPrintClientInfo(c);
    if (o) _prezAssertPrintObject(o);
    _prezAssert(estr,file,line);
}
void _prezPanic(char *msg, char *file, int line) {
    bugReportStart();
    prezLog(PREZ_WARNING,"------------------------------------------------");
    prezLog(PREZ_WARNING,"!!! Software Failure. Press left mouse button to continue");
    prezLog(PREZ_WARNING,"Guru Meditation: %s #%s:%d",msg,file,line);
#ifdef HAVE_BACKTRACE
    prezLog(PREZ_WARNING,"(forcing SIGSEGV in order to print the stack trace)");
#endif
    prezLog(PREZ_WARNING,"------------------------------------------------");
    *((char*)-1) = 'x';
}

void _prezAssert(char *estr, char *file, int line) {
    bugReportStart();
    prezLog(PREZ_WARNING,"=== ASSERTION FAILED ===");
    prezLog(PREZ_WARNING,"==> %s:%d '%s' is not true",file,line,estr);
#ifdef HAVE_BACKTRACE
    server.assert_failed = estr;
    server.assert_file = file;
    server.assert_line = line;
    prezLog(PREZ_WARNING,"(forcing SIGSEGV to print the bug report.)");
#endif
    *((char*)-1) = 'x';
}


void bugReportStart(void) {
    if (server.bug_report_start == 0) {
        prezLog(PREZ_WARNING,
            "\n\n=== PREZ BUG REPORT START: Cut & paste starting from here ===");
        server.bug_report_start = 1;
    }
}

#ifdef HAVE_BACKTRACE
static void *getMcontextEip(ucontext_t *uc) {
#if defined(__APPLE__) && !defined(MAC_OS_X_VERSION_10_6)
    /* OSX < 10.6 */
    #if defined(__x86_64__)
    return (void*) uc->uc_mcontext->__ss.__rip;
    #elif defined(__i386__)
    return (void*) uc->uc_mcontext->__ss.__eip;
    #else
    return (void*) uc->uc_mcontext->__ss.__srr0;
    #endif
#elif defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)
    /* OSX >= 10.6 */
    #if defined(_STRUCT_X86_THREAD_STATE64) && !defined(__i386__)
    return (void*) uc->uc_mcontext->__ss.__rip;
    #else
    return (void*) uc->uc_mcontext->__ss.__eip;
    #endif
#elif defined(__linux__)
    /* Linux */
    #if defined(__i386__)
    return (void*) uc->uc_mcontext.gregs[14]; /* Linux 32 */
    #elif defined(__X86_64__) || defined(__x86_64__)
    return (void*) uc->uc_mcontext.gregs[16]; /* Linux 64 */
    #elif defined(__ia64__) /* Linux IA64 */
    return (void*) uc->uc_mcontext.sc_ip;
    #endif
#else
    return NULL;
#endif
}

void logStackContent(void **sp) {
    int i;
    for (i = 15; i >= 0; i--) {
        unsigned long addr = (unsigned long) sp+i;
        unsigned long val = (unsigned long) sp[i];

        if (sizeof(long) == 4)
            prezLog(PREZ_WARNING, "(%08lx) -> %08lx", addr, val);
        else
            prezLog(PREZ_WARNING, "(%016lx) -> %016lx", addr, val);
    }
}

void logRegisters(ucontext_t *uc) {
    prezLog(PREZ_WARNING, "--- REGISTERS");

/* OSX */
#if defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_6)
  /* OSX AMD64 */
    #if defined(_STRUCT_X86_THREAD_STATE64) && !defined(__i386__)
    prezLog(PREZ_WARNING,
    "\n"
    "RAX:%016lx RBX:%016lx\nRCX:%016lx RDX:%016lx\n"
    "RDI:%016lx RSI:%016lx\nRBP:%016lx RSP:%016lx\n"
    "R8 :%016lx R9 :%016lx\nR10:%016lx R11:%016lx\n"
    "R12:%016lx R13:%016lx\nR14:%016lx R15:%016lx\n"
    "RIP:%016lx EFL:%016lx\nCS :%016lx FS:%016lx  GS:%016lx",
        (unsigned long) uc->uc_mcontext->__ss.__rax,
        (unsigned long) uc->uc_mcontext->__ss.__rbx,
        (unsigned long) uc->uc_mcontext->__ss.__rcx,
        (unsigned long) uc->uc_mcontext->__ss.__rdx,
        (unsigned long) uc->uc_mcontext->__ss.__rdi,
        (unsigned long) uc->uc_mcontext->__ss.__rsi,
        (unsigned long) uc->uc_mcontext->__ss.__rbp,
        (unsigned long) uc->uc_mcontext->__ss.__rsp,
        (unsigned long) uc->uc_mcontext->__ss.__r8,
        (unsigned long) uc->uc_mcontext->__ss.__r9,
        (unsigned long) uc->uc_mcontext->__ss.__r10,
        (unsigned long) uc->uc_mcontext->__ss.__r11,
        (unsigned long) uc->uc_mcontext->__ss.__r12,
        (unsigned long) uc->uc_mcontext->__ss.__r13,
        (unsigned long) uc->uc_mcontext->__ss.__r14,
        (unsigned long) uc->uc_mcontext->__ss.__r15,
        (unsigned long) uc->uc_mcontext->__ss.__rip,
        (unsigned long) uc->uc_mcontext->__ss.__rflags,
        (unsigned long) uc->uc_mcontext->__ss.__cs,
        (unsigned long) uc->uc_mcontext->__ss.__fs,
        (unsigned long) uc->uc_mcontext->__ss.__gs
    );
    logStackContent((void**)uc->uc_mcontext->__ss.__rsp);
    #else
    /* OSX x86 */
    prezLog(PREZ_WARNING,
    "\n"
    "EAX:%08lx EBX:%08lx ECX:%08lx EDX:%08lx\n"
    "EDI:%08lx ESI:%08lx EBP:%08lx ESP:%08lx\n"
    "SS:%08lx  EFL:%08lx EIP:%08lx CS :%08lx\n"
    "DS:%08lx  ES:%08lx  FS :%08lx GS :%08lx",
        (unsigned long) uc->uc_mcontext->__ss.__eax,
        (unsigned long) uc->uc_mcontext->__ss.__ebx,
        (unsigned long) uc->uc_mcontext->__ss.__ecx,
        (unsigned long) uc->uc_mcontext->__ss.__edx,
        (unsigned long) uc->uc_mcontext->__ss.__edi,
        (unsigned long) uc->uc_mcontext->__ss.__esi,
        (unsigned long) uc->uc_mcontext->__ss.__ebp,
        (unsigned long) uc->uc_mcontext->__ss.__esp,
        (unsigned long) uc->uc_mcontext->__ss.__ss,
        (unsigned long) uc->uc_mcontext->__ss.__eflags,
        (unsigned long) uc->uc_mcontext->__ss.__eip,
        (unsigned long) uc->uc_mcontext->__ss.__cs,
        (unsigned long) uc->uc_mcontext->__ss.__ds,
        (unsigned long) uc->uc_mcontext->__ss.__es,
        (unsigned long) uc->uc_mcontext->__ss.__fs,
        (unsigned long) uc->uc_mcontext->__ss.__gs
    );
    logStackContent((void**)uc->uc_mcontext->__ss.__esp);
    #endif
/* Linux */
#elif defined(__linux__)
    /* Linux x86 */
    #if defined(__i386__)
    prezLog(PREZ_WARNING,
    "\n"
    "EAX:%08lx EBX:%08lx ECX:%08lx EDX:%08lx\n"
    "EDI:%08lx ESI:%08lx EBP:%08lx ESP:%08lx\n"
    "SS :%08lx EFL:%08lx EIP:%08lx CS:%08lx\n"
    "DS :%08lx ES :%08lx FS :%08lx GS:%08lx",
        (unsigned long) uc->uc_mcontext.gregs[11],
        (unsigned long) uc->uc_mcontext.gregs[8],
        (unsigned long) uc->uc_mcontext.gregs[10],
        (unsigned long) uc->uc_mcontext.gregs[9],
        (unsigned long) uc->uc_mcontext.gregs[4],
        (unsigned long) uc->uc_mcontext.gregs[5],
        (unsigned long) uc->uc_mcontext.gregs[6],
        (unsigned long) uc->uc_mcontext.gregs[7],
        (unsigned long) uc->uc_mcontext.gregs[18],
        (unsigned long) uc->uc_mcontext.gregs[17],
        (unsigned long) uc->uc_mcontext.gregs[14],
        (unsigned long) uc->uc_mcontext.gregs[15],
        (unsigned long) uc->uc_mcontext.gregs[3],
        (unsigned long) uc->uc_mcontext.gregs[2],
        (unsigned long) uc->uc_mcontext.gregs[1],
        (unsigned long) uc->uc_mcontext.gregs[0]
    );
    logStackContent((void**)uc->uc_mcontext.gregs[7]);
    #elif defined(__X86_64__) || defined(__x86_64__)
    /* Linux AMD64 */
    prezLog(PREZ_WARNING,
    "\n"
    "RAX:%016lx RBX:%016lx\nRCX:%016lx RDX:%016lx\n"
    "RDI:%016lx RSI:%016lx\nRBP:%016lx RSP:%016lx\n"
    "R8 :%016lx R9 :%016lx\nR10:%016lx R11:%016lx\n"
    "R12:%016lx R13:%016lx\nR14:%016lx R15:%016lx\n"
    "RIP:%016lx EFL:%016lx\nCSGSFS:%016lx",
        (unsigned long) uc->uc_mcontext.gregs[13],
        (unsigned long) uc->uc_mcontext.gregs[11],
        (unsigned long) uc->uc_mcontext.gregs[14],
        (unsigned long) uc->uc_mcontext.gregs[12],
        (unsigned long) uc->uc_mcontext.gregs[8],
        (unsigned long) uc->uc_mcontext.gregs[9],
        (unsigned long) uc->uc_mcontext.gregs[10],
        (unsigned long) uc->uc_mcontext.gregs[15],
        (unsigned long) uc->uc_mcontext.gregs[0],
        (unsigned long) uc->uc_mcontext.gregs[1],
        (unsigned long) uc->uc_mcontext.gregs[2],
        (unsigned long) uc->uc_mcontext.gregs[3],
        (unsigned long) uc->uc_mcontext.gregs[4],
        (unsigned long) uc->uc_mcontext.gregs[5],
        (unsigned long) uc->uc_mcontext.gregs[6],
        (unsigned long) uc->uc_mcontext.gregs[7],
        (unsigned long) uc->uc_mcontext.gregs[16],
        (unsigned long) uc->uc_mcontext.gregs[17],
        (unsigned long) uc->uc_mcontext.gregs[18]
    );
    logStackContent((void**)uc->uc_mcontext.gregs[15]);
    #endif
#else
    prezLog(PREZ_WARNING,
        "  Dumping of registers not supported for this OS/arch");
#endif
}

/* Logs the stack trace using the backtrace() call. This function is designed
 * to be called from signal handlers safely. */
void logStackTrace(ucontext_t *uc) {
    void *trace[100];
    int trace_size = 0, fd;
    int log_to_stdout = server.logfile[0] == '\0';

    /* Open the log file in append mode. */
    fd = log_to_stdout ?
        STDOUT_FILENO :
        open(server.logfile, O_APPEND|O_CREAT|O_WRONLY, 0644);
    if (fd == -1) return;

    /* Generate the stack trace */
    trace_size = backtrace(trace, 100);

    /* overwrite sigaction with caller's address */
    if (getMcontextEip(uc) != NULL)
        trace[1] = getMcontextEip(uc);

    /* Write symbols to log file */
    backtrace_symbols_fd(trace, trace_size, fd);

    /* Cleanup */
    if (!log_to_stdout) close(fd);
}

void sigsegvHandler(int sig, siginfo_t *info, void *secret) {
    ucontext_t *uc = (ucontext_t*) secret;
    struct sigaction act;
    PREZ_NOTUSED(info);

    bugReportStart();
    prezLog(PREZ_WARNING,
        "    prez %s crashed by signal: %d", PREZ_VERSION, sig);
    prezLog(PREZ_WARNING,
        "    Failed assertion: %s (%s:%d)", server.assert_failed,
                        server.assert_file, server.assert_line);

    /* Log the stack trace */
    prezLog(PREZ_WARNING, "--- STACK TRACE");
    logStackTrace(uc);

    /* Log dump of processor registers */
    logRegisters(uc);

    prezLog(PREZ_WARNING,
"\n=== PREZ BUG REPORT END. Make sure to include from START to END. ===\n\n"
);
    /* free(messages); Don't call free() with possibly corrupted memory. */
    if (server.daemonize) unlink(server.pidfile);

    /* Make sure we exit with the right signal at the end. So for instance
     * the core will be dumped if enabled. */
    sigemptyset (&act.sa_mask);
    act.sa_flags = SA_NODEFER | SA_ONSTACK | SA_RESETHAND;
    act.sa_handler = SIG_DFL;
    sigaction (sig, &act, NULL);
    kill(getpid(),sig);
}
#endif /* HAVE_BACKTRACE */
