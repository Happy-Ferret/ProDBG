//
// Copyright 2014 Celtoys Ltd
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

/*
@Contents:

    @DEPS:          External Dependencies
    @TIMERS:        Platform-specific timers
    @TLS:           Thread-Local Storage
    @ATOMIC:        Atomic Operations
    @VMBUFFER:      Mirror Buffer using Virtual Memory for auto-wrap
    @THREADS:       Threads
    @SAFEC:         Safe C Library excerpts
    @OBJALLOC:      Reusable Object Allocator
    @DYNBUF:        Dynamic Buffer
    @SOCKETS:       Sockets TCP/IP Wrapper
    @SHA1:          SHA-1 Cryptographic Hash Function
    @BASE64:        Base-64 encoder
    @MURMURHASH:    Murmur-Hash 3
    @WEBSOCKETS:    WebSockets
    @MESSAGEQ:      Multiple producer, single consumer message queue
    @NETWORK:       Network Server
    @JSON:          Basic, text-based JSON serialisation
    @SAMPLE:        Base Sample Description (CPU by default)
    @SAMPLETREE:    A tree of samples with their allocator
    @TSAMPLER:      Per-Thread Sampler
    @REMOTERY:      Remotery
    @CUDA:          CUDA event sampling
    @D3D11:         Direct3D 11 event sampling
*/

#include "remotery.h"

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif

#ifdef RMT_ENABLED



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @DEPS: External Dependencies
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



//
// Required CRT dependencies
//
#ifdef RMT_USE_TINYCRT

    #include <TinyCRT/TinyCRT.h>
    #include <TinyCRT/TinyWinsock.h>

    #define CreateFileMapping CreateFileMappingA

#else

    #ifdef RMT_PLATFORM_MACOS
        #include <mach/mach_time.h>
        #include <mach/vm_map.h>
        #include <mach/mach.h>
        #include <sys/time.h>
    #else
        #include <malloc.h>
    #endif

    #include <assert.h>

    #ifdef RMT_PLATFORM_WINDOWS
        #include <winsock2.h>
        #include <intrin.h>
        #undef min
        #undef max
    #endif

    #ifdef RMT_PLATFORM_LINUX
        #include <time.h>
    #endif

    #if defined(RMT_PLATFORM_POSIX)
        #include <stdlib.h>
        #include <pthread.h>
        #include <unistd.h>
        #include <string.h>
        #include <sys/socket.h>
        #include <sys/mman.h>
        #include <netinet/in.h>
        #include <fcntl.h>
        #include <errno.h>
    #endif

#endif

#ifdef RMT_USE_CUDA
    #include <cuda.h>
#endif

#ifdef RMT_USE_D3D11

    // As clReflect has no way of disabling C++ compile mode, this forces C interfaces everywhere...
    #define CINTERFACE

    // ...unfortunately these C++ helpers aren't wrapped by the same macro but they can be disabled individually
    #define D3D11_NO_HELPERS

    // Allow use of the D3D11 helper macros for accessing the C-style vtable
    #define COBJMACROS

    #include <d3d11.h>
#endif


rmtU64 min(rmtS64 a, rmtS64 b)
{
    return a < b ? a : b;
}


rmtU64 max(rmtS64 a, rmtS64 b)
{
    return a > b ? a : b;
}



// Config
// TODO: Expose to user

// How long to sleep between server updates, hopefully trying to give
// a little CPU back to other threads.
static const rmtU32 MS_SLEEP_BETWEEN_SERVER_UPDATES = 10;

// Will be rounded to page granularity of 64k
static const rmtU32 MESSAGE_QUEUE_SIZE_BYTES = 64 * 1024;

// If the user continuously pushes to the message queue, the server network
// code won't get a chance to update unless there's an upper-limit on how
// many messages can be consumed per loop.
static const rmtU32 MAX_NB_MESSAGES_PER_UPDATE = 100;



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @TIMERS: Platform-specific timers
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



//
// Get millisecond timer value that has only one guarantee: multiple calls are consistently comparable.
// On some platforms, even though this returns milliseconds, the timer may be far less accurate.
//
static rmtU32 msTimer_Get()
{
    #ifdef RMT_PLATFORM_WINDOWS
        return (rmtU32)GetTickCount();
    #else
        clock_t time = clock();
        rmtU32 msTime = (rmtU32) (time / (CLOCKS_PER_SEC / 1000));
        return msTime;
    #endif
}


//
// Micro-second accuracy high performance counter
//
#ifndef RMT_PLATFORM_WINDOWS
    typedef rmtU64 LARGE_INTEGER;
#endif
typedef struct
{
    LARGE_INTEGER counter_start;
    double counter_scale;
} usTimer;


static void usTimer_Init(usTimer* timer)
{
    #if defined(RMT_PLATFORM_WINDOWS)
        LARGE_INTEGER performance_frequency;

        assert(timer != NULL);

        // Calculate the scale from performance counter to microseconds
        QueryPerformanceFrequency(&performance_frequency);
        timer->counter_scale = 1000000.0 / performance_frequency.QuadPart;

        // Record the offset for each read of the counter
        QueryPerformanceCounter(&timer->counter_start);

    #elif defined(RMT_PLATFORM_MACOS)

        mach_timebase_info_data_t nsScale;
        mach_timebase_info( &nsScale );
        const double ns_per_us = 1.0e3;
        timer->counter_scale = (double)(nsScale.numer) / ((double)nsScale.denom * ns_per_us);

        timer->counter_start = mach_absolute_time();

    #elif defined(RMT_PLATFORM_LINUX)

        struct timespec tv;
        clock_gettime(CLOCK_REALTIME, &tv);
        timer->counter_start = tv.tv_nsec;

    #endif
}


static rmtU64 usTimer_Get(usTimer* timer)
{
    #if defined(RMT_PLATFORM_WINDOWS)
        LARGE_INTEGER performance_count;

        assert(timer != NULL);

        // Read counter and convert to microseconds
        QueryPerformanceCounter(&performance_count);
        return (rmtU64)((performance_count.QuadPart - timer->counter_start.QuadPart) * timer->counter_scale);

    #elif defined(RMT_PLATFORM_MACOS)

        rmtU64 curr_time = mach_absolute_time();
        return (rmtU64)((curr_time - timer->counter_start) * timer->counter_scale);

    #elif defined(RMT_PLATFORM_LINUX)

        struct timespec tv;
        clock_gettime(CLOCK_REALTIME, &tv);
        return tv.tv_nsec - timer->counter_start;

    #endif
}


