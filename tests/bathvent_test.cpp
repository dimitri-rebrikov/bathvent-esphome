// =============================================================================
// bathvent_test.cpp — host tests for the fan control state machine.
//
// The controller in bathvent.cpp is hardware independent, so it can be driven
// on the PC with synthetic readings and a synthetic clock. Every scenario runs
// in microseconds, which makes timer behaviour (afterrun 5 min, periodic flush
// 30 min) testable without waiting.
//
// Build & run (from the repo root):
//   g++ -std=c++17 -Wall -Wextra -DBATHVENT_HOST_TEST -I. -o tests/bathvent_test tests/bathvent_test.cpp bathvent.cpp
//   ./tests/bathvent_test
//
// No test framework on purpose — a CHECK macro is all this needs.
// =============================================================================

#include "bathvent.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_checks = 0;
int g_failures = 0;
std::size_t g_worst_trace = 0;  // longest trace seen (guard against truncation)

void check(bool ok, const char *what, int line) {
  ++g_checks;
  if (!ok) {
    ++g_failures;
    std::printf("  FAIL (line %d): %s\n", line, what);
  }
}
#define CHECK(cond) check((cond), #cond, __LINE__)

// ---- harness ---------------------------------------------------------------

struct Harness {
  BathventConfig cfg;
  BathventInputs in;
  uint32_t now = 0;
  BathventResult r{};
};

// One tick, one second later.
BathventResult step(Harness &h) {
  h.now += 1;
  h.in.now_s = h.now;
  h.r = bathvent_tick(h.in, h.cfg);
  const std::size_t n = std::strlen(h.r.trace);
  if (n > g_worst_trace) {
    g_worst_trace = n;
  }
  return h.r;
}

void reset(Harness &h, bool light, float humidity, float voc) {
  bathvent_reset_state();
  h = Harness();
  h.in.light = light;
  h.in.humidity = humidity;
  h.in.voc = voc;
}

// Advance until `pred` holds; returns the tick at which it happened (0 = never).
template <typename Pred>
uint32_t advance_until(Harness &h, Pred pred, uint32_t limit) {
  for (uint32_t i = 0; i < limit; ++i) {
    step(h);
    if (pred(h.r)) {
      return h.now;
    }
  }
  return 0;
}

// ---- scenarios -------------------------------------------------------------

// Presence with clean air keeps the fan at LOW forever (no toggling).
void test_presence_clean_stays_low() {
  std::printf("presence + clean air -> steady LOW\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/50.0f, /*voc=*/100.0f);

  CHECK(step(h).stage == Stage::kLow);  // presence starts a flush

  const uint32_t sniff_at =
      advance_until(h, [](const BathventResult &r) {
        return r.state == FanState::kSniff;
      }, 200);
  CHECK(sniff_at != 0);
  CHECK(h.r.stage == Stage::kLow);

  // Two hours of presence must not change the stage or leave the sniff state.
  bool steady = true;
  for (int i = 0; i < 7200; ++i) {
    const BathventResult r = step(h);
    if (r.stage != Stage::kLow || r.state != FanState::kSniff) {
      steady = false;
      break;
    }
  }
  CHECK(steady);
}

// Presence with humid air holds MID — the "clicker" regression.
void test_presence_elevated_holds_mid() {
  std::printf("presence + humid air -> steady MID (no clicking)\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/80.0f, /*voc=*/100.0f);

  const uint32_t mid_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kMid;
      }, 200);
  CHECK(mid_at != 0);
  CHECK(mid_at > 30);  // only after the flush

  bool steady = true;
  for (int i = 0; i < 7200; ++i) {
    const BathventResult r = step(h);
    if (r.stage != Stage::kMid) {
      steady = false;
      std::printf("  left MID at t=%u stage=%s state=%s trace=%s\n", h.now,
                  stage_name(r.stage), state_name(r.state), r.trace);
      break;
    }
  }
  CHECK(steady);
}

// Absence with air that stays above the threshold: short FULL bursts, spaced by
// max_off_time (deliberate periodic air exchange, no rapid clicking).
void test_absence_periodic_full() {
  std::printf("absence + humid air -> periodic FULL bursts\n");
  Harness h;
  reset(h, /*light=*/false, /*humidity=*/80.0f, /*voc=*/100.0f);

  const uint32_t full_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kFull;
      }, 10000);
  CHECK(full_at != 0);
  CHECK(full_at > 1800);   // only after max_off_time + flush
  CHECK(full_at < 2000);

  const uint32_t off_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kOff;
      }, 2000);
  CHECK(off_at != 0);
  CHECK(off_at - full_at <= 400);  // ~one change-check interval of FULL

  const uint32_t full_again =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kFull;
      }, 6000);
  CHECK(full_again != 0);
  CHECK(full_again - off_at > 1800);  // the fan really stayed off for a while
}

