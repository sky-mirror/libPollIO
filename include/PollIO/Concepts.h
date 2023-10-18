#pragma once

#include <cstdint>
#include <concepts>
#include <utility>
#include <sys/types.h>

namespace pollio {

class TCPConnection;

// TODO: Enable clang-format after clang-format 16.0 is available.
// clang-format off
template <typename Buf>
concept IsBuf = requires(Buf buf, ssize_t size) {
  { buf.data() } -> std::same_as<char *>;
  { buf.writeBuf() } -> std::same_as<char *>;
  { buf.commit(size) } -> std::same_as<void>;
  { buf.consume(size) } -> std::same_as<void>;
  { std::as_const(buf).readableSize() } -> std::same_as<ssize_t>;
  { std::as_const(buf).writableSize() } -> std::same_as<ssize_t>;
};

template <typename ReadFn>
concept IsReadFn = requires(ReadFn readFn, int32_t fd, char *buf, size_t size) {
  { readFn(fd, buf, size) } -> std::same_as<ssize_t>;
};

template <typename Handler>
concept IsReadHandler = requires(Handler handler, TCPConnection *conn,
                                 char *buf, size_t size) {
  { handler.onRead(conn, buf, size) } -> std::same_as<size_t>;
};

template <typename Handler>
concept IsEventHandler = requires(Handler handler, TCPConnection *conn,
                                 char *buf, size_t size) {
  { handler.onAccepted(conn, conn) } -> std::same_as<void>;
  { handler.onConnected(conn) } -> std::same_as<void>;
  { handler.onDisconnected(conn) } -> std::same_as<void>;
  { handler.onRead(conn, buf, size) } -> std::same_as<size_t>;
  { handler.onFailed(conn) } -> std::same_as<void>;
};
// clang-format on

} // namespace pollio
