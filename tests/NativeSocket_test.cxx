#include <deque>
#include <tuple>
#include <gtest/gtest.h>
#include <functional>
#include <thread>
#include "PollIO/NativeSocket.h"

using namespace std;
using namespace pollio;

struct Feeder {
  ssize_t invoke(int fd, void *buf, size_t size)
  {
    if(results.empty()) {
      printf("warning: no results to feed\n");
      exit(1);
    }

    memset(buf, results.size(), size);

    auto &[ret, err] = results.front();
    results.pop_front();
    if(ret < 0)
      errno = err;
    return ret;
  }

  void add(ssize_t read_bytes, int err)
  {
    results.push_back({read_bytes, err});
  }

  bool ok()
  {
    if(!results.empty()) {
      printf("f result size %ld\n", results.size());
    }
    return results.empty();
  }

  deque<tuple<ssize_t, int>> results;
};

struct TestHandler {
  size_t onRead(TCPConnection *, char *buf, size_t size)
  {
    if(results.empty()) {
      printf("warning: no results to consume\n");
      exit(1);
    }

    auto &[ret, expSize] = results.front();
    results.pop_front();
    if(expSize != size) {
      printf("expSize(%ld) != size(%ld)\n", expSize, size);
      exit(1);
    }

    return ret;
  }

  deque<tuple<size_t, size_t>> results;

  void will_consume(size_t ret, size_t expSize)
  {
    results.push_back({ret, expSize});
  }

  bool ok()
  {
    if(!results.empty()) {
      printf("h result size %ld\n", results.size());
    }
    return results.empty();
  }
};

TEST(NativeSocket, ReadAll)
{
  TCPConnection conn;
  NativeSocket sock(128);

  Feeder f;
  auto rd = [&f](int fd, void *buf, size_t size) -> ssize_t {
    return f.invoke(fd, buf, size);
  };
  TestHandler h;

  bool ret;
  pollio::RecvBuf<128> sbuf, sbuf2;

  // if error, return false and do close later.
  f.add(-1, ENOMEM);
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // if EAGAIN, no more data in buffer.
  f.add(-1, EAGAIN);
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_TRUE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // return 0 == EOF.
  f.add(0, 0);
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // feed 128 bytes and consumes 0 byte, buffer full triggers error.
  f.add(128, 0);
  h.will_consume(0, 128);
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // call again triggers error too.
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_FALSE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // reset buffer.
  sbuf.consume(sbuf.readableSize());

  // feed 128 bytes and consumes 100 bytes
  f.add(128, 0);
  h.will_consume(100, 128);
  // 28 bytes remaining and still has 44 bytes.
  f.add(44, 0);
  f.add(-1, EAGAIN);
  // incompleted, consumes 0.
  h.will_consume(0, 28 + 44);
  ret = sock.readAll(sbuf, &conn, rd, h);
  ASSERT_TRUE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  // feed 128 bytes and consumes 100 bytes
  f.add(128, 0);
  // buffer => [3] * 128
  h.will_consume(100, 128);
  // buffer => [3] * 28 [0] * 100

  // 28 bytes remaining and still has 88 bytes.
  f.add(88, 0);

  // buffer => [3] * 28 [2] * 88 [0] * 28
  f.add(-1, EAGAIN);
  // incompleted, consumes 0.
  h.will_consume(0, 28 + 88);
  ret = sock.readAll(sbuf2, &conn, rd, h);
  ASSERT_TRUE(ret);
  ASSERT_TRUE(f.ok());
  ASSERT_TRUE(h.ok());

  for(int i = 0; i < 128; i++) {
    auto data = sbuf2.data()[i];

    if(i < 28) {
      EXPECT_EQ(3, data) << i;
    }
    else if(i < 28 + 88) {
      EXPECT_EQ(2, data) << i;
    }
  }
}

struct MyHandler {
  void onAccepted(TCPConnection *, TCPConnection *)
  {
    printf("accepted\n");
    connected = true;
  }

  void onTxTimestamp(timespec timestamp)
  {
  }

  void onConnected(TCPConnection *conn)
  {
    while(!connected) {
      // wait server accepted.
    }

    const char data[] = "hello, world";
    printf("write data (%s)\n", data);

    loop.write(conn, data, sizeof(data));
  }

  void onDisconnected(TCPConnection *conn)
  {
    stopped = true;
  }

  void onFailed(TCPConnection *conn)
  {
  }

  // ECHO
  size_t onRead(TCPConnection *conn, char *buf, size_t size)
  {
    printf("read data(%.*s)\n", int(size), buf);
    loop.write(conn, buf, size);
    loop.close(conn);
    return size;
  }

  NativeSocket &loop;
  atomic<bool> &stopped;
  atomic<bool> &connected;
};

TEST(NativeSocket, TCPClientAndServer)
{
  atomic<bool> started = false;
  atomic<bool> clientStopped;
  atomic<bool> serverConnected;
  atomic<bool> unused;

  thread serverThread([&]() {
    NativeSocket loop(2048);
    MyHandler handler{
      .loop = loop, .stopped = unused, .connected = serverConnected};
    loop.listen(nullptr, "127.0.0.1", 12345);
    while(1) {
      loop.wait(handler);
      started = true;

      if(clientStopped) {
        printf("server shutdown\n");
        return;
      }
    }
  });

  while(!started) {
    // wait server start
  }

  NativeSocket loop(2048);
  MyHandler handler{
    .loop = loop, .stopped = clientStopped, .connected = serverConnected};
  loop.connect(nullptr, "127.0.0.1", 12345);
  while(1) {
    loop.wait(handler);

    if(clientStopped) {
      printf("close client\n");
      break;
    }
  }

  serverThread.join();
}
