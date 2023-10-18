#pragma once

#include <cstddef>
#include "PollIO/TCPConnection.h"

namespace pollio {

class SocketHandler {
public:
  inline void setVisitorIndex(size_t idx);

protected:
  size_t visitorIndex_;

private:
};

void SocketHandler::setVisitorIndex(size_t idx)
{
  visitorIndex_ = idx;
}

} // namespace pollio
