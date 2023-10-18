#pragma once

#include <string>
#include <cstdint>
#include <spdlog/spdlog.h>
#include "PollIO/detail/Buffer.h"

namespace pollio {

class TCPConnection {
  template <typename U>
  friend class PollIOBase;

  friend class TCPDirect;
  friend class NativeSocket;

public:
  TCPConnection() = default;

  template <typename T>
  inline T *getUserData();

  template <typename T>
  inline void setUserData(T *p);

  inline void setSourceIP(const char *ip)
  {
    strncpy(sourceIP_, ip, 16);
    sourceIP_[15] = '\0';
  }

  inline const char *sourceIP() const
  {
    return sourceIP_;
  }

private:
  // store pointer or fd, depends on event loop implementation.
  // this variable is to replace epoll_event's data field and store related
  // data in one class to improve data locality.
  union {
    int32_t fd_;
    void *ptr_;
  } data{0};

  // it stores user data and its lifetime should be handled by application.
  // created when OnConnected event and destroyed when OnDisconnected.
  void *userData_{nullptr};

  // it helps HandlerVisitor to determine which handler should be invoked.
  size_t visitorIdx_{0};

  char sourceIP_[16]{};

  // it stores incompleted incoming packet data. since it could larger than
  // cache line size, put it on the last field of this class to avoid cache miss.
  RecvBuf<> inbuf_;
};

template <typename T>
T *TCPConnection::getUserData()
{
  return static_cast<T *>(userData_);
}

template <typename T>
void TCPConnection::setUserData(T *p)
{
  userData_ = p;
}

} // namespace pollio
