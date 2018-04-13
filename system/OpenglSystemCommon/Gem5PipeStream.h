/*
* Copyright (C) 2011 The Android Open Source Project
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
#ifndef __Gem5_PIPE_STREAM_H
#define __Gem5_PIPE_STREAM_H
/* This file implements an IOStream that uses a Gem5 psuedo instructions to communicate
 'opengles' commands. 
 */
#include <stdlib.h>
#include <pthread.h>
#include "IOStream.h"
extern "C" {
    #include "gem5/m5op.h"
}


//the start of gpu graphics calls, defined in src/api/cuda_syscalls.h
const unsigned GEM5_CUDA_CALLS_START = 100;
 enum gem5GraphicsCall {
    gem5_write = GEM5_CUDA_CALLS_START,
    gem5_read, //101
    gem5_graphics_mem, //102
    gem5_block, //103
    gem5_debug, //104
    gem5_call_buffer_fail, //105
    gem5_sim_active, //106
    gem5_get_procId, //107
};

enum gem5DebugCall {
   gmem_alloc_fail,
   gmem_lock_fail,
   pipe_mem_alloc_fail,
   gem5_info,
   pipe_mem_alloc
};


typedef struct gpucall {
    uint64_t unique_id;
    int32_t pid;
    int32_t tid;
    uint64_t total_bytes;
    uint64_t num_args;
    //pointers stored in unsigned 64
    uint64_t arg_lengths_ptr;
    uint64_t args_ptr;
    uint64_t ret_ptr;
} gpusyscall_t;

class Gem5PipeStream : public IOStream {
public:
    explicit Gem5PipeStream(size_t bufsize = 10000);
    ~Gem5PipeStream();

    virtual void *allocBuffer(size_t minSize);
    virtual int commitBuffer(size_t size);
    virtual const unsigned char *readFully( void *buf, size_t len);
    virtual const unsigned char *read( void *buf, size_t *inout_len);
    virtual int writeFully(const void *buf, size_t len);

    int recv(void *buf, size_t len);
    static bool is_graphics_mem_init;

private:
    size_t m_bufsize;
    unsigned char *m_buf;
    int page_size;
    void pack(char *bytes, uint64_t &bytes_off, uint64_t *lengths, uint64_t &lengths_off, char *arg, uint64_t arg_size);
    int doRWOperation(gem5GraphicsCall type, const unsigned char* buf, uint64_t len);
    int sendRWOperation(gem5GraphicsCall type, const void*buf, size_t len);
    inline bool needToMapUponSend(gem5GraphicsCall type);
    void allocGBuffer();
};

#endif
