// =============================================================================
// bathvent.cpp — Implementation of the bathroom fan control state machine.
//
// Four states (see docs/state-machine.md):
//   off   — idle; presence or max_off_time starts a flush
//   flush — duct flush run (LOW); after flush_duration_s -> sniff
//   sniff — sensing run (LOW); readings decide run vs. afterrun vs. off
//   run   — ventilation (MID at presence / FULL at absence); stays on while a
//           control sensor keeps moving by more than its change threshold per
//           check interval
//
// There is deliberately NO baseline/EMA: the comfort thresholds are absolute.
// The change check interval doubles as the debounce — a run only ends after a
// full interval in which nothing moved significantly.
// =============================================================================

#include "bathvent.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Logging: real ESPHome logs on the device. Under the host test it is a no-op
// by default (the logic stays free of ESPHome headers); compile with
// -DBATHVENT_HOST_LOG as well to print the "long text" step log to stdout.
#if !defined(BATHVENT_HOST_TEST)
#include "esphome/core/log.h"
#define BV_LOG(...) ESP_LOGD("bathvent", __VA_ARGS__)
#elif defined(BATHVENT_HOST_LOG)
#define BV_LOG(...)                            \
  do {                                         \
    std::printf("[bathvent] " __VA_ARGS__);    \
    std::printf("\n");                         \
  } while (0)
#else
#define BV_LOG(...) ((void)0)
#endif

// ---- Short trace (MQTT) -----------------------------------------------------
// One string per tick: emptied at the start of the tick, appended step by step
// (decisions with their inputs and result, then the actions taken), published
// at the end of the tick. Tokens are terse — this is the short form; the log
// carries the same information as long text.
namespace {
constexpr size_t kTraceSize = 320;
char g_trace[kTraceSize];
size_t g_trace_len = 0;

void trace_reset() {
  g_trace[0] = '\0';
  g_trace_len = 0;
}

void trace_add(const char *fmt, ...) {
  if (g_trace_len + 1 >= kTraceSize) {
    return;
  }
  va_list args;
  va_start(args, fmt);
  const int n =
      std::vsnprintf(g_trace + g_trace_len, kTraceSize - g_trace_len, fmt, args);
  va_end(args);
  if (n <= 0) {
    return;
  }
  if (static_cast<size_t>(n) < kTraceSize - g_trace_len) {
    g_trace_len += static_cast<size_t>(n);
  } else {
    g_trace[kTraceSize - 1] = '\0';
    g_trace_len = kTraceSize - 1;
  }
}
}  // namespace

// ---- Persisted state (survives across ticks) --------------------------------

struct BathventState {
  FanState fan_state = FanState::kOff;
  float stored_humidity = NAN;  // change-check reference (run)
  float stored_voc = NAN;
  uint32_t last_sensor_check_ts = 0;  // last change-check in a run
  uint32_t last_on_ts = 0;            // last tick with a non-off stage
  uint32_t after_run_ts = 0;          // last presence while in sniff
  uint32_t flush_started_ts = 0;
  bool initialized = false;  // timestamps pinned to the first tick's now_s
};

namespace {
BathventState g_state;

// Wrap-safe elapsed seconds (uint32 subtraction is correct across the
// ~49.7 day millis() overflow).
inline uint32_t elapsed(uint32_t now, uint32_t then) { return now - then; }
}  // namespace

void bathvent_reset_state() { g_state = BathventState(); }

const char *stage_name(Stage stage) {
  static const char *const kNames[] = {"OFF", "LOW", "MID", "FULL"};
  return kNames[static_cast<uint8_t>(stage)];
}

const char *state_name(FanState state) {
  static const char *const kNames[] = {"OFF", "FLUSH", "SNIFF", "RUN"};
  return kNames[static_cast<uint8_t>(state)];
}

