#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <functional>

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms ), _current_RTO_ms( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }

  // 应用程序给我们的stream,我们要进行发送
  ByteStream input_;
  Wrap32 isn_;
  // Retranmition TimeOut
  uint64_t initial_RTO_ms_;

  //*------ 我们新增的控制数据结构 ------*//

  // 1.Timer State
  bool _timer_running = {false};
  uint64_t _timer_ms = { 0 };
  uint64_t _current_RTO_ms; // 这是我们当前的超时阈值 ---> 翻倍,超时重传

  // 2.维护基本状态
  uint64_t _next_seqno = { 0 }; // 下一个要发送的序号
  uint64_t _receiver_window_size = { 1 }; // 接收窗口的大小,初始化为1,允许发送SYN
  uint64_t _bytes_in_flight = { 0 }; // sent,但是没有ack的字节数
  uint64_t _consecutive_transmissions = { 0 }; // 一个packet重传多少次,意义在哪?

  // 3.标志位的基本状态
  bool _syn_sent = { false }; // 是否发送了SYN标志
  bool _fin_sent = { false };


  // 4.维护 OutStanding segments
  // push()--->刚发送完的数据
  // pop() ack的数据
  // front() 检查,是否重传
  std::deque<TCPSenderMessage> _outstanding_segments {};
};
