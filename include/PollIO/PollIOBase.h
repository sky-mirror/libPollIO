#pragma once

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <spdlog/spdlog.h>
#include <sys/epoll.h>
#include "PollIO/TCPConnection.h"

namespace pollio {

using TxTimestampHandler = std::function<void(timespec)>;

template <typename Impl>
class PollIOBase {
public:
  explicit inline PollIOBase(ssize_t recvBufSize = 2048);

  inline bool listen(size_t visitIdx, const char *interface,
                     const char *serverIp, uint16_t serverPort);

  inline bool connect(size_t visitIdx, const char *interface,
                      const char *serverIp, uint16_t serverPort,
                      uint16_t localPort = 0);

  inline void close(TCPConnection *conn);

  inline size_t getSendSpace(TCPConnection *conn);
  inline int writeSome(TCPConnection *conn, const char *buf, size_t size,
                       bool more = false);
  inline bool write(TCPConnection *conn, const char *buf, size_t size,
                    bool more = false);
  inline bool writeNonblock(TCPConnection *conn, const char *buf, size_t size,
                            bool more = false);

  inline void setInterval(size_t idx, uint64_t milliseconds);

#ifdef ENABLE_TIMESTAMP
  inline timespec getLastRx(int getHW) const;
#endif

  inline std::string getLastError();
  inline std::string getPeerIP(TCPConnection *conn);

  template <typename Visitor>
  inline void run(Visitor &visitor);

  inline void setTxTimestampHandler(TxTimestampHandler h);

private:
  template <typename Visitor>
  struct Handler {
    inline void onAccepted(TCPConnection *listenConn,
                           TCPConnection *clientConn);
    inline void onConnected(TCPConnection *conn);
    inline void onDisconnected(TCPConnection *conn);
    inline void onFailed(TCPConnection *conn);
    inline size_t onRead(TCPConnection *conn, char *buf, size_t size);
    inline void onTxTimestamp(timespec timestamp);

    PollIOBase<Impl> &self_;
    Visitor &visitor_;
  };

  Impl impl_;
  uint64_t skipIntervalCheckCounter_{0};

  std::map<size_t, uint64_t> visitorIntervals_;
  std::map<size_t, timespec> lastEmitTimes_;

