#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

/**
 * 收到信息,然后进行相关的处理,交给reassembler来进行一些处理
 * 这里和我们在课本上学习的区别就是 SYN 和 FIN都是占具体的一个序列号的.
 */
void TCPReceiver::receive( TCPSenderMessage message )
{

  // 对方强行终止 RST
  if (message.RST) {
    // 这就是error的用处所在
    reassembler_.writer().set_error();
    return;
  }

  // 相当于是第一次握手--->初始化序列号
  if (message.SYN) {
    _isn = message.seqno;
    _syn_received = true;
  } 

  // 如果本次还没有建立连接,那就不进行任何处理
  if (!_syn_received) {
    return;
  }

  // 准备一个绝对的checkpoint benchmark--->SYN会占有一个位置
  uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;
  // 32bit->64bit
  uint64_t abs_seq_no = message.seqno.unwrap(this->_isn.value(), checkpoint);
  // 计算reassembler中的index序号
  uint64_t stream_index = abs_seq_no - 1 + (message.SYN ? 1 : 0);
  // 然后直接插入重组器处理
  this->reassembler_.insert(stream_index, message.payload, message.FIN);
}

/**
 * 回单的基本逻辑,构造rsv message然后返回.
 * ACK确认号以及窗口大小
 */
TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg;

  // 同理,如果我们的stream这里有问题,也要进行RST对应的设置
  if (this->reassembler_.writer().has_error()) {
    msg.RST = true;
  }

  auto cap = this->reassembler_.writer().available_capacity();
  // 设置窗口的最大值,不能大于 int_16 max
  msg.window_size = cap > UINT16_MAX ? UINT16_MAX : cap;
  // ACK--->返回下一个期望的字节的位置
  if (_syn_received) {
    uint64_t abs_seqno = this->reassembler_.writer().bytes_pushed() + 1;
    // 我们的流已经关闭,我们认为自己收到了对方来的FIN标志位 FIN消耗了一个序列号
    if (this->reassembler_.writer().is_closed()) {
      ++abs_seqno;
    }
    msg.ackno = Wrap32::wrap(abs_seqno, this->_isn.value());
  } else {
    msg.ackno = std::nullopt;
  } 
  return msg;
}
