#ifndef VOYAGER_CORE_CONNECTOR_H_
#define VOYAGER_CORE_CONNECTOR_H_

#include <functional>
#include <memory>
#include <string>
#include <netdb.h>

#include "voyager/core/client_socket.h"
#include "voyager/core/dispatch.h"
#include "voyager/core/sockaddr.h"
#include "voyager/util/scoped_ptr.h"
#include "voyager/util/timeops.h"
#ifdef __linux__
#include "voyager/core/newtimer.h"
#endif

namespace voyager {

class EventLoop;

class Connector : public std::enable_shared_from_this<Connector> {
 public:
  typedef std::function<void (int fd)> NewConnectionCallback;

  Connector(EventLoop* ev, const SockAddr& addr);

  void SetNewConnectionCallback(const NewConnectionCallback& cb) {
    newconnection_cb_ = cb;
  }
  void SetNewConnectionCallback(NewConnectionCallback&& cb) {
    newconnection_cb_ = std::move(cb);
  }

  void Start();
  void Stop();

 private:
  enum ConnectState {
    kDisConnected,
    kConnected,
    kConnecting
  };

  static const uint64_t kMaxRetryTime = 30000000;
  static const uint64_t kInitRetryTime = 2000000;

  void StartInLoop();
  void Connect();
  void Connecting();
  void Retry();

  void ConnectCallback();

  void ResetDispatch();

  std::string StateToString() const;

  EventLoop* ev_;
  SockAddr addr_;
  ConnectState state_;
  uint64_t retry_time_;
  bool connect_;
  scoped_ptr<Dispatch> dispatch_;
  scoped_ptr<ClientSocket> socket_;
  NewConnectionCallback newconnection_cb_;

#ifdef __linux__
  scoped_ptr<NewTimer> timer_;
#endif

  // No copying allow
  Connector(const Connector&);
  void operator=(const Connector&);
};

}  // namespace voyager

#endif  // VOYAGER_CORE_CONNECTOR_H_
