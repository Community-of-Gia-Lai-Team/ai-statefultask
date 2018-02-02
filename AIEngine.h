/**
 * @file
 * @brief Declaration of class AIEngine.
 *
 * Copyright (C) 2010 - 2013, 2017  Carlo Wood.
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
 *
 * CHANGELOG
 *   and additional copyright holders.
 *
 *   01/03/2010
 *   - Initial version, written by Aleric Inglewood @ SL
 *
 *   28/02/2013
 *   - Rewritten from scratch to fully support threading.
 *
 *   2017/01/07
 *   - Changed license to Affero GPL.
 *   - Transfered copyright to Carlo Wood.
 */

#pragma once

#include "threadsafe/aithreadsafe.h"
#include "threadsafe/Condition.h"
#include "AIStatefulTask.h"
#include "debug.h"
#include <list>
#include <chrono>
#include <boost/intrusive_ptr.hpp>

/*!
 * @brief Task queue and dispatcher.
 *
 * This object dispatches tasks from \ref mainloop().
 *
 * Each of member functions @link group_run AIStatefulTask::run()@endlink end with a call to <code>AIStatefulTask::reset()</code>
 * which in turn calls <code>AIStatefulTask::multiplex(initial_run)</code>.
 * When a default engine was passed to \c run then \c multiplex adds the task to the queue of that engine. When no default engine was
 * passed to \c run then the task is being run immediately in the thread that called \c run and will <em>keep</em>
 * running until it is either aborted or one of @link AIStatefulTask::finish finish()@endlink, @link group_yield yield*()@endlink or @link group_wait wait*()@endlink
 * is called!
 *
 * Moreover, every time a task without default engine (nor target engine) calls \c wait, then the task will continue running
 * immediately when some thread calls @link AIStatefulTask::signal signal()@endlink, and again <em>keep</em> running!
 *
 * If you don't want a call to \c run and/or \c signal to take too long, or it would not be thread-safe to not run the task from
 * the main loop of a thread, then either pass a default engine or make sure the task
 * \htmlonly&dash;\endhtmlonly when (re)started \htmlonly&dash;\endhtmlonly always quickly calls <code>yield*()</code> or <code>wait*()</code> (again), etc.
 *
 * Note that if during such engineless state @link AIStatefulTask::yield yield()@endlink is called <em>without</em> passing an engine,
 * then the task will be added to the \ref gAuxiliaryThreadEngine.
 *
 * Sinds normally \htmlonly&dash;\endhtmlonly for some instance of AIEngine \htmlonly&dash;\endhtmlonly
 * it is the <em>same</em> thread that calls the AIEngine::mainloop member function in the main loop of that thread,
 * there is a one-on-one relationship between a thread and an AIEngine object.
 *
 * Once a task is added to an engine then every time the thread of that engine returns to its main loop,
 * it processes one or more tasks in its queue until either &mdash; all tasks are finished, idle, moved to another engine
 * or aborted &mdash; or, if a maximum duration was set, until more than @link AIEngine::AIEngine(char const*, float) max_duration@endlink
 * milliseconds was spent in the \c mainloop (this applies to new tasks, not a task whose \c multiplex_impl is already called
 * \htmlonly&mdash;\endhtmlonly \c a frequent call to @link AIStatefulTask::yield yield()@endlink is your friend there).
 *
 * Note that each @link AIStatefulTask task@endlink object keeps track of three
 * engine pointers:
 * * <code>AIStatefulTask::mTargetEngine&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;// Last engine to passed target() or yield*().</code>
 * * <code>AIStatefulTask::mState.current_engine // While non-idle, the first non-null engine from the top, or gAuxiliaryThreadEngine</code>.
 * * <code>AIStatefulTask::mDefaultEngine&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;// Engine passed to run().</code>
 *
 * The first, \c mTargetEngine, is the engine that was passed to the last call of member
 * function AIStatefulTask::target (which is also called by the
 * @link group_yield AIStatefulTask::yield*()@endlink member functions that take an engine as parameter).
 * It will be \c nullptr when \c target wasn't called yet, or when \c nullptr is
 * explicitly passed as engine to one of these member functions.
 *
 * The second, \c current_engine, is the engine that the task is added to \htmlonly&dash;\endhtmlonly for as long
 * as the task needs to be run. It is \c nullptr when task didn't run at all yet or doesn't need to run anymore (e.g., when it is idle).
 * As soon as this value is changed to a different value than the engine that the task
 * is currently added to then that engine will not run that task anymore and remove it from
 * its queue; it is therefore the canonical engine that the task runs in.
 * If a task goes idle, this value is set to \c nullptr; otherwise it is set to the
 * last engine that that task did run in, which is the first non-null engine from the top.
 * If all three are \c nullptr and the task isn't idle then the task is added to \ref gAuxiliaryThreadEngine
 * (this only happens when the task doesn't have a default engine (obviously) and calls <code>yield()</code>
 * without engine when it is first \c run or woken up by a call to \c signal (see above)).
 *
 * The last, \c mDefaultEngine, is the engine that is passed to @link group_run run@endlink
 * and never changes. It might be \c nullptr (no default engine).
 */
