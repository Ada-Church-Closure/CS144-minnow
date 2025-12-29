#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

/**
 * 把绝对序列号更换成网络包中的序列号即可
 */
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // 这里的运算符是直接重写了,n强转之后 + ISN即可处理
  return zero_point + static_cast<uint32_t>(n);
}

/**
 * 拿到网络包中的序列号之后,要转换成我们能使用的绝对序列号
 *  原理就是:来了一个32bit的数字,我怎么转换成64bit,没有道理啊,谁来做这个参考
 *  显然就是重组器中下一个需要的字节能够作为我们转换的参考--->循环,只要离我们的checkpoint最近即可.
 */
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // 1.算出checkpoint在32bit环上面的位置
  uint32_t checkpoint_seqno = zero_point.raw_value_ + static_cast<uint32_t>(checkpoint);

  // 2.计算在环上的距离--->最小的有符号的距离
  int32_t diff = this->raw_value_ - checkpoint_seqno;

  // 3.直接算最终结果
  int64_t res = static_cast<int64_t>(checkpoint) + diff;

  // 4.小于0,那就要向后移动一个周期
  if (res < 0) {
    res += (1ul << 32);
  }

  return static_cast<uint64_t>(res);
}
