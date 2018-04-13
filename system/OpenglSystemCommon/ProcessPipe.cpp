/*
* Copyright (C) 2016 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

#include "renderControl_enc.h"
#include "qemu_pipe.h"

#include <cutils/log.h>
#include <pthread.h>
#include <Gem5PipeStream.h>

//TODO: only needed for pid
#include <sys/types.h>
#include <unistd.h>

extern const int sUseGem5Pipe;
static void gem5ProcessPipeInitOnce();

static int                sProcPipe = 0;
static pthread_once_t     sProcPipeOnce = PTHREAD_ONCE_INIT;
// sProcUID is a unique ID per process assigned by the host.
// It is different from getpid().
static uint64_t           sProcUID = 0;

// processPipeInitOnce is used to generate a process unique ID (puid).
// processPipeInitOnce will only be called at most once per process.
// Use it with pthread_once for thread safety.
// The host associates resources with process unique ID (puid) for memory cleanup.
// It will fallback to the default path if the host does not support it.
// Processes are identified by acquiring a per-process 64bit unique ID from the
// host.
static void processPipeInitOnce() {
    //check if we are using gem5 pipe
    if(sUseGem5Pipe){
      gem5ProcessPipeInitOnce();
      return;
    }

    //otherwise use the qemu pipe
    sProcPipe = qemu_pipe_open("GLProcessPipe");
    if (sProcPipe < 0) {
        sProcPipe = 0;
        ALOGW("Process pipe failed");
        return;
    }
    // Send a confirmation int to the host
    int32_t confirmInt = 100;
    ssize_t stat = 0;
    do {
        stat = ::write(sProcPipe, (const char*)&confirmInt,
                sizeof(confirmInt));
    } while (stat < 0 && errno == EINTR);

    if (stat != sizeof(confirmInt)) { // failed
        close(sProcPipe);
        sProcPipe = 0;
        ALOGW("Process pipe failed");
        return;
    }

    // Ask the host for per-process unique ID
    do {
        stat = ::read(sProcPipe, (char*)&sProcUID,
                      sizeof(sProcUID));
    } while (stat < 0 && errno == EINTR);

    if (stat != sizeof(sProcUID)) {
        close(sProcPipe);
        sProcPipe = 0;
        sProcUID = 0;
        ALOGW("Process pipe failed");
        return;
    }
}

bool processPipeInit(renderControl_encoder_context_t *rcEnc) {
    pthread_once(&sProcPipeOnce, processPipeInitOnce);
    if (!sProcPipe) return false;
    rcEnc->rcSetPuid(rcEnc, sProcUID);
    return true;
}

static void gem5ProcessPipeInitOnce(){
  ALOGI(">> gem5ProcessPipeInitOnce calling\n", gem5_get_procId);

  //TODO: fixme
  //use pid & tid for now
  sProcUID= (((uint64_t)getpid() << 32 ) | gettid());
  sProcPipe = -1;


  /*gpusyscall_t call_params;
  call_params.pid = getpid();
  call_params.tid = gettid();
  call_params.num_args = 0;
  call_params.total_bytes =  0;

  call_params.ret = new char[sizeof(uint64_t)];
  uint64_t * ret_spot = (uint64_t*)call_params.ret;
  *ret_spot = 0;

  m5_gpu(gem5_get_procId, (uint64_t) &call_params);

  sProcUID= *((uint64_t*)call_params.ret);
  sProcPipe = -1;

  ALOGI("gem5ProcessPipeInitOnce received a return value of %lx \n", sProcUID);

  delete [] call_params.ret;
  */
}