// Light off after clean air: LOW for exactly afterrun_duration_s, then off.
void test_afterrun_after_light_off() {
  std::printf("light off -> afterrun LOW, then off\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/50.0f, /*voc=*/100.0f);
  for (int i = 0; i < 60; ++i) {
    step(h);
  }
  CHECK(h.r.state == FanState::kSniff);
  CHECK(h.r.stage == Stage::kLow);

  h.in.light = false;
  uint32_t low_ticks = 0;
  uint32_t off_at = 0;
  for (uint32_t i = 0; i < 2000; ++i) {
    const BathventResult r = step(h);
    if (r.stage == Stage::kLow) {
      ++low_ticks;
    } else if (r.stage == Stage::kOff) {
      off_at = h.now;
      break;
    }
  }
  CHECK(off_at != 0);
  CHECK(low_ticks == 300);  // == afterrun_duration_s
}

// A dead humidity sensor must ventilate: NAN is substituted above the
// threshold, so presence keeps MID steady.
void test_dead_humidity_sensor_failsafe() {
  std::printf("dead humidity sensor + presence -> steady MID\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/NAN, /*voc=*/100.0f);

  const uint32_t mid_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kMid;
      }, 200);
  CHECK(mid_at != 0);

  bool steady = true;
  for (int i = 0; i < 3600; ++i) {
    const BathventResult r = step(h);
    if (r.stage != Stage::kMid) {
      steady = false;
      std::printf("  left MID at t=%u stage=%s trace=%s\n", h.now,
                  stage_name(r.stage), r.trace);
      break;
    }
  }
  CHECK(steady);
}

// A dead humidity sensor while absent still does the periodic FULL exchange.
void test_dead_humidity_sensor_absence() {
  std::printf("dead humidity sensor + absence -> periodic FULL\n");
  Harness h;
  reset(h, /*light=*/false, /*humidity=*/NAN, /*voc=*/100.0f);

  const uint32_t full_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kFull;
      }, 10000);
  CHECK(full_at != 0);
}

// The optional VOC sensor must not trigger anything when it is missing.
void test_missing_voc_is_ignored() {
  std::printf("missing VOC sensor -> ignored\n");
  Harness h;
  reset(h, /*light=*/false, /*humidity=*/50.0f, /*voc=*/NAN);

  // Clean, dry air: the periodic flush must end in OFF, never in a run.
  bool ever_ran = false;
  for (uint32_t i = 0; i < 5000; ++i) {
    const BathventResult r = step(h);
    if (r.stage == Stage::kMid || r.stage == Stage::kFull) {
      ever_ran = true;
      std::printf("  unexpected run at t=%u trace=%s\n", h.now, r.trace);
      break;
    }
  }
  CHECK(!ever_ran);
  CHECK(h.r.humidity_ok);
  CHECK(!h.r.voc_ok);
}

// Presence with clean air must not be treated as "elevated" by a missing VOC.
void test_missing_voc_presence_stays_low() {
  std::printf("missing VOC sensor + presence -> LOW\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/50.0f, /*voc=*/NAN);
  for (int i = 0; i < 200; ++i) {
    step(h);
  }
  CHECK(h.r.state == FanState::kSniff);
  CHECK(h.r.stage == Stage::kLow);
}

// Timestamps must survive the uint32 millis() overflow without spurious trips.
void test_timestamp_wrap() {
  std::printf("timestamp wrap -> no spurious trigger\n");
  Harness h;
  reset(h, /*light=*/false, /*humidity=*/80.0f, /*voc=*/100.0f);
  h.now = 0xFFFFFF00u;  // ~1 minute before the 49.7 day wrap

  bool quiet = true;
  for (int i = 0; i < 1700; ++i) {  // stays below max_off_time (1800)
    const BathventResult r = step(h);
    if (r.stage != Stage::kOff) {
      quiet = false;
      std::printf("  spurious stage=%s at t=%u trace=%s\n", stage_name(r.stage),
                  h.now, r.trace);
      break;
    }
  }
  CHECK(quiet);

  // And it must still fire once the off-time really elapsed (across the wrap).
  const uint32_t flush_at =
      advance_until(h, [](const BathventResult &r) {
        return r.stage == Stage::kLow;
      }, 400);
  CHECK(flush_at != 0);
}

// The trace must read like the state machine: decision (with inputs) + actions.
void test_trace_is_filled() {
  std::printf("trace names the decision (with inputs) and the actions\n");
  Harness h;
  reset(h, /*light=*/true, /*humidity=*/80.0f, /*voc=*/100.0f);
  const BathventResult r = step(h);
  CHECK(r.trace != nullptr);
  CHECK(r.trace[0] != '\0');
  CHECK(std::strstr(r.trace, "presence(light=1)yes") != nullptr);  // decision
  CHECK(std::strstr(r.trace, "start_flush") != nullptr);           // action
  CHECK(std::strstr(r.trace, "reset_flush_ts") != nullptr);        // action
  CHECK(std::strstr(r.trace, "run_fan_on_low") != nullptr);        // action
  CHECK(std::strstr(r.trace, "reset_last_on_ts") != nullptr);      // action
}

}  // namespace

int main() {
  test_presence_clean_stays_low();
  test_presence_elevated_holds_mid();
  test_absence_periodic_full();
  test_afterrun_after_light_off();
  test_dead_humidity_sensor_failsafe();
  test_dead_humidity_sensor_absence();
  test_missing_voc_is_ignored();
  test_missing_voc_presence_stays_low();
  test_timestamp_wrap();
  test_trace_is_filled();

  std::printf("\nlongest trace: %zu chars (buffer 320)\n", g_worst_trace);
  CHECK(g_worst_trace > 0);
  CHECK(g_worst_trace < 320);  // would mean the trace got truncated

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
