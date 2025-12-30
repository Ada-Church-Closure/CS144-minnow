#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// How many sequence numbers are outstanding?
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return this->_bytes_in_flight;
}

// How many consecutive retransmissions have happened?
uint64_t TCPSender::consecutive_retransmissions() const
{
  return _consecutive_transmissions;
}

/**
 * 对于消息的核心处理逻辑,决定我们应该如何发送?
 */
void TCPSender::push( const TransmitFunction& transmit )
{
  // 1.获取当前可用窗口大小,若为0,也要发送--->进行0窗口探测
  uint64_t curr_window_size = this->_receiver_window_size == 0 ? 1 : this->_receiver_window_size;

  // 2.我们sender的逻辑是,对于可填满的窗口,我们要疯狂的填入
  while (curr_window_size > this->_bytes_in_flight) {
    uint64_t truly_useful_window_space = curr_window_size - this->_bytes_in_flight;
    uint64_t len = std::min(truly_useful_window_space, TCPConfig::MAX_PAYLOAD_SIZE);

    TCPSenderMessage msg;

    if (this->input_.reader().has_error()) {
      msg.RST = true;
      msg.seqno = Wrap32::wrap(this->_next_seqno, this->isn_);
      transmit(msg);
      return;
    }
    
    if (!this->_syn_sent) {
      this->_syn_sent = true;
      msg.SYN = true;
      --len;
    }

    std::string payload (this->input_.reader().peek());
    if (payload.size() > len) {
      payload.resize(len);
    }
    input_.reader().pop(payload.size());
    msg.payload = payload;

    // piggy Backing
    if (!this->_fin_sent && this->input_.reader().is_finished()) {
      if (truly_useful_window_space > msg.sequence_length()) {
        this->_fin_sent = true;
        msg.FIN = true;
      }
    }

    // 出现这种情况是意味着,应用层没有给我们数据
    if (msg.sequence_length() == 0) {
      break;
    }

    msg.seqno = Wrap32::wrap(this->_next_seqno, this->isn_);

    transmit(msg);
    // state renew Part
    this->_next_seqno += msg.sequence_length();
    this->_bytes_in_flight += msg.sequence_length();
    this->_outstanding_segments.push_back(msg);

    // start Timer
    if (!this->_timer_running) {
      this->_timer_running = true;
      this->_timer_ms = 0;
    }

  }
}

/**
 * 发送一个纯ACK,不进入队列,这个意义在哪?
 */
TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap(this->_next_seqno, this->isn_);
  if (this->input_.reader().has_error()) {
    msg.RST = true;
  }
  return msg;
}

/**
 * 获取了对方的ack之后,我们从自己的out_standing队列中划走能被确认的msg消息
 */
void TCPSender::receive( const TCPReceiverMessage& msg )
{

  // 要设置RST标志位,也就是接受方出错,我们直接断连.
  if (msg.RST) {
    this->input_.reader().set_error();
    return;
  }

  this->_receiver_window_size = msg.window_size;

  // 消息有可能只是单纯通告了一下自己窗口的大小
  if (!msg.ackno.has_value()) {
    return;
  }

  uint64_t abs_ackno = msg.ackno.value().unwrap(this->isn_, this->_next_seqno);

  if (abs_ackno > this->_next_seqno) {
    return;
  }

  bool new_data_acknowledged = false;

  while (!_outstanding_segments.empty()) {
    const auto& seg = _outstanding_segments.front();

    uint64_t seg_abs_seqno = seg.seqno.unwrap(this->isn_, this->_next_seqno);
    uint64_t seg_seq_length = seg.sequence_length();

    if (abs_ackno >= seg_abs_seqno + seg_seq_length) {
      this->_outstanding_segments.pop_front();
      this->_bytes_in_flight -= seg_seq_length;
      new_data_acknowledged = true;
    } else {
      // 头部没办法被ack,同理,后面也不行
      break;
    }
  }

  // 当本次有packet被确认的时候,我们就会进行重置RTO的操作
  if (new_data_acknowledged) {
    this->_consecutive_transmissions = 0;
    this->_current_RTO_ms = initial_RTO_ms_;
    this->_timer_ms = 0;
  }

  if (!this->_outstanding_segments.empty()) {
    this->_timer_running = true;
  } else {
    this->_timer_running = false;
    this->_timer_ms = 0;
  }
}

/**
 * 一旦超时了,到底重发谁?
 *  只会重发最早的那个数据包,并且超时时间加倍
 */
void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if (!this->_timer_running) {
    return;
  }
  this->_timer_ms += ms_since_last_tick;
  // 超时重传机制:只会重传最早的那个包
  if (this->_timer_ms >= this->_current_RTO_ms && !this->_outstanding_segments.empty()) {
    transmit(this->_outstanding_segments.front());
    if (this->_receiver_window_size > 0) {
      this->_current_RTO_ms *= 2;
    }
    this->_timer_ms = 0;
    ++this->_consecutive_transmissions;
  }
}
