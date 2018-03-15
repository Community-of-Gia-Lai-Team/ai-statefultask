/**
 * @file
 * @brief Declaration of class TimerQueue.
 *
 * @Copyright (C) 2018 Carlo Wood.
 *
 * RSA-1024 0x624ACAD5 1997-01-26                    Sign & Encrypt
 * Fingerprint16 = 32 EC A7 B6 AC DB 65 A6  F6 F6 55 DD 1C DC FF 61
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <deque>
#include <cstdint>
#include "Timer.h"
#include "debug.h"

namespace statefultask {

/*!
 * @brief A queue of running (possibly cancelled) timers, all of the same interval.
 *
 * This queue stores Timer*'s. Each Timer will have the same interval (which interval
 * that is depends on the context in which the TimerQueue was found). If a pointer
 * is nullptr then it represents a cancelled timer; such timers are not removed
 * from the queue because that would cost too much CPU.
 *
 * In the description of the member functions, 'current' means the next timer
 * that will be returned by pop(), also if that timer was already cancelled!
 */
class TimerQueue
{
  using running_timers_type = std::deque<Timer*>;

 private:
  uint64_t m_sequence_offset;                   // The number of timers that were popped from m_running_timers.
  running_timers_type m_running_timers;         // All running timers for the related interval.

 public:
  //! Construct an empty queue.
  TimerQueue() : m_sequence_offset(0) { }

  /*!
   * @brief Add a new timer to the end of the queue.
   *
   * @returns An ever increasing sequence number starting with 0.
   */
  uint64_t push(Timer* timer)
  {
    m_running_timers.emplace_back(timer);
    return m_running_timers.size() - 1 + m_sequence_offset;
  }

  /*!
   * @brief Check if a timer is current.
   *
   * @returns True if \a sequence is the value returned by a call to push() for a timer that is now at the front (will be returned by pop() next).
   */
  bool is_current(uint64_t sequence) const { return sequence == m_sequence_offset; }

  /*!
   * @brief Cancelled a running timer.
   *
   * The \a sequence passed must be returned by a previous call to push() and may not have expired.
   * This implies that the queue cannot be empty.
   *
   * @returns True if the cancelled Timer was the current timer.
   */
  bool cancel(uint64_t sequence)
  {
    size_t i = sequence - m_sequence_offset;
    // Sequence must be returned by a previous call to push() and the Timer may not already have expired.
    ASSERT(0 <= i && i < m_running_timers.size());
    // Do not cancel a timer twice.
    ASSERT(m_running_timers[i]);
    m_running_timers[i] = nullptr;
    bool is_current = i == 0;
    if (is_current)
    {
      // The cancelled timer is at the front of the queue. Remove it, and any subsequent cancelled timers.
      do
      {
        ++m_sequence_offset;
        m_running_timers.pop_front();
      }
      while (!m_running_timers.empty() && m_running_timers.front() == nullptr);       // Is the next timer also cancelled?
    }
    return is_current;
  }

  /*!
   * @brief Remove one timer from the front of the queue and return it.
   *
   * This function may only be called when the queue is not empty.
   * The returned point will never be null.
   *
   * @returns The current timer.
   */
  Timer* pop()
  {
    running_timers_type::iterator b = m_running_timers.begin();
    running_timers_type::iterator const e = m_running_timers.end();

    // Do not call pop() when the queue is empty.
    ASSERT(b != e);

    Timer* timer = *b;

    do
    {
      ++m_sequence_offset;
      ++b;
    }
    while (b != e && *b == nullptr);   // Is the next timer cancelled?

    // Erase the range [begin, b).
    m_running_timers.erase(m_running_timers.begin(), b);

    return timer;
  }

  /*!
   * @brief Return the next time point at which a timer of this interval will expire.
   */
  Timer::time_point next_expiration_point() const
  {
    if (m_running_timers.empty())
      return Timer::none;
    return m_running_timers.front()->get_expiration_point();
  }

  //--------------------------------------------------------------------------
  // Everything below is just for debugging.

  // Return true if are no running timers for the related interval.
  bool empty() const { return m_running_timers.empty(); }

  running_timers_type::size_type size() const { return m_running_timers.size(); }

  int cancelled_in_queue() const
  {
    int sz = 0;
    for (auto timer : m_running_timers)
      sz += timer ? 0 : 1;
    return sz;
  }

  uint64_t get_sequence_offset() const { return m_sequence_offset; }

  auto begin() const { return m_running_timers.begin(); }
  auto end() const { return m_running_timers.end(); }
};

} // namespace statefultask
