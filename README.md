# libPollIO

## Introduction

The library is inspired by [MengRao/pollnet](https://github.com/MengRao/pollnet). It's a busy polling networking library which supports BSD Socket, Solarflare TCPDirect, and efvi. But it only polls the single connection. We developed the library to make an abstraction layer for TCP connection multiplexing to handle multiple connections in a single thread. For Socket implementation, it uses epoll. And TCPDirect supports multiplexing natively.

## Installation

1. Clone

```
git submodule add https://github.com/sky-mirror/libPollIO.git libPollIO
```

2. Adding the library as a subdirectory

edit CMakeLists.txt

```
add_subdirectory(libPollIO)
```

## Authors

- @xnum
- tinlans
