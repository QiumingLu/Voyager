#include "voyager/core/tcp_server.h"

#include <assert.h>
#include "voyager/core/acceptor.h"
#include "voyager/core/eventloop.h"
#include "voyager/core/schedule.h"
#include "voyager/core/online_connections.h"
#include "voyager/util/logging.h"
#include "voyager/util/stringprintf.h"

using namespace std::placeholders;

namespace voyager {

TcpServer::TcpServer(EventLoop* eventloop, 
                     const SockAddr& addr,
                     const std::string& name,
                     int thread_size,
                     int backlog)
    : eventloop_(CHECK_NOTNULL(eventloop)),
      ipbuf_(addr.Ipbuf()),
      name_(name),
      acceptor_ptr_(new Acceptor(eventloop_, addr, backlog)),
      schedule_(new Schedule(eventloop_, thread_size-1)),
      conn_id_(0) {
  acceptor_ptr_->SetNewConnectionCallback(
      std::bind(&TcpServer::NewConnection, this, _1, _2));
  VOYAGER_LOG(INFO) << "TcpServer::TcpServer [" << name_ << "] is running";
}

TcpServer::~TcpServer() {
  VOYAGER_LOG(INFO) << "TcpServer::~TcpServer [" << name_ << "] is down";
}

void TcpServer::Start() {
  if (seq_.GetNext() == 0) {
    schedule_->Start();
    assert(!acceptor_ptr_->IsListenning());
    eventloop_->RunInLoop(
        std::bind(&Acceptor::EnableListen, acceptor_ptr_.get()));
  }
}

void TcpServer::NewConnection(int fd, const struct sockaddr_storage& sa) {
  eventloop_->AssertInMyLoop();
  char peer[64];
  SockAddr::SockAddrToIPPort(reinterpret_cast<const sockaddr*>(&sa),
                             peer, sizeof(peer)); 
  std::string conn_name = StringPrintf("%s-%s#%d", 
                                       ipbuf_.c_str(), peer, ++conn_id_);
  
  VOYAGER_LOG(INFO) << "TcpServer::NewConnection [" << name_ 
                    << "] - new connection [" << conn_name
                    << "] from " << peer;

  EventLoop* ev = schedule_->AssignLoop();
  TcpConnectionPtr conn_ptr(new TcpConnection(conn_name, ev, fd));
  
  conn_ptr->SetConnectionCallback(connection_cb_);
  conn_ptr->SetCloseCallback(close_cb_);
  conn_ptr->SetWriteCompleteCallback(writecomplete_cb_);
  conn_ptr->SetMessageCallback(message_cb_);
  
  ev->RunInLoop(std::bind(&TcpConnection::StartWorking, conn_ptr));
}

}  // namespace voyager
