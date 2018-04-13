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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <cutils/log.h>
#include <utils/CallStack.h>
#include "Gem5PipeStream.h"

#define  LOG_TAG  "gem5-tag"


void dumpStack(){
    android::CallStack a;
    a.update();
    a.log(LOG_TAG);
}

#if defined(__i386__) || defined(__x86_64__)
//define an empty m5_gpu for x86
extern "C" void m5_gpu(uint64_t __gpusysno, uint64_t call_params){}
#endif

//#define GEM5PIPE_DBG

#ifdef GEM5PIPE_DBG
#define DBGSTACK(...)    dumpStack()
#define DBG ALOGI
#define GEM5DBG(x, y) sendRWOperation(gem5_debug, x, y)
#else
#define DBGSTACK(...)
#define DBG(...)
#define GEM5DBG(x, y)
#endif


Gem5PipeStream::Gem5PipeStream(size_t bufSize) :
IOStream(bufSize), m_bufsize(bufSize), m_buf(NULL)
{
   DBG(">> Gem5PipeStream::Gem5PipeStream with size of %d\n");
   rlimit limitS;
   getrlimit(RLIMIT_MEMLOCK, &limitS);
   page_size = getpagesize();
   GEM5DBG(gem5_info, limitS.rlim_cur);
   GEM5DBG(gem5_info, limitS.rlim_max);
   GEM5DBG(gem5_info, page_size);
}

Gem5PipeStream::~Gem5PipeStream()
{
   DBG("~Gem5PipeStream\n");
   if (m_buf != NULL)
      free(m_buf);
}

void *Gem5PipeStream::allocBuffer(size_t minSize)
{
    DBG(">> Gem5PipeStream::allocBuffer size of %d \n", minSize);
    DBG(">> Gem5PipeStream::allocBuffer m_buf = %llx \n", m_buf);
    DBGSTACK();
    size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
    if (!m_buf) {
        DBG("mallocing %d\n", allocSize);
        m_buf = (unsigned char *)malloc(allocSize);
        //m_bufsize = allocSize;
        if(!m_buf){
          GEM5DBG((void*)pipe_mem_alloc_fail, 0);
          ERR("realloc (%d) failed\n", allocSize);
          return NULL;
        }
        GEM5DBG(pipe_mem_alloc, allocSize);
        DBG("malloc done  \n");
    } else if (m_bufsize < allocSize) {
        DBG("re allocing for %d\n", allocSize);
        unsigned char *p = (unsigned char *)realloc(m_buf, allocSize);
        if (p != NULL) {
          GEM5DBG(pipe_mem_alloc, allocSize);
          DBG("re allocing sucess\n");
          m_buf = p;
          m_bufsize = allocSize;
        } else {
          GEM5DBG(pipe_mem_alloc_fail, 0);
          ERR("realloc (%d) failed\n", allocSize);
          free(m_buf);
          m_buf = NULL;
          m_bufsize = 0;
        }
    }

    DBG("<< Gem5PipeStream::allocBuffer returning %p\n", m_buf);
    return m_buf;
};

void Gem5PipeStream::pack(char *bytes, uint64_t &bytes_off, uint64_t *lengths, uint64_t &lengths_off, char *arg, uint64_t arg_size)
{
    for (int i = 0; i < arg_size; i++) {
        bytes[bytes_off + i] = *arg;
        arg++;
    }
    *(lengths + lengths_off) = arg_size;

    bytes_off += arg_size;
    lengths_off += 1;
}

int Gem5PipeStream::doRWOperation(gem5GraphicsCall type, const unsigned char* buff, uint64_t len){
    //DBG(">> Gem5PipeStream::doRWOperation calling %d with buf=%x  and len=%d \n",type, (unsigned int)buff,len);
    //DBGSTACK();

    gpusyscall_t call_params;
    call_params.pid = getpid();
    call_params.tid = gettid();
    call_params.num_args = 2;
    uint64_t* arg_lengths = new uint64_t[call_params.num_args];
    call_params.arg_lengths_ptr = (uintptr_t) arg_lengths;

    arg_lengths[0] = sizeof(uint64_t);
    arg_lengths[1] = sizeof(uint64_t);
    call_params.total_bytes = arg_lengths[0]+arg_lengths[1];

    char* args = new char[call_params.total_bytes];
    call_params.args_ptr = (uintptr_t) args;

    uint64_t bytes_off = 0;
    uint64_t lengths_off = 0;

    uint64_t buff64 = (uintptr_t) buff;
    pack(args, bytes_off, arg_lengths, lengths_off, (char *)&buff64, arg_lengths[0]);
    pack(args, bytes_off, arg_lengths, lengths_off, (char *)&len, arg_lengths[1]);

    char* ret_spot = new char[sizeof(int32_t)];
    *ret_spot = 0;
    call_params.ret_ptr = (uintptr_t) ret_spot;
    static uint64_t uniqueNum  = 0;
    call_params.unique_id = ++uniqueNum;

    DBG("doRWOperation, (pid, tid, uniqueId) = (%d, %d, %llu)\n", call_params.pid, call_params.tid, call_params.unique_id);

    m5_gpu(type, (uint64_t) (uintptr_t) &call_params);

    int32_t return_value = *((int32_t*)ret_spot);

    delete [] arg_lengths;
    delete [] args;
    delete [] ret_spot;

    return return_value;
}

