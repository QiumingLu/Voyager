// Copyright (c) 2016 Mirants Lu. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "voyager/port/mutex.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "voyager/util/logging.h"
#include "voyager/util/timeops.h"

namespace voyager {
namespace port {

static void PthreadCall(const char* label, int result) {
  if (result != 0) {
    VOYAGER_LOG(FATAL) << label << ": " << strerror(result);
  }
}

Mutex::Mutex() {
  PthreadCall("pthread_mutex_init", pthread_mutex_init(&mutex_, nullptr));
}

Mutex::~Mutex() {
  PthreadCall("pthread_mutex_destory", pthread_mutex_destroy(&mutex_));
}

void Mutex::Lock() {
  PthreadCall("pthread_mutex_lock", pthread_mutex_lock(&mutex_));
}

void Mutex::UnLock() {
  PthreadCall("pthread_mutex_unlock", pthread_mutex_unlock(&mutex_));
}

Condition::Condition(Mutex* mutex) : mutex_(mutex) {
  PthreadCall("pthread_cond_init", pthread_cond_init(&cond_, nullptr));
}

Condition::~Condition() {
  PthreadCall("pthread_cond_destory", pthread_cond_destroy(&cond_));
}

void Condition::Wait() {
  PthreadCall("pthread_cond_wait",
              pthread_cond_wait(&cond_, &mutex_->mutex_));
}

bool Condition::Wait(uint64_t milliseconds) {
  uint64_t timeout = (timeops::NowMicros() + milliseconds * 1000) * 1000;
  struct timespec outtime;
  outtime.tv_sec  = timeout / 1000000000;
  outtime.tv_nsec = timeout % 1000000000;
  int ret = pthread_cond_timedwait(&cond_, &mutex_->mutex_, &outtime);
  if (ret != 0 && ret != ETIMEDOUT) {
    PthreadCall("pthread_cond_timedwait", ret);
  }
  return ret == 0;
}

void Condition::Signal() {
  PthreadCall("pthread_cond_signal", pthread_cond_signal(&cond_));
}

void Condition::SignalAll() {
  PthreadCall("pthread_cond_broadcast", pthread_cond_broadcast(&cond_));
}

}  // namespace port
}  // namespace voyager
