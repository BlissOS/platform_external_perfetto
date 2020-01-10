/*
 * Copyright (C) 2018 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "src/trace_processor/process_tracker.h"

#include "perfetto/base/logging.h"
#include "src/trace_processor/args_tracker.h"
#include "src/trace_processor/event_tracker.h"
#include "src/trace_processor/importers/ftrace/sched_event_tracker.h"
#include "test/gtest_and_gmock.h"

namespace perfetto {
namespace trace_processor {
namespace {

using ::testing::_;
using ::testing::InSequence;
using ::testing::Invoke;

class ProcessTrackerTest : public ::testing::Test {
 public:
  ProcessTrackerTest() {
    context.storage.reset(new TraceStorage());
    context.global_args_tracker.reset(new GlobalArgsTracker(&context));
    context.args_tracker.reset(new ArgsTracker(&context));
    context.process_tracker.reset(new ProcessTracker(&context));
    context.event_tracker.reset(new EventTracker(&context));
  }

 protected:
  TraceProcessorContext context;
};

TEST_F(ProcessTrackerTest, PushProcess) {
  TraceStorage storage;
  context.process_tracker->SetProcessMetadata(1, base::nullopt, "test");
  auto pair_it = context.process_tracker->UpidsForPidForTesting(1);
  ASSERT_EQ(pair_it.first->second, 1u);
}

TEST_F(ProcessTrackerTest, GetOrCreateNewProcess) {
  TraceStorage storage;
  auto upid = context.process_tracker->GetOrCreateProcess(123);
  ASSERT_EQ(context.process_tracker->GetOrCreateProcess(123), upid);
}

TEST_F(ProcessTrackerTest, StartNewProcess) {
  TraceStorage storage;
  auto upid = context.process_tracker->StartNewProcess(1000, 0, 123, 0);
  ASSERT_EQ(context.process_tracker->GetOrCreateProcess(123), upid);
  ASSERT_EQ(context.storage->process_table().start_ts()[upid], 1000);
}

TEST_F(ProcessTrackerTest, PushTwoProcessEntries_SamePidAndName) {
  context.process_tracker->SetProcessMetadata(1, base::nullopt, "test");
  context.process_tracker->SetProcessMetadata(1, base::nullopt, "test");
  auto pair_it = context.process_tracker->UpidsForPidForTesting(1);
  ASSERT_EQ(pair_it.first->second, 1u);
  ASSERT_EQ(++pair_it.first, pair_it.second);
}

TEST_F(ProcessTrackerTest, PushTwoProcessEntries_DifferentPid) {
  context.process_tracker->SetProcessMetadata(1, base::nullopt, "test");
  context.process_tracker->SetProcessMetadata(3, base::nullopt, "test");
  auto pair_it = context.process_tracker->UpidsForPidForTesting(1);
  ASSERT_EQ(pair_it.first->second, 1u);
  auto second_pair_it = context.process_tracker->UpidsForPidForTesting(3);
  ASSERT_EQ(second_pair_it.first->second, 2u);
}

TEST_F(ProcessTrackerTest, AddProcessEntry_CorrectName) {
  context.process_tracker->SetProcessMetadata(1, base::nullopt, "test");
  auto name =
      context.storage->GetString(context.storage->process_table().name()[1]);
  ASSERT_EQ(name, "test");
}

TEST_F(ProcessTrackerTest, UpdateThreadMatch) {
  uint32_t cpu = 3;
  int64_t timestamp = 100;
  int64_t prev_state = 32;
  static const char kCommProc1[] = "process1";
  static const char kCommProc2[] = "process2";
  int32_t prio = 1024;
  SchedEventTracker* sched_tracker = SchedEventTracker::GetOrCreate(&context);

  sched_tracker->PushSchedSwitch(cpu, timestamp, /*tid=*/1, kCommProc2, prio,
                                 prev_state,
                                 /*tid=*/4, kCommProc1, prio);
  sched_tracker->PushSchedSwitch(cpu, timestamp + 1, /*tid=*/4, kCommProc1,
                                 prio, prev_state,
                                 /*tid=*/1, kCommProc2, prio);

  context.process_tracker->SetProcessMetadata(2, base::nullopt, "test");
  context.process_tracker->UpdateThread(4, 2);

  ASSERT_EQ(context.storage->thread_table().tid()[1], 4u);
  ASSERT_EQ(context.storage->thread_table().upid()[1].value(), 1u);
  ASSERT_EQ(context.storage->process_table().pid()[1], 2u);
  ASSERT_EQ(context.storage->process_table().start_ts()[1], base::nullopt);
}

TEST_F(ProcessTrackerTest, UpdateThreadCreate) {
  context.process_tracker->UpdateThread(12, 2);

  // We expect 3 threads: Invalid thread, main thread for pid, tid 12.
  ASSERT_EQ(context.storage->thread_table().row_count(), 3u);

  auto tid_it = context.process_tracker->UtidsForTidForTesting(12);
  ASSERT_NE(tid_it.first, tid_it.second);
  ASSERT_EQ(context.storage->thread_table().upid()[1].value(), 1u);
  auto pid_it = context.process_tracker->UpidsForPidForTesting(2);
  ASSERT_NE(pid_it.first, pid_it.second);
  ASSERT_EQ(context.storage->process_table().row_count(), 2u);
}

}  // namespace
}  // namespace trace_processor
}  // namespace perfetto