  TxTimestampHandler timestampHandler_;
};

template <typename Impl>
PollIOBase<Impl>::PollIOBase(ssize_t recvBufSize) : impl_(recvBufSize)
{
}

template <typename Impl>
bool PollIOBase<Impl>::listen(size_t visitIdx, const char *interface,
                              const char *serverIp, uint16_t serverPort)
{
  TCPConnection *conn = impl_.listen(interface, serverIp, serverPort);

  if(!conn) {
    auto err = impl_.getLastError();
    SPDLOG_ERROR("err: {}", err);
    return false;
  }

  conn->visitorIdx_ = visitIdx;

  return true;
}

template <typename Impl>
bool PollIOBase<Impl>::connect(size_t visitIdx, const char *interface,
                               const char *serverIp, uint16_t serverPort,
                               uint16_t localPort)
{
  TCPConnection *conn =
    impl_.connect(interface, serverIp, serverPort, localPort);

  if(!conn) {
    auto err = impl_.getLastError();
    SPDLOG_ERROR("err: {}", err);
    return false;
  }

  conn->visitorIdx_ = visitIdx;

  return true;
}

template <typename Impl>
void PollIOBase<Impl>::close(TCPConnection *conn)
{
  impl_.close(conn);
}

template <typename Impl>
size_t PollIOBase<Impl>::getSendSpace(TCPConnection *conn)
{
  return impl_.getSendSpace(conn);
}

template <typename Impl>
int PollIOBase<Impl>::writeSome(TCPConnection *conn, const char *buf,
                                size_t size, bool more)
{
  int sz = impl_.writeSome(conn, buf, size, more);

  return sz;
}

template <typename Impl>
bool PollIOBase<Impl>::write(TCPConnection *conn, const char *buf, size_t size,
                             bool more)
{
  bool ret = impl_.write(conn, buf, size, more);

  return ret;
}

template <typename Impl>
bool PollIOBase<Impl>::writeNonblock(TCPConnection *conn, const char *buf,
                                     size_t size, bool more)
{
  bool ret = impl_.writeNonblock(conn, buf, size, more);

  return ret;
}

template <typename Impl>
void PollIOBase<Impl>::setInterval(size_t idx, uint64_t milliseconds)
{
  if(!milliseconds) {
    visitorIntervals_.erase(idx);
    return;
  }

  // XXX minimum milliseconds is 2 because of COARSE
  visitorIntervals_[idx] = milliseconds;
  clock_gettime(CLOCK_MONOTONIC_COARSE, &lastEmitTimes_[idx]);
}

#ifdef ENABLE_TIMESTAMP
template <typename Impl>
timespec PollIOBase<Impl>::getLastRx(int getHW) const
{
  return impl_.getLastRx(getHW);
}
#endif

template <typename Impl>
std::string PollIOBase<Impl>::getLastError()
{
  return impl_.getLastError();
}

template <typename Impl>
std::string PollIOBase<Impl>::getPeerIP(TCPConnection *conn)
{
  return impl_.getPeerIP(conn);
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::run(Visitor &visitor)
{
  auto checkInterval = [&]() {
    // check interval every N loop.
    if constexpr(Impl::loopTimesInMillisecond > 1) {
      if(skipIntervalCheckCounter_++ <= Impl::loopTimesInMillisecond) {
        return;
      }
      skipIntervalCheckCounter_ = 0;
    }

    timespec now;
    clock_gettime(CLOCK_MONOTONIC_COARSE, &now);

    for(const auto &[idx, intervalMilliseconds] : visitorIntervals_) {
      timespec &lastEmitTime = lastEmitTimes_[idx];
      uint64_t milliseconds = (now.tv_sec - lastEmitTime.tv_sec) * 1'000 +
                              (now.tv_nsec - lastEmitTime.tv_nsec) / 1'000'000;
      if(milliseconds >= intervalMilliseconds) {
        lastEmitTime = now;
        visitor.emitInterval(idx, now);
      }
    }
  };

  PollIOBase<Impl>::Handler<Visitor> handler{.self_ = *this,
                                             .visitor_ = visitor};

  while(1) {
    impl_.wait(handler);
    checkInterval();
  }
}

template <typename Impl>
void PollIOBase<Impl>::setTxTimestampHandler(TxTimestampHandler h)
{
  timestampHandler_ = h;
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::Handler<Visitor>::onAccepted(TCPConnection *listenConn,
                                                    TCPConnection *clientConn)
{
  clientConn->visitorIdx_ = listenConn->visitorIdx_;

  visitor_.emitAccepted(listenConn->visitorIdx_, clientConn);
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::Handler<Visitor>::onConnected(TCPConnection *conn)
{
  visitor_.emitConnected(conn->visitorIdx_, conn);
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::Handler<Visitor>::onDisconnected(TCPConnection *conn)
{
  visitor_.emitDisconnected(conn->visitorIdx_, conn);
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::Handler<Visitor>::onFailed(TCPConnection *conn)
{
  visitor_.emitFailed(conn->visitorIdx_, conn);
}

template <typename Impl>
template <typename Visitor>
size_t PollIOBase<Impl>::Handler<Visitor>::onRead(TCPConnection *conn,
                                                  char *buf, size_t size)
{
  return visitor_.emitRead(conn->visitorIdx_, conn, buf, size);
}

template <typename Impl>
template <typename Visitor>
void PollIOBase<Impl>::Handler<Visitor>::onTxTimestamp(timespec timestamp)
{
  if(self_.timestampHandler_)
    self_.timestampHandler_(timestamp);
}

} // namespace pollio
