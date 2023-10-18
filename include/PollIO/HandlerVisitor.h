#pragma once

#include <cassert>
#include <cstddef>
#include <variant>
#include <vector>
#include "PollIO/TCPConnection.h"

namespace pollio {

template <typename... Ts>
class HandlerVisitor {
public:
  template <typename T>
  inline void addHandler(T handler);

  void emitAccepted(size_t idx, TCPConnection *conn);
  void emitConnected(size_t idx, TCPConnection *conn);
  void emitDisconnected(size_t idx, TCPConnection *conn);
  void emitFailed(size_t idx, TCPConnection *conn);
  size_t emitRead(size_t idx, TCPConnection *conn, const char *buf, size_t sz);
  void emitInterval(size_t idx, const timespec &);

private:
  std::vector<std::variant<Ts...>> handlers_;
};

template <typename... Ts>
template <typename T>
void HandlerVisitor<Ts...>::addHandler(T handler)
{
  handlers_.push_back({handler});
  handler->setVisitorIndex(handlers_.size() - 1);
}

template <typename... Ts>
void HandlerVisitor<Ts...>::emitAccepted(size_t idx, TCPConnection *conn)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  std::visit([&](auto &h) { h->onAccepted(conn); }, handler);
}

template <typename... Ts>
void HandlerVisitor<Ts...>::emitConnected(size_t idx, TCPConnection *conn)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  std::visit([&](auto &h) { h->onConnected(conn); }, handler);
}

template <typename... Ts>
void HandlerVisitor<Ts...>::emitDisconnected(size_t idx, TCPConnection *conn)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  std::visit([&](auto &h) { h->onDisconnected(conn); }, handler);
}

template <typename... Ts>
void HandlerVisitor<Ts...>::emitFailed(size_t idx, TCPConnection *conn)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  std::visit([&](auto &h) { h->onFailed(conn); }, handler);
}

template <typename... Ts>
size_t HandlerVisitor<Ts...>::emitRead(size_t idx, TCPConnection *conn,
                                       const char *buf, size_t sz)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  size_t consumed;
  std::visit([&](auto &h) { consumed = h->onRead(conn, buf, sz); }, handler);
  return consumed;
}

template <typename... Ts>
void HandlerVisitor<Ts...>::emitInterval(size_t idx, const timespec &ts)
{
  assert(idx < handlers_.size());

  auto &handler = handlers_[idx];

  std::visit([&](auto &h) { h->onInterval(ts); }, handler);
}

} // namespace pollio
