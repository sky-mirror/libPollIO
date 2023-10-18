#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <memory>
#include <vector>
#include <set>
#include <error.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "PollIO/TCPConnection.h"
#include "PollIO/Concepts.h"

namespace pollio {

class NativeSocket {
public:
  inline static constexpr uint64_t loopTimesInMillisecond = 1;

  explicit inline NativeSocket(ssize_t recvBufSize = 2048);

  inline std::string getLastError();

  inline TCPConnection *listen(const char *interface, const char *serverIp,
                               uint16_t serverPort, int backlog = 5);

  inline TCPConnection *connect(const char *interface, const char *serverIp,
                                uint16_t serverPort, uint16_t localPort = 0);

  inline void close(TCPConnection *conn);
  inline size_t getSendSpace(TCPConnection *conn);

  inline int writeSome(TCPConnection *conn, const void *data, uint32_t size,
                       bool more = false);
  inline bool write(TCPConnection *conn, const void *data, uint32_t size,
                    bool more = false);
  inline bool writeNonblock(TCPConnection *conn, const void *data,
                            uint32_t size, bool more = false);

  inline std::string getPeerIP(TCPConnection *conn);

  bool readAll(IsBuf auto &buf, TCPConnection *conn, IsReadFn auto readFn,
               IsReadHandler auto &handler);

  inline timespec getLastRx(int getHW) const;

  inline int wait(IsEventHandler auto &handler, int maxEvents = 10,
                  int timeout = 1);

private:
  ssize_t recvBufSize_;
  std::set<int> waitConnecteds_;