BathventResult bathvent_tick(const BathventInputs &in,
                             const BathventConfig &cfg) {
  if (!g_state.initialized) {
    // Pin every timestamp to the first tick, so the ages start at 0 instead of
    // "seconds since boot".
    g_state.last_sensor_check_ts = in.now_s;
    g_state.last_on_ts = in.now_s;
    g_state.after_run_ts = in.now_s;
    g_state.flush_started_ts = in.now_s;
    g_state.initialized = true;
  }

  trace_reset();

  // --- Readings (NAN is replaced by the per-sensor fail-safe policy value) ---
  const bool humidity_ok = !std::isnan(in.humidity);
  const bool voc_ok = !std::isnan(in.voc);
  const float hum = humidity_ok ? in.humidity : cfg.humidity_nan_value;
  const float voc = voc_ok ? in.voc : cfg.voc_nan_value;
  const bool above =
      (hum > cfg.humidity_threshold) || (voc > cfg.voc_threshold);

  Stage stage = Stage::kOff;
  trace_add("%s", state_name(g_state.fan_state));

  // ---- decision / action logging -------------------------------------------
  // The tick is a literal translation of the state machine in
  // docs/state-machine.md. EVERY decision is logged with its input parameters
  // and its result, EVERY action of the machine with its effect:
  //  - "long text"  -> one verbose log line,
  //  - "short text" -> one terse token in the trace string, which is emptied at
  //    the start of the tick and published to MQTT at the end of the tick.
  // `detail` is a single reused scratch buffer; it is consumed immediately.
  char detail[160];
  auto decide = [&](const char *name, const char *params, bool result) {
    (void)params;  // consumed by BV_LOG, which is a no-op in some builds
    trace_add("|%s(%s)%s", name, params, result ? "yes" : "no");
    BV_LOG("  decide %-12s %-46s -> %s", name, params, result ? "YES" : "NO");
    return result;
  };
  auto act = [&](const char *name, const char *effect) {
    (void)effect;  // consumed by BV_LOG, which is a no-op in some builds
    trace_add("|%s", name);
    BV_LOG("  action %-12s %s", name, effect);
  };

  // fan_run -> run_full_level_check (is human absent?) -> run_fan_on_full/mid
  auto a_fan_run = [&]() {
    std::snprintf(detail, sizeof(detail), "light=%d", in.light ? 1 : 0);
    const bool absent = !in.light;
    decide("level", detail, absent);
    stage = absent ? Stage::kFull : Stage::kMid;
    act(absent ? "run_fan_on_full" : "run_fan_on_mid",
        absent ? "stage=FULL (absence)" : "stage=MID (presence)");
  };

  // start_flush -> reset_flush_started_timestamp + run_fan_on_low
  auto a_start_flush = [&](const char *why) {
    act("start_flush", why);
    g_state.fan_state = FanState::kFlush;
    act("reset_flush_ts", "FlushStartedTimestamp = now");
    g_state.flush_started_ts = in.now_s;
    stage = Stage::kLow;
    act("run_fan_on_low", "stage=LOW");
  };

  // fan_start_run -> update_stored_sensor_values + reset_last_sensor_check_ts
  auto a_start_run = [&](const char *why) {
    act("start_run", why);
    g_state.fan_state = FanState::kRun;
    act("update_stored", "SensorStoredValue = SensorCurrentValue");
    g_state.stored_humidity = hum;
    g_state.stored_voc = voc;
    act("reset_check_ts", "LastSensorCheckTimestamp = now");
    g_state.last_sensor_check_ts = in.now_s;
  };

  // fan_sniff -> run_fan_on_low (stay in the sensing state)
  auto a_fan_sniff = [&](const char *why) {
    g_state.fan_state = FanState::kSniff;
    stage = Stage::kLow;
    act("fan_sniff", why);
    act("run_fan_on_low", "stage=LOW");
  };

  auto a_fan_off = [&](const char *why) {
    g_state.fan_state = FanState::kOff;
    stage = Stage::kOff;
    act("fan_off", why);
  };

  // sensor_check (the sniff decision). Reached from the sniff state and, in the
  // SAME tick, from a stable run while somebody is present (run -> sensor_check).
  auto sensor_check = [&]() {
    std::snprintf(detail, sizeof(detail), "hum %.1f vs %.1f, voc %.0f vs %.0f",
                  hum, cfg.humidity_threshold, voc, cfg.voc_threshold);
    decide("sensor", detail, above);
    if (above) {
      a_start_run("a value is above its threshold");
      a_fan_run();
      return;
    }
    std::snprintf(detail, sizeof(detail), "light=%d", in.light ? 1 : 0);
    const bool present = in.light;
    decide("presence", detail, present);
    if (present) {
      act("reset_afterrun_ts", "AfterRunTimestamp = now");
      g_state.after_run_ts = in.now_s;
      a_fan_sniff("presence");
      return;
    }
    const uint32_t since = elapsed(in.now_s, g_state.after_run_ts);
    std::snprintf(detail, sizeof(detail), "afterrun %us vs %us",
                  static_cast<unsigned>(since),
                  static_cast<unsigned>(cfg.afterrun_duration_s));
    if (decide("afterrun_done", detail,
               since > static_cast<uint32_t>(cfg.afterrun_duration_s))) {
      a_fan_off("afterrun finished");
    } else {
      a_fan_sniff("afterrun still running");
    }
  };

  // ===== fan_state_check =====
  switch (g_state.fan_state) {
    // ----------------------------------------------------------------- run --
    case FanState::kRun: {
      const uint32_t since_check =
          elapsed(in.now_s, g_state.last_sensor_check_ts);
      std::snprintf(detail, sizeof(detail), "since check %us vs interval %us",
                    static_cast<unsigned>(since_check),
                    static_cast<unsigned>(cfg.change_check_interval_s));
      if (!decide("run_time", detail,
                  since_check >
                      static_cast<uint32_t>(cfg.change_check_interval_s))) {
        a_fan_run();  // still inside the interval -> keep ventilating
        break;
      }

      const float dh = std::fabs(g_state.stored_humidity - hum);
      const float dv = std::fabs(g_state.stored_voc - voc);
      const bool changed = (dh > cfg.humidity_change_threshold) ||
                           (dv > cfg.voc_change_threshold);
      std::snprintf(detail, sizeof(detail), "dhum %.2f vs %.2f, dvoc %.1f vs %.1f",
                    dh, cfg.humidity_change_threshold, dv,
                    cfg.voc_change_threshold);
      decide("change", detail, changed);
      if (changed) {
        a_start_run("a sensor moved more than its threshold");
        a_fan_run();
        break;
      }

      std::snprintf(detail, sizeof(detail), "light=%d", in.light ? 1 : 0);
      if (decide("presence", detail, in.light)) {
        sensor_check();  // run -> sensor_check (in the SAME tick)
        break;
      }
      a_fan_off("stable and absent");
      break;
    }

    // ----------------------------------------------------------------- off --
    case FanState::kOff: {
      std::snprintf(detail, sizeof(detail), "light=%d", in.light ? 1 : 0);
      if (decide("presence", detail, in.light)) {
        a_start_flush("presence");
        break;
      }
      const uint32_t idle_s = elapsed(in.now_s, g_state.last_on_ts);
      std::snprintf(detail, sizeof(detail), "idle %us vs max_off %us",
                    static_cast<unsigned>(idle_s),
                    static_cast<unsigned>(cfg.max_off_time_s));
      if (decide("off_time", detail,
                 idle_s > static_cast<uint32_t>(cfg.max_off_time_s))) {
        a_start_flush("max_off_time exceeded");
        break;
      }
      a_fan_off("idle");
      break;
    }

    // --------------------------------------------------------------- sniff --
    case FanState::kSniff: {
      sensor_check();
      break;
    }

    // --------------------------------------------------------------- flush --
    case FanState::kFlush: {
      const uint32_t since_flush = elapsed(in.now_s, g_state.flush_started_ts);
      std::snprintf(detail, sizeof(detail), "flushing %us vs %us",
                    static_cast<unsigned>(since_flush),
                    static_cast<unsigned>(cfg.flush_duration_s));
      if (decide("flush_time", detail,
                 since_flush > static_cast<uint32_t>(cfg.flush_duration_s))) {
        a_fan_sniff("flush finished");
      } else {
        g_state.fan_state = FanState::kFlush;
        stage = Stage::kLow;
        act("keep_flush", "flush not finished");
        act("run_fan_on_low", "stage=LOW");
      }
      break;
    }
  }

  // run_fan_on_low / run_fan_on_mid / run_fan_on_full -> reset_last_on_timestamp
  if (stage != Stage::kOff) {
    g_state.last_on_ts = in.now_s;
    act("reset_last_on_ts", "LastOnTimestamp = now");
  }

  BV_LOG("tick end: state=%s stage=%s trace=%s", state_name(g_state.fan_state),
         stage_name(stage), g_trace);

  BathventResult result;
  result.stage = stage;
  result.state = g_state.fan_state;
  result.humidity_ok = humidity_ok;
  result.voc_ok = voc_ok;
  result.humidity_used = hum;
  result.voc_used = voc;
  result.stored_humidity = g_state.stored_humidity;
  result.stored_voc = g_state.stored_voc;
  result.since_sensor_check_s = elapsed(in.now_s, g_state.last_sensor_check_ts);
  result.since_last_on_s = elapsed(in.now_s, g_state.last_on_ts);
  result.since_afterrun_s = elapsed(in.now_s, g_state.after_run_ts);
  result.since_flush_s = elapsed(in.now_s, g_state.flush_started_ts);
  result.trace = g_trace;
  return result;
}
