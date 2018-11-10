/******************************************************************************
 * Copyright 2018 The Apollo Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *****************************************************************************/

#include "cyber/croutine/croutine.h"

#include <utility>

#include "cyber/base/concurrent_object_pool.h"
#include "cyber/common/log.h"
#include "cyber/croutine/routine_context.h"
#include "cyber/event/perf_event_cache.h"

namespace apollo {
namespace cyber {
namespace croutine {

using apollo::cyber::event::PerfEventCache;
using apollo::cyber::event::SchedPerf;

thread_local CRoutine *CRoutine::current_routine_;
thread_local std::shared_ptr<RoutineContext> CRoutine::main_context_;

namespace {
std::shared_ptr<base::CCObjectPool<RoutineContext>> context_pool = nullptr;

void CRoutineEntry(void *arg) {
  CRoutine *r = static_cast<CRoutine *>(arg);
  r->Run();
  CRoutine::Yield(RoutineState::FINISHED);
}
}

CRoutine::CRoutine(const std::function<void()> &func) : func_(func) {
  if (unlikely(context_pool == nullptr)) {
    auto routine_num = 100;
    auto global_conf = common::GlobalData::Instance()->Config();
    if (global_conf.has_scheduler_conf() &&
        global_conf.scheduler_conf().has_routine_num()) {
      routine_num = global_conf.scheduler_conf().routine_num();
    }
    context_pool.reset(new base::CCObjectPool<RoutineContext>(routine_num));
  }
  context_ = context_pool->GetObject();
  CHECK_NOTNULL(context_);
  MakeContext(CRoutineEntry, this, context_.get());
  state_ = RoutineState::READY;
  updated_.test_and_set(std::memory_order_release);
}

CRoutine::~CRoutine() { context_ = nullptr; }

RoutineState CRoutine::Resume() {
  if (unlikely(force_stop_)) {
    state_ = RoutineState::FINISHED;
    return state_;
  }

  if (unlikely(state_ != RoutineState::READY)) {
    AERROR << "Invalid Routine State!";
    return state_;
  }

  current_routine_ = this;
  PerfEventCache::Instance()->AddSchedEvent(
      SchedPerf::SWAP_IN, id_, processor_id_, static_cast<int>(state_));
  SwapContext(GetMainContext(), this->GetContext());
  PerfEventCache::Instance()->AddSchedEvent(
      SchedPerf::SWAP_OUT, id_, processor_id_, static_cast<int>(state_));
  return state_;
}

void CRoutine::Routine() {
  while (true) {
    AINFO << "inner routine" << std::endl;
    usleep(1000000);
  }
}

void CRoutine::Stop() { force_stop_ = true; }

}  // namespace croutine
}  // namespace cyber
}  // namespace apollo