static void msSleep(rmtU32 time_ms)
{
    #ifdef RMT_PLATFORM_WINDOWS
        Sleep(time_ms);
    #elif defined(RMT_PLATFORM_POSIX)
        usleep(time_ms * 1000);
    #endif
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @TLS: Thread-Local Storage
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



#define TLS_INVALID_HANDLE 0xFFFFFFFF

#if defined(RMT_PLATFORM_WINDOWS)
    typedef rmtU32 rmtTLS;
#else
    typedef pthread_key_t rmtTLS;
#endif

static enum rmtError tlsAlloc(rmtTLS* handle)
{
    assert(handle != NULL);

#if defined(RMT_PLATFORM_WINDOWS)

    *handle = (rmtTLS)TlsAlloc();
    if (*handle == TLS_OUT_OF_INDEXES)
    {
        *handle = TLS_INVALID_HANDLE;
        return RMT_ERROR_TLS_ALLOC_FAIL;
    }

#elif defined(RMT_PLATFORM_POSIX)

    if (pthread_key_create(handle, NULL) != 0)
    {
        *handle = TLS_INVALID_HANDLE;
        return RMT_ERROR_TLS_ALLOC_FAIL;
    }

#endif

    return RMT_ERROR_NONE;
}


static void tlsFree(rmtTLS handle)
{
    assert(handle != TLS_INVALID_HANDLE);

#if defined(RMT_PLATFORM_WINDOWS)

    TlsFree(handle);

#elif defined(RMT_PLATFORM_POSIX)

    pthread_key_delete((pthread_key_t)handle);

#endif
}


static void tlsSet(rmtTLS handle, void* value)
{
    assert(handle != TLS_INVALID_HANDLE);

#if defined(RMT_PLATFORM_WINDOWS)

    TlsSetValue(handle, value);

#elif defined(RMT_PLATFORM_POSIX)

    pthread_setspecific((pthread_key_t)handle, value);

#endif
}


static void* tlsGet(rmtTLS handle)
{
    assert(handle != TLS_INVALID_HANDLE);

#if defined(RMT_PLATFORM_WINDOWS)

    return TlsGetValue(handle);

#elif defined(RMT_PLATFORM_POSIX)

    return pthread_getspecific((pthread_key_t)handle);

#endif
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @ATOMIC: Atomic Operations
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/


static rmtBool AtomicCompareAndSwap(rmtU32 volatile* val, long old_val, long new_val)
{
    #if defined(RMT_PLATFORM_WINDOWS)
        return _InterlockedCompareExchange((long volatile*)val, new_val, old_val) == old_val ? RMT_TRUE : RMT_FALSE;
    #elif defined(RMT_PLATFORM_POSIX)
        return __sync_bool_compare_and_swap(val, old_val, new_val) ? RMT_TRUE : RMT_FALSE;
    #endif
}


static rmtBool AtomicCompareAndSwapPointer(long* volatile* ptr, long* old_ptr, long* new_ptr)
{
    #if defined(RMT_PLATFORM_WINDOWS)
        #ifdef _WIN64
            return _InterlockedCompareExchange64((__int64 volatile*)ptr, (__int64)new_ptr, (__int64)old_ptr) == (__int64)old_ptr ? RMT_TRUE : RMT_FALSE;
        #else
            return _InterlockedCompareExchange((long volatile*)ptr, (long)new_ptr, (long)old_ptr) == (long)old_ptr ? RMT_TRUE : RMT_FALSE;
        #endif
    #elif defined(RMT_PLATFORM_POSIX)
        return __sync_bool_compare_and_swap(ptr, old_ptr, new_ptr) ? RMT_TRUE : RMT_FALSE;
    #endif
}


//
// NOTE: Does not guarantee a memory barrier
// TODO: Make sure all platforms don't insert a memory barrier as this is only for stats
//       Alternatively, add strong/weak memory order equivalents
//
static void AtomicAdd(rmtS32 volatile* value, rmtS32 add)
{
    #if defined(RMT_PLATFORM_WINDOWS)
        _InterlockedExchangeAdd((long volatile*)value, (long)add);
    #elif defined(RMT_PLATFORM_POSIX)
        __sync_fetch_and_add(value, add);
    #endif
}


static void AtomicSub(rmtS32 volatile* value, rmtS32 sub)
{
    // Not all platforms have an implementation so just negate and add
    AtomicAdd(value, -sub);
}


// Compiler read/write fences (windows implementation)
static void ReadFence()
{
#ifdef RMT_PLATFORM_WINDOWS
    _ReadBarrier();
#else
    asm volatile ("" : : : "memory");
#endif
}
static void WriteFence()
{
#ifdef RMT_PLATFORM_WINDOWS
    _WriteBarrier();
#else
    asm volatile ("" : : : "memory");
#endif
}


// Get a shared value with acquire semantics, ensuring the read is complete
// before the function returns.
static void* LoadAcquire(void* volatile const* addr)
{
    // Hardware fence is implicit on x86 so only need the compiler fence
    void* v = *addr;
    ReadFence();
    return v;
}


// Set a shared value with release semantics, ensuring any prior writes
// are complete before the value is set.
static void StoreRelease(void* volatile*  addr, void* v)
{
    // Hardware fence is implicit on x86 so only need the compiler fence
    WriteFence();
    *addr = v;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @VMBUFFER: Mirror Buffer using Virtual Memory for auto-wrap
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct VirtualMirrorBuffer
{
    // Page-rounded size of the buffer without mirroring
    rmtU32 size;

    // Pointer to the first part of the mirror
    // The second part comes directly after at ptr+size bytes
    rmtU8* ptr;

#ifdef RMT_PLATFORM_WINDOWS
    HANDLE file_map_handle;
#endif

} VirtualMirrorBuffer;


static void VirtualMirrorBuffer_Destroy(VirtualMirrorBuffer* buffer)
{
    assert(buffer != 0);

#ifdef RMT_PLATFORM_WINDOWS
    if (buffer->file_map_handle != NULL)
    {
        CloseHandle(buffer->file_map_handle);
        buffer->file_map_handle = NULL;
    }
#endif

#ifdef RMT_PLATFORM_MACOS
    if (buffer->ptr != NULL)
        vm_deallocate(mach_task_self(), (vm_address_t)buffer->ptr, buffer->size * 2);
#endif

#ifdef RMT_PLATFORM_LINUX
    if (buffer->ptr != NULL)
        munmap(buffer->ptr, buffer->size * 2);
#endif

    buffer->ptr = NULL;

    free(buffer);
}


static enum rmtError VirtualMirrorBuffer_Create(VirtualMirrorBuffer** buffer, rmtU32 size, int nb_attempts)
{
    static const rmtU32 k_64 = 64 * 1024;

#ifdef RMT_PLATFORM_LINUX
    char path[] = "/dev/shm/ring-buffer-XXXXXX";
    int file_descriptor;
#endif

    assert(buffer != 0);

    // Allocate container
    *buffer = (VirtualMirrorBuffer*)malloc(sizeof(VirtualMirrorBuffer));
    if (*buffer == 0)
        return RMT_ERROR_MALLOC_FAIL;

    // Round up to page-granulation; the nearest 64k boundary for now
    size = (size + k_64 - 1) / k_64 * k_64;

    // Set defaults
    (*buffer)->size = size;
    (*buffer)->ptr = NULL;
#ifdef RMT_PLATFORM_WINDOWS
    (*buffer)->file_map_handle = INVALID_HANDLE_VALUE;
#endif

#ifdef RMT_PLATFORM_WINDOWS

    // Windows version based on https://gist.github.com/rygorous/3158316

    while (nb_attempts-- > 0)
    {
        rmtU8* desired_addr;

        // Create a file mapping for pointing to its physical address with multiple virtual pages
        (*buffer)->file_map_handle = CreateFileMapping(
            INVALID_HANDLE_VALUE,
            0,
            PAGE_READWRITE,
            0,
            size,
            0);
        if ((*buffer)->file_map_handle == NULL)
            break;

        // Reserve two contiguous pages of virtual memory
        desired_addr = (rmtU8*)VirtualAlloc(0, size * 2, MEM_RESERVE, PAGE_NOACCESS);
        if (desired_addr == NULL)
            break;

        // Release the range immediately but retain the address for the next sequence of code to
        // try and map to it. In the mean-time some other OS thread may come along and allocate this
        // address range from underneath us so multiple attempts need to be made.
        VirtualFree(desired_addr, 0, MEM_RELEASE);

        // Immediately try to point both pages at the file mapping
        if (MapViewOfFileEx((*buffer)->file_map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, desired_addr) == desired_addr &&
            MapViewOfFileEx((*buffer)->file_map_handle, FILE_MAP_ALL_ACCESS, 0, 0, size, desired_addr + size) == desired_addr + size)
        {
            (*buffer)->ptr = desired_addr;
            break;
        }

        // Failed to map the virtual pages; cleanup and try again
        CloseHandle((*buffer)->file_map_handle);
        (*buffer)->file_map_handle = NULL;
    }

#endif

#ifdef RMT_PLATFORM_MACOS

    //
    // Mac version based on https://github.com/mikeash/MAMirroredQueue
    //
    // Copyright (c) 2010, Michael Ash
    // All rights reserved.
    //
    // Redistribution and use in source and binary forms, with or without modification, are permitted provided that
    // the following conditions are met:
    //
    // Redistributions of source code must retain the above copyright notice, this list of conditions and the following
    // disclaimer.
    //
    // Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the
    // following disclaimer in the documentation and/or other materials provided with the distribution.
    // Neither the name of Michael Ash nor the names of its contributors may be used to endorse or promote products
    // derived from this software without specific prior written permission.
    //
    // THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
    // INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
    // ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
    // INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
    // GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
    // LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
    // OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
    //

    while (nb_attempts-- > 0)
    {
        vm_prot_t cur_prot, max_prot;
        kern_return_t mach_error;
        rmtU8* ptr = NULL;
        rmtU8* target = NULL;

        // Allocate 2 contiguous pages of virtual memory
        if (vm_allocate(mach_task_self(), (vm_address_t*)&ptr, size * 2, VM_FLAGS_ANYWHERE) != KERN_SUCCESS)
            break;

        // Try to deallocate the last page, leaving its virtual memory address free
        target = ptr + size;
        if (vm_deallocate(mach_task_self(), (vm_address_t)target, size) != KERN_SUCCESS)
        {
            vm_deallocate(mach_task_self(), (vm_address_t)ptr, size * 2);
            break;
        }

        // Attempt to remap the page just deallocated to the buffer again
        mach_error = vm_remap(
            mach_task_self(),
            (vm_address_t*)&target,
            size,
            0,  // mask
            0,  // anywhere
            mach_task_self(),
            (vm_address_t)ptr,
            0,  //copy
            &cur_prot,
            &max_prot,
            VM_INHERIT_COPY);

        if (mach_error == KERN_NO_SPACE)
        {
            // Failed on this pass, cleanup and make another attempt
            if (vm_deallocate(mach_task_self(), (vm_address_t)ptr, size) != KERN_SUCCESS)
                break;
        }

        else if (mach_error == KERN_SUCCESS)
        {
            // Leave the loop on success
            (*buffer)->ptr = ptr;
            break;
        }

        else
        {
            // Unknown error, can't recover
            vm_deallocate(mach_task_self(), (vm_address_t)ptr, size);
            break;
        }
    }

#endif

#ifdef RMT_PLATFORM_LINUX

    // Linux version based on now-defunct Wikipedia section http://en.wikipedia.org/w/index.php?title=Circular_buffer&oldid=600431497

    // Create a unique temporary filename in the shared memory folder
    file_descriptor = mkstemp(path);
    if (file_descriptor < 0)
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

    // Delete the name
    if (unlink(path))
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

    // Set the file size to twice the buffer size
    // TODO: this 2x behaviour can be avoided with similar solution to Win/Mac
    if (ftruncate (file_descriptor, size * 2))
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

    // Map 2 contiguous pages
    (*buffer)->ptr = mmap(NULL, size * 2, PROT_NONE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if ((*buffer)->ptr == MAP_FAILED)
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

    // Point both pages to the same memory file
    if (mmap((*buffer)->ptr, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, file_descriptor, 0) != (*buffer)->ptr ||
        mmap((*buffer)->ptr + size, size, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_SHARED, file_descriptor, 0) != (*buffer)->ptr + size)
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

#endif

    // Cleanup if exceeded number of attempts or failed
    if ((*buffer)->ptr == NULL)
    {
        VirtualMirrorBuffer_Destroy(*buffer);
        return RMT_ERROR_VIRTUAL_MEMORY_BUFFER_FAIL;
    }

    return RMT_ERROR_NONE;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @THREADS: Threads
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct
{
    // OS-specific data
    #if defined(RMT_PLATFORM_WINDOWS)
        HANDLE handle;
    #else
        pthread_t handle;
    #endif

    // Callback executed when the thread is created
    void* callback;

    // Caller-specified parameter passed to Thread_Create
    void* param;

    // Error state returned from callback
    enum rmtError error;

    // External threads can set this to request an exit
    volatile rmtBool request_exit;

} Thread;


typedef enum rmtError (*ThreadProc)(Thread* thread);


#if defined(RMT_PLATFORM_WINDOWS)

    static DWORD WINAPI ThreadProcWindows(LPVOID lpParameter)
    {
        Thread* thread = (Thread*)lpParameter;
        assert(thread != NULL);
        thread->error = ((ThreadProc)thread->callback)(thread);
        return thread->error == RMT_ERROR_NONE ? 1 : 0;
    }

#else
    static void* StartFunc( void* pArgs )
    {
        Thread* thread = (Thread*)pArgs;
        assert(thread != NULL);
        thread->error = ((ThreadProc)thread->callback)(thread);
        return NULL; // returned error not use, check thread->error.
    }
#endif

static void Thread_Destroy(Thread* thread);


static int Thread_Valid(Thread* thread)
{
    assert(thread != NULL);

    #if defined(RMT_PLATFORM_WINDOWS)
        return thread->handle != NULL;
    #else
        return pthread_equal(thread->handle, pthread_self());
    #endif
}


static enum rmtError Thread_Create(Thread** thread, ThreadProc callback, void* param)
{
    assert(thread != NULL);

    // Allocate space for the thread data
    *thread = (Thread*)malloc(sizeof(Thread));
    if (*thread == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    (*thread)->callback = callback;
    (*thread)->param = param;
    (*thread)->error = RMT_ERROR_NONE;
    (*thread)->request_exit = RMT_FALSE;

    // OS-specific thread creation

    #if defined (RMT_PLATFORM_WINDOWS)

        (*thread)->handle = CreateThread(
            NULL,                               // lpThreadAttributes
            0,                                  // dwStackSize
            ThreadProcWindows,                  // lpStartAddress
            *thread,                            // lpParameter
            0,                                  // dwCreationFlags
            NULL);                              // lpThreadId

        if ((*thread)->handle == NULL)
        {
            Thread_Destroy(*thread);
            *thread = NULL;
            return RMT_ERROR_CREATE_THREAD_FAIL;
        }

    #else

        int32_t error = pthread_create( &(*thread)->handle, NULL, StartFunc, *thread );
        if (error)
        {
            // Contents of 'thread' parameter to pthread_create() are undefined after
            // failure call so can't pre-set to invalid value before hand.
            (*thread)->handle = pthread_self();

            Thread_Destroy(*thread);
            *thread = NULL;
            return RMT_ERROR_CREATE_THREAD_FAIL;
        }

    #endif

    return RMT_ERROR_NONE;
}


static void Thread_RequestExit(Thread* thread)
{
    // Not really worried about memory barriers or delayed visibility to the target thread
    assert(thread != NULL);
    thread->request_exit = RMT_TRUE;
}


static void Thread_Join(Thread* thread)
{
    assert(Thread_Valid(thread));

    #if defined(RMT_PLATFORM_WINDOWS)
    WaitForSingleObject(thread->handle, INFINITE);
    #else
    pthread_join(thread->handle, NULL);
    #endif
}


static void Thread_Destroy(Thread* thread)
{
    assert(thread != NULL);

    if (Thread_Valid(thread))
    {
        // Shutdown the thread
        Thread_RequestExit(thread);
        Thread_Join(thread);

        // OS-specific release of thread resources

        #if defined(RMT_PLATFORM_WINDOWS)
        CloseHandle(thread->handle);
        thread->handle = NULL;
        #endif
    }

    free(thread);
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @SAFEC: Safe C Library excerpts
   http://sourceforge.net/projects/safeclib/
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



/*------------------------------------------------------------------
 *
 * November 2008, Bo Berry
 *
 * Copyright (c) 2008-2011 by Cisco Systems, Inc
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *------------------------------------------------------------------
 */


// NOTE: Microsoft also has its own version of these functions so I'm do some hacky PP to remove them
#define strnlen_s strnlen_s_safe_c
#define strncat_s strncat_s_safe_c


#define RSIZE_MAX_STR (4UL << 10)   /* 4KB */
#define RCNEGATE(x) x


#define EOK             ( 0 )
#define ESNULLP         ( 400 )       /* null ptr                    */
#define ESZEROL         ( 401 )       /* length is zero              */
#define ESLEMAX         ( 403 )       /* length exceeds max          */
#define ESOVRLP         ( 404 )       /* overlap undefined           */
#define ESNOSPC         ( 406 )       /* not enough space for s2     */
#define ESUNTERM        ( 407 )       /* unterminated string         */
#define ESNOTFND        ( 409 )       /* not found                   */

#ifndef _ERRNO_T_DEFINED
#define _ERRNO_T_DEFINED
typedef int errno_t;
#endif

#if !defined(_WIN64) && !defined(__APPLE__)
typedef unsigned int rsize_t;
#endif


static rsize_t
strnlen_s (const char *dest, rsize_t dmax)
{
    rsize_t count;

    if (dest == NULL) {
        return RCNEGATE(0);
    }

    if (dmax == 0) {
        return RCNEGATE(0);
    }

    if (dmax > RSIZE_MAX_STR) {
        return RCNEGATE(0);
    }

    count = 0;
    while (*dest && dmax) {
        count++;
        dmax--;
        dest++;
    }

    return RCNEGATE(count);
}


static errno_t
strstr_s (char *dest, rsize_t dmax,
          const char *src, rsize_t slen, char **substring)
{
    rsize_t len;
    rsize_t dlen;
    int i;

    if (substring == NULL) {
        return RCNEGATE(ESNULLP);
    }
    *substring = NULL;

    if (dest == NULL) {
        return RCNEGATE(ESNULLP);
    }

    if (dmax == 0) {
        return RCNEGATE(ESZEROL);
    }

    if (dmax > RSIZE_MAX_STR) {
        return RCNEGATE(ESLEMAX);
    }

    if (src == NULL) {
        return RCNEGATE(ESNULLP);
    }

    if (slen == 0) {
        return RCNEGATE(ESZEROL);
    }

    if (slen > RSIZE_MAX_STR) {
        return RCNEGATE(ESLEMAX);
    }

    /*
     * src points to a string with zero length, or
     * src equals dest, return dest
     */
    if (*src == '\0' || dest == src) {
        *substring = dest;
        return RCNEGATE(EOK);
    }

    while (*dest && dmax) {
        i = 0;
        len = slen;
        dlen = dmax;

        while (src[i] && dlen) {

            /* not a match, not a substring */
            if (dest[i] != src[i]) {
                break;
            }

            /* move to the next char */
            i++;
            len--;
            dlen--;

            if (src[i] == '\0' || !len) {
                *substring = dest;
                return RCNEGATE(EOK);
            }
        }
        dest++;
        dmax--;
    }

    /*
     * substring was not found, return NULL
     */
    *substring = NULL;
    return RCNEGATE(ESNOTFND);
}


static errno_t
strncat_s (char *dest, rsize_t dmax, const char *src, rsize_t slen)
{
    rsize_t orig_dmax;
    char *orig_dest;
    const char *overlap_bumper;

    if (dest == NULL) {
        return RCNEGATE(ESNULLP);
    }

    if (src == NULL) {
        return RCNEGATE(ESNULLP);
    }

    if (slen > RSIZE_MAX_STR) {
        return RCNEGATE(ESLEMAX);
    }

    if (dmax == 0) {
        return RCNEGATE(ESZEROL);
    }

    if (dmax > RSIZE_MAX_STR) {
        return RCNEGATE(ESLEMAX);
    }

    /* hold base of dest in case src was not copied */
    orig_dmax = dmax;
    orig_dest = dest;

    if (dest < src) {
        overlap_bumper = src;

        /* Find the end of dest */
        while (*dest != '\0') {

            if (dest == overlap_bumper) {
                return RCNEGATE(ESOVRLP);
            }

            dest++;
            dmax--;
            if (dmax == 0) {
                return RCNEGATE(ESUNTERM);
            }
        }

        while (dmax > 0) {
            if (dest == overlap_bumper) {
                return RCNEGATE(ESOVRLP);
            }

            /*
             * Copying truncated before the source null is encountered
             */
            if (slen == 0) {
                *dest = '\0';
                return RCNEGATE(EOK);
            }

            *dest = *src;
            if (*dest == '\0') {
                return RCNEGATE(EOK);
            }

            dmax--;
            slen--;
            dest++;
            src++;
        }

    } else {
        overlap_bumper = dest;

        /* Find the end of dest */
        while (*dest != '\0') {

            /*
             * NOTE: no need to check for overlap here since src comes first
             * in memory and we're not incrementing src here.
             */
            dest++;
            dmax--;
            if (dmax == 0) {
                return RCNEGATE(ESUNTERM);
            }
        }

        while (dmax > 0) {
            if (src == overlap_bumper) {
                return RCNEGATE(ESOVRLP);
            }

            /*
             * Copying truncated
             */
            if (slen == 0) {
                *dest = '\0';
                return RCNEGATE(EOK);
            }

            *dest = *src;
            if (*dest == '\0') {
                return RCNEGATE(EOK);
            }

            dmax--;
            slen--;
            dest++;
            src++;
        }
    }

    /*
     * the entire src was not copied, so the string will be nulled.
     */
    return RCNEGATE(ESNOSPC);
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @OBJALLOC: Reusable Object Allocator
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



//
// All objects that require free-list-backed allocation need to inherit from this type.
//
typedef struct ObjectLink
{
    struct ObjectLink* volatile next;
} ObjectLink;


static void ObjectLink_Constructor(ObjectLink* link)
{
    assert(link != NULL);
    link->next = NULL;
}


typedef enum rmtError (*ObjConstructor)(void*);
typedef void (*ObjDestructor)(void*);


typedef struct
{
    // Object create/destroy parameters
    rmtU32 object_size;
    ObjConstructor constructor;
    ObjDestructor destructor;

    // Number of objects in the free list
    volatile rmtS32 nb_free;

    // Number of objects used by callers
    volatile rmtS32 nb_inuse;

    // Total allocation count
    volatile rmtS32 nb_allocated;

    ObjectLink* first_free;
} ObjectAllocator;


static enum rmtError ObjectAllocator_Create(ObjectAllocator** allocator, rmtU32 object_size, ObjConstructor constructor, ObjDestructor destructor)
{
    // Allocate space for the allocator
    assert(allocator != NULL);
    *allocator = (ObjectAllocator*)malloc(sizeof(ObjectAllocator));
    if (*allocator == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Construct it
    (*allocator)->object_size = object_size;
    (*allocator)->constructor = constructor;
    (*allocator)->destructor = destructor;
    (*allocator)->nb_free = 0;
    (*allocator)->nb_inuse = 0;
    (*allocator)->nb_allocated = 0;
    (*allocator)->first_free = NULL;

    return RMT_ERROR_NONE;
}


static void ObjectAllocator_Push(ObjectAllocator* allocator, ObjectLink* start, ObjectLink* end)
{
    assert(allocator != NULL);
    assert(start != NULL);
    assert(end != NULL);

    // CAS pop add range to the front of the list
    while (1)
    {
        ObjectLink* old_link = (ObjectLink*)allocator->first_free;
        end->next = old_link;
        if (AtomicCompareAndSwapPointer((long* volatile*)&allocator->first_free, (long*)old_link, (long*)start) == RMT_TRUE)
            break;
    }
}


static ObjectLink* ObjectAllocator_Pop(ObjectAllocator* allocator)
{
    ObjectLink* link;

    assert(allocator != NULL);
    assert(allocator->first_free != NULL);

    // CAS pop from the front of the list
    while (1)
    {
        ObjectLink* old_link = (ObjectLink*)allocator->first_free;
        ObjectLink* next_link = old_link->next;
        if (AtomicCompareAndSwapPointer((long* volatile*)&allocator->first_free, (long*)old_link, (long*)next_link) == RMT_TRUE)
        {
            link = old_link;
            break;
        }
    }

    link->next = NULL;

    return link;
}


static enum rmtError ObjectAllocator_Alloc(ObjectAllocator* allocator, void** object)
{
    // This function only calls the object constructor on initial malloc of an object

    assert(allocator != NULL);
    assert(object != NULL);

    // Has the free list run out?
    if (allocator->first_free == NULL)
    {
        enum rmtError error;

        // Allocate/construct a new object
        void* free_object = malloc(allocator->object_size);
        if (free_object == NULL)
            return RMT_ERROR_MALLOC_FAIL;
        assert(allocator->constructor != NULL);
        error = allocator->constructor(free_object);
        if (error != RMT_ERROR_NONE)
        {
            // Auto-teardown on failure
            assert(allocator->destructor != NULL);
            allocator->destructor(free_object);
            free(free_object);
            return error;
        }

        // Add to the free list
        ObjectAllocator_Push(allocator, (ObjectLink*)free_object, (ObjectLink*)free_object);
        AtomicAdd(&allocator->nb_allocated, 1);
        AtomicAdd(&allocator->nb_free, 1);
    }

    // Pull available objects from the free list
    *object = ObjectAllocator_Pop(allocator);
    AtomicSub(&allocator->nb_free, 1);
    AtomicAdd(&allocator->nb_inuse, 1);

    return RMT_ERROR_NONE;
}


static void ObjectAllocator_Free(ObjectAllocator* allocator, void* object)
{
    // Add back to the free-list
    assert(allocator != NULL);
    ObjectAllocator_Push(allocator, (ObjectLink*)object, (ObjectLink*)object);
    AtomicSub(&allocator->nb_inuse, 1);
    AtomicAdd(&allocator->nb_free, 1);
}


static void ObjectAllocator_FreeRange(ObjectAllocator* allocator, void* start, void* end, rmtU32 count)
{
    assert(allocator != NULL);
    ObjectAllocator_Push(allocator, (ObjectLink*)start, (ObjectLink*)end);
    AtomicSub(&allocator->nb_inuse, count);
    AtomicAdd(&allocator->nb_free, count);
}


static void ObjectAllocator_Destroy(ObjectAllocator* allocator)
{
    // Ensure everything has been released to the allocator
    assert(allocator != NULL);
    assert(allocator->nb_inuse == 0);

    // Destroy all objects released to the allocator
    assert(allocator != NULL);
    while (allocator->first_free != NULL)
    {
        ObjectLink* next = allocator->first_free->next;
        allocator->destructor(allocator->first_free);
        free(allocator->first_free);
        allocator->first_free = next;
    }

    free(allocator);
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @DYNBUF: Dynamic Buffer
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct
{
    rmtU32 alloc_granularity;

    rmtU32 bytes_allocated;
    rmtU32 bytes_used;

    rmtU8* data;
} Buffer;


static enum rmtError Buffer_Create(Buffer** buffer, rmtU32 alloc_granularity)
{
    assert(buffer != NULL);

    // Allocate and set defaults as nothing allocated

    *buffer = (Buffer*)malloc(sizeof(Buffer));
    if (*buffer == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    (*buffer)->alloc_granularity = alloc_granularity;
    (*buffer)->bytes_allocated = 0;
    (*buffer)->bytes_used = 0;
    (*buffer)->data = NULL;

    return RMT_ERROR_NONE;
}


static void Buffer_Destroy(Buffer* buffer)
{
    assert(buffer != NULL);

    if (buffer->data != NULL)
    {
        free(buffer->data);
        buffer->data = NULL;
    }

    free(buffer);
}


static enum rmtError Buffer_Write(Buffer* buffer, void* data, rmtU32 length)
{
    assert(buffer != NULL);

    // Reallocate the buffer on overflow
    if (buffer->bytes_used + length > buffer->bytes_allocated)
    {
        // Calculate size increase rounded up to the requested allocation granularity
        rmtU32 g = buffer->alloc_granularity;
        rmtU32 a = buffer->bytes_allocated + length;
        a = a + ((g - 1) - ((a - 1) % g));
        buffer->bytes_allocated = a;
        buffer->data = (rmtU8*)realloc(buffer->data, buffer->bytes_allocated);
        if (buffer->data == NULL)
            return RMT_ERROR_MALLOC_FAIL;
    }

    // Copy all bytes
    memcpy(buffer->data + buffer->bytes_used, data, length);
    buffer->bytes_used += length;

    // NULL terminate (if possible) for viewing in debug
    if (buffer->bytes_used < buffer->bytes_allocated)
        buffer->data[buffer->bytes_used] = 0;

    return RMT_ERROR_NONE;
}


static enum rmtError Buffer_WriteString(Buffer* buffer, rmtPStr string)
{
    assert(string != NULL);
    return Buffer_Write(buffer, (void*)string, strnlen_s(string, 2048));
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @SOCKETS: Sockets TCP/IP Wrapper
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/

#ifndef RMT_PLATFORM_WINDOWS
    typedef int SOCKET;
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR   -1
    #define SD_SEND SHUT_WR
    #define closesocket close
#endif
typedef struct
{
    SOCKET socket;
} TCPSocket;


typedef struct
{
    rmtBool can_read;
    rmtBool can_write;
    enum rmtError error_state;
} SocketStatus;


//
// Function prototypes
//
static void TCPSocket_Close(TCPSocket* tcp_socket);
static enum rmtError TCPSocket_Destroy(TCPSocket** tcp_socket, enum rmtError error);


static enum rmtError InitialiseNetwork()
{
    #ifdef RMT_PLATFORM_WINDOWS

        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data))
            return RMT_ERROR_SOCKET_INIT_NETWORK_FAIL;
        if (LOBYTE(wsa_data.wVersion) != 2 || HIBYTE(wsa_data.wVersion) != 2)
            return RMT_ERROR_SOCKET_INIT_NETWORK_FAIL;

        return RMT_ERROR_NONE;

    #else

        return RMT_ERROR_NONE;

    #endif
}


static void ShutdownNetwork()
{
    #ifdef RMT_PLATFORM_WINDOWS
        WSACleanup();
    #endif
}


static enum rmtError TCPSocket_Create(TCPSocket** tcp_socket)
{
    enum rmtError error;
    
    assert(tcp_socket != NULL);

    // Allocate and initialise
    *tcp_socket = (TCPSocket*)malloc(sizeof(TCPSocket));
    if (*tcp_socket == NULL)
        return RMT_ERROR_MALLOC_FAIL;
    (*tcp_socket)->socket = INVALID_SOCKET;

    error = InitialiseNetwork();
    if (error != RMT_ERROR_NONE)
        return TCPSocket_Destroy(tcp_socket, error);

    return RMT_ERROR_NONE;
}


static enum rmtError TCPSocket_CreateServer(rmtU16 port, TCPSocket** tcp_socket)
{
    SOCKET s = INVALID_SOCKET;
    struct sockaddr_in sin = { 0 };
    #ifdef RMT_PLATFORM_WINDOWS
        u_long nonblock = 1;
    #endif

    // Create socket container
    enum rmtError error = TCPSocket_Create(tcp_socket);
    if (error != RMT_ERROR_NONE)
        return error;

    // Try to create the socket
    s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == SOCKET_ERROR)
        return TCPSocket_Destroy(tcp_socket, RMT_ERROR_SOCKET_CREATE_FAIL);

    // Bind the socket to the incoming port
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    if (bind(s, (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR)
        return TCPSocket_Destroy(tcp_socket, RMT_ERROR_SOCKET_BIND_FAIL);

    // Connection is valid, remaining code is socket state modification
    (*tcp_socket)->socket = s;

    // Enter a listening state with a backlog of 1 connection
    if (listen(s, 1) == SOCKET_ERROR)
        return TCPSocket_Destroy(tcp_socket, RMT_ERROR_SOCKET_LISTEN_FAIL);

    // Set as non-blocking
    #ifdef RMT_PLATFORM_WINDOWS
        if (ioctlsocket((*tcp_socket)->socket, FIONBIO, &nonblock) == SOCKET_ERROR)
            return TCPSocket_Destroy(tcp_socket, RMT_ERROR_SOCKET_SET_NON_BLOCKING_FAIL);
    #else
        if (fcntl((*tcp_socket)->socket, F_SETFL, O_NONBLOCK) == SOCKET_ERROR)
            return TCPSocket_Destroy(tcp_socket, RMT_ERROR_SOCKET_SET_NON_BLOCKING_FAIL);
    #endif

    return RMT_ERROR_NONE;
}


static enum rmtError TCPSocket_Destroy(TCPSocket** tcp_socket, enum rmtError error)
{
    assert(tcp_socket != NULL);

    TCPSocket_Close(*tcp_socket);
    ShutdownNetwork();

    free(*tcp_socket);
    *tcp_socket = NULL;

    return error;
}


static void TCPSocket_Close(TCPSocket* tcp_socket)
{
    assert(tcp_socket != NULL);

    if (tcp_socket->socket != INVALID_SOCKET)
    {
        // Shutdown the connection, stopping all sends
        int result = shutdown(tcp_socket->socket, SD_SEND);
        if (result != SOCKET_ERROR)
        {
            // Keep receiving until the peer closes the connection
            int total = 0;
            char temp_buf[128];
            while (result > 0)
            {
                result = (int)recv(tcp_socket->socket, temp_buf, sizeof(temp_buf), 0);
                total += result;
            }
        }

        // Close the socket and issue a network shutdown request
        closesocket(tcp_socket->socket);
        tcp_socket->socket = INVALID_SOCKET;
    }
}


static SocketStatus TCPSocket_PollStatus(TCPSocket* tcp_socket)
{
    SocketStatus status;
    fd_set fd_read, fd_write, fd_errors;
    struct timeval tv;

    status.can_read = RMT_FALSE;
    status.can_write = RMT_FALSE;
    status.error_state = RMT_ERROR_NONE;

    assert(tcp_socket != NULL);
    if (tcp_socket->socket == INVALID_SOCKET)
    {
        status.error_state = RMT_ERROR_SOCKET_INVALID_POLL;
        return status;
    }

    // Set read/write/error markers for the socket
    FD_ZERO(&fd_read);
    FD_ZERO(&fd_write);
    FD_ZERO(&fd_errors);
    FD_SET(tcp_socket->socket, &fd_read);
    FD_SET(tcp_socket->socket, &fd_write);
    FD_SET(tcp_socket->socket, &fd_errors);

    // Poll socket status without blocking
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(tcp_socket->socket+1, &fd_read, &fd_write, &fd_errors, &tv) == SOCKET_ERROR)
    {
        status.error_state = RMT_ERROR_SOCKET_SELECT_FAIL;
        return status;
    }

    status.can_read = FD_ISSET(tcp_socket->socket, &fd_read) != 0 ? RMT_TRUE : RMT_FALSE;
    status.can_write = FD_ISSET(tcp_socket->socket, &fd_write) != 0 ? RMT_TRUE : RMT_FALSE;
    status.error_state = FD_ISSET(tcp_socket->socket, &fd_errors) != 0 ? RMT_ERROR_SOCKET_POLL_ERRORS : RMT_ERROR_NONE;
    return status;
}


static enum rmtError TCPSocket_AcceptConnection(TCPSocket* tcp_socket, TCPSocket** client_socket)
{
    SocketStatus status;
    SOCKET s;
    enum rmtError error;

    // Ensure there is an incoming connection
    assert(tcp_socket != NULL);
    status = TCPSocket_PollStatus(tcp_socket);
    if (status.error_state != RMT_ERROR_NONE || !status.can_read)
        return status.error_state;

    // Accept the connection
    s = accept(tcp_socket->socket, 0, 0);
    if (s == SOCKET_ERROR)
        return RMT_ERROR_SOCKET_ACCEPT_FAIL;

    // Create a client socket for the new connection
    assert(client_socket != NULL);
    error = TCPSocket_Create(client_socket);
    if (error != RMT_ERROR_NONE)
        return error;
    (*client_socket)->socket = s;

    return RMT_ERROR_NONE;
}

int TCPSocketWouldBlock()
{
#ifdef RMT_PLATFORM_WINDOWS
    DWORD error = WSAGetLastError();
    return (error == WSAEWOULDBLOCK);
 #else
    int error = errno;
    return (error == EAGAIN || error == EWOULDBLOCK);
#endif

}

static enum rmtError TCPSocket_Send(TCPSocket* tcp_socket, const void* data, rmtU32 length, rmtU32 timeout_ms)
{
    SocketStatus status;
    char* cur_data = NULL;
    char* end_data = NULL;
    rmtU32 start_ms = 0;
    rmtU32 cur_ms = 0;

    assert(tcp_socket != NULL);

    // Can't send if there are socket errors
    status = TCPSocket_PollStatus(tcp_socket);
    if (status.error_state != RMT_ERROR_NONE)
        return status.error_state;
    if (!status.can_write)
        return RMT_ERROR_SOCKET_SEND_TIMEOUT;

    cur_data = (char*)data;
    end_data = cur_data + length;

    start_ms = msTimer_Get();
    while (cur_data < end_data)
    {
        // Attempt to send the remaining chunk of data
        int bytes_sent = (int)send(tcp_socket->socket, cur_data, end_data - cur_data, 0);

        if (bytes_sent == SOCKET_ERROR || bytes_sent == 0)
        {
            // Close the connection if sending fails for any other reason other than blocking
            if (bytes_sent != 0 && !TCPSocketWouldBlock())
                return RMT_ERROR_SOCKET_SEND_FAIL;

            // First check for tick-count overflow and reset, giving a slight hitch every 49.7 days
            cur_ms = msTimer_Get();
            if (cur_ms < start_ms)
            {
                start_ms = cur_ms;
                continue;
            }

            //
            // Timeout can happen when:
            //
            //    1) endpoint is no longer there
            //    2) endpoint can't consume quick enough
            //    3) local buffers overflow
            //
            // As none of these are actually errors, we have to pass this timeout back to the caller.
            //
            // TODO: This strategy breaks down if a send partially completes and then times out!
            //
            if (cur_ms - start_ms > timeout_ms)
            {
                return RMT_ERROR_SOCKET_SEND_TIMEOUT;
            }
        }
        else
        {
            // Jump over the data sent
            cur_data += bytes_sent;
        }
    }

    return RMT_ERROR_NONE;
}


static enum rmtError TCPSocket_Receive(TCPSocket* tcp_socket, void* data, rmtU32 length, rmtU32 timeout_ms)
{
    SocketStatus status;
    char* cur_data = NULL;
    char* end_data = NULL;
    rmtU32 start_ms = 0;
    rmtU32 cur_ms = 0;

    assert(tcp_socket != NULL);

    // Ensure there is data to receive
    status = TCPSocket_PollStatus(tcp_socket);
    if (status.error_state != RMT_ERROR_NONE)
        return status.error_state;
    if (!status.can_read)
        return RMT_ERROR_SOCKET_RECV_NO_DATA;

    cur_data = (char*)data;
    end_data = cur_data + length;

    // Loop until all data has been received
    start_ms = msTimer_Get();
    while (cur_data < end_data)
    {
        int bytes_received = (int)recv(tcp_socket->socket, cur_data, end_data - cur_data, 0);

        if (bytes_received == SOCKET_ERROR || bytes_received == 0)
        {
            // Close the connection if receiving fails for any other reason other than blocking
            if (bytes_received != 0 && !TCPSocketWouldBlock())
                return RMT_ERROR_SOCKET_RECV_FAILED;

            // First check for tick-count overflow and reset, giving a slight hitch every 49.7 days
            cur_ms = msTimer_Get();
            if (cur_ms < start_ms)
            {
                start_ms = cur_ms;
                continue;
            }

            //
            // Timeout can happen when:
            //
            //    1) data is delayed by sender
            //    2) sender fails to send a complete set of packets
            //
            // As not all of these scenarios are errors, we need to pass this information back to the caller.
            //
            // TODO: This strategy breaks down if a receive partially completes and then times out!
            //
            if (cur_ms - start_ms > timeout_ms)
            {
                return RMT_ERROR_SOCKET_RECV_TIMEOUT;
            }
        }
        else
        {
            // Jump over the data received
            cur_data += bytes_received;
        }
    }

    return RMT_ERROR_NONE;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @SHA1: SHA-1 Cryptographic Hash Function
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/


//
// Typed to allow enforced data size specification
//
typedef struct
{
    rmtU8 data[20];
} SHA1;


/*
 Copyright (c) 2011, Micael Hildenborg
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Micael Hildenborg nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY Micael Hildenborg ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL Micael Hildenborg BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 Contributors:
 Gustav
 Several members in the gamedev.se forum.
 Gregory Petrosyan
 */


// Rotate an integer value to left.
static unsigned int rol(const unsigned int value, const unsigned int steps)
{
    return ((value << steps) | (value >> (32 - steps)));
}


// Sets the first 16 integers in the buffert to zero.
// Used for clearing the W buffert.
static void clearWBuffert(unsigned int* buffert)
{
    int pos;
    for (pos = 16; --pos >= 0;)
    {
        buffert[pos] = 0;
    }
}

static void innerHash(unsigned int* result, unsigned int* w)
{
    unsigned int a = result[0];
    unsigned int b = result[1];
    unsigned int c = result[2];
    unsigned int d = result[3];
    unsigned int e = result[4];

    int round = 0;

    #define sha1macro(func,val) \
    { \
        const unsigned int t = rol(a, 5) + (func) + e + val + w[round]; \
        e = d; \
        d = c; \
        c = rol(b, 30); \
        b = a; \
        a = t; \
    }

    while (round < 16)
    {
        sha1macro((b & c) | (~b & d), 0x5a827999)
        ++round;
    }
    while (round < 20)
    {
        w[round] = rol((w[round - 3] ^ w[round - 8] ^ w[round - 14] ^ w[round - 16]), 1);
        sha1macro((b & c) | (~b & d), 0x5a827999)
        ++round;
    }
    while (round < 40)
    {
        w[round] = rol((w[round - 3] ^ w[round - 8] ^ w[round - 14] ^ w[round - 16]), 1);
        sha1macro(b ^ c ^ d, 0x6ed9eba1)
        ++round;
    }
    while (round < 60)
    {
        w[round] = rol((w[round - 3] ^ w[round - 8] ^ w[round - 14] ^ w[round - 16]), 1);
        sha1macro((b & c) | (b & d) | (c & d), 0x8f1bbcdc)
        ++round;
    }
    while (round < 80)
    {
        w[round] = rol((w[round - 3] ^ w[round - 8] ^ w[round - 14] ^ w[round - 16]), 1);
        sha1macro(b ^ c ^ d, 0xca62c1d6)
        ++round;
    }

    #undef sha1macro

    result[0] += a;
    result[1] += b;
    result[2] += c;
    result[3] += d;
    result[4] += e;
}


static void calc(const void* src, const int bytelength, unsigned char* hash)
{
    int roundPos;
    int lastBlockBytes;
    int hashByte;

    // Init the result array.
    unsigned int result[5] = { 0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0 };

    // Cast the void src pointer to be the byte array we can work with.
    const unsigned char* sarray = (const unsigned char*) src;

    // The reusable round buffer
    unsigned int w[80];

    // Loop through all complete 64byte blocks.
    const int endOfFullBlocks = bytelength - 64;
    int endCurrentBlock;
    int currentBlock = 0;

    while (currentBlock <= endOfFullBlocks)
    {
        endCurrentBlock = currentBlock + 64;

        // Init the round buffer with the 64 byte block data.
        for (roundPos = 0; currentBlock < endCurrentBlock; currentBlock += 4)
        {
            // This line will swap endian on big endian and keep endian on little endian.
            w[roundPos++] = (unsigned int) sarray[currentBlock + 3]
                | (((unsigned int) sarray[currentBlock + 2]) << 8)
                | (((unsigned int) sarray[currentBlock + 1]) << 16)
                | (((unsigned int) sarray[currentBlock]) << 24);
        }
        innerHash(result, w);
    }

    // Handle the last and not full 64 byte block if existing.
    endCurrentBlock = bytelength - currentBlock;
    clearWBuffert(w);
    lastBlockBytes = 0;
    for (;lastBlockBytes < endCurrentBlock; ++lastBlockBytes)
    {
        w[lastBlockBytes >> 2] |= (unsigned int) sarray[lastBlockBytes + currentBlock] << ((3 - (lastBlockBytes & 3)) << 3);
    }
    w[lastBlockBytes >> 2] |= 0x80 << ((3 - (lastBlockBytes & 3)) << 3);
    if (endCurrentBlock >= 56)
    {
        innerHash(result, w);
        clearWBuffert(w);
    }
    w[15] = bytelength << 3;
    innerHash(result, w);

    // Store hash in result pointer, and make sure we get in in the correct order on both endian models.
    for (hashByte = 20; --hashByte >= 0;)
    {
        hash[hashByte] = (result[hashByte >> 2] >> (((3 - hashByte) & 0x3) << 3)) & 0xff;
    }
}


static SHA1 SHA1_Calculate(const void* src, unsigned int length)
{
    SHA1 hash;
    assert((int)length >= 0);
    calc(src, length, hash.data);
    return hash;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @BASE64: Base-64 encoder
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



static const char* b64_encoding_table =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";


static rmtU32 Base64_CalculateEncodedLength(rmtU32 length)
{
    // ceil(l * 4/3)
    return 4 * ((length + 2) / 3);
}


static void Base64_Encode(const rmtU8* in_bytes, rmtU32 length, rmtU8* out_bytes)
{
    rmtU32 i;
    rmtU32 encoded_length;
    rmtU32 remaining_bytes;

    rmtU8* optr = out_bytes;

    for (i = 0; i < length; )
    {
        // Read input 3 values at a time, null terminating
        rmtU32 c0 = i < length ? in_bytes[i++] : 0;
        rmtU32 c1 = i < length ? in_bytes[i++] : 0;
        rmtU32 c2 = i < length ? in_bytes[i++] : 0;

        // Encode 4 bytes for ever 3 input bytes
        rmtU32 triple = (c0 << 0x10) + (c1 << 0x08) + c2;
        *optr++ = b64_encoding_table[(triple >> 3 * 6) & 0x3F];
        *optr++ = b64_encoding_table[(triple >> 2 * 6) & 0x3F];
        *optr++ = b64_encoding_table[(triple >> 1 * 6) & 0x3F];
        *optr++ = b64_encoding_table[(triple >> 0 * 6) & 0x3F];
    }

    // Pad output to multiple of 3 bytes with terminating '='
    encoded_length = Base64_CalculateEncodedLength(length);
    remaining_bytes = (3 - ((length + 2) % 3)) - 1;
    for (i = 0; i < remaining_bytes; i++)
        out_bytes[encoded_length - 1 - i] = '=';

    // Null terminate
    out_bytes[encoded_length] = 0;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @MURMURHASH: MurmurHash3
   https://code.google.com/p/smhasher
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/


//-----------------------------------------------------------------------------
// MurmurHash3 was written by Austin Appleby, and is placed in the public
// domain. The author hereby disclaims copyright to this source code.
//-----------------------------------------------------------------------------


static rmtU32 rotl32(rmtU32 x, rmtS8 r)
{
    return (x << r) | (x >> (32 - r));
}


// Block read - if your platform needs to do endian-swapping or can only
// handle aligned reads, do the conversion here
static rmtU32 getblock32(const rmtU32* p, int i)
{
    return p[i];
}


// Finalization mix - force all bits of a hash block to avalanche
static rmtU32 fmix32(rmtU32 h)
{
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}


static rmtU32 MurmurHash3_x86_32(const void* key, int len, rmtU32 seed)
{
    const rmtU8* data = (const rmtU8*)key;
    const int nblocks = len / 4;

    rmtU32 h1 = seed;

    const rmtU32 c1 = 0xcc9e2d51;
    const rmtU32 c2 = 0x1b873593;

    int i;

    const rmtU32 * blocks = (const rmtU32 *)(data + nblocks*4);
    const rmtU8 * tail = (const rmtU8*)(data + nblocks*4);

    rmtU32 k1 = 0;

    //----------
    // body

    for (i = -nblocks; i; i++)
    {
        rmtU32 k1 = getblock32(blocks,i);

        k1 *= c1;
        k1 = rotl32(k1,15);
        k1 *= c2;

        h1 ^= k1;
        h1 = rotl32(h1,13); 
        h1 = h1*5+0xe6546b64;
    }

    //----------
    // tail

    switch(len & 3)
    {
    case 3: k1 ^= tail[2] << 16;
    case 2: k1 ^= tail[1] << 8;
    case 1: k1 ^= tail[0];
        k1 *= c1;
        k1 = rotl32(k1,15);
        k1 *= c2;
        h1 ^= k1;
    };

    //----------
    // finalization

    h1 ^= len;

    h1 = fmix32(h1);

    return h1;
} 



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @WEBSOCKETS: WebSockets
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



enum WebSocketMode
{
    WEBSOCKET_NONE = 0,
    WEBSOCKET_TEXT = 1,
    WEBSOCKET_BINARY = 2,
};


typedef struct
{
    TCPSocket* tcp_socket;

    enum WebSocketMode mode;

    rmtU32 frame_bytes_remaining;
    rmtU32 mask_offset;

    rmtU8 data_mask[4];
} WebSocket;


static void WebSocket_Destroy(WebSocket* web_socket);


static char* GetField(char* buffer, rsize_t buffer_length, rmtPStr field_name)
{
    char* field = NULL;
    char* buffer_end = buffer + buffer_length - 1;

    rsize_t field_length = strnlen_s(field_name, buffer_length);
    if (field_length == 0)
        return NULL;

    // Search for the start of the field
    if (strstr_s(buffer, buffer_length, field_name, field_length, &field) != EOK)
        return NULL;

    // Field name is now guaranteed to be in the buffer so its safe to jump over it without hitting the bounds
    field += strlen(field_name);

    // Skip any trailing whitespace
    while (*field == ' ')
    {
        if (field >= buffer_end)
            return NULL;
        field++;
    }

    return field;
}


static const char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
static const char websocket_response[] =
    "HTTP/1.1 101 Switching Protocols\r\n"
    "Upgrade: websocket\r\n"
    "Connection: Upgrade\r\n"
    "Sec-WebSocket-Accept: ";


static enum rmtError WebSocketHandshake(TCPSocket* tcp_socket, rmtPStr limit_host)
{
    rmtU32 start_ms, now_ms;

    // Parsing scratchpad
    char buffer[1024];
    char* buffer_ptr = buffer;
    int buffer_len = sizeof(buffer) - 1;
    char* buffer_end = buffer + buffer_len;

    char response_buffer[256];
    int response_buffer_len = sizeof(response_buffer) - 1;

    char* version;
    char* host;
    char* key;
    char* key_end;
    SHA1 hash;

    assert(tcp_socket != NULL);

    start_ms = msTimer_Get();

    // Really inefficient way of receiving the handshake data from the browser
    // Not really sure how to do this any better, as the termination requirement is \r\n\r\n
    while (buffer_ptr - buffer < buffer_len)
    {
        enum rmtError error = TCPSocket_Receive(tcp_socket, buffer_ptr, 1, 20);
        if (error == RMT_ERROR_SOCKET_RECV_FAILED)
            return error;

        // If there's a stall receiving the data, check for a handshake timeout
        if (error == RMT_ERROR_SOCKET_RECV_NO_DATA || error == RMT_ERROR_SOCKET_RECV_TIMEOUT)
        {
            now_ms = msTimer_Get();
            if (now_ms - start_ms > 1000)
                return RMT_ERROR_SOCKET_RECV_TIMEOUT;

            continue;
        }

        // Just in case new enums are added...
        assert(error == RMT_ERROR_NONE);

        if (buffer_ptr - buffer >= 4)
        {
            if (*(buffer_ptr - 3) == '\r' &&
                *(buffer_ptr - 2) == '\n' &&
                *(buffer_ptr - 1) == '\r' &&
                *(buffer_ptr - 0) == '\n')
                break;
        }

        buffer_ptr++;
    }
    *buffer_ptr = 0;

    // HTTP GET instruction
    if (memcmp(buffer, "GET", 3) != 0)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_NOT_GET;

    // Look for the version number and verify that it's supported
    version = GetField(buffer, buffer_len, "Sec-WebSocket-Version:");
    if (version == NULL)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_NO_VERSION;
    if (buffer_end - version < 2 || (version[0] != '8' && (version[0] != '1' || version[1] != '3')))
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_BAD_VERSION;

    // Make sure this connection comes from a known host
    host = GetField(buffer, buffer_len, "Host:");
    if (host == NULL)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_NO_HOST;
    if (limit_host != NULL)
    {
        rsize_t limit_host_len = strnlen_s(limit_host, 128);
        char* found = NULL;
        if (strstr_s(host, buffer_end - host, limit_host, limit_host_len, &found) != EOK)
            return RMT_ERROR_WEBSOCKET_HANDSHAKE_BAD_HOST;
    }

    // Look for the key start and null-terminate it within the receive buffer
    key = GetField(buffer, buffer_len, "Sec-WebSocket-Key:");
    if (key == NULL)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_NO_KEY;
    if (strstr_s(key, buffer_end - key, "\r\n", 2, &key_end) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_BAD_KEY;
    *key_end = 0;

    // Concatenate the browser's key with the WebSocket Protocol GUID and base64 encode
    // the hash, to prove to the browser that this is a bonafide WebSocket server
    buffer[0] = 0;
    if (strncat_s(buffer, buffer_len, key, key_end - key) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_STRING_FAIL;
    if (strncat_s(buffer, buffer_len, websocket_guid, sizeof(websocket_guid)) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_STRING_FAIL;
    hash = SHA1_Calculate(buffer, strnlen_s(buffer, buffer_len));
    Base64_Encode(hash.data, sizeof(hash.data), (rmtU8*)buffer);

    // Send the response back to the server with a longer timeout than usual
    response_buffer[0] = 0;
    if (strncat_s(response_buffer, response_buffer_len, websocket_response, sizeof(websocket_response)) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_STRING_FAIL;
    if (strncat_s(response_buffer, response_buffer_len, buffer, buffer_len) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_STRING_FAIL;
    if (strncat_s(response_buffer, response_buffer_len, "\r\n\r\n", 4) != EOK)
        return RMT_ERROR_WEBSOCKET_HANDSHAKE_STRING_FAIL;

    return TCPSocket_Send(tcp_socket, response_buffer, strnlen_s(response_buffer, response_buffer_len), 1000);
}


static enum rmtError WebSocket_Create(WebSocket** web_socket)
{
    *web_socket = (WebSocket*)malloc(sizeof(WebSocket));
    if (*web_socket == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Set default state
    (*web_socket)->tcp_socket = NULL;
    (*web_socket)->mode = WEBSOCKET_NONE;
    (*web_socket)->frame_bytes_remaining = 0;
    (*web_socket)->mask_offset = 0;
    (*web_socket)->data_mask[0] = 0;
    (*web_socket)->data_mask[1] = 0;
    (*web_socket)->data_mask[2] = 0;
    (*web_socket)->data_mask[3] = 0;

    return RMT_ERROR_NONE;
}


static enum rmtError WebSocket_CreateServer(rmtU32 port, enum WebSocketMode mode, WebSocket** web_socket)
{
    enum rmtError error;

    assert(web_socket != NULL);

    error = WebSocket_Create(web_socket);
    if (error != RMT_ERROR_NONE)
        return error;

    (*web_socket)->mode = mode;

    // Create the server's listening socket
    error = TCPSocket_CreateServer(port, &(*web_socket)->tcp_socket);
    if (error != RMT_ERROR_NONE)
    {
        WebSocket_Destroy(*web_socket);
        *web_socket = NULL;
        return error;
    }

    return RMT_ERROR_NONE;
}


static void WebSocket_Close(WebSocket* web_socket)
{
    assert(web_socket != NULL);

    if (web_socket->tcp_socket != NULL)
    {
        TCPSocket_Destroy(&web_socket->tcp_socket, RMT_ERROR_NONE);
        web_socket->tcp_socket = NULL;
    }
}


static void WebSocket_Destroy(WebSocket* web_socket)
{
    assert(web_socket != NULL);
    WebSocket_Close(web_socket);
    free(web_socket);
}


static SocketStatus WebSocket_PollStatus(WebSocket* web_socket)
{
    assert(web_socket != NULL);
    return TCPSocket_PollStatus(web_socket->tcp_socket);
}


static enum rmtError WebSocket_AcceptConnection(WebSocket* web_socket, WebSocket** client_socket)
{
    TCPSocket* tcp_socket = NULL;
    enum rmtError error;

    // Is there a waiting connection?
    assert(web_socket != NULL);
    error = TCPSocket_AcceptConnection(web_socket->tcp_socket, &tcp_socket);
    if (error != RMT_ERROR_NONE || tcp_socket == NULL)
        return error;

    // Need a successful handshake between client/server before allowing the connection
    // TODO: Specify limit_host
    error = WebSocketHandshake(tcp_socket, NULL);
    if (error != RMT_ERROR_NONE)
        return error;

    // Allocate and return a new client socket
    assert(client_socket != NULL);
    error = WebSocket_Create(client_socket);
    if (error != RMT_ERROR_NONE)
        return error;

    (*client_socket)->tcp_socket = tcp_socket;
    (*client_socket)->mode = web_socket->mode;

    return RMT_ERROR_NONE;
}


static void WriteSize(rmtU32 size, rmtU8* dest, rmtU32 dest_size, rmtU32 dest_offset)
{
    int size_size = dest_size - dest_offset;
    rmtU32 i;
    for (i = 0; i < dest_size; i++)
    {
        int j = i - dest_offset;
        dest[i] = (j < 0) ? 0 : (size >> ((size_size - j - 1) * 8)) & 0xFF;
    }
}


static enum rmtError WebSocket_Send(WebSocket* web_socket, const void* data, rmtU32 length, rmtU32 timeout_ms)
{
    enum rmtError error;
    SocketStatus status;
    rmtU8 final_fragment, frame_type, frame_header[10];
    rmtU32 frame_header_size;

    assert(web_socket != NULL);

    // Can't send if there are socket errors
    status = WebSocket_PollStatus(web_socket);
    if (status.error_state != RMT_ERROR_NONE)
        return status.error_state;
    if (!status.can_write)
        return RMT_ERROR_SOCKET_SEND_TIMEOUT;

    final_fragment = 0x1 << 7;
    frame_type = (rmtU8)web_socket->mode;
    frame_header[0] = final_fragment | frame_type;

    // Construct the frame header, correctly applying the narrowest size
    frame_header_size = 0;
    if (length <= 125)
    {
        frame_header_size = 2;
        frame_header[1] = length;
    }
    else if (length <= 65535)
    {
        frame_header_size = 2 + 2;
        frame_header[1] = 126;
        WriteSize(length, frame_header + 2, 2, 0);
    }
    else
    {
        frame_header_size = 2 + 8;
        frame_header[1] = 127;
        WriteSize(length, frame_header + 2, 8, 4);
    }

    // Send frame header followed by data
    assert(data != NULL);
    error = TCPSocket_Send(web_socket->tcp_socket, frame_header, frame_header_size, timeout_ms);
    if (error != RMT_ERROR_NONE)
        return error;
    return TCPSocket_Send(web_socket->tcp_socket, data, length, timeout_ms);
}


static enum rmtError ReceiveFrameHeader(WebSocket* web_socket)
{
    // TODO: Specify infinite timeout?

    enum rmtError error;
    rmtU8 msg_header[2] = { 0, 0 };
    int msg_length, size_bytes_remaining, i;
    rmtBool mask_present;

    assert(web_socket != NULL);

    // Get message header
    error = TCPSocket_Receive(web_socket->tcp_socket, msg_header, 2, 20);
    if (error != RMT_ERROR_NONE)
        return error;

    // Check for WebSocket Protocol disconnect
    if (msg_header[0] == 0x88)
        return RMT_ERROR_WEBSOCKET_DISCONNECTED;

    // Check that the client isn't sending messages we don't understand
    if (msg_header[0] != 0x81 && msg_header[0] != 0x82)
        return RMT_ERROR_WEBSOCKET_BAD_FRAME_HEADER;

    // Get message length and check to see if it's a marker for a wider length
    msg_length = msg_header[1] & 0x7F;
    size_bytes_remaining = 0;
    switch (msg_length)
    {
        case 126: size_bytes_remaining = 2; break;
        case 127: size_bytes_remaining = 8; break;
    }

    if (size_bytes_remaining > 0)
    {
        // Receive the wider bytes of the length
        rmtU8 size_bytes[4];
        error = TCPSocket_Receive(web_socket->tcp_socket, size_bytes, size_bytes_remaining, 20);
        if (error != RMT_ERROR_NONE)
            return RMT_ERROR_WEBSOCKET_BAD_FRAME_HEADER_SIZE;

        // Calculate new length, MSB first
        msg_length = 0;
        for (i = 0; i < size_bytes_remaining; i++)
            msg_length |= size_bytes[i] << ((size_bytes_remaining - 1 - i) * 8);
    }

    // Receive any message data masks
    mask_present = (msg_header[1] & 0x80) != 0 ? RMT_TRUE : RMT_FALSE;
    if (mask_present)
    {
        error = TCPSocket_Receive(web_socket->tcp_socket, web_socket->data_mask, 4, 20);
        if (error != RMT_ERROR_NONE)
            return error;
    }

    web_socket->frame_bytes_remaining = msg_length;
    web_socket->mask_offset = 0;

    return RMT_ERROR_NONE;
}


static enum rmtError WebSocket_Receive(WebSocket* web_socket, void* data, rmtU32 length, rmtU32 timeout_ms)
{
    SocketStatus status;
    char* cur_data;
    char* end_data;
    rmtU32 start_ms, now_ms;
    rmtU32 bytes_to_read;
    enum rmtError error;

    assert(web_socket != NULL);

    // Ensure there is data to receive
    status = WebSocket_PollStatus(web_socket);
    if (status.error_state != RMT_ERROR_NONE)
        return status.error_state;
    if (!status.can_read)
        return RMT_ERROR_SOCKET_RECV_NO_DATA;

    cur_data = (char*)data;
    end_data = cur_data + length;

    start_ms = msTimer_Get();
    while (cur_data < end_data)
    {
        // Get next WebSocket frame if we've run out of data to read from the socket
        if (web_socket->frame_bytes_remaining == 0)
        {
            error = ReceiveFrameHeader(web_socket);
            if (error != RMT_ERROR_NONE)
                return error;
        }

        // Read as much required data as possible
        bytes_to_read = web_socket->frame_bytes_remaining < length ? web_socket->frame_bytes_remaining : length;
        error = TCPSocket_Receive(web_socket->tcp_socket, cur_data, bytes_to_read, 20);
        if (error == RMT_ERROR_SOCKET_RECV_FAILED)
            return error;

        // If there's a stall receiving the data, check for timeout
        if (error == RMT_ERROR_SOCKET_RECV_NO_DATA || error == RMT_ERROR_SOCKET_RECV_TIMEOUT)
        {
            now_ms = msTimer_Get();
            if (now_ms - start_ms > timeout_ms)
                return RMT_ERROR_SOCKET_RECV_TIMEOUT;
            continue;
        }

        // Apply data mask
        if (*(rmtU32*)web_socket->data_mask != 0)
        {
            rmtU32 i;
            for (i = 0; i < bytes_to_read; i++)
            {
                *((rmtU8*)cur_data + i) ^= web_socket->data_mask[web_socket->mask_offset & 3];
                web_socket->mask_offset++;
            }
        }

        cur_data += bytes_to_read;
        web_socket->frame_bytes_remaining -= bytes_to_read;
    }

    return RMT_ERROR_NONE;
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @MESSAGEQ: Multiple producer, single consumer message queue
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/


typedef enum MessageID
{
    MsgID_NotReady,
    MsgID_LogText,
    MsgID_SampleTree,
} MessageID;


typedef struct Message
{
    MessageID id;

    rmtU32 payload_size;

    // For telling which thread the message came from in the debugger
    struct ThreadSampler* thread_sampler;

    rmtU8 payload[0];
} Message;


// Multiple producer, single consumer message queue that uses its own data buffer
// to store the message data. 
typedef struct MessageQueue
{
    rmtU32 size;

    // The physical address of this data buffer is pointed to by two sequential
    // virtual memory pages, allowing automatic wrap-around of any reads or writes
    // that exceed the limits of the buffer.
    VirtualMirrorBuffer* data;

    // Read/write position never wrap allowing trivial overflow checks
    // with easier debugging
    rmtU32 read_pos;
    rmtU32 write_pos;

} MessageQueue;


static void MessageQueue_Destroy(MessageQueue* queue)
{
    assert(queue != NULL);

    if (queue->data != NULL)
    {
        VirtualMirrorBuffer_Destroy(queue->data);
        queue->data = NULL;
    }

    free(queue);
}


static enum rmtError MessageQueue_Create(MessageQueue** queue, rmtU32 size)
{
    enum rmtError error;

    assert(queue != NULL);

    // Allocate the container
    *queue = (MessageQueue*)malloc(sizeof(MessageQueue));
    if (*queue == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Set defaults
    (*queue)->size = 0;
    (*queue)->data = NULL;
    (*queue)->read_pos = 0;
    (*queue)->write_pos = 0;

    error = VirtualMirrorBuffer_Create(&(*queue)->data, size, 10);
    if (error != RMT_ERROR_NONE)
    {
        MessageQueue_Destroy(*queue);
        *queue = NULL;
        return error;
    }

    // The mirror buffer needs to be page-aligned and will change the requested
    // size to match that.
    (*queue)->size = (*queue)->data->size;

    // Set the entire buffer to not ready message
    memset((*queue)->data->ptr, MsgID_NotReady, (*queue)->size);

    return RMT_ERROR_NONE;
}


static Message* MessageQueue_AllocMessage(MessageQueue* queue, rmtU32 payload_size, struct ThreadSampler* thread_sampler)
{
    Message* msg;

    rmtU32 write_size = sizeof(Message) + payload_size;

    assert(queue != NULL);

    while (1)
    {
        // Check for potential overflow
        rmtU32 s = queue->size;
        rmtU32 r = queue->read_pos;
        rmtU32 w = queue->write_pos;
        if ((int)(w - r) > s - write_size)
            return NULL;

        // Point to the newly allocated space
        msg = (Message*)(queue->data->ptr + (w & (s - 1)));

        // Increment the write position, leaving the loop if this is the thread that succeeded
        if (AtomicCompareAndSwap(&queue->write_pos, w, w + write_size) == RMT_TRUE)
        {
            // Safe to set payload size after thread claims ownership of this allocated range
            msg->payload_size = payload_size;
            msg->thread_sampler = thread_sampler;
            break;
        }
    }

    return msg;
}


static void MessageQueue_CommitMessage(MessageQueue* queue, Message* message, MessageID id)
{
    assert(queue != NULL);
    assert(message != NULL);

    // Ensure message writes complete before commit
    WriteFence();

    // Setting the message ID signals to the consumer that the message is ready
    assert(message->id == MsgID_NotReady);
    message->id = id;
}


Message* MessageQueue_PeekNextMessage(MessageQueue* queue)
{
    Message* ptr;
    rmtU32 r;

    assert(queue != NULL);

    // First check that there are bytes queued
    if (queue->write_pos - queue->read_pos == 0)
        return NULL;

    // Messages are in the queue but may not have been commit yet
    // Messages behind this one may have been commit but it's not reachable until
    // the next one in the queue is ready.
    r = queue->read_pos & (queue->size - 1);
    ptr = (Message*)(queue->data->ptr + r);
    if (ptr->id != MsgID_NotReady)
        return ptr;

    return NULL;
}


static void MessageQueue_ConsumeNextMessage(MessageQueue* queue, Message* message)
{
    rmtU32 message_size;

    assert(queue != NULL);
    assert(message != NULL);

    // Setting the message ID to "not ready" serves as a marker to the consumer that even though
    // space has been allocated for a message, the message isn't ready to be consumed
    // yet.
    //
    // We can't do that when allocating the message because multiple threads will be fighting for
    // the same location. Instead, clear out any messages just read by the consumer before advancing
    // the read position so that a winning thread's allocation will inherit the "not ready" state.
    //
    // This costs some write bandwidth and has the potential to flush cache to other cores.
    message_size = sizeof(Message) + message->payload_size;
    memset(message, MsgID_NotReady, message_size);

    // Ensure clear completes before advancing the read position
    WriteFence();
    queue->read_pos += message_size;
}


/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @NETWORK: Network Server
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct
{
    WebSocket* listen_socket;

    WebSocket* client_socket;

    rmtU32 last_ping_time;

    rmtU16 port;
} Server;


static void Server_Destroy(Server* server);


static enum rmtError Server_Create(rmtU16 port, Server** server)
{
    enum rmtError error;

    assert(server != NULL);
    *server = (Server*)malloc(sizeof(Server));
    if (*server == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Initialise defaults
    (*server)->listen_socket = NULL;
    (*server)->client_socket = NULL;
    (*server)->last_ping_time = 0;
    (*server)->port = port;

    // Create the listening WebSocket
    error = WebSocket_CreateServer(port, WEBSOCKET_TEXT, &(*server)->listen_socket);
    if (error != RMT_ERROR_NONE)
    {
        Server_Destroy(*server);
        *server = NULL;
        return error;
    }

    return RMT_ERROR_NONE;
}


static void Server_Destroy(Server* server)
{
    assert(server != NULL);

    if (server->client_socket != NULL)
        WebSocket_Destroy(server->client_socket);
    if (server->listen_socket != NULL)
        WebSocket_Destroy(server->listen_socket);

    free(server);
}


static rmtBool Server_IsClientConnected(Server* server)
{
    assert(server != NULL);
    return server->client_socket != NULL ? RMT_TRUE : RMT_FALSE;
}


static enum rmtError Server_Send(Server* server, const void* data, rmtU32 length, rmtU32 timeout)
{
    assert(server != NULL);
    if (Server_IsClientConnected(server))
    {
        enum rmtError error = WebSocket_Send(server->client_socket, data, length, timeout);
        if (error == RMT_ERROR_SOCKET_SEND_FAIL)
        {
            WebSocket_Destroy(server->client_socket);
            server->client_socket = NULL;
        }
        return error;
    }

    return RMT_ERROR_NONE;
}


static void Server_Update(Server* server)
{
    rmtU32 cur_time;

    assert(server != NULL);

    // Recreate the listening socket if it's been destroyed earlier
    if (server->listen_socket == NULL)
        WebSocket_CreateServer(server->port, WEBSOCKET_TEXT, &server->listen_socket);

    if (server->listen_socket != NULL && server->client_socket == NULL)
    {
        // Accept connections as long as there is no client connected
        WebSocket* client_socket = NULL;
        enum rmtError error = WebSocket_AcceptConnection(server->listen_socket, &client_socket);
        if (error == RMT_ERROR_NONE)
        {
            server->client_socket = client_socket;
        }
        else
        {
            // Destroy the listen socket on failure to accept
            // It will get recreated in another update
            WebSocket_Destroy(server->listen_socket);
            server->listen_socket = NULL;
        }
    }

    else
    {
        // Check for any incoming messages
        char message_first_byte;
        enum rmtError error = WebSocket_Receive(server->client_socket, &message_first_byte, 1, 0);
        if (error == RMT_ERROR_NONE)
        {
            // data available to read
        }
        else if (error == RMT_ERROR_SOCKET_RECV_NO_DATA)
        {
            // no data available
        }
        else if (error == RMT_ERROR_SOCKET_RECV_TIMEOUT)
        {
            // data not available yet, can afford to ignore as we're only reading the first byte
        }
        else
        {
            // Anything else is an error that may have closed the connection
            // NULL the variable before destroying the socket
            WebSocket* client_socket = server->client_socket;
            server->client_socket = NULL;
            WebSocket_Destroy(client_socket);
        }
    }

    // Send pings to the client every second
    cur_time = msTimer_Get();
    if (cur_time - server->last_ping_time > 1000)
    {
        rmtPStr ping_message = "{ \"id\": \"PING\" }";
        Server_Send(server, ping_message, strlen(ping_message), 20);
        server->last_ping_time = cur_time;
    }
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @JSON: Basic, text-based JSON serialisation
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



//
// Simple macro for hopefully making the serialisation a little clearer by hiding the error handling
//
#define JSON_ERROR_CHECK(stmt) { error = stmt; if (error != RMT_ERROR_NONE) return error; }



static enum rmtError json_OpenObject(Buffer* buffer)
{
    return Buffer_Write(buffer, (void*)"{", 1);
}


static enum rmtError json_CloseObject(Buffer* buffer)
{
    return Buffer_Write(buffer, (void*)"}", 1);
}


static enum rmtError json_Comma(Buffer* buffer)
{
    return Buffer_Write(buffer, (void*)",", 1);
}


static enum rmtError json_Colon(Buffer* buffer)
{
    return Buffer_Write(buffer, (void*)":", 1);
}


static enum rmtError json_String(Buffer* buffer, rmtPStr string)
{
    enum rmtError error;
    JSON_ERROR_CHECK(Buffer_Write(buffer, (void*)"\"", 1));
    JSON_ERROR_CHECK(Buffer_WriteString(buffer, string));
    return Buffer_Write(buffer, (void*)"\"", 1);
}


static enum rmtError json_FieldStr(Buffer* buffer, rmtPStr name, rmtPStr value)
{
    enum rmtError error;
    JSON_ERROR_CHECK(json_String(buffer, name));
    JSON_ERROR_CHECK(json_Colon(buffer));
    return json_String(buffer, value);
}


static enum rmtError json_FieldU64(Buffer* buffer, rmtPStr name, rmtU64 value)
{
    static char temp_buf[32];

    char* end;
    char* tptr;

    json_String(buffer, name);
    json_Colon(buffer);

    if (value == 0)
        return Buffer_Write(buffer, (void*)"0", 1);

    // Null terminate and start at the end
    end = temp_buf + sizeof(temp_buf) - 1;
    *end = 0;
    tptr = end;

    // Loop through the value with radix 10
    do
    {
        rmtU64 next_value = value / 10;
        *--tptr = (char)('0' + (value - next_value * 10));
        value = next_value;
    } while (value);

    return Buffer_Write(buffer, tptr, end - tptr);
}


static enum rmtError json_OpenArray(Buffer* buffer, rmtPStr name)
{
    enum rmtError error;
    JSON_ERROR_CHECK(json_String(buffer, name));
    JSON_ERROR_CHECK(json_Colon(buffer));
    return Buffer_Write(buffer, (void*)"[", 1);
}


static enum rmtError json_CloseArray(Buffer* buffer)
{
    return Buffer_Write(buffer, (void*)"]", 1);
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @SAMPLE: Base Sample Description for CPU by default
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



enum SampleType
{
    SampleType_CPU,
    SampleType_CUDA,
    SampleType_D3D11,
    SampleType_Count,
};


typedef struct Sample
{
    // Inherit so that samples can be quickly allocated
    ObjectLink ObjectLink;

    enum SampleType type;

    // Used to anonymously copy sample data without knowning its type
    rmtU32 size_bytes;

    // Sample name and unique hash
    rmtPStr name;
    rmtU32 name_hash;

    // Unique, persistent ID among all samples
    rmtU32 unique_id;

    // Links to related samples in the tree
    struct Sample* parent;
    struct Sample* first_child;
    struct Sample* last_child;
    struct Sample* next_sibling;

    // Keep track of child count to distinguish from repeated calls to the same function at the same stack level
    // This is also mixed with the callstack hash to allow consistent addressing of any point in the tree
    rmtU32 nb_children;

    // Start and end of the sample in microseconds
    rmtU64 us_start;
    rmtU64 us_end;

} Sample;


static enum rmtError Sample_Constructor(Sample* sample)
{
    assert(sample != NULL);

    ObjectLink_Constructor((ObjectLink*)sample);

    sample->type = SampleType_CPU;
    sample->size_bytes = sizeof(Sample);
    sample->name = NULL;
    sample->name_hash = 0;
    sample->unique_id = 0;
    sample->parent = NULL;
    sample->first_child = NULL;
    sample->last_child = NULL;
    sample->next_sibling = NULL;
    sample->nb_children = 0;
    sample->us_start = 0;
    sample->us_end = 0;

    return RMT_ERROR_NONE;
}


static void Sample_Destructor(Sample* sample)
{
}


static void Sample_Prepare(Sample* sample, rmtPStr name, rmtU32 name_hash, Sample* parent)
{
    sample->name = name;
    sample->name_hash = name_hash;
    sample->unique_id = 0;
    sample->parent = parent;
    sample->first_child = NULL;
    sample->last_child = NULL;
    sample->next_sibling = NULL;
    sample->nb_children = 0;
    sample->us_start = 0;
    sample->us_end = 0;
}


static enum rmtError json_SampleArray(Buffer* buffer, Sample* first_sample, rmtPStr name);


static enum rmtError json_Sample(Buffer* buffer, Sample* sample)
{
    enum rmtError error;

    assert(sample != NULL);

    JSON_ERROR_CHECK(json_OpenObject(buffer));

        JSON_ERROR_CHECK(json_FieldStr(buffer, "name", sample->name));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldU64(buffer, "id", sample->unique_id));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldU64(buffer, "us_start", sample->us_start));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldU64(buffer, "us_length", max(sample->us_end - sample->us_start, 0)));

        if (sample->first_child != NULL)
        {
            JSON_ERROR_CHECK(json_Comma(buffer));
            JSON_ERROR_CHECK(json_SampleArray(buffer, sample->first_child, "children"));
        }

    return json_CloseObject(buffer);
}


static enum rmtError json_SampleArray(Buffer* buffer, Sample* first_sample, rmtPStr name)
{
    enum rmtError error;

    Sample* sample;

    JSON_ERROR_CHECK(json_OpenArray(buffer, name));

    for (sample = first_sample; sample != NULL; sample = sample->next_sibling)
    {
        JSON_ERROR_CHECK(json_Sample(buffer, sample));
        if (sample->next_sibling != NULL)
            JSON_ERROR_CHECK(json_Comma(buffer));
    }

    return json_CloseArray(buffer);
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @SAMPLETREE: A tree of samples with their allocator
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct SampleTree
{
    // Allocator for all samples 
    ObjectAllocator* allocator;

    // Root sample for all samples created by this thread
    Sample* root;

    // Most recently pushed sample
    Sample* current_parent;

} SampleTree;


static void SampleTree_Destroy(SampleTree* tree);


static enum rmtError SampleTree_Create(SampleTree** tree, rmtU32 sample_size, ObjConstructor constructor, ObjDestructor destructor)
{
    enum rmtError error;

    assert(tree != NULL);

    *tree = (SampleTree*)malloc(sizeof(SampleTree));
    if (*tree == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    (*tree)->allocator = NULL;
    (*tree)->root = NULL;
    (*tree)->current_parent = NULL;

    // Create the sample allocator
    error = ObjectAllocator_Create(&(*tree)->allocator, sample_size, constructor, destructor);
    if (error != RMT_ERROR_NONE)
    {
        SampleTree_Destroy(*tree);
        *tree = NULL;
        return error;
    }

    // Create a root sample that's around for the lifetime of the thread
    error = ObjectAllocator_Alloc((*tree)->allocator, (void**)&(*tree)->root);
    if (error != RMT_ERROR_NONE)
    {
        SampleTree_Destroy(*tree);
        *tree = NULL;
        return error;
    }
    Sample_Prepare((*tree)->root, "<Root Sample>", 0, NULL);
    (*tree)->current_parent = (*tree)->root;

    return RMT_ERROR_NONE;
}


static void SampleTree_Destroy(SampleTree* tree)
{
    assert(tree != NULL);

    if (tree->root != NULL)
    {
        ObjectAllocator_Free(tree->allocator, tree->root);
        tree->root = NULL;
    }

    if (tree->allocator != NULL)
    {
        ObjectAllocator_Destroy(tree->allocator);
        tree->allocator = NULL;
    }

    free(tree);
}


rmtU32 HashCombine(rmtU32 hash_a, rmtU32 hash_b)
{
    // A sequence of 32 uniformly random bits so that each bit of the combined hash is changed on application
    // Derived from the golden ratio: UINT_MAX / ((1 + sqrt(5)) / 2)
    // In reality it's just an arbitrary value which happens to work well, avoiding mapping all zeros to zeros.
    // http://burtleburtle.net/bob/hash/doobs.html
    static rmtU32 random_bits = 0x9E3779B9;
    hash_a ^= hash_b + random_bits + (hash_a << 6) + (hash_a >> 2);
    return hash_a;
}


static enum rmtError SampleTree_Push(SampleTree* tree, rmtPStr name, rmtU32 name_hash, Sample** sample)
{
    Sample* parent;
    enum rmtError error;
    rmtU32 unique_id;

    // As each tree has a root sample node allocated, a parent must always be present
    assert(tree != NULL);
    assert(tree->current_parent != NULL);
    parent = tree->current_parent;

    if (parent->last_child != NULL && parent->last_child->name_hash == name_hash)
    {
        // TODO: Collapse siblings with flag exception?
        //       Note that above check is not enough - requires a linear search
    }
    if (parent->name_hash == name_hash)
    {
        // TODO: Collapse recursion on flag?
    }

    // Allocate a new sample
    error = ObjectAllocator_Alloc(tree->allocator, (void**)sample);
    if (error != RMT_ERROR_NONE)
        return error;
    Sample_Prepare(*sample, name, name_hash, parent);

    // Generate a unique ID for this sample in the tree
    unique_id = parent->unique_id;
    unique_id = HashCombine(unique_id, (*sample)->name_hash);
    unique_id = HashCombine(unique_id, parent->nb_children);
    (*sample)->unique_id = unique_id;

    // Add sample to its parent
    parent->nb_children++;
    if (parent->first_child == NULL)
    {
        parent->first_child = *sample;
        parent->last_child = *sample;
    }
    else
    {
        assert(parent->last_child != NULL);
        parent->last_child->next_sibling = *sample;
        parent->last_child = *sample;
    }

    // Make this sample the new parent of any newly created samples
    tree->current_parent = *sample;

    return RMT_ERROR_NONE;
}


static void SampleTree_Pop(SampleTree* tree, Sample* sample)
{
    assert(tree != NULL);
    assert(sample != NULL);
    assert(sample != tree->root);
    tree->current_parent = sample->parent;
}


static ObjectLink* FlattenSampleTree(Sample* sample, rmtU32* nb_samples)
{
    Sample* child;
    ObjectLink* cur_link = &sample->ObjectLink;

    assert(sample != NULL);
    assert(nb_samples != NULL);

    *nb_samples += 1;
    sample->ObjectLink.next = (ObjectLink*)sample->first_child;

    // Link all children together
    for (child = sample->first_child; child != NULL; child = child->next_sibling)
    {
        ObjectLink* last_link = FlattenSampleTree(child, nb_samples);
        last_link->next = (ObjectLink*)child->next_sibling;
        cur_link = last_link;
    }

    // Clear child info
    sample->first_child = NULL;
    sample->last_child = NULL;
    sample->nb_children = 0;

    return cur_link;
}


static void FreeSampleTree(Sample* sample, ObjectAllocator* allocator)
{
    // Chain all samples together in a flat list
    rmtU32 nb_cleared_samples = 0;
    ObjectLink* last_link = FlattenSampleTree(sample, &nb_cleared_samples);

    // Release the complete sample memory range
    if (sample->ObjectLink.next != NULL)
        ObjectAllocator_FreeRange(allocator, sample, last_link, nb_cleared_samples);
    else
        ObjectAllocator_Free(allocator, sample);
}


typedef struct Msg_SampleTree
{
    Sample* root_sample;

    ObjectAllocator* allocator;

    rmtPStr thread_name;
} Msg_SampleTree;


static void AddSampleTreeMessage(MessageQueue* queue, Sample* sample, ObjectAllocator* allocator, rmtPStr thread_name, struct ThreadSampler* thread_sampler)
{
    Msg_SampleTree* payload;

    // Attempt to allocate a message for sending the tree to the viewer
    Message* message = MessageQueue_AllocMessage(queue, sizeof(Msg_SampleTree), thread_sampler);
    if (message == NULL)
    {
        // Discard the tree on failure
        FreeSampleTree(sample, allocator);
        return;
    }

    // Populate and commit
    payload = (Msg_SampleTree*)message->payload;
    payload->root_sample = sample;
    payload->allocator = allocator;
    payload->thread_name = thread_name;
    MessageQueue_CommitMessage(queue, message, MsgID_SampleTree);
}




/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @TSAMPLER: Per-Thread Sampler
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



typedef struct ThreadSampler
{
    // Name to assign to the thread in the viewer
    rmtS8 name[64];

    // Store a unique sample tree for each type
    SampleTree* sample_trees[SampleType_Count];

    // Microsecond accuracy timer for CPU timestamps
    usTimer timer;

    // Next in the global list of active thread samplers
    struct ThreadSampler* volatile next;

} ThreadSampler;


static void ThreadSampler_Destroy(ThreadSampler* ts);


static enum rmtError ThreadSampler_Create(ThreadSampler** thread_sampler)
{
    enum rmtError error;
    int i;

    // Allocate space for the thread sampler
    *thread_sampler = (ThreadSampler*)malloc(sizeof(ThreadSampler));
    if (*thread_sampler == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Set defaults
    for (i = 0; i < SampleType_Count; i++)
        (*thread_sampler)->sample_trees[i] = NULL; 
    (*thread_sampler)->next = NULL;

    // Set the initial name based on the unique thread sampler address
    Base64_Encode((rmtU8*)thread_sampler, sizeof(thread_sampler), (rmtU8*)(*thread_sampler)->name);

    // Create the CPU sample tree only - the rest are created on-demand as they need
    // extra context information to function correctly.
    error = SampleTree_Create(&(*thread_sampler)->sample_trees[SampleType_CPU], sizeof(Sample), (ObjConstructor)Sample_Constructor, (ObjDestructor)Sample_Destructor);
    if (error != RMT_ERROR_NONE)
    {
        ThreadSampler_Destroy(*thread_sampler);
        *thread_sampler = NULL;
        return error;
    }

    // Kick-off the timer
    usTimer_Init(&(*thread_sampler)->timer);

    return RMT_ERROR_NONE;
}


static void ThreadSampler_Destroy(ThreadSampler* ts)
{
    int i;

    assert(ts != NULL);

    for (i = 0; i < SampleType_Count; i++)
    {
        if (ts->sample_trees[i] != NULL)
        {
            SampleTree_Destroy(ts->sample_trees[i]);
            ts->sample_trees[i] = NULL;
        }
    }

    free(ts);
}


static enum rmtError ThreadSampler_Push(ThreadSampler* ts, SampleTree* tree, rmtPStr name, rmtU32 name_hash, Sample** sample)
{
    return SampleTree_Push(tree, name, name_hash, sample);
}


static void ThreadSampler_Pop(ThreadSampler* ts, MessageQueue* queue, Sample* sample)
{
    SampleTree* tree = ts->sample_trees[sample->type];
    SampleTree_Pop(tree, sample);

    // Are we back at the root?
    if (tree->current_parent == tree->root)
    {
        // Disconnect all samples from the root and pack in the chosen message queue
        Sample* root = tree->root;
        root->first_child = NULL;
        root->last_child = NULL;
        root->nb_children = 0;
        AddSampleTreeMessage(queue, sample, tree->allocator, ts->name, ts);
    }
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @REMOTERY: Remotery
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



struct Remotery
{
    Server* server;

    rmtTLS thread_sampler_tls_handle;

    // Linked list of all known threads being sampled
    ThreadSampler* volatile first_thread_sampler;

    // Queue between clients and main remotery thread
    MessageQueue* mq_to_rmt_thread;

    // A dynamically-sized buffer used for encoding the sample tree as JSON and sending to the client
    Buffer* json_buf;

    // The main server thread
    Thread* thread;

#ifdef RMT_USE_CUDA
    rmtCUDABind cuda;
#endif

#ifdef RMT_USE_D3D11
    // Context set by user
    ID3D11Device* d3d11_device;
    ID3D11DeviceContext* d3d11_context;

    HRESULT d3d11_last_error;

    // An allocator separate to the samples themselves so that D3D resource lifetime can be controlled
    // outside of the Remotery thread.
    ObjectAllocator* d3d11_timestamp_allocator;

    // Queue to the D3D 11 main update thread
    // Given that BeginSample/EndSample need to be called from the same thread that does the update, there
    // is really no need for this to be a thread-safe queue. I'm using it for its convenience.
    MessageQueue* mq_to_d3d11_main;

    // Mark the first time so that remaining timestamps are offset from this
    rmtU64 d3d11_first_timestamp;
#endif
};


//
// Global remotery context
//
static Remotery* g_Remotery = NULL;


//
// This flag marks the EXE/DLL that created the global remotery instance. We want to allow
// only the creating EXE/DLL to destroy the remotery instance.
//
static rmtBool g_RemoteryCreated = RMT_FALSE;


static void Remotery_Destroy(Remotery* rmt);
static void Remotery_DestroyThreadSamplers(Remotery* rmt);


static void GetSampleDigest(Sample* sample, rmtU32* digest_hash, rmtU32* nb_samples)
{
    Sample* child;

    assert(sample != NULL);
    assert(digest_hash != NULL);
    assert(nb_samples != NULL);

    // Concatenate this sample
    (*nb_samples)++;
    *digest_hash = MurmurHash3_x86_32(&sample->unique_id, sizeof(sample->unique_id), *digest_hash);

    // Concatenate children
    for (child = sample->first_child; child != NULL; child = child->next_sibling)
        GetSampleDigest(child, digest_hash, nb_samples);
}


static enum rmtError Remotery_SendLogTextMessage(Remotery* rmt, Message* message)
{
    assert(rmt != NULL);
    assert(message != NULL);
    return Server_Send(rmt->server, message->payload, message->payload_size, 20);
}


static enum rmtError json_SampleTree(Buffer* buffer, Msg_SampleTree* msg)
{
    Sample* root_sample;
    char thread_name[64];
    rmtU32 digest_hash = 0, nb_samples = 0;
    enum rmtError error;

    assert(buffer != NULL);
    assert(msg != NULL);

    // Get the message root sample
    root_sample = msg->root_sample;
    assert(root_sample != NULL);

    // Reset the buffer position to the start
    buffer->bytes_used = 0;

    // Add any sample types as a thread name post-fix to ensure they get their own viewer
    thread_name[0] = 0;
    strncat_s(thread_name, sizeof(thread_name), msg->thread_name, strnlen_s(msg->thread_name, 64));
    if (root_sample->type == SampleType_CUDA)
        strncat_s(thread_name, sizeof(thread_name), " (CUDA)", 7);
    if (root_sample->type == SampleType_D3D11)
        strncat_s(thread_name, sizeof(thread_name), " (D3D11)", 8);

    // Get digest hash of samples so that viewer can efficiently rebuild its tables
    GetSampleDigest(root_sample, &digest_hash, &nb_samples);

    // Build the sample data
    JSON_ERROR_CHECK(json_OpenObject(buffer));

        JSON_ERROR_CHECK(json_FieldStr(buffer, "id", "SAMPLES"));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldStr(buffer, "thread_name", thread_name));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldU64(buffer, "nb_samples", nb_samples));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_FieldU64(buffer, "sample_digest", digest_hash));
        JSON_ERROR_CHECK(json_Comma(buffer));
        JSON_ERROR_CHECK(json_SampleArray(buffer, root_sample, "samples"));

    JSON_ERROR_CHECK(json_CloseObject(buffer));

    return RMT_ERROR_NONE;
}


#ifdef RMT_USE_CUDA
static rmtBool AreCUDASamplesReady(Sample* sample);
static rmtBool GetCUDASampleTimes(Sample* root_sample, Sample* sample);
#endif


static enum rmtError Remotery_SendSampleTreeMessage(Remotery* rmt, Message* message)
{
    Msg_SampleTree* sample_tree;
    enum rmtError error = RMT_ERROR_NONE;
    Sample* sample;

    assert(rmt != NULL);
    assert(message != NULL);

    // Get the message root sample
    sample_tree = (Msg_SampleTree*)message->payload;
    sample = sample_tree->root_sample;
    assert(sample != NULL);

    #ifdef RMT_USE_CUDA
    if (sample->type == SampleType_CUDA)
    {
        // If these CUDA samples aren't ready yet, stick them to the back of the queue and continue
        rmtBool are_samples_ready;
        rmt_BeginCPUSample(AreCUDASamplesReady);
        are_samples_ready = AreCUDASamplesReady(sample);
        rmt_EndCPUSample();
        if (!are_samples_ready)
        {
            AddSampleTreeMessage(rmt->mq_to_rmt_thread, sample, sample_tree->allocator, sample_tree->thread_name, message->thread_sampler);
            return RMT_ERROR_NONE;
        }

        // Retrieve timing of all CUDA samples
        rmt_BeginCPUSample(GetCUDASampleTimes);
        GetCUDASampleTimes(sample->parent, sample);
        rmt_EndCPUSample();
    }
    #endif

    // Serialise the sample tree and send to the viewer
    error = json_SampleTree(rmt->json_buf, sample_tree);
    if (error == RMT_ERROR_NONE)
        error = Server_Send(rmt->server, rmt->json_buf->data, rmt->json_buf->bytes_used, 20);

    // Release the sample tree back to its allocator
    FreeSampleTree(sample, sample_tree->allocator);

    return error;
}


static enum rmtError Remotery_ConsumeMessageQueue(Remotery* rmt)
{
    rmtU32 nb_messages_sent = 0;

    assert(rmt != NULL);

    // Absorb as many messages in the queue while disconnected
    if (Server_IsClientConnected(rmt->server) == RMT_FALSE)
        return RMT_ERROR_NONE;

    // Loop reading the max number of messages for this update
    while (nb_messages_sent++ < MAX_NB_MESSAGES_PER_UPDATE)
    {
        enum rmtError error = RMT_ERROR_NONE;
        Message* message = MessageQueue_PeekNextMessage(rmt->mq_to_rmt_thread);
        if (message == NULL)
            break;

        switch (message->id)
        {
            // This shouldn't be possible
            case MsgID_NotReady:
                assert(RMT_FALSE); 
                break;

            // Dispatch to message handler
            case MsgID_LogText:
                error = Remotery_SendLogTextMessage(rmt, message);
                break;
            case MsgID_SampleTree:
                error = Remotery_SendSampleTreeMessage(rmt, message);
                break;
        }

        // Consume the message before reacting to any errors
        MessageQueue_ConsumeNextMessage(rmt->mq_to_rmt_thread, message);
        if (error != RMT_ERROR_NONE)
            return error;
    }

    return RMT_ERROR_NONE;
}


static void Remotery_FlushMessageQueue(Remotery* rmt)
{
    assert(rmt != NULL);

    // Loop reading all remaining messages
    while (1)
    {
        Message* message = MessageQueue_PeekNextMessage(rmt->mq_to_rmt_thread);
        if (message == NULL)
            break;

        switch (message->id)
        {
            // These can be safely ignored
            case MsgID_NotReady:
            case MsgID_LogText:
                break;

            // Release all samples back to their allocators
            case MsgID_SampleTree:
            {
                Msg_SampleTree* sample_tree = (Msg_SampleTree*)message->payload;
                FreeSampleTree(sample_tree->root_sample, sample_tree->allocator);
                break;
            }
        }

        MessageQueue_ConsumeNextMessage(rmt->mq_to_rmt_thread, message);
    }
}


static enum rmtError Remotery_ThreadMain(Thread* thread)
{
    Remotery* rmt = (Remotery*)thread->param;
    assert(rmt != NULL);

    rmt_SetCurrentThreadName("Remotery");

    while (thread->request_exit == RMT_FALSE)
    {
        rmt_BeginCPUSample(Wakeup);

            rmt_BeginCPUSample(ServerUpdate);
            Server_Update(rmt->server);
            rmt_EndCPUSample();

            rmt_BeginCPUSample(ConsumeMessageQueue);
            Remotery_ConsumeMessageQueue(rmt);
            rmt_EndCPUSample();

        rmt_EndCPUSample();

        //
        // [NOTE-A]
        //
        // Possible sequence of user events at this point:
        //
        //    1. Add samples to the queue.
        //    2. Shutdown remotery.
        //
        // This loop will exit with unrelease samples.
        //

        msSleep(MS_SLEEP_BETWEEN_SERVER_UPDATES);
    }

    // Release all samples to their allocators as a consequence of [NOTE-A]
    Remotery_FlushMessageQueue(rmt);

    return RMT_ERROR_NONE;
}


static enum rmtError Remotery_Create(Remotery** rmt)
{
    enum rmtError error;

    assert(rmt != NULL);

    *rmt = (Remotery*)malloc(sizeof(Remotery));
    if (*rmt == NULL)
        return RMT_ERROR_MALLOC_FAIL;

    // Set default state
    (*rmt)->server = NULL;
    (*rmt)->thread_sampler_tls_handle = TLS_INVALID_HANDLE;
    (*rmt)->first_thread_sampler = NULL;
    (*rmt)->mq_to_rmt_thread = NULL;
    (*rmt)->json_buf = NULL;
    (*rmt)->thread = NULL;

    // Allocate a TLS handle for the thread sampler
    error = tlsAlloc(&(*rmt)->thread_sampler_tls_handle);
    if (error != RMT_ERROR_NONE)
    {
        Remotery_Destroy(*rmt);
        *rmt = NULL;
        return error;
    }

    // Create the server
    error = Server_Create(0x4597, &(*rmt)->server);
    if (error != RMT_ERROR_NONE)
    {
        Remotery_Destroy(*rmt);
        *rmt = NULL;
        return error;
    }

    // Create the main message thread with only one page
    error = MessageQueue_Create(&(*rmt)->mq_to_rmt_thread, MESSAGE_QUEUE_SIZE_BYTES);
    if (error != RMT_ERROR_NONE)
    {
        Remotery_Destroy(*rmt);
        *rmt = NULL;
        return error;
    }

    // Create the JSON serialisation buffer
    error = Buffer_Create(&(*rmt)->json_buf, 4096);
    if (error != RMT_ERROR_NONE)
    {
        Remotery_Destroy(*rmt);
        *rmt = NULL;
        return error;
    }

    #ifdef RMT_USE_CUDA

        (*rmt)->cuda.CtxSetCurrent = NULL;
        (*rmt)->cuda.EventCreate = NULL;
        (*rmt)->cuda.EventDestroy = NULL;
        (*rmt)->cuda.EventElapsedTime = NULL;
        (*rmt)->cuda.EventQuery = NULL;
        (*rmt)->cuda.EventRecord = NULL;

    #endif

    #ifdef RMT_USE_D3D11
        (*rmt)->d3d11_device = NULL;
        (*rmt)->d3d11_context = NULL;
        (*rmt)->d3d11_last_error = S_OK;
        (*rmt)->d3d11_timestamp_allocator = NULL;
        (*rmt)->mq_to_d3d11_main = NULL;
        (*rmt)->d3d11_first_timestamp = 0;

        error = MessageQueue_Create(&(*rmt)->mq_to_d3d11_main, MESSAGE_QUEUE_SIZE_BYTES);
        if (error != RMT_ERROR_NONE)
        {
            Remotery_Destroy(*rmt);
            *rmt = NULL;
            return error;
        }
    #endif

    // Set as the global instance before creating any threads that uses it for sampling itself
    assert(g_Remotery == NULL);
    g_Remotery = *rmt;
    g_RemoteryCreated = RMT_TRUE;

    // Ensure global instance writes complete before other threads get a chance to use it
    WriteFence();

    // Create the main update thread once everything has been defined for the global remotery object
    error = Thread_Create(&(*rmt)->thread, Remotery_ThreadMain, *rmt);
    if (error != RMT_ERROR_NONE)
    {
        Remotery_Destroy(*rmt);
        *rmt = NULL;
        return error;
    }

    return RMT_ERROR_NONE;
}


static void Remotery_Destroy(Remotery* rmt)
{
    assert(rmt != NULL);

    // Join the remotery thread before clearing the global object as the thread is profiling itself
    if (rmt->thread != NULL)
    {
        Thread_Destroy(rmt->thread);
        rmt->thread = NULL;
    }

    // Ensure this is the module that created it
    assert(g_RemoteryCreated == RMT_TRUE);
    assert(g_Remotery == rmt);
    g_Remotery = NULL;
    g_RemoteryCreated = RMT_FALSE;

    #ifdef RMT_USE_D3D11
        if (rmt->d3d11_timestamp_allocator != NULL)
        {
            ObjectAllocator_Destroy(rmt->d3d11_timestamp_allocator);
            rmt->d3d11_timestamp_allocator = NULL;
        }
        if (rmt->mq_to_d3d11_main != NULL)
        {
            MessageQueue_Destroy(rmt->mq_to_d3d11_main);
            rmt->mq_to_d3d11_main = NULL;
        }
    #endif

    if (rmt->json_buf != NULL)
    {
        Buffer_Destroy(rmt->json_buf);
        rmt->json_buf = NULL;
    }

    if (rmt->mq_to_rmt_thread != NULL)
    {
        MessageQueue_Destroy(rmt->mq_to_rmt_thread);
        rmt->mq_to_rmt_thread = NULL;
    }

    Remotery_DestroyThreadSamplers(rmt);

    if (rmt->server != NULL)
    {
        Server_Destroy(rmt->server);
        rmt->server = NULL;
    }

    if (rmt->thread_sampler_tls_handle != TLS_INVALID_HANDLE)
    {
        tlsFree(rmt->thread_sampler_tls_handle);
        rmt->thread_sampler_tls_handle = 0;
    }

    free(rmt);
}


static enum rmtError Remotery_GetThreadSampler(Remotery* rmt, ThreadSampler** thread_sampler)
{
    ThreadSampler* ts;

    // Is there a thread sampler associated with this thread yet?
    assert(rmt != NULL);
    ts = (ThreadSampler*)tlsGet(rmt->thread_sampler_tls_handle);
    if (ts == NULL)
    {
        // Allocate on-demand
        enum rmtError error = ThreadSampler_Create(thread_sampler);
        if (error != RMT_ERROR_NONE)
            return error;
        ts = *thread_sampler;

        // Add to the beginning of the global linked list of thread samplers
        while (1)
        {
            ThreadSampler* old_ts = rmt->first_thread_sampler;
            ts->next = old_ts;

            // If the old value is what we expect it to be then no other thread has
            // changed it since this thread sampler was used as a candidate first list item
            if (AtomicCompareAndSwapPointer((long* volatile*)&rmt->first_thread_sampler, (long*)old_ts, (long*)ts) == RMT_TRUE)
                break;
        }

        tlsSet(rmt->thread_sampler_tls_handle, ts);
    }

    assert(thread_sampler != NULL);
    *thread_sampler = ts;
    return RMT_ERROR_NONE;
}


static void Remotery_DestroyThreadSamplers(Remotery* rmt)
{
    // If the handle failed to create in the first place then it shouldn't be possible to create thread samplers
    assert(rmt != NULL);
    if (rmt->thread_sampler_tls_handle == TLS_INVALID_HANDLE)
    {
        assert(rmt->first_thread_sampler == NULL);
        return;
    }

    // Keep popping thread samplers off the linked list until they're all gone
    // This does not make any assumptions, making it possible for thread samplers to be created while they're all
    // deleted. While this is erroneous calling code, this will prevent a confusing crash.
    while (rmt->first_thread_sampler != NULL)
    {
        ThreadSampler* ts;

        while (1)
        {
            ThreadSampler* old_ts = rmt->first_thread_sampler;
            ThreadSampler* next_ts = old_ts->next;

            if (AtomicCompareAndSwapPointer((long* volatile*)&rmt->first_thread_sampler, (long*)old_ts, (long*)next_ts) == RMT_TRUE)
            {
                ts = old_ts;
                break;
            }
        }

        // Release the thread sampler
        ThreadSampler_Destroy(ts);
    }
}


enum rmtError _rmt_CreateGlobalInstance(Remotery** remotery)
{
    // Creating the Remotery instance also records it as the global instance
    assert(remotery != NULL);
    return Remotery_Create(remotery);
}


void _rmt_DestroyGlobalInstance(Remotery* remotery)
{
    if (remotery != NULL)
        Remotery_Destroy(remotery);
}


void _rmt_SetGlobalInstance(Remotery* remotery)
{
    g_Remotery = remotery;
}


Remotery* _rmt_GetGlobalInstance(void)
{
    return g_Remotery;
}


#ifdef RMT_PLATFORM_WINDOWS
    #pragma pack(push,8)
    typedef struct tagTHREADNAME_INFO
    {
       DWORD dwType; // Must be 0x1000.
       LPCSTR szName; // Pointer to name (in user addr space).
       DWORD dwThreadID; // Thread ID (-1=caller thread).
       DWORD dwFlags; // Reserved for future use, must be zero.
    } THREADNAME_INFO;
    #pragma pack(pop)
#endif

static void SetDebuggerThreadName(const char* name)
{
    #ifdef RMT_PLATFORM_WINDOWS
        THREADNAME_INFO info;
        info.dwType = 0x1000;
        info.szName = name;
        info.dwThreadID = -1;
        info.dwFlags = 0;

        __try
        {
            RaiseException(0x406D1388, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info);
        }
        __except(1 /* EXCEPTION_EXECUTE_HANDLER */)
        {
        }
    #endif
}


void _rmt_SetCurrentThreadName(rmtPStr thread_name)
{
    ThreadSampler* ts;
    rsize_t slen;

    if (g_Remotery == NULL)
        return;

    // Get data for this thread
    if (Remotery_GetThreadSampler(g_Remotery, &ts) != RMT_ERROR_NONE)
        return;

    // Use strcat to strcpy the thread name over
    slen = strnlen_s(thread_name, sizeof(ts->name));
    ts->name[0] = 0;
    strncat_s(ts->name, sizeof(ts->name), thread_name, slen);

    // Apply to the debugger
    SetDebuggerThreadName(thread_name);
}


static rmtBool QueueLine(MessageQueue* queue, char* text, rmtU32 size, struct ThreadSampler* thread_sampler)
{
    Message* message;

    assert(queue != NULL);

    // String/JSON block/null terminate
    text[size++] = '\"';
    text[size++] = '}';
    text[size] = 0;

    // Allocate some space for the line
    message = MessageQueue_AllocMessage(queue, size, thread_sampler);
    if (message == NULL)
        return RMT_FALSE;

    // Copy the text and commit the message
    memcpy(message->payload, text, size);
    MessageQueue_CommitMessage(queue, message, MsgID_LogText);

    return RMT_TRUE;
}


static const char log_message[] = "{ \"id\": \"LOG\", \"text\": \"";


void _rmt_LogText(rmtPStr text)
{
    int start_offset, prev_offset, i;
    char line_buffer[1024] = { 0 };
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    Remotery_GetThreadSampler(g_Remotery, &ts);

    // Start the line buffer off with the JSON message markup
    strncat_s(line_buffer, sizeof(line_buffer), log_message, sizeof(log_message));
    start_offset = strnlen_s(line_buffer, sizeof(line_buffer) - 1);

    // There might be newlines in the buffer, so split them into multiple network calls
    prev_offset = start_offset;
    for (i = 0; text[i] != 0; i++)
    {
        char c = text[i];

        // Line wrap when too long or newline encountered
        if (prev_offset == sizeof(line_buffer) - 3 || c == '\n')
        {
            if (QueueLine(g_Remotery->mq_to_rmt_thread, line_buffer, prev_offset, ts) == RMT_FALSE)
                return;

            // Restart line
            prev_offset = start_offset;
        }

        // Safe to insert 2 characters here as previous check would split lines if not enough space left
        switch (c)
        {
            // Skip newline, dealt with above
            case '\n':
                break;

            // Escape these
            case '\\':
                line_buffer[prev_offset++] = '\\';
                line_buffer[prev_offset++] = '\\';
                break;

            case '\"':
                line_buffer[prev_offset++] = '\\';
                line_buffer[prev_offset++] = '\"';
                break;

            // Add the rest
            default:
                line_buffer[prev_offset++] = c;
                break;
        }
    }

    // Send the last line
    if (prev_offset > start_offset)
    {
        assert(prev_offset < sizeof(line_buffer) - 3);
        QueueLine(g_Remotery->mq_to_rmt_thread, line_buffer, prev_offset, ts);
    }
}


static rmtU32 GetNameHash(rmtPStr name, rmtU32* hash_cache)
{
    // Hash cache provided?
    if (hash_cache != NULL)
    {
        // Calculate the hash first time round only
        if (*hash_cache == 0)
        {
            assert(name != NULL);
            *hash_cache = MurmurHash3_x86_32(name, strnlen_s(name, 256), 0);
        }

        return *hash_cache;
    }

    // Have to recalculate every time when no cache storage exists
    return MurmurHash3_x86_32(name, strnlen_s(name, 256), 0);
}


void _rmt_BeginCPUSample(rmtPStr name, rmtU32* hash_cache)
{
    // 'hash_cache' stores a pointer to a sample name's hash value. Internally this is used to identify unique callstacks and it
    // would be ideal that it's not recalculated each time the sample is used. This can be statically cached at the point
    // of call or stored elsewhere when dynamic names are required.
    //
    // If 'hash_cache' is NULL then this call becomes more expensive, as it has to recalculate the hash of the name.
    
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    // TODO: Time how long the bits outside here cost and subtract them from the parent

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        Sample* sample;
        rmtU32 name_hash = GetNameHash(name, hash_cache);
        if (ThreadSampler_Push(ts, ts->sample_trees[SampleType_CPU], name, name_hash, &sample) == RMT_ERROR_NONE)
            sample->us_start = usTimer_Get(&ts->timer);
    }
}


void _rmt_EndCPUSample(void)
{
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        Sample* sample = ts->sample_trees[SampleType_CPU]->current_parent;
        sample->us_end = usTimer_Get(&ts->timer);
        ThreadSampler_Pop(ts, g_Remotery->mq_to_rmt_thread, sample);
    }
}



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @CUDA: CUDA event sampling
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



#ifdef RMT_USE_CUDA


typedef struct CUDASample
{
    // IS-A inheritance relationship
    Sample Sample;

    // Pair of events that wrap the sample
    CUevent event_start;
    CUevent event_end;

} CUDASample;


static enum rmtError MapCUDAResult(CUresult result)
{
    switch (result)
    {
        case CUDA_SUCCESS: return RMT_ERROR_NONE;
        case CUDA_ERROR_DEINITIALIZED: return RMT_ERROR_CUDA_DEINITIALIZED;
        case CUDA_ERROR_NOT_INITIALIZED: return RMT_ERROR_CUDA_NOT_INITIALIZED;
        case CUDA_ERROR_INVALID_CONTEXT: return RMT_ERROR_CUDA_INVALID_CONTEXT;
        case CUDA_ERROR_INVALID_VALUE: return RMT_ERROR_CUDA_INVALID_VALUE;
        case CUDA_ERROR_INVALID_HANDLE: return RMT_ERROR_CUDA_INVALID_HANDLE;
        case CUDA_ERROR_OUT_OF_MEMORY: return RMT_ERROR_CUDA_OUT_OF_MEMORY;
        case CUDA_ERROR_NOT_READY: return RMT_ERROR_ERROR_NOT_READY;
        default: return RMT_ERROR_CUDA_UNKNOWN;
    }
}


#define CUDA_MAKE_FUNCTION(name, params)                    \
    typedef CUresult (CUDAAPI *name##Ptr) params;           \
    name##Ptr name = (name##Ptr)g_Remotery->cuda.name;


#define CUDA_GUARD(call)                \
    {                                   \
        enum rmtError error = call;     \
        if (error != RMT_ERROR_NONE)    \
            return error;               \
    }


// Wrappers around CUDA driver functions that manage the active context.
static enum rmtError CUDASetContext(void* context)
{
    CUDA_MAKE_FUNCTION(CtxSetCurrent, (CUcontext ctx));
    assert(CtxSetCurrent != NULL);
    return MapCUDAResult(CtxSetCurrent((CUcontext)context));
}
static enum rmtError CUDAGetContext(void** context)
{
    CUDA_MAKE_FUNCTION(CtxGetCurrent, (CUcontext* ctx));
    assert(CtxGetCurrent != NULL);
    return MapCUDAResult(CtxGetCurrent((CUcontext*)context));
}
static enum rmtError CUDAEnsureContext()
{
    void* current_context;
    CUDA_GUARD(CUDAGetContext(&current_context));

    assert(g_Remotery != NULL);
    if (current_context != g_Remotery->cuda.context)
        CUDA_GUARD(CUDASetContext(g_Remotery->cuda.context));

    return RMT_ERROR_NONE;
}


// Wrappers around CUDA driver functions that manage events
static enum rmtError CUDAEventCreate(CUevent* phEvent, unsigned int Flags)
{
    CUDA_MAKE_FUNCTION(EventCreate, (CUevent *phEvent, unsigned int Flags));
    CUDA_GUARD(CUDAEnsureContext());
    return MapCUDAResult(EventCreate(phEvent, Flags));
}
static enum rmtError CUDAEventDestroy(CUevent hEvent)
{
    CUDA_MAKE_FUNCTION(EventDestroy, (CUevent hEvent));
    CUDA_GUARD(CUDAEnsureContext());
    return MapCUDAResult(EventDestroy(hEvent));
}
static enum rmtError CUDAEventRecord(CUevent hEvent, void* hStream)
{
    CUDA_MAKE_FUNCTION(EventRecord, (CUevent hEvent, CUstream hStream));
    CUDA_GUARD(CUDAEnsureContext());
    return MapCUDAResult(EventRecord(hEvent, (CUstream)hStream));
}
static enum rmtError CUDAEventQuery(CUevent hEvent)
{
    CUDA_MAKE_FUNCTION(EventQuery,  (CUevent hEvent));
    CUDA_GUARD(CUDAEnsureContext());
    return MapCUDAResult(EventQuery(hEvent));
}
static enum rmtError CUDAEventElapsedTime(float* pMilliseconds, CUevent hStart, CUevent hEnd)
{
    CUDA_MAKE_FUNCTION(EventElapsedTime, (float *pMilliseconds, CUevent hStart, CUevent hEnd));
    CUDA_GUARD(CUDAEnsureContext());
    return MapCUDAResult(EventElapsedTime(pMilliseconds, hStart, hEnd));
}


static enum rmtError CUDASample_Constructor(CUDASample* sample)
{
    enum rmtError error;

    assert(sample != NULL);

    // Chain to sample constructor
    Sample_Constructor((Sample*)sample);
    sample->Sample.type = SampleType_CUDA;
    sample->Sample.size_bytes = sizeof(CUDASample);
    sample->event_start = NULL;
    sample->event_end = NULL;

    // Create non-blocking events with timing
    assert(g_Remotery != NULL);
    error = CUDAEventCreate(&sample->event_start, CU_EVENT_DEFAULT);
    if (error == RMT_ERROR_NONE)
        error = CUDAEventCreate(&sample->event_end, CU_EVENT_DEFAULT);
    return error;
}


static void CUDASample_Destructor(CUDASample* sample)
{
    assert(sample != NULL);

    // Destroy events
    if (sample->event_start != NULL)
        CUDAEventDestroy(sample->event_start);
    if (sample->event_end != NULL)
        CUDAEventDestroy(sample->event_end);

    Sample_Destructor((Sample*)sample);
}


static rmtBool AreCUDASamplesReady(Sample* sample)
{
    enum rmtError error;
    Sample* child;

    CUDASample* cuda_sample = (CUDASample*)sample;
    assert(sample->type == SampleType_CUDA);

    // Check to see if both of the CUDA events have been processed
    error = CUDAEventQuery(cuda_sample->event_start);
    if (error != RMT_ERROR_NONE)
        return RMT_FALSE;
    error = CUDAEventQuery(cuda_sample->event_end);
    if (error != RMT_ERROR_NONE)
        return RMT_FALSE;

    // Check child sample events
    for (child = sample->first_child; child != NULL; child = child->next_sibling)
    {
        if (!AreCUDASamplesReady(child))
            return RMT_FALSE;
    }

    return RMT_TRUE;
}


static rmtBool GetCUDASampleTimes(Sample* root_sample, Sample* sample)
{
    Sample* child;

    CUDASample* cuda_root_sample = (CUDASample*)root_sample;
    CUDASample* cuda_sample = (CUDASample*)sample;

    float ms_start, ms_end;

    assert(root_sample != NULL);
    assert(sample != NULL);

    // Get millisecond timing of each sample event, relative to initial root sample
    if (CUDAEventElapsedTime(&ms_start, cuda_root_sample->event_start, cuda_sample->event_start) != RMT_ERROR_NONE)
        return RMT_FALSE;
    if (CUDAEventElapsedTime(&ms_end, cuda_root_sample->event_start, cuda_sample->event_end) != RMT_ERROR_NONE)
        return RMT_FALSE;

    // Convert to microseconds and add to the sample
    sample->us_start = (rmtU64)(ms_start * 1000);
    sample->us_end = (rmtU64)(ms_end * 1000);

    // Get child sample times
    for (child = sample->first_child; child != NULL; child = child->next_sibling)
    {
        if (!GetCUDASampleTimes(root_sample, child))
            return RMT_FALSE;
    }

    return RMT_TRUE;
}


void _rmt_BindCUDA(const rmtCUDABind* bind)
{
    assert(bind != NULL);
    if (g_Remotery != NULL)
        g_Remotery->cuda = *bind;
}


void _rmt_BeginCUDASample(rmtPStr name, rmtU32* hash_cache, void* stream)
{
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        Sample* sample;
        rmtU32 name_hash = GetNameHash(name, hash_cache);

        // Create the CUDA tree on-demand as the tree needs an up-front-created root.
        // This is not possible to create on initialisation as a CUDA binding is not yet available.
        SampleTree** cuda_tree = &ts->sample_trees[SampleType_CUDA];
        if (*cuda_tree == NULL)
        {
            CUDASample* root_sample;
            
            enum rmtError error = SampleTree_Create(cuda_tree, sizeof(CUDASample), (ObjConstructor)CUDASample_Constructor, (ObjDestructor)CUDASample_Destructor);
            if (error != RMT_ERROR_NONE)
                return;

            // Record an event once on the root sample, used to measure absolute sample
            // times since this point
            root_sample = (CUDASample*)(*cuda_tree)->root;
            error = CUDAEventRecord(root_sample->event_start, stream);
            if (error != RMT_ERROR_NONE)
                return;
        }

        // Push the same and record its event
        if (ThreadSampler_Push(ts, *cuda_tree, name, name_hash, &sample) == RMT_ERROR_NONE)
        {
            CUDASample* cuda_sample = (CUDASample*)sample;
            CUDAEventRecord(cuda_sample->event_start, stream);
        }
    }
}


void _rmt_EndCUDASample(void* stream)
{
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        CUDASample* sample = (CUDASample*)ts->sample_trees[SampleType_CUDA]->current_parent;
        CUDAEventRecord(sample->event_end, stream);
        ThreadSampler_Pop(ts, g_Remotery->mq_to_rmt_thread, (Sample*)sample);
    }
}


#endif  // RMT_USE_CUDA



/*
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
   @D3D11: Direct3D 11 event sampling
------------------------------------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------------------------------------
*/



#ifdef RMT_USE_D3D11


typedef struct D3D11Timestamp
{
    // Inherit so that timestamps can be quickly allocated
    ObjectLink ObjectLink;

    // Pair of timestamp queries that wrap the sample
    ID3D11Query* query_start;
    ID3D11Query* query_end;

    // A disjoint to measure frequency/stability
    // TODO: Does *each* sample need one of these?
    ID3D11Query* query_disjoint;
} D3D11Timestamp;


static enum rmtError D3D11Timestamp_Constructor(D3D11Timestamp* stamp)
{
    D3D11_QUERY_DESC timestamp_desc;
    D3D11_QUERY_DESC disjoint_desc;

    assert(stamp != NULL);

    ObjectLink_Constructor((ObjectLink*)stamp);

    // Set defaults
    stamp->query_start = NULL;
    stamp->query_end = NULL;
    stamp->query_disjoint = NULL;

    // Create start/end timestamp queries
    assert(g_Remotery != NULL);
    timestamp_desc.Query = D3D11_QUERY_TIMESTAMP;
    timestamp_desc.MiscFlags = 0;
    g_Remotery->d3d11_last_error = ID3D11Device_CreateQuery(g_Remotery->d3d11_device, &timestamp_desc, &stamp->query_start);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_ERROR_D3D11_FAILED_TO_CREATE_QUERY;
    g_Remotery->d3d11_last_error = ID3D11Device_CreateQuery(g_Remotery->d3d11_device, &timestamp_desc, &stamp->query_end);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_ERROR_D3D11_FAILED_TO_CREATE_QUERY;

    // Create disjoint query
    disjoint_desc.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
    disjoint_desc.MiscFlags = 0;
    g_Remotery->d3d11_last_error = ID3D11Device_CreateQuery(g_Remotery->d3d11_device, &disjoint_desc, &stamp->query_disjoint);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_ERROR_D3D11_FAILED_TO_CREATE_QUERY;

    return RMT_ERROR_NONE;
}


static void D3D11Timestamp_Destructor(D3D11Timestamp* stamp)
{
    assert(stamp != NULL);

    // Destroy queries
    if (stamp->query_disjoint != NULL)
        ID3D11Query_Release(stamp->query_disjoint);
    if (stamp->query_end != NULL)
        ID3D11Query_Release(stamp->query_end);
    if (stamp->query_start != NULL)
        ID3D11Query_Release(stamp->query_start);
}


static void D3D11Timestamp_Begin(D3D11Timestamp* stamp)
{
    assert(stamp != NULL);

    // Start of disjoint and first query
    assert(g_Remotery != NULL);
    assert(g_Remotery->d3d11_context != NULL);
    ID3D11DeviceContext_Begin(g_Remotery->d3d11_context, (ID3D11Asynchronous*)stamp->query_disjoint);
    ID3D11DeviceContext_End(g_Remotery->d3d11_context, (ID3D11Asynchronous*)stamp->query_start);
}


static void D3D11Timestamp_End(D3D11Timestamp* stamp)
{
    assert(stamp != NULL);

    // End of disjoint and second query
    assert(g_Remotery != NULL);
    assert(g_Remotery->d3d11_context != NULL);
    ID3D11DeviceContext_End(g_Remotery->d3d11_context, (ID3D11Asynchronous*)stamp->query_end);
    ID3D11DeviceContext_End(g_Remotery->d3d11_context, (ID3D11Asynchronous*)stamp->query_disjoint);
}


static rmtBool D3D11Timestamp_GetData(D3D11Timestamp* stamp, rmtU64 first_timestamp, rmtU64* out_start, rmtU64* out_end, rmtU64* out_first_timestamp)
{

    ID3D11DeviceContext* d3d_context;
    ID3D11Asynchronous* query_start;
    ID3D11Asynchronous* query_end;
    ID3D11Asynchronous* query_disjoint;

    UINT64 start;
    UINT64 end;
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint;

    assert(g_Remotery != NULL);
    d3d_context = g_Remotery->d3d11_context;

    assert(stamp != NULL);
    query_start = (ID3D11Asynchronous*)stamp->query_start;
    query_end = (ID3D11Asynchronous*)stamp->query_end;
    query_disjoint = (ID3D11Asynchronous*)stamp->query_disjoint;

    // Check to see if all queries are ready
    // If any fail to arrive, wait until later
    g_Remotery->d3d11_last_error = ID3D11DeviceContext_GetData(d3d_context, query_start, &start, sizeof(start), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_FALSE;
    g_Remotery->d3d11_last_error = ID3D11DeviceContext_GetData(d3d_context, query_end, &end, sizeof(end), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_FALSE;
    g_Remotery->d3d11_last_error = ID3D11DeviceContext_GetData(d3d_context, query_disjoint, &disjoint, sizeof(disjoint), D3D11_ASYNC_GETDATA_DONOTFLUSH);
    if (g_Remotery->d3d11_last_error != S_OK)
        return RMT_FALSE;

    if (disjoint.Disjoint == FALSE)
    {
        double frequency = disjoint.Frequency / 1000000.0;

        // Mark the first timestamp
        assert(out_first_timestamp != NULL);
        if (*out_first_timestamp == 0)
            *out_first_timestamp = start;

        // Calculate start and end timestamps from the disjoint info
        *out_start = (rmtU64)((start - first_timestamp) / frequency);
        *out_end = (rmtU64)((end - first_timestamp) / frequency);
    }

    return RMT_TRUE;
}


typedef struct D3D11Sample
{
    // IS-A inheritance relationship
    Sample Sample;

    D3D11Timestamp* timestamp;

} D3D11Sample;


static enum rmtError D3D11Sample_Constructor(D3D11Sample* sample)
{
    assert(sample != NULL);

    // Chain to sample constructor
    Sample_Constructor((Sample*)sample);
    sample->Sample.type = SampleType_D3D11;
    sample->Sample.size_bytes = sizeof(D3D11Sample);
    sample->timestamp = NULL;

    return RMT_ERROR_NONE;
}


static void D3D11Sample_Destructor(D3D11Sample* sample)
{
    Sample_Destructor((Sample*)sample);
}


void _rmt_BindD3D11(void* device, void* context)
{
    if (g_Remotery != NULL)
    {
        assert(device != NULL);
        g_Remotery->d3d11_device = (ID3D11Device*)device;
        assert(context != NULL);
        g_Remotery->d3d11_context = (ID3D11DeviceContext*)context;
    }
}


static void FreeD3D11TimeStamps(Sample* sample)
{
    Sample* child;

    D3D11Sample* d3d_sample = (D3D11Sample*)sample;

    assert(d3d_sample->timestamp != NULL);
    ObjectAllocator_Free(g_Remotery->d3d11_timestamp_allocator, (void*)d3d_sample->timestamp);
    d3d_sample->timestamp = NULL;

    for (child = sample->first_child; child != NULL; child = child->next_sibling)
        FreeD3D11TimeStamps(child);
}


void _rmt_UnbindD3D11(void)
{
    if (g_Remotery != NULL)
    {
        // Inform sampler to not add any more samples
        g_Remotery->d3d11_device = NULL;
        g_Remotery->d3d11_context = NULL;

        // Flush the main queue of allocated D3D timestamps
        while (1)
        {
            Msg_SampleTree* sample_tree;
            Sample* sample;

            Message* message = MessageQueue_PeekNextMessage(g_Remotery->mq_to_d3d11_main);
            if (message == NULL)
                break;

            // There's only one valid message type in this queue
            assert(message->id == MsgID_SampleTree);
            sample_tree = (Msg_SampleTree*)message->payload;
            sample = sample_tree->root_sample;
            assert(sample->type == SampleType_D3D11);
            FreeD3D11TimeStamps(sample);
            FreeSampleTree(sample, sample_tree->allocator);

            MessageQueue_ConsumeNextMessage(g_Remotery->mq_to_d3d11_main, message);
        }

        // Free all allocated D3D resources
        ObjectAllocator_Destroy(g_Remotery->d3d11_timestamp_allocator);
        g_Remotery->d3d11_timestamp_allocator = NULL;
    }
}


void _rmt_BeginD3D11Sample(rmtPStr name, rmtU32* hash_cache)
{
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    // Has D3D11 been unbound?
    if (g_Remotery->d3d11_device == NULL || g_Remotery->d3d11_context == NULL)
        return;

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        enum rmtError error;
        Sample* sample;
        rmtU32 name_hash = GetNameHash(name, hash_cache);

        // Create the D3D11 tree on-demand as the tree needs an up-front-created root.
        // This is not possible to create on initialisation as a D3D11 binding is not yet available.
        SampleTree** d3d_tree = &ts->sample_trees[SampleType_D3D11];
        if (*d3d_tree == NULL)
        {
            error = SampleTree_Create(d3d_tree, sizeof(D3D11Sample), (ObjConstructor)D3D11Sample_Constructor, (ObjDestructor)D3D11Sample_Destructor);
            if (error != RMT_ERROR_NONE)
                return;
        }

        // Also create the timestamp allocator on-demand to keep the D3D11 code localised to the same file section
        if (g_Remotery->d3d11_timestamp_allocator == NULL)
            error = ObjectAllocator_Create(&g_Remotery->d3d11_timestamp_allocator, sizeof(D3D11Timestamp), (ObjConstructor)D3D11Timestamp_Constructor, (ObjDestructor)D3D11Timestamp_Destructor);

        // Push the sample
        if (ThreadSampler_Push(ts, *d3d_tree, name, name_hash, &sample) == RMT_ERROR_NONE)
        {
            D3D11Sample* d3d_sample = (D3D11Sample*)sample;

            // Allocate a timestamp for the sample and activate it
            assert(d3d_sample->timestamp == NULL);
            error = ObjectAllocator_Alloc(g_Remotery->d3d11_timestamp_allocator, (void**)&d3d_sample->timestamp);
            if (error == RMT_ERROR_NONE)
                D3D11Timestamp_Begin(d3d_sample->timestamp);
        }
    }
}


void _rmt_EndD3D11Sample(void)
{
    ThreadSampler* ts;

    if (g_Remotery == NULL)
        return;

    // Has D3D11 been unbound?
    if (g_Remotery->d3d11_device == NULL || g_Remotery->d3d11_context == NULL)
        return;

    if (Remotery_GetThreadSampler(g_Remotery, &ts) == RMT_ERROR_NONE)
    {
        // Close the timestamp
        D3D11Sample* d3d_sample = (D3D11Sample*)ts->sample_trees[SampleType_D3D11]->current_parent;
        if (d3d_sample->timestamp != NULL)
            D3D11Timestamp_End(d3d_sample->timestamp);

        // Send to the update loop for ready-polling
        ThreadSampler_Pop(ts, g_Remotery->mq_to_d3d11_main, (Sample*)d3d_sample);
    }
}


static rmtBool GetD3D11SampleTimes(Sample* sample, rmtU64 first_timestamp, rmtU64* out_first_timestamp)
{
    Sample* child;

    D3D11Sample* d3d_sample = (D3D11Sample*)sample;

    assert(sample != NULL);
    if (d3d_sample->timestamp != NULL)
    {
        if (!D3D11Timestamp_GetData(d3d_sample->timestamp, first_timestamp, &sample->us_start, &sample->us_end, out_first_timestamp))
            return RMT_FALSE;
    }

    // Get child sample times
    for (child = sample->first_child; child != NULL; child = child->next_sibling)
    {
        if (!GetD3D11SampleTimes(child, first_timestamp, out_first_timestamp))
            return RMT_FALSE;
    }

    return RMT_TRUE;
}


void _rmt_UpdateD3D11Frame(void)
{
    Message* first_message = NULL;

    if (g_Remotery == NULL)
        return;

    rmt_BeginCPUSample(rmt_UpdateD3D11Frame);

    // Process all messages in the D3D queue
    while (1)
    {
        Msg_SampleTree* sample_tree;
        Sample* sample;
        rmtU64 first_timestamp;
        rmtBool are_samples_ready;

        Message* message = MessageQueue_PeekNextMessage(g_Remotery->mq_to_d3d11_main);
        if (message == NULL)
            break;

        // Keep track of the first message encountered during this loop and leave it's encountered
        // again. This means the loop as had a good attempt at trying to get timing data for all messages
        // in the queue.
        if (first_message == NULL)
            first_message = message;
        else if (first_message == message)
            break;

        // There's only one valid message type in this queue
        assert(message->id == MsgID_SampleTree);
        sample_tree = (Msg_SampleTree*)message->payload;
        sample = sample_tree->root_sample;
        assert(sample->type == SampleType_D3D11);

        // Retrieve timing of all D3D11 samples
        first_timestamp = g_Remotery->d3d11_first_timestamp;
        are_samples_ready = GetD3D11SampleTimes(sample, first_timestamp, &g_Remotery->d3d11_first_timestamp);

        // If the samples are ready, pass them onto the remotery thread for sending to the viewer
        if (are_samples_ready)
        {
            FreeD3D11TimeStamps(sample);
            AddSampleTreeMessage(g_Remotery->mq_to_rmt_thread, sample, sample_tree->allocator, sample_tree->thread_name, message->thread_sampler);
        }
        else
        {
            // Otherwise just put them to the back of the queue
            AddSampleTreeMessage(g_Remotery->mq_to_d3d11_main, sample, sample_tree->allocator, sample_tree->thread_name, message->thread_sampler);
        }

        MessageQueue_ConsumeNextMessage(g_Remotery->mq_to_d3d11_main, message);
    }

    rmt_EndCPUSample();
}


#endif  // RMT_USE_CUDA


#endif // RMT_ENABLED
