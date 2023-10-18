#pragma once

#include <cerrno>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <error.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <zf/zf.h>
#include "PollIO/TCPConnection.h"
#include "PollIO/Concepts.h"

namespace pollio {

class TCPDirect {
public:
  inline static constexpr uint64_t loopTimesInMillisecond = 8192;

  explicit inline TCPDirect(ssize_t unused = -1);
  inline ~TCPDirect();

  inline std::string getLastError();

  // ownership of return value is not transferred, borrow to caller to fill-in
  // user data.
  inline TCPConnection *listen(const char *interface, const char *serverIp,
                               uint16_t serverPort);

  inline TCPConnection *connect(const char *interface, const char *serverIp,
                                uint16_t serverPort, uint16_t localPort = 0);

  inline void close(TCPConnection *conn);
  inline size_t getSendSpace(TCPConnection *conn);

  inline int writeSome(TCPConnection *conn, const void *data, uint32_t size,
                       bool more = false);
  inline bool write(TCPConnection *conn, const void *data_, uint32_t size,
                    bool more = false);
  inline bool writeNonblock(TCPConnection *conn, const void *data,
                            uint32_t size, bool more = false);

  inline std::string getPeerIP(TCPConnection *conn);

  inline bool readAll(TCPConnection *conn, IsReadHandler auto &handler);

#ifdef ENABLE_TIMESTAMP
  timespec inline getLastRx(int getHW) const;
#endif

  inline int wait(IsEventHandler auto &handler, int maxEvents = 10);

private:
  // allocate one app for each interface. the app can only call listen once.
  struct App {
    std::string interface_;
    zf_stack *stack_{nullptr};
    zf_muxer_set *muxer_{nullptr};
    struct zftl *listener_{nullptr};
  };

  std::set<TCPConnection *> waitConnecteds_;

#ifdef ENABLE_TIMESTAMP
  struct timespec lastHWRx_;
  struct timespec lastSWRx_;
#endif

