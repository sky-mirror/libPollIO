#pragma once

#include "PollIO/PollIOBase.h"

#define POLLIO_IMPL_epoll 1
#define POLLIO_IMPL_tcpdirect 2

#if defined NET_IMPL

#if NET_IMPL == POLLIO_IMPL_tcpdirect
#include "PollIO/TCPDirect.h"
#elif NET_IMPL == POLLIO_IMPL_epoll
#include "PollIO/NativeSocket.h"
#else
#error "Invalid NET_IMPL value"
#endif

namespace pollio {

#if NET_IMPL == POLLIO_IMPL_tcpdirect
class PollIO : public PollIOBase<pollio::TCPDirect> {
  using PollIOBase<pollio::TCPDirect>::PollIOBase;
};
#elif NET_IMPL == POLLIO_IMPL_epoll
class PollIO : public PollIOBase<pollio::NativeSocket> {
  using PollIOBase<pollio::NativeSocket>::PollIOBase;
};
#endif

} // namespace pollio
#else
#error "NET_IMPL must be set"
#endif