int Gem5PipeStream::sendRWOperation(gem5GraphicsCall type, const void* buffer, size_t len){
    //block till we are allowed to send
    while(doRWOperation(gem5_block, NULL,  0)){
      usleep(10);
    }

    //now we can do the operation
    int ret = 0;

    unsigned char* buff = (unsigned char*) buffer;

    if(needToMapUponSend(type) and (len > 0)){
      //touching each page before sending over to gem5
      unsigned char* buff_end = buff + len;
      for(volatile unsigned char* ptr=buff; ptr < buff_end; ptr+=page_size)
      {
        //touching start page
        if(type == gem5_read){
          *ptr = 0;
        } else {
          (void)*ptr;
        }
        volatile unsigned char* end_ptr;
        //touch end page
        if((ptr+page_size-1) < buff_end){
          end_ptr = ptr + page_size - 1;
        } else {
          end_ptr = buff_end - 1;
        }

        if(type == gem5_read){
          *end_ptr = 0;
        } else {
          (void)*end_ptr;
        }

        //send buffer
        ret += doRWOperation(type, (const unsigned char*) ptr, (buff_end - ptr) > page_size? page_size : (buff_end - ptr));

        //touch after modifying
        (void)*ptr;
        (void)*end_ptr;
      }
    } else {
      ret = doRWOperation(type, buff, len);
    }

    //if(sim_active) Gem5PipeMemory::Memory.UnlockGBuffer();
    return ret;
}

int Gem5PipeStream::writeFully(const void *buf, size_t len)
{
    DBG(">> Gem5PipeStream::writeFully %d\n", len);
    DBGSTACK();

    if (!buf) {
       if (len>0) {
            // If len is non-zero, buf must not be NULL. Otherwise the pipe would be
            // in a corrupted state, which is lethal for the emulator.
           ERR("Gem5PipeStream::writeFully failed, buf=NULL, len %d,"
                   " lethal error, exiting", len);
           abort();
       }
       return 0;
    }

    size_t res = len;
    int retval = 0;

    while (res > 0) {
        //ssize_t stat = sendRWOperation(gem5_writeFully, buf,  len);
        ssize_t stat = sendRWOperation(gem5_write, (const char *)(buf) + (len - res), res);
        if (stat > 0) {
            res -= stat;
            continue;
        }
        if (stat == 0) { /* EOF */
            ERR("Gem5PipeStream::writeFully failed: premature EOF\n");
            retval = -1;
            break;
        }
        retval =  stat;
        ERR("Gem5PipeStream::writeFully failed, lethal error, exiting.\n");
        abort();
    }

    DBG("<< Gem5PipeStream::writeFully %d\n", len );
    return retval;
}

int Gem5PipeStream::commitBuffer(size_t size)
{
    DBG(">> Gem5PipeStream::commitBuffer with buf=%llx  and len=%d \n", m_buf,size);
    DBGSTACK();

    return writeFully(m_buf, size);
}

const unsigned char *Gem5PipeStream::readFully(void *buf, size_t len)
{
    DBG(">> Gem5PipeStream::readFully buf=%llx, len=%d\n", (uintptr_t)buf, len);
    DBGSTACK();
    if (!buf) {
        if (len > 0) {
            // If len is non-zero, buf must not be NULL. Otherwise the pipe would be
            // in a corrupted state, which is lethal for the emulator.
            ERR("Gem5PipeStream::readFully failed, buf=NULL, len %zu, lethal"
                    " error, exiting.", len);
            abort();
        }
        return NULL;  // do not allow NULL buf in that implementation
    }
    size_t res = len;
    while (res > 0) {
        ssize_t stat = sendRWOperation(gem5_read, (char *)(buf) + len - res, res);
        if (stat == 0) {
            DBG("Gem5PipeStream: readFully client shutdown");
            return NULL;
        } else if (stat < 0) {
            ERR("Gem5PipeStream::readFully failed (buf %p, len %zu"
                ", res %zu), lethal error, exiting.", buf, len, res);
            abort();
        } else {
            res -= stat;
        }
    }
    DBG("<< Gem5PipeStream::readFully %d\n", len);
    return (const unsigned char *)buf;
}

const unsigned char *Gem5PipeStream::read( void *buf, size_t *inout_len)
{
    DBG(">> Gem5PipeStream::read %d\n", *inout_len);
    DBGSTACK();

    if (!buf) {
      ERR("Gem5PipeStream::read failed, buf=NULL");
      return NULL;  // do not allow NULL buf in that implementation
    }

    int n = recv(buf, *inout_len);

    if (n > 0) {
      *inout_len = n;
      return (const unsigned char *)buf;
    }

    DBG("<< Gem5PipeStream::read %d\n", *inout_len);
    return NULL;
}

int Gem5PipeStream::recv(void *buf, size_t len)
{
    DBG(">> Gem5PipeSteram::recv with buf=%llx, len=%d \n", buf,len);
    DBGSTACK();

    char* p = (char *)buf;
    int ret = 0;
    while(len > 0) {
        int res = sendRWOperation(gem5_read, p, len);
        if (res > 0) {
            p += res;
            ret += res;
            len -= res;
            continue;
        }
        if (res == 0) {
             continue;
        }

        /* A real error */
        if (ret == 0)
            ret = -1;
        break;
    }
    return ret;
}

bool Gem5PipeStream::needToMapUponSend(gem5GraphicsCall type){
   if((type == gem5_write)
      or (type == gem5_read))
      return true;
   return false;
}

void Gem5PipeStream::allocGBuffer(){
   DBG(">> Gem5PipeStream::allocGBuffer\n");
   //doRWOperation(gem5_graphics_mem, -1, -1);
}
