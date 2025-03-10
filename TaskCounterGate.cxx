#include "sys.h"
#include "TaskCounterGate.h"

namespace statefultask {

void TaskCounterGate::wakeup()
{
  Dout(dc::notice, "TaskCounterGate::wakeup(): waking up [" << this << "]");
  // Make sure we don't lose a notify because both the decrement and the notify
  // are done while inside the lambda but after the m_counter == 0 test.
  { std::lock_guard<std::mutex> lk(m_counter_is_zero_mutex); }
  m_counter_is_zero.notify_one();
}

void TaskCounterGate::wait()
{
  DoutEntering(dc::notice, "TaskCounterGate::wait() [" << this << "]");
  std::unique_lock<std::mutex> lk(m_counter_is_zero_mutex);
  // Only call wait() once.
  ASSERT(!is_waiting());
  // Reset the not_waiting_magic bit.
  m_counter.fetch_and(~not_waiting_magic, std::memory_order::relaxed);
  // Wait until m_counter reaches zero.
  m_counter_is_zero.wait(lk, [this](){ return m_counter == 0; });
}

} // namespace statefultask