class AIEngine
{
 private:
  struct QueueElementComp;
  class QueueElement {
    private:
      boost::intrusive_ptr<AIStatefulTask> mStatefulTask;

    public:
      QueueElement(AIStatefulTask* stateful_task) : mStatefulTask(stateful_task) { }
      friend bool operator==(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask == e2.mStatefulTask; }
      friend bool operator!=(QueueElement const& e1, QueueElement const& e2) { return e1.mStatefulTask != e2.mStatefulTask; }
      friend struct QueueElementComp;

      AIStatefulTask const& stateful_task() const { return *mStatefulTask; }
      AIStatefulTask& stateful_task() { return *mStatefulTask; }
  };
  struct QueueElementComp {
    inline bool operator()(QueueElement const& e1, QueueElement const& e2) const;
  };

  using queued_type = std::list<QueueElement>;

  struct engine_state_st
  {
    queued_type list;
    bool waiting;
    engine_state_st() : waiting(false) { }
  };

  using engine_state_type = aithreadsafe::Wrapper<engine_state_st, aithreadsafe::policy::Primitive<aithreadsafe::Condition>>;

#ifndef DOXYGEN
 public:       // Used by AIStatefulTask.
  using clock_type = AIStatefulTask::clock_type;
  using duration_type = AIStatefulTask::duration_type;
#endif

 private:
  engine_state_type mEngineState;
  char const* mName;
  duration_type mMaxDuration;
  bool mHasMaxDuration;

 public:
  /*!
   * @brief Construct an AIEngine.
   *
   * The argument \a name must be a string-literal (only the pointer to it is stored).
   * If \a max_duration is less than or equal zero (the default) then no duration is set
   * and the engine won't return from \ref mainloop until all tasks in its queue either
   * finished, are waiting (idle) or did yield to a different engine.
   *
   * @param name A human readable name for this engine. Mainly used for debug output.
   * @param max_duration The maximum duration for which new tasks are run per loop. See SetMaxDuration.
   */
  AIEngine(char const* name, float max_duration = 0.0f) : mName(name) { setMaxDuration(max_duration); }

  /*!
   * @brief Add \a stateful_task to this engine.
   *
   * The task will remain assigned to the engine until it no longer @link AIStatefulTask::active active@endlink
   * (tested after returning from @link Example::multiplex_impl multiplex_impl@endlink).
   *
   * Normally you should not call this function directly. Instead, use @link group_run AIStatefulTask::run@endlink.
   *
   * @param stateful_task The task to add.
   */
  void add(AIStatefulTask* stateful_task);

  /*!
   * @brief The main loop of the engine.
   *
   * Run all tasks that were @link add added@endlink to the engine until
   * they are all finished and/or idle, or until mMaxDuration milliseconds
   * have passed if a maximum duration was set.
   */
  void mainloop();

  /*!
   * @brief Wake up a sleeping engine.
   */
  void wake_up();

  /*!
   * @brief Flush all tasks from this engine.
   *
   * All queued tasks are removed from the engine and marked as killed.
   * This can be used when terminating a program, just prior to destructing
   * all remaining objects, to avoid that tasks do call backs and use objects
   * that are being destructed.
   */
  void flush();

  /*!
   * @brief Return a human readable name of this engine.
   *
   * This is simply the string that was passed upon construction.
   */
  char const* name() const { return mName; }

  /*!
   * @brief Set mMaxDuration in milliseconds.
   *
   * The maximum time the engine will spend in \ref mainloop calling \c multiplex on unfinished and non-idle tasks.
   * Note that if the last call to \c multiplex takes considerable time then it is possible that the time spend
   * in \c mainloop will go arbitrarily far beyond \c mMaxDuration. It is the responsibility of the user to not
   * run states (of task) that can take too long in engines that have an \c mMaxDuration set.
   */
  void setMaxDuration(float max_duration);

  /*!
   * @brief Return true if a maximum duration was set.
   *
   * Note, only engines with a set maximum duration can be used to sleep
   * on by using AIStatefulTask::yield_frame or AIStatefulTask::yield_ms.
   */
  bool hasMaxDuration() const { return mHasMaxDuration; }
};
