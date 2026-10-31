/**
 * @file priority_scheduler_c_test.cpp
 * @brief Deterministic GoogleTest suite for ISO C23 priority_scheduler.
 * Traceability: SCHED-REQ-001 through SCHED-REQ-010, SCHED-REQ-013 through
 *               SCHED-REQ-015.
 */

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

extern "C" {
#include <systems/priority_scheduler.h>
}

#include "systems_test/allocation_counter.hpp"

namespace {

constexpr uint64_t kMsToNs = 1'000'000ULL;

void noop_task_callback(void* const user_data, const uint64_t now_ns) {
  static_cast<void>(user_data);
  static_cast<void>(now_ns);
}

struct DispatchLog {
  std::array<uint32_t, 256> task_ids{};
  std::array<uint64_t, 256> timestamps_ns{};
  size_t count{0U};
};

struct LoggedTaskContext {
  DispatchLog* log{nullptr};
  uint32_t task_id{0U};
};

void record_dispatch_callback(void* const user_data, const uint64_t now_ns) {
  auto* const ctx = static_cast<LoggedTaskContext*>(user_data);
  if (ctx != nullptr && ctx->log != nullptr &&
      ctx->log->count < ctx->log->task_ids.size()) {
    const size_t idx = ctx->log->count++;
    ctx->log->task_ids[idx] = ctx->task_id;
    ctx->log->timestamps_ns[idx] = now_ns;
  }
}

// Static .bss instance for verifying .bss placement (SCHED-REQ-010).
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
struct priority_scheduler g_bss_scheduler = {};

// ============================================================================
// Tier 1: Binary Min-Heap Priority Queue from Scratch (Follow-up 6)
//         (SCHED-REQ-001, SCHED-REQ-003, SCHED-REQ-015)
// ============================================================================

TEST(PriorityQueueCTest, PushPeekPopMaintainsMinHeapOrder) {
  RecordProperty("requirements", "SCHED-REQ-001,SCHED-REQ-015");

  struct priority_queue pq = {};
  ASSERT_EQ(priority_queue_init(&pq), PRIORITY_SCHEDULER_OK);
  EXPECT_TRUE(priority_queue_is_empty(&pq));
  EXPECT_FALSE(priority_queue_is_full(&pq));
  EXPECT_EQ(priority_queue_size(&pq), 0U);

  const std::array<uint64_t, 6> deadlines = {1000ULL, 10ULL,  500ULL,
                                             100ULL,  250ULL, 50ULL};
  for (size_t i = 0U; i < deadlines.size(); ++i) {
    struct scheduled_task task = {};
    task.task_id = static_cast<uint32_t>(i + 1U);
    task.priority = 10U;
    task.period_ns = 10ULL;
    task.next_deadline_ns = deadlines[i];
    task.sequence_id = i;
    task.callback = noop_task_callback;
    ASSERT_EQ(priority_queue_push(&pq, &task), PRIORITY_SCHEDULER_OK);
  }

  EXPECT_EQ(priority_queue_size(&pq), 6U);
  EXPECT_FALSE(priority_queue_is_empty(&pq));

  struct scheduled_task peeked = {};
  ASSERT_EQ(priority_queue_peek(&pq, &peeked), PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(peeked.next_deadline_ns, 10ULL);
  EXPECT_EQ(peeked.task_id, 2U);
  EXPECT_EQ(priority_queue_size(&pq), 6U);

  const std::array<uint64_t, 6> expected_deadlines = {10ULL,  50ULL,  100ULL,
                                                      250ULL, 500ULL, 1000ULL};
  for (const uint64_t expected : expected_deadlines) {
    struct scheduled_task popped = {};
    ASSERT_EQ(priority_queue_pop(&pq, &popped), PRIORITY_SCHEDULER_OK);
    EXPECT_EQ(popped.next_deadline_ns, expected);
  }

  EXPECT_TRUE(priority_queue_is_empty(&pq));
  EXPECT_EQ(priority_queue_size(&pq), 0U);
  EXPECT_EQ(priority_queue_peek(&pq, &peeked), PRIORITY_SCHEDULER_ERR_EMPTY);
  EXPECT_EQ(priority_queue_pop(&pq, &peeked), PRIORITY_SCHEDULER_ERR_EMPTY);

  // Verify null helpers and full-capacity boundary (64 tasks).
  EXPECT_EQ(priority_queue_size(nullptr), 0U);
  EXPECT_TRUE(priority_queue_is_empty(nullptr));
  EXPECT_FALSE(priority_queue_is_full(nullptr));
  for (size_t i = 0U; i < PRIORITY_SCHEDULER_MAX_TASKS; ++i) {
    struct scheduled_task task = {};
    task.task_id = static_cast<uint32_t>(i + 1U);
    task.next_deadline_ns =
        static_cast<uint64_t>(PRIORITY_SCHEDULER_MAX_TASKS - i);
    task.period_ns = 10ULL;
    task.callback = noop_task_callback;
    ASSERT_EQ(priority_queue_push(&pq, &task), PRIORITY_SCHEDULER_OK);
  }
  EXPECT_TRUE(priority_queue_is_full(&pq));
  struct scheduled_task extra = {};
  extra.next_deadline_ns = 1ULL;
  extra.task_id = 99U;
  EXPECT_EQ(priority_queue_push(&pq, &extra),
            PRIORITY_SCHEDULER_ERR_QUEUE_FULL);
}

TEST(PriorityQueueCTest, TieBreakingByPrioritySequenceAndTaskId) {
  RecordProperty("requirements", "SCHED-REQ-001,SCHED-REQ-003");

  struct scheduled_task early = {};
  early.next_deadline_ns = 100ULL;
  early.priority = 50U;
  early.sequence_id = 10ULL;
  early.task_id = 10U;

  struct scheduled_task late = early;
  late.next_deadline_ns = 200ULL;
  late.priority = 0U;
  EXPECT_TRUE(scheduled_task_comes_before(&early, &late));
  EXPECT_FALSE(scheduled_task_comes_before(&late, &early));

  // Same deadline: smaller numeric priority (0 = highest urgency) wins.
  struct scheduled_task high_prio = early;
  high_prio.priority = 0U;
  high_prio.sequence_id = 99ULL;
  EXPECT_TRUE(scheduled_task_comes_before(&high_prio, &early));
  EXPECT_FALSE(scheduled_task_comes_before(&early, &high_prio));

  // Same deadline and same priority: smaller sequence_id wins.
  struct scheduled_task first_seq = early;
  first_seq.sequence_id = 1ULL;
  first_seq.task_id = 99U;
  EXPECT_TRUE(scheduled_task_comes_before(&first_seq, &early));
  EXPECT_FALSE(scheduled_task_comes_before(&early, &first_seq));

  // Same deadline, priority, and sequence_id: smaller task_id wins.
  struct scheduled_task smaller_id = early;
  smaller_id.task_id = 2U;
  EXPECT_TRUE(scheduled_task_comes_before(&smaller_id, &early));
  EXPECT_FALSE(scheduled_task_comes_before(&early, &smaller_id));

  // Irreflexivity and null-safety of strict weak ordering.
  EXPECT_FALSE(scheduled_task_comes_before(&early, &early));
  EXPECT_FALSE(scheduled_task_comes_before(nullptr, &early));
  EXPECT_FALSE(scheduled_task_comes_before(&early, nullptr));
  EXPECT_FALSE(scheduled_task_comes_before(nullptr, nullptr));
}

// ============================================================================
// Tier 2: Multi-Frequency Periodic Scheduler & Tie-Breaking
//         (SCHED-REQ-002, SCHED-REQ-003, SCHED-REQ-005, SCHED-REQ-006,
//          SCHED-REQ-007)
// ============================================================================

TEST(PrioritySchedulerCTest, MotorSensorDisplayMultiFrequencySimulation) {
  RecordProperty("requirements", "SCHED-REQ-002,SCHED-REQ-003");
  RecordProperty("requirements", "SCHED-REQ-007");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  DispatchLog log{};
  LoggedTaskContext motor_ctx{.log = &log, .task_id = 1U};
  LoggedTaskContext sensor_ctx{.log = &log, .task_id = 2U};
  LoggedTaskContext display_ctx{.log = &log, .task_id = 3U};

  // Register in reverse importance order to verify heap sorting:
  // Task C (Display): 1000 ms period, priority 50
  // Task B (Sensor):   100 ms period, priority 10
  // Task A (Motor):     10 ms period, priority 0 (highest urgency)
  const struct priority_task_config display_cfg = {
      .task_id = 3U,
      .priority = 50U,
      .period_ns = 1000ULL * kMsToNs,
      .first_deadline_ns = 1000ULL * kMsToNs,
      .callback = record_dispatch_callback,
      .user_data = &display_ctx,
  };
  const struct priority_task_config sensor_cfg = {
      .task_id = 2U,
      .priority = 10U,
      .period_ns = 100ULL * kMsToNs,
      .first_deadline_ns = 100ULL * kMsToNs,
      .callback = record_dispatch_callback,
      .user_data = &sensor_ctx,
  };
  const struct priority_task_config motor_cfg = {
      .task_id = 1U,
      .priority = PRIORITY_HIGHEST,
      .period_ns = 10ULL * kMsToNs,
      .first_deadline_ns = 10ULL * kMsToNs,
      .callback = record_dispatch_callback,
      .user_data = &motor_ctx,
  };

  ASSERT_EQ(priority_scheduler_schedule(&sched, &display_cfg),
            PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(priority_scheduler_schedule(&sched, &sensor_cfg),
            PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(priority_scheduler_schedule(&sched, &motor_cfg),
            PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(priority_scheduler_size(&sched), 3U);

  // Step synthetically from 10 ms to 1000 ms in 10 ms increments (zero
  // wall-clock dependency).
  size_t total_ran = 0U;
  for (uint64_t tick_ms = 10ULL; tick_ms <= 1000ULL; tick_ms += 10ULL) {
    size_t ran_this_tick = 0U;
    size_t missed_this_tick = 0U;
    ASSERT_EQ(
        priority_scheduler_run_until_idle(&sched, tick_ms * kMsToNs, 8U,
                                          &ran_this_tick, &missed_this_tick),
        PRIORITY_SCHEDULER_OK);
    EXPECT_EQ(missed_this_tick, 0U);
    total_ran += ran_this_tick;
  }

  // Over 1000 ms: Motor runs 100 times, Sensor runs 10 times, Display runs 1
  // time = 111 total executions.
  EXPECT_EQ(total_ran, 111U);
  ASSERT_EQ(log.count, 111U);

  // At t = 1000 ms (the final 3 entries in the log), all three tasks shared the
  // 1000 ms deadline; Motor (1) must run before Sensor (2) before Display (3).
  EXPECT_EQ(log.task_ids[108], 1U);
  EXPECT_EQ(log.task_ids[109], 2U);
  EXPECT_EQ(log.task_ids[110], 3U);
  EXPECT_EQ(log.timestamps_ns[108], 1000ULL * kMsToNs);
  EXPECT_EQ(log.timestamps_ns[109], 1000ULL * kMsToNs);
  EXPECT_EQ(log.timestamps_ns[110], 1000ULL * kMsToNs);

  priority_scheduler_destroy(&sched);
}

TEST(PrioritySchedulerCTest, SameDeadlineAndSamePriorityFifoTieBreaking) {
  RecordProperty("requirements", "SCHED-REQ-003");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  DispatchLog log{};
  LoggedTaskContext ctx_a{.log = &log, .task_id = 30U};
  LoggedTaskContext ctx_b{.log = &log, .task_id = 10U};
  LoggedTaskContext ctx_c{.log = &log, .task_id = 20U};

  // Register tasks 30, 10, 20 in that order with identical deadline and
  // priority. Registration sequence_id (0, 1, 2) must win over task_id.
  const std::array<LoggedTaskContext*, 3> contexts = {&ctx_a, &ctx_b, &ctx_c};
  for (auto* const ctx : contexts) {
    const struct priority_task_config cfg = {
        .task_id = ctx->task_id,
        .priority = 5U,
        .period_ns = 100ULL * kMsToNs,
        .first_deadline_ns = 50ULL * kMsToNs,
        .callback = record_dispatch_callback,
        .user_data = ctx,
    };
    ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);
  }

  size_t dispatched = 0U;
  ASSERT_EQ(priority_scheduler_run_until_idle(&sched, 50ULL * kMsToNs, 10U,
                                              &dispatched, nullptr),
            PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(dispatched, 3U);
  EXPECT_EQ(log.task_ids[0], 30U);
  EXPECT_EQ(log.task_ids[1], 10U);
  EXPECT_EQ(log.task_ids[2], 20U);

  // Register a 4th task after stepping to verify `next_sequence_id` stays
  // monotonically increasing across dispatches and preserves FIFO tie-breaking.
  LoggedTaskContext ctx_d{.log = &log, .task_id = 5U};
  const struct priority_task_config cfg_d = {
      .task_id = ctx_d.task_id,
      .priority = 5U,
      .period_ns = 100ULL * kMsToNs,
      .first_deadline_ns = 150ULL * kMsToNs,
      .callback = record_dispatch_callback,
      .user_data = &ctx_d,
  };
  ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg_d), PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(priority_scheduler_run_until_idle(&sched, 150ULL * kMsToNs, 10U,
                                              &dispatched, nullptr),
            PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(dispatched, 4U);
  EXPECT_EQ(log.task_ids[3], 30U);
  EXPECT_EQ(log.task_ids[4], 10U);
  EXPECT_EQ(log.task_ids[5], 20U);
  EXPECT_EQ(log.task_ids[6], 5U);

  priority_scheduler_destroy(&sched);
}

TEST(PrioritySchedulerCTest, FutureTaskNotReadyAndPeekNextDeadline) {
  RecordProperty("requirements", "SCHED-REQ-005");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  const struct priority_task_config cfg = {
      .task_id = 7U,
      .priority = 1U,
      .period_ns = 20ULL * kMsToNs,
      .first_deadline_ns = 50ULL * kMsToNs,
      .callback = noop_task_callback,
      .user_data = nullptr,
  };
  ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);

  uint64_t next_deadline = 0ULL;
  uint32_t next_id = 0U;
  ASSERT_EQ(
      priority_scheduler_peek_next_deadline(&sched, &next_deadline, &next_id),
      PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(next_deadline, 50ULL * kMsToNs);
  EXPECT_EQ(next_id, 7U);

  struct priority_dispatch_result res = {};
  EXPECT_EQ(priority_scheduler_step(&sched, 49ULL * kMsToNs, &res),
            PRIORITY_SCHEDULER_ERR_NO_TASK_READY);
  EXPECT_EQ(res.task_id, 0U);
  EXPECT_EQ(res.next_deadline_ns, 50ULL * kMsToNs);

  priority_scheduler_destroy(&sched);
}

TEST(PrioritySchedulerCTest, CancelTaskRemovesFromHeapAndPreservesOrdering) {
  RecordProperty("requirements", "SCHED-REQ-006");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  for (uint32_t id = 1U; id <= 5U; ++id) {
    const struct priority_task_config cfg = {
        .task_id = id,
        .priority = 1U,
        .period_ns = 100ULL * kMsToNs,
        .first_deadline_ns = static_cast<uint64_t>(id) * 10ULL * kMsToNs,
        .callback = noop_task_callback,
        .user_data = nullptr,
    };
    ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);
  }

  ASSERT_EQ(priority_scheduler_cancel(&sched, 2U), PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(priority_scheduler_cancel(&sched, 2U),
            PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND);
  EXPECT_EQ(priority_scheduler_size(&sched), 4U);

  const std::array<uint32_t, 4> expected_ids = {1U, 3U, 4U, 5U};
  for (const uint32_t expected_id : expected_ids) {
    struct priority_dispatch_result res = {};
    ASSERT_EQ(priority_scheduler_step(&sched, 50ULL * kMsToNs, &res),
              PRIORITY_SCHEDULER_OK);
    EXPECT_EQ(res.task_id, expected_id);
  }

  priority_scheduler_destroy(&sched);
}

TEST(PrioritySchedulerCTest, CancelHandlesLastLeafSiftUpAndDrainToEmpty) {
  RecordProperty("requirements", "SCHED-REQ-006");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  // Build a 6-node heap where left subtree has deadlines (50, 60, 70) and right
  // subtree has deadlines (15, 20):
  //   idx 0: id 1 (10 ms)
  //   idx 1: id 2 (50 ms)        idx 2: id 3 (15 ms)
  //   idx 3: id 4 (60 ms)        idx 4: id 5 (70 ms)    idx 5: id 6 (20 ms)
  const std::array<uint64_t, 6> deadlines_ms = {10ULL, 50ULL, 15ULL,
                                                60ULL, 70ULL, 20ULL};
  for (size_t i = 0U; i < deadlines_ms.size(); ++i) {
    const struct priority_task_config cfg = {
        .task_id = static_cast<uint32_t>(i + 1U),
        .priority = 1U,
        .period_ns = 1000ULL * kMsToNs,
        .first_deadline_ns = deadlines_ms[i] * kMsToNs,
        .callback = noop_task_callback,
        .user_data = nullptr,
    };
    ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);
  }

  // Cancelling id 4 (idx 3, 60 ms) replaces it with last leaf id 6 (20 ms),
  // which is < parent id 2 (50 ms) and MUST sift UP above id 2.
  ASSERT_EQ(priority_scheduler_cancel(&sched, 4U), PRIORITY_SCHEDULER_OK);

  // Cancelling current last leaf id 5 (70 ms, idx == size - 1) must not access
  // out-of-bounds indices.
  ASSERT_EQ(priority_scheduler_cancel(&sched, 5U), PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(priority_scheduler_size(&sched), 4U);

  const std::array<uint32_t, 4> expected_order = {1U, 3U, 6U, 2U};
  for (const uint32_t expected_id : expected_order) {
    struct priority_dispatch_result res = {};
    ASSERT_EQ(priority_scheduler_step(&sched, 100ULL * kMsToNs, &res),
              PRIORITY_SCHEDULER_OK);
    EXPECT_EQ(res.task_id, expected_id);
  }

  // Cancel all remaining tasks down to empty.
  for (const uint32_t id : expected_order) {
    ASSERT_EQ(priority_scheduler_cancel(&sched, id), PRIORITY_SCHEDULER_OK);
  }
  EXPECT_EQ(priority_scheduler_size(&sched), 0U);
  EXPECT_EQ(priority_scheduler_step(&sched, 100ULL * kMsToNs, nullptr),
            PRIORITY_SCHEDULER_ERR_EMPTY);

  priority_scheduler_destroy(&sched);
}

TEST(PrioritySchedulerCTest, RunUntilIdleRespectsZeroAndBoundedMaxDispatches) {
  RecordProperty("requirements", "SCHED-REQ-007,SCHED-REQ-014");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  for (uint32_t id = 1U; id <= 3U; ++id) {
    const struct priority_task_config cfg = {
        .task_id = id,
        .priority = static_cast<uint8_t>(id),
        .period_ns = 100ULL * kMsToNs,
        .first_deadline_ns = 10ULL * kMsToNs,
        .callback = noop_task_callback,
        .user_data = nullptr,
    };
    ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);
  }

  // `max_dispatches == 0` must zero output counts and execute zero tasks.
  size_t dispatched = 99U;
  size_t missed = 77U;
  ASSERT_EQ(priority_scheduler_run_until_idle(&sched, 20ULL * kMsToNs, 0U,
                                              &dispatched, &missed),
            PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(dispatched, 0U);
  EXPECT_EQ(missed, 0U);

  // `max_dispatches == 2` must stop after 2 dispatches even though 3 are due.
  ASSERT_EQ(priority_scheduler_run_until_idle(&sched, 20ULL * kMsToNs, 2U,
                                              &dispatched, &missed),
            PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(dispatched, 2U);
  EXPECT_EQ(missed, 2U);

  // Timestamp overflow inside `run_until_idle` propagates `ERR_TIME_OVERFLOW`.
  EXPECT_EQ(priority_scheduler_run_until_idle(
                &sched, std::numeric_limits<uint64_t>::max(), 4U, &dispatched,
                &missed),
            PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW);

  priority_scheduler_destroy(&sched);
}

// ============================================================================
// Tier 3: Missed Deadline Handling (Follow-up 2) (SCHED-REQ-004)
// ============================================================================

TEST(PrioritySchedulerCTest, MissedDeadlinePreventCatchUpBurst) {
  RecordProperty("requirements", "SCHED-REQ-004");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  const struct priority_task_config cfg = {
      .task_id = 1U,
      .priority = PRIORITY_HIGHEST,
      .period_ns = 10ULL * kMsToNs,
      .first_deadline_ns = 10ULL * kMsToNs,
      .callback = noop_task_callback,
      .user_data = nullptr,
  };
  ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);

  // Dispatch at t = 55 ms (45 ms late): must execute once, flag
  // missed_deadline, and advance next_deadline_ns to 55 ms + 10 ms = 65 ms (no
  // catch-up burst).
  struct priority_dispatch_result res = {};
  ASSERT_EQ(priority_scheduler_step(&sched, 55ULL * kMsToNs, &res),
            PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(res.task_id, 1U);
  EXPECT_TRUE(res.missed_deadline);
  EXPECT_EQ(res.scheduled_deadline_ns, 10ULL * kMsToNs);
  EXPECT_EQ(res.dispatched_at_ns, 55ULL * kMsToNs);
  EXPECT_EQ(res.next_deadline_ns, 65ULL * kMsToNs);
  EXPECT_EQ(res.executions, 1ULL);
  EXPECT_EQ(res.deadline_misses, 1ULL);

  // Stepping again at t = 55 ms must NOT burst-fire stale periods (20, 30, 40,
  // 50 ms).
  EXPECT_EQ(priority_scheduler_step(&sched, 55ULL * kMsToNs, &res),
            PRIORITY_SCHEDULER_ERR_NO_TASK_READY);

  // Stepping on time at t = 65 ms succeeds with missed_deadline == false.
  ASSERT_EQ(priority_scheduler_step(&sched, 65ULL * kMsToNs, &res),
            PRIORITY_SCHEDULER_OK);
  EXPECT_FALSE(res.missed_deadline);
  EXPECT_EQ(res.scheduled_deadline_ns, 65ULL * kMsToNs);
  EXPECT_EQ(res.next_deadline_ns, 75ULL * kMsToNs);
  EXPECT_EQ(res.executions, 2ULL);
  EXPECT_EQ(res.deadline_misses, 1ULL);

  priority_scheduler_destroy(&sched);
}

// ============================================================================
// Tier 4: Minimized Lock Scope — Unlocked Callback Execution (Follow-up 4)
//         (SCHED-REQ-008)
// ============================================================================

struct ReentrantCallbackContext {
  struct priority_scheduler* sched{nullptr};
  bool schedule_succeeded{false};
  bool peek_succeeded{false};
  size_t observed_size{0U};
};

void reentrant_callback(void* const user_data, const uint64_t now_ns) {
  auto* const ctx = static_cast<ReentrantCallbackContext*>(user_data);
  if (ctx == nullptr || ctx->sched == nullptr) {
    return;
  }
  // If sched->mutex were still held during callback execution, these calls
  // would deadlock on the non-recursive pthread_mutex_t.
  const struct priority_task_config child_cfg = {
      .task_id = 99U,
      .priority = 2U,
      .period_ns = 50ULL * kMsToNs,
      .first_deadline_ns = now_ns + (50ULL * kMsToNs),
      .callback = noop_task_callback,
      .user_data = nullptr,
  };
  ctx->schedule_succeeded =
      (priority_scheduler_schedule(ctx->sched, &child_cfg) ==
       PRIORITY_SCHEDULER_OK);
  uint64_t next_dl = 0ULL;
  ctx->peek_succeeded =
      (priority_scheduler_peek_next_deadline(ctx->sched, &next_dl, nullptr) ==
       PRIORITY_SCHEDULER_OK);
  ctx->observed_size = priority_scheduler_size(ctx->sched);
}

TEST(PrioritySchedulerCTest, CallbackRunsUnlockedAndReentrant) {
  RecordProperty("requirements", "SCHED-REQ-008");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  ReentrantCallbackContext ctx{.sched = &sched};
  const struct priority_task_config cfg = {
      .task_id = 1U,
      .priority = PRIORITY_HIGHEST,
      .period_ns = 100ULL * kMsToNs,
      .first_deadline_ns = 10ULL * kMsToNs,
      .callback = reentrant_callback,
      .user_data = &ctx,
  };
  ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);

  struct priority_dispatch_result res = {};
  ASSERT_EQ(priority_scheduler_step(&sched, 10ULL * kMsToNs, &res),
            PRIORITY_SCHEDULER_OK);
  EXPECT_TRUE(ctx.schedule_succeeded);
  EXPECT_TRUE(ctx.peek_succeeded);
  EXPECT_EQ(ctx.observed_size, 2U);

  priority_scheduler_destroy(&sched);
}

// ============================================================================
// Tier 5: Boundary & Validation Error Precedence
//         (SCHED-REQ-013, SCHED-REQ-014)
// ============================================================================

TEST(PrioritySchedulerCTest, ValidationPrecedenceCapacityDuplicateAndOverflow) {
  RecordProperty("requirements", "SCHED-REQ-013,SCHED-REQ-014");

  // 1. Null pointer checks
  EXPECT_EQ(priority_queue_init(nullptr), PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_queue_push(nullptr, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_queue_peek(nullptr, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_queue_pop(nullptr, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_scheduler_init(nullptr), PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_scheduler_schedule(nullptr, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_scheduler_cancel(nullptr, 1U),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_scheduler_peek_next_deadline(nullptr, nullptr, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(priority_scheduler_step(nullptr, 0ULL, nullptr),
            PRIORITY_SCHEDULER_ERR_NULL_PTR);
  EXPECT_EQ(
      priority_scheduler_run_until_idle(nullptr, 0ULL, 1U, nullptr, nullptr),
      PRIORITY_SCHEDULER_ERR_NULL_PTR);

  // 2. Uninitialized scheduler rejection takes precedence over invalid config
  struct priority_scheduler uninit_sched = {};
  struct priority_task_config bad_cfg = {};
  uint64_t dummy_dl = 0ULL;
  size_t dummy_dispatched = 0U;
  EXPECT_EQ(priority_scheduler_size(nullptr), 0U);
  EXPECT_EQ(priority_scheduler_size(&uninit_sched), 0U);
  EXPECT_EQ(priority_scheduler_schedule(&uninit_sched, &bad_cfg),
            PRIORITY_SCHEDULER_ERR_UNINITIALIZED);
  EXPECT_EQ(priority_scheduler_cancel(&uninit_sched, 0U),
            PRIORITY_SCHEDULER_ERR_UNINITIALIZED);
  EXPECT_EQ(
      priority_scheduler_peek_next_deadline(&uninit_sched, &dummy_dl, nullptr),
      PRIORITY_SCHEDULER_ERR_UNINITIALIZED);
  EXPECT_EQ(priority_scheduler_step(&uninit_sched, 0ULL, nullptr),
            PRIORITY_SCHEDULER_ERR_UNINITIALIZED);
  EXPECT_EQ(priority_scheduler_run_until_idle(&uninit_sched, 0ULL, 1U,
                                              &dummy_dispatched, nullptr),
            PRIORITY_SCHEDULER_ERR_UNINITIALIZED);

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(priority_scheduler_peek_next_deadline(&sched, &dummy_dl, nullptr),
            PRIORITY_SCHEDULER_ERR_EMPTY);
  EXPECT_EQ(priority_scheduler_cancel(&sched, 0U),
            PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID);

  // 3. Config validation precedence: INVALID_TASK_ID -> INVALID_PERIOD ->
  //    NULL_CALLBACK
  EXPECT_EQ(priority_scheduler_schedule(&sched, &bad_cfg),
            PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID);
  bad_cfg.task_id = 1U;
  EXPECT_EQ(priority_scheduler_schedule(&sched, &bad_cfg),
            PRIORITY_SCHEDULER_ERR_INVALID_PERIOD);
  bad_cfg.period_ns = 10ULL;
  EXPECT_EQ(priority_scheduler_schedule(&sched, &bad_cfg),
            PRIORITY_SCHEDULER_ERR_NULL_CALLBACK);

  // 4. Fill queue to PRIORITY_SCHEDULER_MAX_TASKS (64) and verify duplicate ID
  //    and QUEUE_FULL
  for (uint32_t id = 1U; id <= PRIORITY_SCHEDULER_MAX_TASKS; ++id) {
    const struct priority_task_config cfg = {
        .task_id = id,
        .priority = 1U,
        .period_ns = 100ULL,
        .first_deadline_ns = 100ULL,
        .callback = noop_task_callback,
        .user_data = nullptr,
    };
    ASSERT_EQ(priority_scheduler_schedule(&sched, &cfg), PRIORITY_SCHEDULER_OK);
  }
  const struct priority_task_config dup_cfg = {
      .task_id = 1U,
      .priority = 1U,
      .period_ns = 100ULL,
      .first_deadline_ns = 100ULL,
      .callback = noop_task_callback,
      .user_data = nullptr,
  };
  EXPECT_EQ(priority_scheduler_schedule(&sched, &dup_cfg),
            PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID);

  const struct priority_task_config overflow_cap_cfg = {
      .task_id = 65U,
      .priority = 1U,
      .period_ns = 100ULL,
      .first_deadline_ns = 100ULL,
      .callback = noop_task_callback,
      .user_data = nullptr,
  };
  EXPECT_EQ(priority_scheduler_schedule(&sched, &overflow_cap_cfg),
            PRIORITY_SCHEDULER_ERR_QUEUE_FULL);

  // 5. Timestamp overflow check (now_ns + period_ns > UINT64_MAX)
  EXPECT_EQ(priority_scheduler_step(
                &sched, std::numeric_limits<uint64_t>::max(), nullptr),
            PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW);

  // 6. Error strings are non-null for all enumerators and unknown values
  EXPECT_STREQ(priority_scheduler_error_to_string(PRIORITY_SCHEDULER_OK), "ok");
  const std::array<enum priority_scheduler_error, 13> all_errors = {
      PRIORITY_SCHEDULER_OK,
      PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED,
      PRIORITY_SCHEDULER_ERR_NULL_PTR,
      PRIORITY_SCHEDULER_ERR_UNINITIALIZED,
      PRIORITY_SCHEDULER_ERR_INVALID_TASK_ID,
      PRIORITY_SCHEDULER_ERR_INVALID_PERIOD,
      PRIORITY_SCHEDULER_ERR_NULL_CALLBACK,
      PRIORITY_SCHEDULER_ERR_QUEUE_FULL,
      PRIORITY_SCHEDULER_ERR_DUPLICATE_TASK_ID,
      PRIORITY_SCHEDULER_ERR_TASK_NOT_FOUND,
      PRIORITY_SCHEDULER_ERR_EMPTY,
      PRIORITY_SCHEDULER_ERR_NO_TASK_READY,
      PRIORITY_SCHEDULER_ERR_TIME_OVERFLOW,
  };
  for (const auto err : all_errors) {
    EXPECT_NE(priority_scheduler_error_to_string(err), nullptr);
  }
  EXPECT_NE(priority_scheduler_error_to_string(
                static_cast<enum priority_scheduler_error>(999)),
            nullptr);

  priority_scheduler_destroy(&sched);
  priority_scheduler_destroy(&sched);
  priority_scheduler_destroy(nullptr);
}

// ============================================================================
// Tier 6: Resource / Zero Heap Allocation across .bss, Stack, and Startup Heap
//         (SCHED-REQ-010)
// ============================================================================

TEST(PrioritySchedulerCTest, ZeroHeapAllocationAcrossBssStackAndStartupArena) {
  RecordProperty("requirements", "SCHED-REQ-010");

  struct priority_scheduler stack_sched = {};
  auto startup_heap_sched = std::make_unique<struct priority_scheduler>();

  ASSERT_EQ(priority_scheduler_init(&g_bss_scheduler), PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(priority_scheduler_init(&stack_sched), PRIORITY_SCHEDULER_OK);
  ASSERT_EQ(priority_scheduler_init(startup_heap_sched.get()),
            PRIORITY_SCHEDULER_OK);

  enum priority_scheduler_error bss_res =
      PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED;
  enum priority_scheduler_error stack_res =
      PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED;
  enum priority_scheduler_error heap_res =
      PRIORITY_SCHEDULER_ERR_NOT_IMPLEMENTED;
  std::size_t allocations = 0U;

  {
    const systems_test::AllocationScope scope;
    const struct priority_task_config cfg = {
        .task_id = 1U,
        .priority = PRIORITY_HIGHEST,
        .period_ns = 10ULL * kMsToNs,
        .first_deadline_ns = 10ULL * kMsToNs,
        .callback = noop_task_callback,
        .user_data = nullptr,
    };
    if (priority_scheduler_schedule(&g_bss_scheduler, &cfg) ==
        PRIORITY_SCHEDULER_OK) {
      bss_res =
          priority_scheduler_step(&g_bss_scheduler, 10ULL * kMsToNs, nullptr);
    }
    if (priority_scheduler_schedule(&stack_sched, &cfg) ==
        PRIORITY_SCHEDULER_OK) {
      stack_res =
          priority_scheduler_step(&stack_sched, 10ULL * kMsToNs, nullptr);
    }
    if (priority_scheduler_schedule(startup_heap_sched.get(), &cfg) ==
        PRIORITY_SCHEDULER_OK) {
      heap_res = priority_scheduler_step(startup_heap_sched.get(),
                                         10ULL * kMsToNs, nullptr);
    }
    allocations = scope.allocations();
  }

  priority_scheduler_destroy(startup_heap_sched.get());
  priority_scheduler_destroy(&stack_sched);
  priority_scheduler_destroy(&g_bss_scheduler);

  EXPECT_EQ(allocations, 0U);
  EXPECT_EQ(bss_res, PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(stack_res, PRIORITY_SCHEDULER_OK);
  EXPECT_EQ(heap_res, PRIORITY_SCHEDULER_OK);
}

// ============================================================================
// Tier 7: Multi-Thread Concurrency Stress (Follow-up 3) (SCHED-REQ-009)
// ============================================================================

void atomic_counter_callback(void* const user_data, const uint64_t now_ns) {
  static_cast<void>(now_ns);
  auto* const counter = static_cast<std::atomic<uint64_t>*>(user_data);
  counter->fetch_add(1ULL, std::memory_order_relaxed);
}

TEST(PrioritySchedulerConcurrencyCTest, ConcurrentScheduleAndStep) {
  RecordProperty("requirements", "SCHED-REQ-009");

  struct priority_scheduler sched = {};
  ASSERT_EQ(priority_scheduler_init(&sched), PRIORITY_SCHEDULER_OK);

  constexpr uint32_t kProducerThreads = 4U;
  constexpr uint32_t kTasksPerProducer = 8U;
  std::atomic<uint64_t> callback_invocations{0ULL};
  std::atomic<uint32_t> scheduled_count{0U};

  // Phase 1: 4 producer threads concurrently schedule 32 distinct periodic
  // tasks.
  {
    std::vector<std::thread> producers;
    producers.reserve(kProducerThreads);
    for (uint32_t t = 0U; t < kProducerThreads; ++t) {
      producers.emplace_back(
          [t, &sched, &callback_invocations, &scheduled_count] {
            for (uint32_t i = 0U; i < kTasksPerProducer; ++i) {
              const uint32_t id = (t * kTasksPerProducer) + i + 1U;
              const struct priority_task_config cfg = {
                  .task_id = id,
                  .priority = static_cast<uint8_t>(id % 8U),
                  .period_ns = 100ULL * kMsToNs,
                  .first_deadline_ns = 10ULL * kMsToNs,
                  .callback = atomic_counter_callback,
                  .user_data = &callback_invocations,
              };
              if (priority_scheduler_schedule(&sched, &cfg) ==
                  PRIORITY_SCHEDULER_OK) {
                scheduled_count.fetch_add(1U, std::memory_order_relaxed);
              }
            }
          });
    }
    for (auto& p : producers) {
      p.join();
    }
  }

  ASSERT_EQ(scheduled_count.load(), kProducerThreads * kTasksPerProducer);
  EXPECT_EQ(priority_scheduler_size(&sched),
            static_cast<size_t>(kProducerThreads * kTasksPerProducer));

  // Phase 2: 4 worker threads concurrently dispatch all tasks due at t = 10 ms.
  std::atomic<uint32_t> step_successes{0U};
  {
    std::vector<std::thread> workers;
    workers.reserve(kProducerThreads);
    for (uint32_t t = 0U; t < kProducerThreads; ++t) {
      workers.emplace_back([&sched, &step_successes] {
        for (uint32_t i = 0U; i < kTasksPerProducer * 2U; ++i) {
          struct priority_dispatch_result res = {};
          const enum priority_scheduler_error err =
              priority_scheduler_step(&sched, 10ULL * kMsToNs, &res);
          if (err == PRIORITY_SCHEDULER_OK) {
            step_successes.fetch_add(1U, std::memory_order_relaxed);
          } else if (err == PRIORITY_SCHEDULER_ERR_NO_TASK_READY) {
            break;
          }
        }
      });
    }
    for (auto& w : workers) {
      w.join();
    }
  }

  EXPECT_EQ(step_successes.load(), kProducerThreads * kTasksPerProducer);
  EXPECT_EQ(callback_invocations.load(),
            static_cast<uint64_t>(kProducerThreads * kTasksPerProducer));

  priority_scheduler_destroy(&sched);
}

}  // namespace
