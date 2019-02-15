// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>

#include "queue.h"

#define MAX_ACQUIRE_WORKERS 1

namespace ioqueue {

Queue::Queue(const IoQueueCallbacks* cb) : sched_(), shutdown_(false), ops_(cb) {}

Queue::~Queue() {
    // printf("%s:%u\n", __FUNCTION__, __LINE__);
    if (!shutdown_) {
        Shutdown();
    }
}

zx_status_t Queue::OpenStream(uint32_t priority, uint32_t id) {
    // printf("%s:%u\n", __FUNCTION__, __LINE__);
    if (priority > IO_SCHED_MAX_PRI) {
        return ZX_ERR_INVALID_ARGS;
    }
    fbl::AllocChecker ac;
    StreamRef stream = fbl::AdoptRef(new (&ac) Stream(priority));
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }
    stream->id_ = id;
    // {
    //     fbl::AutoLock lock(&lock_);
    //     assert(!shutdown_);
    //     if (shutdown_) {
    //         fprintf(stderr, "Attempted to open stream on closed queue.\n");
    //         return ZX_ERR_BAD_STATE;
    //     }
    // }
    return sched_.AddStream(std::move(stream));
}

zx_status_t Queue::CloseStream(uint32_t id) {
     // printf("%s:%u\n", __FUNCTION__, __LINE__);
    for (bool first = true; ; first = false) {
        fbl::AutoLock lock(&lock_);
        if (first) {
            assert(!shutdown_);
            first = false;
        }
         if (shutdown_) {
             // Shutdown will handle closing this stream
             return ZX_ERR_BAD_STATE;
         }
        StreamRef stream = sched_.FindStream(id);
        if (stream == nullptr) {
             return ZX_ERR_INVALID_ARGS;
        }
        fbl::AutoLock stream_lock(&stream->lock_);
        stream->flags_ |= kIoStreamFlagClosed;
        // Once closed, the stream cannot transition from idle to scheduled.
        if ((stream->flags_ & kIoStreamFlagScheduled) == 0) {
            sched_.RemoveStreamLocked(stream);
            return ZX_OK;
        }
        lock.release();
        // Wait until all commands have completed.
        stream->event_unscheduled_.Wait(&stream->lock_);
        assert((stream->flags_ & kIoStreamFlagScheduled) == 0);
        // Have stream lock, but not queue lock. Order of locks requires taking queue lock first.
    }
}

zx_status_t Queue::Serve(uint32_t num_workers) {
    // printf("%s:%u\n", __FUNCTION__, __LINE__);
    if ((num_workers == 0) || (num_workers > IO_QUEUE_MAX_WORKERS)) {
        return ZX_ERR_INVALID_ARGS;
    }

    for (uint32_t i = 0; i < num_workers; i++) {
        zx_status_t status = worker[i].Launch(this, i, 0);
        if (status != ZX_OK) {
            fprintf(stderr, "Failed to create worker thread\n");
            // Shutdown required. TODO, signal fatal error.
            return status;
        }
        active_workers_++;
        num_workers_++;
    }
    return ZX_OK;
}

void Queue::Shutdown() {
    assert(shutdown_ == false);
    shutdown_ = true;

    // Wake threads blocking on incoming ops.
    ops_->cancel_acquire(ops_->context);
    // Close all open streams.
    sched_.CloseAll();
    // Wait until all ops have been completed.
    sched_.WaitUntilDrained();
    // Wait for all workers to exit.
    {
        fbl::AutoLock lock(&lock_);
        if (active_workers_ > 0) {
            // printf("q: waiting on worker exit\n");
            event_workers_exited_.Wait(&lock_);
            assert(active_workers_ == 0);
        }
        for (uint32_t i = 0; i < num_workers_; i++) {
            worker[i].Join();
        }
    }
    // printf("q: shutdown complete\n");
}

void Queue::WorkerExited(uint32_t id) {
    fbl::AutoLock lock(&lock_);
    active_workers_--;
    // printf("worker %u exiting, num_workers = %u\n", id, active_workers_);
    if (active_workers_ == 0) {
        // printf("signalling all workers exited\n");
        event_workers_exited_.Broadcast();
    }
}

zx_status_t Queue::GetAcquireSlot() {
    fbl::AutoLock lock(&lock_);
    if (acquire_workers_ >= MAX_ACQUIRE_WORKERS) {
        return ZX_ERR_SHOULD_WAIT;
    }
    acquire_workers_++;
    return ZX_OK;
}

void Queue::ReleaseAcquireSlot() {
    fbl::AutoLock lock(&lock_);
    assert(acquire_workers_ > 0);
    acquire_workers_--;
}

zx_status_t Queue::AcquireOps(Op** op_list, size_t* op_count, bool wait) {
    return ops_->acquire(ops_->context, Op::ToIoOpList(op_list), op_count, wait);
}

zx_status_t Queue::IssueOp(Op* op) {
    return ops_->issue(ops_->context, Op::ToIoOp(op));
}

void Queue::ReleaseOp(Op* op) {
    ops_->release(ops_->context, Op::ToIoOp(op));
}

} // namespace

// User API

zx_status_t IoQueueCreate(const IoQueueCallbacks* cb, IoQueue** q_out) {
    ioqueue::Queue* q = new ioqueue::Queue(cb);
    *q_out = static_cast<IoQueue*>(q);
    return ZX_OK;
}

void IoQueueDestroy(IoQueue* q) {
    ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
    delete queue;
}

zx_status_t IoQueueOpenStream(IoQueue* q, uint32_t priority, uint32_t id) {
    ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
    return queue->OpenStream(priority, id);
}

zx_status_t IoQueueCloseStream(IoQueue* q, uint32_t id) {
    ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
    return queue->CloseStream(id);
}

zx_status_t IoQueueServe(IoQueue* q, uint32_t num_workers) {
   ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
   return queue->Serve(num_workers);
}

void IoQueueShutdown(IoQueue* q) {
   ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
   queue->Shutdown();
}

void IoQueueAsyncCompleteOp(IoQueue* q, IoOp* op) {
   ioqueue::Queue* queue = static_cast<ioqueue::Queue*>(q);
   queue->AsyncCompleteOp(ioqueue::Op::FromIoOp(op));
}