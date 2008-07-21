/*******************************************************************************
 * Copyright (c) 2007, 2008 Wind River Systems, Inc. and others.
 * All rights reserved. This program and the accompanying materials 
 * are made available under the terms of the Eclipse Public License v1.0 
 * which accompanies this distribution, and is available at 
 * http://www.eclipse.org/legal/epl-v10.html 
 *  
 * Contributors:
 *     Wind River Systems - initial API and implementation
 *******************************************************************************/

/*
 * Agent self-testing service.
 */

#include "mdep.h"
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <assert.h>
#include "myalloc.h"
#include "test.h"
#include "trace.h"
#include "context.h"
#include "errors.h"

extern void tcf_test_func2(void);
extern void tcf_test_func1(void);
extern void tcf_test_func0(void);

void tcf_test_func2(void) {
    int func2_local1 = 1;
    int func2_local2 = 2;
    usleep(1000);
    func2_local1++;
    func2_local2 = func2_local1;
}

void tcf_test_func1(void) {
    tcf_test_func2();
}

void tcf_test_func0(void) {
    tcf_test_func1();
}

static char array[0x1000];
char * tcf_test_array = array;

static void * test_sub(void * x) {
    volatile int * test_done = (int *)x;
    while (!*test_done) {
        tcf_test_func0();
    }
    return NULL;
}

void test_proc(void) {
    int i;
    pthread_t thread[4];
    int test_done = 0;
    tcf_test_func0();
    for (i = 0; i < 4; i++) {
        thread[i] = 0;
    }
    for (i = 0; i < 4; i++) {
        if (pthread_create(thread + i, &pthread_create_attr, test_sub, &test_done) != 0) {
            perror("pthread_create");
            break;
        }
    }
    for (i = 0; i < 9; i++) {
        tcf_test_func0();
    }
    test_done = 1;
    for (i = 0; i < 4; i++) {
        if (thread[i]) pthread_join(thread[i], NULL);
    }
}

#if defined(WIN32)
typedef struct ContextAttachArgs {
    ContextAttachCallBack * done;
    void * data;
    HANDLE thread;
    HANDLE process;
} ContextAttachArgs;

static void done_context_attach(int error, Context * ctx, void * data) {
    ContextAttachArgs * args = (ContextAttachArgs *)data;
    args->done(error, ctx, args->data);
    assert(error || args->process != ctx->handle);
    CloseHandle(args->thread);
    CloseHandle(args->process);
    loc_free(args);
}
#endif

int run_test_process(ContextAttachCallBack * done, void * data) {
#if defined(WIN32)
    char fnm[FILE_PATH_SIZE];
    char cmd[FILE_PATH_SIZE];
    int res = 0;
    STARTUPINFO si;
    PROCESS_INFORMATION prs;
    ContextAttachArgs * args;

    memset(&si, 0, sizeof(si));
    memset(&prs, 0, sizeof(prs));
    memset(fnm, 0, sizeof(fnm));
    if (GetModuleFileName(NULL, fnm, sizeof(fnm)) == 0) {
        set_win32_errno(GetLastError());
        return -1;
    }
    si.cb = sizeof(si);
    strcpy(cmd, "agent.exe -t");
    if (CreateProcess(fnm, cmd, NULL, NULL,
            FALSE, CREATE_SUSPENDED | CREATE_DEFAULT_ERROR_MODE | CREATE_NO_WINDOW,
            NULL, NULL, &si, &prs) == 0) {
        set_win32_errno(GetLastError());
        return -1;
    }
    args = (ContextAttachArgs *)loc_alloc(sizeof(ContextAttachArgs));
    args->done = done;
    args->data = data;
    args->thread = prs.hThread;
    args->process = prs.hProcess;
    res = context_attach(prs.dwProcessId, done_context_attach, args, 0);
    if (res != 0) loc_free(args);
    return res;
#elif defined(_WRS_KERNEL)
    int tid = taskCreate("tTcf", 100, 0, 0x4000, (FUNCPTR)test_proc, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    if (tid == 0) return -1;
    taskStop(tid);
    taskActivate(tid);
    assert(taskIsStopped(tid));
    return context_attach(tid, done, data, 0);
#else
    /* Create child process to debug */
    int pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        int fd;
        if (context_attach_self() < 0) exit(1);
        fd = sysconf(_SC_OPEN_MAX);
        while (fd-- > 2) close(fd);
        if (tkill(getpid(), SIGSTOP) < 0) exit(1);
        test_proc();
        exit(0);
    }
    return context_attach(pid, done, data, 1);
#endif
}



