#pragma once

#include <cassert>
#include <cstring>
#include <spdlog/spdlog.h>

namespace pollio {

template <ssize_t RecvBufSize = 2048>
class RecvBuf {
public:
  inline ssize_t maxSize() const;
  inline char *writeBuf();
  inline char *data();
  inline ssize_t writableSize() const;
  inline ssize_t readableSize() const;
  inline void commit(ssize_t size);
  inline void consume(ssize_t size);

private:
  ssize_t readableSize_{0};
  char buf_[RecvBufSize];
};

template <ssize_t RecvBufSize>
ssize_t RecvBuf<RecvBufSize>::maxSize() const
{
  return RecvBufSize;
}

template <ssize_t RecvBufSize>
char *RecvBuf<RecvBufSize>::writeBuf()
{
  return buf_ + readableSize_;
}

template <ssize_t RecvBufSize>
char *RecvBuf<RecvBufSize>::data()
{
  return buf_;
}

template <ssize_t RecvBufSize>
ssize_t RecvBuf<RecvBufSize>::writableSize() const
{
  size_t ret = RecvBufSize - readableSize_;

  assert(ret >= 0);

  return ret;
}

template <ssize_t RecvBufSize>
ssize_t RecvBuf<RecvBufSize>::readableSize() const
{
  return readableSize_;
}

template <ssize_t RecvBufSize>
void RecvBuf<RecvBufSize>::commit(ssize_t size)
{
  SPDLOG_TRACE("this({:p}) commit size({})", (void *)this, size);
  assert(readableSize_ + size <= RecvBufSize);

  readableSize_ += size;
  SPDLOG_TRACE("this({:p}) readableSize_({})", (void *)this, readableSize_);
}

template <ssize_t RecvBufSize>
void RecvBuf<RecvBufSize>::consume(ssize_t size)
{
  SPDLOG_TRACE("this({:p}) consume size({}) readableSize_({})", (void *)this,
               size, readableSize_);
  assert(readableSize_ <= RecvBufSize);
  assert(readableSize_ >= size);
  readableSize_ -= size;
  if(readableSize_ == 0)
    return;

  assert(readableSize_ <= RecvBufSize);

  memmove(buf_, buf_ + size, readableSize_);
  SPDLOG_TRACE("this({:p}) after move size({}) readableSize_({})", (void *)this,
               size, readableSize_);
}

} // namespace pollio
