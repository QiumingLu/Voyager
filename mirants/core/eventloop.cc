#include "mirants/core/eventloop.h"

#include <signal.h>
#ifndef __MACH__
#include <sys/eventfd.h>
#endif
#include <unistd.h>

#include "mirants/core/dispatch.h"
#include "mirants/core/event_poll.h"
#include "mirants/core/socket_util.h"
#include "mirants/port/mutexlock.h"
#include "mirants/util/logging.h"
#include "mirants/util/timestamp.h"

namespace mirants {
namespace {

__thread EventLoop* t_eventloop = NULL;

const int kPollTime = 10*1000;

#ifndef __MACH__
int CreateEventfd() {
  int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (fd < 0) {
    MIRANTS_LOG(ERROR) << "Failed in eventfd";
    abort();
  }
  return fd;
}
#endif

class IgnoreSIGPIPE {
 public:
  IgnoreSIGPIPE() {
    ::signal(SIGPIPE, SIG_IGN);
  }
};

IgnoreSIGPIPE ignore_SIGPIPE;

}  // namespace anonymous

EventLoop* EventLoop::GetEventLoopOfCurrentThread() {
  return t_eventloop;
}

#ifndef __MACH__

EventLoop::EventLoop()
    : exit_(false),
      runfuncqueue_(false),
      tid_(port::CurrentThread::Tid()),
      poller_(new EventPoll(this)),
      timer_ev_(new TimerEvent(this)),
      wakeup_fd_(CreateEventfd()),
      wakeup_dispatch_(new Dispatch(this, wakeup_fd_)) {
        
  MIRANTS_LOG(INFO) << "EventLoop "<< this << " created in thread " << tid_;
  if (t_eventloop) {
    MIRANTS_LOG(FATAL) << "Another EventLoop " << t_eventloop
                       << " exists in this thread " << tid_;
  } else {
    t_eventloop = this;
  }

  wakeup_dispatch_->SetReadCallback(std::bind(&EventLoop::HandleRead, this));
  wakeup_dispatch_->EnableRead();
}

#else

EventLoop::EventLoop()
    : exit_(false),
      runfuncqueue_(false),
      tid_(port::CurrentThread::Tid()),
      poller_(new EventPoll(this)),
      timer_ev_(new TimerEvent(this)) {

  if (::socketpair(AF_UNIX, SOCK_STREAM, 0, wakeup_fd_) < 0) {
    MIRANTS_LOG(FATAL) << "socketpair failed";
  }
  wakeup_dispatch_.reset(new Dispatch(this, wakeup_fd_[0]));

  MIRANTS_LOG(INFO) << "EventLoop "<< this << " created in thread " << tid_;
  if (t_eventloop) {
    MIRANTS_LOG(FATAL) << "Another EventLoop " << t_eventloop
                       << " exists in this thread " << tid_;
  } else {
    t_eventloop = this;
  }

  wakeup_dispatch_->SetReadCallback(std::bind(&EventLoop::HandleRead, this));
  wakeup_dispatch_->EnableRead();
}

#endif



EventLoop::~EventLoop() {
  MIRANTS_LOG(INFO) << "EventLoop " << this << " of thread " << tid_
                     << " destructs in thread " << port::CurrentThread::Tid();
  
  wakeup_dispatch_->DisableAll();
  wakeup_dispatch_->RemoveEvents();
#ifndef __MACH__
  ::close(wakeup_fd_);
#else
  ::close(wakeup_fd_[0]);
  ::close(wakeup_fd_[1]);
#endif
  t_eventloop = NULL;  
}

void EventLoop::Loop() {
  AssertThreadSafe();
  exit_ = false;

  while(!exit_) {
    std::vector<Dispatch*> dispatches;
    poller_->Poll(kPollTime, &dispatches);
    for (std::vector<Dispatch*>::iterator it = dispatches.begin();
        it != dispatches.end(); ++it) {
      (*it)->HandleEvent();
    }
    RunFuncQueue();
  }
}

void EventLoop::Exit() {
  exit_ = true;
  
  // 在必要时唤醒IO线程，让它及时终止循环。
  if (!IsInCreatedThread()) {
    WakeUp();
  }
}

void EventLoop::RunInLoop(const Func& func) {
  if (IsInCreatedThread()) {
    func();
  } else {
    QueueInLoop(func);
  }
}

void EventLoop::RunInLoop(Func&& func) {
  if (IsInCreatedThread()) {
    func();
  } else {
    QueueInLoop(std::move(func));
  }
}

void EventLoop::QueueInLoop(const Func& func) {
  {
    port::MutexLock lock(&mu_);
    funcqueue_.push_back(func);
  }

  // "必要时"有两种情况：
  // 1、如果调用QueueInLoop()的线程不是IO线程，那么唤醒是必需的；
  // 2、如果在IO线程调用QueueInLoop(),而此时正在调用RunFuncQueue
  if (!IsInCreatedThread() || runfuncqueue_) {
    WakeUp();
  }
}

void EventLoop::QueueInLoop(Func&& func) {
  {
    port::MutexLock lock(&mu_);
    funcqueue_.push_back(std::move(func));
  }
  // "必要时"有两种情况：
  // 1、如果调用QueueInLoop()的线程不是IO线程，那么唤醒是必需的；
  // 2、如果在IO线程调用QueueInLoop(),而此时正在调用RunFuncQueue
  if (!IsInCreatedThread() || runfuncqueue_) {
    WakeUp();
  }
}

#ifndef __MACH__
Timer* EventLoop::RunAt(const Timestamp& t, const TimeProcCallback& timeproc) {
  return timer_ev_->AddTimer(timeproc, t, 0.0);
}

Timer* EventLoop::RunAfter(double delay, const TimeProcCallback& timeproc) {
  Timestamp t(AddTime(Timestamp::Now(), delay));
  return RunAt(t, timeproc);
}

Timer* EventLoop::RunEvery(double interval, const TimeProcCallback& timeproc) {
  Timestamp t(AddTime(Timestamp::Now(), interval));
  return timer_ev_->AddTimer(timeproc, t, interval);
}

Timer* EventLoop::RunAt(const Timestamp& t, TimeProcCallback&& timeproc) {
  return timer_ev_->AddTimer(std::move(timeproc), t, 0.0);
}

Timer* EventLoop::RunAfter(double delay, TimeProcCallback&& timeproc) {
  Timestamp t(AddTime(Timestamp::Now(), delay));
  return RunAt(t, std::move(timeproc));
}

Timer* EventLoop::RunEvery(double interval, TimeProcCallback&& timeproc) {
  Timestamp t(AddTime(Timestamp::Now(), interval));
  return timer_ev_->AddTimer(timeproc, t, interval);
}

void EventLoop::DeleteTimer(Timer* t) {
  timer_ev_->DeleteTimer(t);
}
#endif

void EventLoop::RemoveDispatch(Dispatch* dispatch) {
  assert(dispatch->OwnerEventLoop() == this);
  this->AssertThreadSafe();
  poller_->RemoveDispatch(dispatch);
}

void EventLoop::UpdateDispatch(Dispatch* dispatch) {
  assert(dispatch->OwnerEventLoop() == this);
  this->AssertThreadSafe();
  poller_->UpdateDispatch(dispatch);
}

bool EventLoop::HasDispatch(Dispatch* dispatch) {
  assert(dispatch->OwnerEventLoop() == this);
  this->AssertThreadSafe();
  return poller_->HasDispatch(dispatch);
}

void EventLoop::RunFuncQueue() {
  std::vector<Func> funcs;
  runfuncqueue_ = true;
  {
    port::MutexLock lock(&mu_);
    funcs.swap(funcqueue_);
  }
  for (std::vector<Func>::iterator it = funcs.begin(); 
       it != funcs.end(); ++it) {
    (*it)();
  }
  runfuncqueue_ = false;
}

void EventLoop::WakeUp() {
  uint64_t one = 1;
#ifndef __MACH__
  ssize_t n  = sockets::Write(wakeup_fd_, &one, sizeof(one));
#else
    ssize_t n  = sockets::Write(wakeup_fd_[1], &one, sizeof(one));
#endif
  if (n != sizeof(one)) {
    MIRANTS_LOG(ERROR) << "EventLoop::WakeUp - " << wakeup_fd_ << " writes "
                       << n << " bytes instead of 8";
  }
}

void EventLoop::HandleRead() {
  uint64_t one = 1;
#ifndef __MACH__
  ssize_t n = sockets::Read(wakeup_fd_, &one, sizeof(one));
#else
    ssize_t n = sockets::Read(wakeup_fd_[0], &one, sizeof(one));
#endif
  if (n != sizeof(one)) {
    MIRANTS_LOG(ERROR) << "EventLoop::HandleRead - " << wakeup_fd_ << " reads "
                       << n << " bytes instead of 8";
  }
}

void EventLoop::AbortForNotInCreatedThread() {
  MIRANTS_LOG(FATAL) << "EventLoop::AbortForNotInCreatedThread - (EventLoop)"
                     << this << " was created in tid_ = " << tid_
                     << ", currentthread's tid_ = " 
                     << port::CurrentThread::Tid();
}

}  // namespace mirants
