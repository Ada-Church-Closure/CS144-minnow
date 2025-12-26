#pragma once

#include <optional>
#include <map>
#include <string>

#include "byte_stream.hh"

class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.
  // 这里做初始化:开始期望字节index肯定为0,capacity取决于stream能处理的大小.
  explicit Reassembler( ByteStream&& output ) : output_( std::move( output ) ),
                                                _next_index(0),
                                                _eof_index(std::nullopt),
                                                _stored_data() {}

  /*
   * Insert a new substring to be reassembled into a ByteStream.
   *   `first_index`: the index of the first byte of the substring
   *   `data`: the substring itself
   *   `is_last_substring`: this substring represents the end of the stream
   *   `output`: a mutable reference to the Writer
   *
   * The Reassembler's job is to reassemble the indexed substrings (possibly out-of-order
   * and possibly overlapping) back into the original ByteStream. As soon as the Reassembler
   * learns the next byte in the stream, it should write it to the output.
   *
   * If the Reassembler learns about bytes that fit within the stream's available capacity
   * but can't yet be written (because earlier bytes remain unknown), it should store them
   * internally until the gaps are filled in.
   *
   * The Reassembler should discard any bytes that lie beyond the stream's available capacity
   * (i.e., bytes that couldn't be written even if earlier gaps get filled in).
   *
   * The Reassembler should close the stream after writing the last byte.
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // How many bytes are stored in the Reassembler itself?
  // This function is for testing only; don't add extra state to support it.
  uint64_t count_bytes_pending() const;

  // Access output stream reader
  // read字节流
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // Access output stream writer, but const-only (can't write from outside)
  // 这就是我们刚实现的写入字节流
  const Writer& writer() const { return output_.writer(); }

private:
  ByteStream output_;
  // 维护一些私有变量:
  // 下一个期望的index
  uint64_t _next_index;
  // 最后一个字节到达的时候,先记录一下最后的index
  std::optional<uint64_t> _eof_index;
  // 核心数据结构:存放乱序到达的,但是还不能直接写入stream的数据
  // 直接利用map的有序性
  std::map<uint64_t, std::string> _stored_data;
};
