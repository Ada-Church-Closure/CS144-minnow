#include "reassembler.hh"
#include "debug.hh"

using namespace std;

/**
 * 这里就是重组器的核心:--->就是我们的滑动窗口协议.
 *  insert就是我们来了一些数据(string),我们现在要对于这些string进行处理
 */
void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( is_last_substring ) {
    _eof_index = first_index + data.size();
  }

  if ( !data.empty() ) {
    // 定义接收窗口的边界
    uint64_t left_win_index = _next_index;
    // 我们乱序到达的能接收的区域也是取决于byteStream的buffer
    uint64_t right_win_index = _next_index + output_.writer().available_capacity();

    uint64_t arrived_left_index = first_index;
    uint64_t arrived_right_index = first_index + data.size();
    // 1.截断
    // 抛弃packet,整个packet都是不在窗口范围之内
    if ( arrived_right_index <= left_win_index || arrived_left_index >= right_win_index ) {
      return;
    }

    // 可截断的情况
    if ( arrived_left_index < left_win_index ) {
      // 左边截断
      data.erase( 0, left_win_index - arrived_left_index );
      arrived_left_index = left_win_index;
    }

    // 容易出错,很细节的控制变量
    if ( arrived_right_index > right_win_index ) {
      // 直接用resize截取右边,效率高并且方便.
      data.resize( right_win_index - arrived_left_index );
    }
    // 2.merge合并,处理乱序和重叠的情况.
    // 这个挺像中等力扣的那种感觉
    // [arrived_left_index, arrived_right_index)
    uint64_t new_end = arrived_left_index + data.size();

    auto it = _stored_data.lower_bound( arrived_left_index );
    // 先考虑新到packet前面的数据
    if ( it != _stored_data.begin() ) {
      auto prev_it = std::prev( it );
      uint64_t prev_end = prev_it->first + prev_it->second.size();

      // 前面的尾巴到我们的头
      if ( prev_end > arrived_left_index ) {
        // 旧块比新块还要更长--->直接覆盖即可
        if ( prev_end > new_end ) {
          new_end = prev_end;
          data = prev_it->second;
          // 更新最前方的index
          arrived_left_index = prev_it->first;
        } else {
          // 没有更长的情况下
          uint64_t overlap = prev_end - arrived_left_index;
          if ( new_end > prev_end ) {
            data = prev_it->second + data.substr( overlap );
          } else {
            data = prev_it->second;
          }
          // 更新最前方的index
          arrived_left_index = prev_it->first;
        }
        // 此时,可以做删除的操作
        _stored_data.erase( prev_it );
      }
    }
    
    // data改变的话,就要进行显式的更新,这样处理更保险一些.
    new_end = arrived_left_index + data.size();

    // 接下来我们处理后面的string,处理后方重叠的情况
    while ( it != _stored_data.end() && it->first < new_end ) {
      uint64_t curr_end = it->first + it->second.size();
      if ( curr_end > new_end ) {
        data = data + it->second.substr( new_end - it->first );
        new_end = curr_end;
      }
      // 删除元素,并且立刻返回下一个元素iterator
      it = _stored_data.erase( it );
    }

    _stored_data[arrived_left_index] = data;

    // 插入之后,可以向bytestream进行写入的操作
    // 从头开始遍历,看能否做写入的操作......
    auto head = _stored_data.begin();
    while ( head != _stored_data.end() && _next_index == head->first ) {
      const std::string& string_to_write = head->second;
      output_.writer().push( string_to_write );
      _next_index += head->second.size();
      _stored_data.erase( head );
      head = _stored_data.begin();
    }
  }

  // 检查本次是否写入了eof标志
  if ( _eof_index.has_value() && _next_index == _eof_index.value() ) {
    output_.writer().close();
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  uint64_t bytes = 0;
  for ( const auto& [index, data] : _stored_data ) {
    bytes += data.size();
  }
  return bytes;
}