  std::vector<App> apps_;
  inline static bool zf_inited{false};
};

TCPDirect::TCPDirect(ssize_t)
{
  if(zf_inited)
    return;
  zf_inited = true;

  int rc = zf_init();
  if(rc < 0) {
    SPDLOG_CRITICAL("zf_init: {}", strerror(errno));
    exit(1);
  }
}

TCPDirect::~TCPDirect()
{
  for(auto &app : apps_) {
    if(app.listener_) {
      zf_muxer_del(zftl_to_waitable(app.listener_));
    }

    if(app.muxer_) {
      zf_muxer_free(app.muxer_);
    }

    if(app.listener_) {
      zftl_free(app.listener_);
    }

    if(app.stack_) {
      zf_stack_free(app.stack_);
    }
  }
  zf_deinit();
}

std::string TCPDirect::getLastError()
{
  return strerror(errno);
}

TCPConnection *TCPDirect::listen(const char *interface, const char *serverIp,
                                 uint16_t serverPort)
{
  App *app{nullptr};
  for(size_t i = 0; i < apps_.size(); i++) {
    if(apps_[i].interface_ == interface) {
      SPDLOG_TRACE("use previous app slot: {}", i);
      app = &apps_[i];
      break;
    }
  }

  if(app == nullptr) {
    apps_.push_back({});
    app = &apps_[apps_.size() - 1];
    app->interface_ = interface;
    SPDLOG_TRACE("create new app slot: {}", apps_.size() - 1);
  }

  if(app->listener_ != nullptr) {
    SPDLOG_CRITICAL("the app is listened");
    exit(1);
  }

  struct zf_attr *attr;
  int rc = zf_attr_alloc(&attr);
  if(rc < 0) {
    SPDLOG_CRITICAL("zf_attr_alloc: {}", strerror(errno));
    exit(1);
  }

  // set attr if needed
  zf_attr_set_int(attr, "reactor_spin_count", 1);
  zf_attr_set_str(attr, "interface", interface);
#ifdef ENABLE_TIMESTAMP
  zf_attr_set_int(attr, "rx_timestamping", 1);
  zf_attr_set_int(attr, "tx_timestamping", 0);
#endif

  rc = zf_stack_alloc(attr, &app->stack_);
  if(rc < 0) {
    SPDLOG_CRITICAL("zf_stack_alloc: {}", strerror(errno));
    exit(1);
  }

  rc = zf_muxer_alloc(app->stack_, &app->muxer_);
  if(rc < 0) {
    SPDLOG_CRITICAL("zf_muxer_alloc: {}", strerror(errno));
    exit(1);
  }

  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = inet_addr(serverIp);
  s_addr.sin_port = htons(serverPort);

  rc = zftl_listen(app->stack_, reinterpret_cast<struct sockaddr *>(&s_addr),
                   sizeof(s_addr), attr, &app->listener_);
  if(rc < 0) {
    SPDLOG_CRITICAL("zftl_listen: {}", strerror(errno));
    exit(1);
  }

  zf_attr_free(attr);

  TCPConnection *conn = new TCPConnection();
  conn->data.ptr_ = app->listener_;

  struct epoll_event event;
  event.events = EPOLLIN;
  event.data.ptr = conn;

#ifdef SHOW_MEM_FOOTPRINT
  SPDLOG_INFO("new object conn({:p}) fd({}) for listening", (void *)conn,
              app->listener_);
#endif

  if(zf_muxer_add(app->muxer_, zftl_to_waitable(app->listener_), &event) < 0) {
    SPDLOG_CRITICAL("zf_muxer_add: listener_: {}", strerror(errno));
    exit(1);
  }

  return conn;
}

TCPConnection *TCPDirect::connect(const char *interface, const char *serverIp,
                                  uint16_t serverPort, uint16_t localPort)
{
  App *app{nullptr};
  for(size_t i = 0; i < apps_.size(); i++) {
    if(apps_[i].interface_ == interface) {
      SPDLOG_TRACE("use previous app slot: {}", i);
      app = &apps_[i];
      break;
    }
  }

  if(app == nullptr) {
    apps_.push_back({});
    app = &apps_[apps_.size() - 1];
    app->interface_ = interface;
    SPDLOG_TRACE("create new app slot: {}", apps_.size() - 1);
  }

  struct zf_attr *attr;
  int rc = zf_attr_alloc(&attr);
  if(rc < 0) {
    SPDLOG_CRITICAL("zf_attr_alloc: {}", strerror(errno));
    exit(1);
  }

  if(app->stack_ == nullptr) {
    // set attr if needed
    zf_attr_set_int(attr, "reactor_spin_count", 1);
    zf_attr_set_str(attr, "interface", interface);
#ifdef ENABLE_TIMESTAMP
    zf_attr_set_int(attr, "rx_timestamping", 1);
    zf_attr_set_int(attr, "tx_timestamping", 1);
#endif

    rc = zf_stack_alloc(attr, &app->stack_);
    if(rc < 0) {
      SPDLOG_CRITICAL("zf_stack_alloc: {}", strerror(errno));
      exit(1);
    }

    rc = zf_muxer_alloc(app->stack_, &app->muxer_);
    if(rc < 0) {
      SPDLOG_CRITICAL("zf_muxer_alloc: {}", strerror(errno));
      exit(1);
    }
  }

  struct zft_handle *tcp_handle;
  rc = zft_alloc(app->stack_, attr, &tcp_handle);
  if(rc < 0) {
    SPDLOG_CRITICAL("zft_alloc: {}", strerror(errno));
    exit(1);
  }

  zf_attr_free(attr);

  if(localPort) {
    struct sockaddr_in local_addr;
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(localPort);
    rc = zft_addr_bind(tcp_handle, (struct sockaddr *)&local_addr,
                       sizeof(local_addr), 0);
    if(rc < 0) {
      SPDLOG_CRITICAL("zft_addr_bind: {}", strerror(errno));
      exit(1);
    }
  }

  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = inet_addr(serverIp);
  s_addr.sin_port = htons(serverPort);

  struct zft *zock{nullptr};
  rc =
    zft_connect(tcp_handle, (struct sockaddr *)&s_addr, sizeof(s_addr), &zock);
  if(rc < 0) {
    SPDLOG_WARN("zft_connect: {}", strerror(errno));
    return nullptr;
  }

  TCPConnection *conn = new TCPConnection();
  conn->data.ptr_ = zock;
  struct epoll_event event;
  event.events = EPOLLIN | EPOLLHUP | EPOLLERR;
  event.data.ptr = conn;

  if(zf_muxer_add(app->muxer_, zft_to_waitable(zock), &event) < 0) {
    SPDLOG_CRITICAL("zf_muxer_add: connect: {}", strerror(errno));
    exit(1);
  }

  waitConnecteds_.insert(conn);

#ifdef SHOW_MEM_FOOTPRINT
  SPDLOG_INFO("new object conn({:p}) zock({:p})", (void *)conn, (void *)zock);
#endif

  return conn;
}

void TCPDirect::close(TCPConnection *conn)
{
  assert(conn != nullptr);
  assert(conn->data.ptr_ != nullptr);

  struct zft *sock = static_cast<struct zft *>(conn->data.ptr_);

  SPDLOG_TRACE("shutdown conn({:p}) sock({:p})", (void *)conn, (void *)sock);

  zft_shutdown_tx(sock);

  // conn->data.ptr_ = nullptr;

  waitConnecteds_.erase(conn);
}

size_t TCPDirect::getSendSpace(TCPConnection *conn)
{
  assert(conn != nullptr);
  assert(conn->data.ptr_ != nullptr);

  struct zft *sock = static_cast<struct zft *>(conn->data.ptr_);

  SPDLOG_TRACE("conn({:p}) sock({:p})", (void *)conn, (void *)sock);

  size_t res = 0;
  int rc = zft_send_space(sock, &res);
  if(rc) {
    SPDLOG_WARN("zft_send_space: {}", strerror(-rc));
    return 0;
  }

  return res;
}

int TCPDirect::writeSome(TCPConnection *conn, const void *data, uint32_t size,
                         bool more)
{
  assert(conn != nullptr);
  assert(conn->data.ptr_ != nullptr);

  struct zft *sock = static_cast<struct zft *>(conn->data.ptr_);

  SPDLOG_TRACE("conn({:p}) sock({:p})", (void *)conn, (void *)sock);

  int flags = 0;
  if(more)
    flags |= MSG_MORE;
  ssize_t ret = zft_send_single(sock, data, size, flags);
  if(ret < 0) {
    if(errno == EAGAIN) {
      ret = 0;
    }
    else {
      SPDLOG_ERROR("write({:p}): {}", (void *)sock, strerror(errno));
    }
  }

  return ret;
}

bool TCPDirect::write(TCPConnection *conn, const void *data_, uint32_t size,
                      bool more)
{
  const uint8_t *data = reinterpret_cast<const uint8_t *>(data_);

  do {
    int sent = writeSome(conn, data, size, more);
    if(sent < 0)
      return false;
    data += sent;
    size -= sent;
  } while(size != 0);

  return true;
}

bool TCPDirect::writeNonblock(TCPConnection *conn, const void *data,
                              uint32_t size, bool more)
{
  if(writeSome(conn, data, size, more) != static_cast<int>(size)) {
    return false;
  }

  return true;
}

std::string TCPDirect::getPeerIP(TCPConnection *conn)
{
  assert(conn != nullptr);
  assert(conn->data.ptr_ != nullptr);

  struct zft *sock = static_cast<struct zft *>(conn->data.ptr_);

  SPDLOG_TRACE("conn({:p}) sock({:p})", (void *)conn, (void *)sock);

  struct sockaddr_in addr;
  socklen_t addr_size = sizeof(struct sockaddr_in);
  zft_getname(sock, nullptr, 0, reinterpret_cast<struct sockaddr *>(&addr),
              &addr_size);

  return std::string(inet_ntoa(addr.sin_addr));
}

bool TCPDirect::readAll(TCPConnection *conn, IsReadHandler auto &handler)
{
  assert(conn != nullptr);
  assert(conn->data.ptr_ != nullptr);

  struct zft *zock = static_cast<struct zft *>(conn->data.ptr_);
  auto &buf = conn->inbuf_;

  struct {
    uint8_t msg[sizeof(struct zft_msg)];
    struct iovec iov;
  } msg;
  struct zft_msg *zm = reinterpret_cast<struct zft_msg *>(msg.msg);
  zm->iovcnt = 1;

  zft_zc_recv(zock, zm, 0);
  if(zm->iovcnt == 0)
    return true;

#ifdef ENABLE_TIMESTAMP
  clock_gettime(CLOCK_MONOTONIC, &lastSWRx_);
  unsigned flags = 0;
  int rc = zft_pkt_get_timestamp(zock, zm, &lastHWRx_, 0, &flags);
  if(rc) {
    SPDLOG_WARN("zft_pkt_get_timestamp failed: {}", strerror(rc));
  }
#endif

  char *new_data = reinterpret_cast<char *>(msg.iov.iov_base);
  uint32_t new_size = msg.iov.iov_len;

  if(new_size == 0) {
    zft_zc_recv_done(zock, zm);
    return false;
  }

  if(new_size > buf.writableSize()) {
    zft_zc_recv_done(zock, zm);
    return false;
  }

  if(buf.readableSize() == 0) {
    size_t used_bytes = handler.onRead(conn, new_data, new_size);
    if(used_bytes) {
      new_data += used_bytes;
      memcpy(buf.writeBuf(), new_data, new_size - used_bytes);
      buf.commit(new_size - used_bytes);
    }
  }
  else {
    memcpy(buf.writeBuf(), new_data, new_size);
    buf.commit(new_size);
    size_t used_bytes = handler.onRead(conn, buf.data(), buf.readableSize());
    if(used_bytes > 0) {
      buf.consume(used_bytes);
    }
  }

  if(zock) { // this could have been closed
    zft_zc_recv_done(zock, zm);
  }
  return true;
}

#ifdef ENABLE_TIMESTAMP
timespec TCPDirect::getLastRx(int getHW) const
{
  return getHW ? lastHWRx_ : lastSWRx_;
}
#endif

int TCPDirect::wait(IsEventHandler auto &handler, int maxEvents)
{
  for(const auto &app : apps_) {
    if(!app.muxer_) {
      continue;
    }

    if(!zf_stack_has_pending_work(app.stack_)) {
      continue;
    }

    struct epoll_event events[maxEvents];

    int nfds = zf_muxer_wait(app.muxer_, events, maxEvents, 1);
    if(nfds < 0) {
      SPDLOG_CRITICAL("zf_muxer_wait: {}", strerror(errno));
      exit(1);
    }

    for(int n = 0; n < nfds; n++) {
      TCPConnection *conn = static_cast<TCPConnection *>(events[n].data.ptr);

      if(conn->data.ptr_ == app.listener_) {
        while(1) {
          struct zft *zock;
          int rc = zftl_accept(app.listener_, &zock);
          if(rc < 0) {
            if(rc == -EAGAIN)
              break;

            SPDLOG_WARN("zftl_accept: {}", strerror(rc));
            break;
          }

          TCPConnection *clientConn = new TCPConnection();
          clientConn->data.ptr_ = zock;

          struct epoll_event ev;
          ev.events = EPOLLIN | EPOLLHUP | EPOLLERR;
          ev.data.ptr = clientConn;
          if(zf_muxer_add(app.muxer_, zft_to_waitable(zock), &ev)) {
            SPDLOG_CRITICAL("zf_muxer_add: conn_sock: {}", strerror(errno));
            exit(1);
          }

          waitConnecteds_.insert(clientConn);

#ifdef SHOW_MEM_FOOTPRINT
          SPDLOG_INFO("new object conn({:p}) zock({:p})", (void *)clientConn,
                      (void *)zock);
#endif

          std::string ip = getPeerIP(clientConn);
          SPDLOG_TRACE(
            "event(onAccepted) conn({:p}) accept new connection from {}",
            (void *)conn, ip);
          clientConn->setSourceIP(ip.c_str());

          handler.onAccepted(conn, clientConn);
        }

        continue;
      }

      struct zft *zock = static_cast<struct zft *>(conn->data.ptr_);
      // EPOLLERR stands for tx timestamp
      if(events[n].events & EPOLLERR) {
#ifdef ENABLE_TIMESTAMP
        struct zf_pkt_report reports[1];
        int count_in_out = 1;
        int rc = zft_get_tx_timestamps(zock, reports, &count_in_out);
        if(rc) {
          SPDLOG_WARN("zft_get_tx_timestamps: {}", strerror(rc));
        }
        // R01 size is 80.
        else if(count_in_out && reports[0].bytes == 80) {
          handler.onTxTimestamp(reports[0].timestamp);
        }
#endif
      }

      if(events[n].events & EPOLLHUP) {
        // get error?

        SPDLOG_TRACE("event(onDisconnected) conn({:p}) peer hup connection",
                     (void *)conn);

        handler.onDisconnected(conn);
        close(conn);

#ifdef SHOW_MEM_FOOTPRINT
        SPDLOG_INFO("delete object conn({:p}) zock({:p}) HUP", (void *)conn,
                    (void *)zock);
#endif

        zft_free(zock);
        delete conn;

        continue;
      }

      if(events[n].events & EPOLLIN) {
        bool ret = readAll(conn, handler);
        if(!ret) {
          SPDLOG_TRACE("event(onDisconnected) conn({:p}) read EOF",
                       (void *)conn);

          handler.onDisconnected(conn);
          close(conn);

#ifdef SHOW_MEM_FOOTPRINT
          SPDLOG_INFO("delete object conn({:p}) zock({:p}) EOF", (void *)conn,
                      (void *)zock);
#endif

          zft_free(zock);
          delete conn;
        }
      }
    }
  }

  if(waitConnecteds_.empty())
    return 0;

  for(auto it = waitConnecteds_.begin(); it != waitConnecteds_.end();) {
    TCPConnection *conn = *it;
    if(zft_state(static_cast<struct zft *>(conn->data.ptr_)) ==
       TCP_ESTABLISHED) {
      SPDLOG_TRACE("event(onConnected) conn({:p}) state is ESTABLISHED",
                   (void *)conn);
      handler.onConnected(*it);
      it = waitConnecteds_.erase(it);
    }
    else {
      it++;
    }
  }

  return 0;
}

} // namespace pollio