  int epollFd_{0};
  int listenSock_{0};

#ifdef ENABLE_TIMESTAMP
  struct timespec lastHWRx_;
  struct timespec lastSWRx_;
  std::vector<struct timespec> lastHWTxs_;
#endif
};

NativeSocket::NativeSocket(ssize_t recvBufSize) : recvBufSize_(recvBufSize)
{
  epollFd_ = epoll_create1(0);
  if(epollFd_ < 0) {
    SPDLOG_CRITICAL("epoll_create1: {}", strerror(errno));
    exit(1);
  }
}

std::string NativeSocket::getLastError()
{
  return strerror(errno);
}

TCPConnection *NativeSocket::listen(const char *interface, const char *serverIp,
                                    uint16_t serverPort, int backlog)
{
  if(listenSock_ != 0) {
    SPDLOG_CRITICAL("listenSock is used");
    exit(1);
  }
  // setup listen sock.

  listenSock_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if(listenSock_ < 0) {
    SPDLOG_CRITICAL("socket: listenSock_: {}", strerror(errno));
    exit(1);
  }

  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = inet_addr(serverIp);
  s_addr.sin_port = htons(serverPort);

  int yes = 1;
  if(setsockopt(listenSock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
    SPDLOG_CRITICAL("setsockopt SO_REUSEADDR: {}", strerror(errno));
    exit(1);
  }

  if(bind(listenSock_, reinterpret_cast<struct sockaddr *>(&s_addr),
          sizeof(s_addr)) < 0) {
    SPDLOG_CRITICAL("bind: listenSock_: {}", strerror(errno));
    exit(1);
  }

  if(::listen(listenSock_, backlog) < 0) {
    SPDLOG_CRITICAL("listen: {}", strerror(errno));
    exit(1);
  }

  TCPConnection *conn = new TCPConnection();
  conn->data.fd_ = listenSock_;

  struct epoll_event event;
  event.events = EPOLLIN | EPOLLET;
  event.data.ptr = conn;

#ifdef SHOW_MEM_FOOTPRINT
  SPDLOG_INFO("new object conn({:p}) fd({}) for listening", (void *)conn,
              listenSock_);
#endif

  if(epoll_ctl(epollFd_, EPOLL_CTL_ADD, listenSock_, &event) < 0) {
    SPDLOG_CRITICAL("epoll_ctl: listenSock_: {}", strerror(errno));
    exit(1);
  }

  return conn;
}

TCPConnection *NativeSocket::connect(const char *interface,
                                     const char *serverIp, uint16_t serverPort,
                                     uint16_t localPort)
{
  int conn_sock = 0;

  conn_sock = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  if(conn_sock == -1) {
    SPDLOG_CRITICAL("socket: {}", strerror(errno));
    exit(1);
  }

  if(interface != nullptr && localPort != 0) {
    int yes = 1;
    if(setsockopt(conn_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0) {
      SPDLOG_CRITICAL("setsockopt SO_REUSEADDR: {}", strerror(errno));
      exit(1);
    }

    struct sockaddr_in local_addr;
    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = inet_addr(interface);
    local_addr.sin_port = htons(localPort);
    if(::bind(conn_sock, reinterpret_cast<struct sockaddr *>(&local_addr),
              sizeof(local_addr)) < 0) {
      SPDLOG_CRITICAL("bind: {}", strerror(errno));
      exit(1);
    }
  }

  struct sockaddr_in s_addr;
  memset(&s_addr, 0, sizeof(s_addr));
  s_addr.sin_family = AF_INET;
  s_addr.sin_addr.s_addr = inet_addr(serverIp);
  s_addr.sin_port = htons(serverPort);

  int rc = ::connect(conn_sock, reinterpret_cast<struct sockaddr *>(&s_addr),
                     sizeof(s_addr));
  if(rc < 0 && errno != EINPROGRESS) {
    SPDLOG_CRITICAL("connect: {}", strerror(errno));
    exit(1);
  }

  TCPConnection *conn = new TCPConnection();
  conn->data.fd_ = conn_sock;

#ifdef SHOW_MEM_FOOTPRINT
  SPDLOG_INFO("new object conn({:p}) fd({}) client", (void *)conn, conn_sock);
#endif

  struct epoll_event event;
  event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
  event.data.ptr = conn;
  if(epoll_ctl(epollFd_, EPOLL_CTL_ADD, conn_sock, &event) < 0) {
    SPDLOG_CRITICAL("epoll_ctl: add conn_sock: {}", strerror(errno));
    exit(1);
  }

  return conn;
}

void NativeSocket::close(TCPConnection *conn)
{
  assert(conn != nullptr);

  ::close(conn->data.fd_);
}

size_t NativeSocket::getSendSpace(TCPConnection *conn)
{
  return recvBufSize_;
}

int NativeSocket::writeSome(TCPConnection *conn, const void *data,
                            uint32_t size, bool more)
{
  assert(conn != nullptr);

  int flags = MSG_NOSIGNAL;
  if(more)
    flags |= MSG_MORE;
  ssize_t ret = ::send(conn->data.fd_, data, size, flags);

#ifdef ENABLE_TIMESTAMP
  // R01 size is 80.
  if(size == 80) {
    // FIXME: use SO_TIMESTAMPING
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    lastHWTxs_.push_back(ts);
  }
#endif

  if(ret < 0) {
    if(errno == EAGAIN) {
      ret = 0;
    }
    else {
      SPDLOG_ERROR("write({}): {}", conn->data.fd_, strerror(errno));
    }
  }

  return ret;
}

bool NativeSocket::write(TCPConnection *conn, const void *data, uint32_t size,
                         bool more)
{
  const uint8_t *ptr = reinterpret_cast<const uint8_t *>(data);

  do {
    int sent = writeSome(conn, ptr, size, more);

    if(sent < 0)
      return false;

    ptr += sent;
    size -= sent;
  } while(size != 0);

  return true;
}

bool NativeSocket::writeNonblock(TCPConnection *conn, const void *data,
                                 uint32_t size, bool more)
{
  if(writeSome(conn, data, size, more) != static_cast<int>(size)) {
    return false;
  }
  return true;
}

std::string NativeSocket::getPeerIP(TCPConnection *conn)
{
  assert(conn != nullptr);

  struct sockaddr_in addr;
  socklen_t addrSize = sizeof(struct sockaddr_in);
  int res = getpeername(conn->data.fd_,
                        reinterpret_cast<struct sockaddr *>(&addr), &addrSize);
  if(res) {
    SPDLOG_ERROR("getpeername: {}", strerror(errno));
    return "";
  }

  return std::string(inet_ntoa(addr.sin_addr));
}

bool NativeSocket::readAll(IsBuf auto &buf, TCPConnection *conn,
                           IsReadFn auto readFn, IsReadHandler auto &handler)
{
  assert(conn != nullptr);
  auto fd = conn->data.fd_;

  // do until read socket buffer is empty.
  while(1) {
    size_t writableSize = buf.writableSize();

    // buffer is full and last time onRead event doesn't help us.
    if(writableSize == 0) {
      SPDLOG_ERROR("fd({}) buffer is full", fd);
      return false;
    }

    SPDLOG_TRACE("fd({}) writableSize({})", fd, writableSize);

#ifdef ENABLE_TIMESTAMP
    clock_gettime(CLOCK_MONOTONIC, &lastHWRx_);
#endif

    ssize_t read_bytes = readFn(fd, buf.writeBuf(), writableSize);

#ifdef ENABLE_TIMESTAMP
    clock_gettime(CLOCK_MONOTONIC, &lastSWRx_);
#endif

    if(read_bytes < 0) {
      // no more data.
      if(errno == EAGAIN || errno == EWOULDBLOCK) {
        return true;
      }

      SPDLOG_TRACE("fd({}) read failed: {}", fd, strerror(errno));
      return false;
    }

    // EOF.
    if(read_bytes == 0) {
      SPDLOG_TRACE("fd({}) EOF", fd);
      return false;
    }

    // has new data, try callback to consume bytes.
    if(read_bytes > 0) {
      SPDLOG_TRACE("fd({}) commit({})", fd, read_bytes);
      buf.commit(read_bytes);
    }

    size_t usedBytes = handler.onRead(conn, buf.data(), buf.readableSize());
    if(usedBytes > 0) {
      SPDLOG_TRACE("fd({}) consume({})", fd, usedBytes);
      buf.consume(usedBytes);
    }

    /*
    if(usedBytes == 0 && buf.writableSize() == 0) {
      // buffer is full and nothing consumed, close conntion.
      SPDLOG_ERROR("fd({}) buffer is full", fd);
      return false;
    }
    */
  }

  return true;
}

#ifdef ENABLE_TIMESTAMP
timespec NativeSocket::getLastRx(int getHW) const
{
  return getHW ? lastHWRx_ : lastSWRx_;
}
#endif

int NativeSocket::wait(IsEventHandler auto &handler, int maxEvents, int timeout)
{
  epoll_event events[maxEvents];

  int nfds = epoll_wait(epollFd_, events, maxEvents, timeout);
  if(nfds < 0) {
    SPDLOG_CRITICAL("epoll_wait: {}", strerror(errno));
    exit(1);
  }

#ifdef ENABLE_TIMESTAMP
  if(!lastHWTxs_.empty()) {
    for(auto ts : lastHWTxs_) {
      handler.onTxTimestamp(ts);
    }
    lastHWTxs_.clear();
  }
#endif

  for(int n = 0; n < nfds; n++) {
    TCPConnection *conn = static_cast<TCPConnection *>(events[n].data.ptr);
    int32_t fd = conn->data.fd_;

    if(fd == listenSock_) {
      while(1) {
        struct sockaddr_in s_addr;
        socklen_t addrlen = 0;
        memset(&s_addr, 0, sizeof(s_addr));
        int conn_sock =
          accept4(listenSock_, reinterpret_cast<struct sockaddr *>(&s_addr),
                  &addrlen, SOCK_NONBLOCK);
        if(conn_sock < 0) {
          if(errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
          }

          SPDLOG_TRACE("accept: {}", strerror(errno));
          continue;
        }

        TCPConnection *clientConn = new TCPConnection();
        clientConn->data.fd_ = conn_sock;

#ifdef SHOW_MEM_FOOTPRINT
        SPDLOG_INFO("new object conn({:p}) fd({}) accepted", (void *)clientConn,
                    conn_sock);
#endif

        struct epoll_event ev;
        ev.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET;
        ev.data.ptr = clientConn;
        if(epoll_ctl(epollFd_, EPOLL_CTL_ADD, conn_sock, &ev) < 0) {
          SPDLOG_CRITICAL("epoll_ctl: conn_sock: {}", strerror(errno));
          exit(1);
        }

        std::string ip = getPeerIP(clientConn);
        SPDLOG_TRACE("event(onAccepted) conn({:p}) fd({}) from {}",
                     (void *)clientConn, fd, ip);
        clientConn->setSourceIP(ip.c_str());
        handler.onAccepted(conn, clientConn);
      }

      continue;
    }

    if(events[n].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
      // get error?

      if(events[n].events & EPOLLRDHUP) {
        SPDLOG_TRACE("fd({}) peer RDHUP", fd);
      }

      if(events[n].events & EPOLLHUP) {
        SPDLOG_TRACE("fd({}) peer HUP", fd);
      }

      if(events[n].events & EPOLLERR) {
        SPDLOG_TRACE("fd({}) peer ERR", fd);
      }

      SPDLOG_TRACE(
        "event(onDisconnected) conn({:p}) fd({}) peer hup connection",
        (void *)conn, fd);
      handler.onDisconnected(conn);

      close(conn);

#ifdef SHOW_MEM_FOOTPRINT
      SPDLOG_INFO("delete object conn({:p}) HUP", (void *)conn);
#endif

      delete conn;

      continue;
    }

    if(events[n].events & EPOLLOUT) {
      SPDLOG_TRACE(
        "event(onConnected) conn({:p}) fd({}) socket is ready to write",
        (void *)conn, fd);
      handler.onConnected(conn);

      struct epoll_event event;
      event.events = EPOLLIN | EPOLLRDHUP | EPOLLET;
      event.data.ptr = conn;
      if(epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        SPDLOG_ERROR("epoll_ctl: remove EPOLLOUT: {}", strerror(errno));
      }
    }

    if(events[n].events & EPOLLIN) {
      auto &inbuf = conn->inbuf_;

      bool ret = readAll(inbuf, conn, ::read, handler);
      if(!ret) {
        SPDLOG_TRACE("event(onDisconnected) conn({:p}) fd({}) read EOF",
                     (void *)conn, fd);
        handler.onDisconnected(conn);

        close(conn);

#ifdef SHOW_MEM_FOOTPRINT
        SPDLOG_INFO("delete object conn({:p}) EOF", (void *)conn);
#endif

        delete conn;
      }
    }
  }

  return 0;
}

} // namespace pollio
