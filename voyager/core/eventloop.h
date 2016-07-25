#ifndef VOYAGER_CORE_EVENTLOOP_H_
#define VOYAGER_CORE_EVENTLOOP_H_

#include <functional>
#include <vector>
#include <map>

#include "voyager/util/scoped_ptr.h"
#include "voyager/port/currentthread.h"
#include "voyager/port/mutexlock.h"
#include "voyager/core/tcp_connection.h"

namespace voyager {

struct Timer;
class Dispatch;
class EventPoller;
class TimerEvent;
class Timestamp;
class EventLoop {
 public:
  typedef std::function<void()> Func;
  EventLoop();
  ~EventLoop();
  void Loop();
  void RunInLoop(const Func& func);
  void RunInLoop(Func&& func);
  void QueueInLoop(const Func& func);
  void QueueInLoop(Func&& func);

  Timer* RunAt(const Timestamp& t, const TimeProcCallback& timeproc);
  Timer* RunAfter(double delay, const TimeProcCallback& timeproc);
  Timer* RunEvery(double interval, const TimeProcCallback& timeproc);
  
  Timer* RunAt(const Timestamp& t, TimeProcCallback&& timeproc);
  Timer* RunAfter(double delay, TimeProcCallback&& timeproc);
  Timer* RunEvery(double interval, TimeProcCallback&& timeproc);

  void DeleteTimer(Timer* t);

  void AssertInMyLoop() {
    if (!IsInMyLoop()) {
      Abort();
    }
  }
  bool IsInMyLoop() const { 
	return tid_ == port::CurrentThread::Tid(); 
  }

  void Exit();

  static EventLoop* EventLoopOfCurrentThread();
  
  // only internal use
  void RemoveDispatch(Dispatch* dispatch);
  void UpdateDispatch(Dispatch* dispatch);
  bool HasDispatch(Dispatch* dispatch);

 private:
  void RunFuncQueue();
  void HandleRead();
  void Abort();
 
  void WakeUp();
 
  bool exit_;
  bool runfuncqueue_;

  const uint64_t tid_;
  scoped_ptr<EventPoller> poller_;

  scoped_ptr<TimerEvent> timer_ev_;

  #ifdef __linux__
  int wakeup_fd_;
  #elif __APPLE__
  int wakeup_fd_[2];
  #endif

  scoped_ptr<Dispatch> wakeup_dispatch_;

  port::Mutex mu_;
  std::vector<Func> funcqueue_;

  // No copying allow
  EventLoop(const EventLoop&);
  void operator=(const EventLoop&);
};

}  // namespace voyager

#endif  // VOYAGER_CORE_EVENTLOOP_H_
