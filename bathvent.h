// =============================================================================
// bathvent.h — Hardware-independent control logic for the bathroom fan.
//
// The controller is a small state machine (off -> flush -> sniff -> run) that
// runs in `bathvent_tick()`, called once per second from the ESPHome interval
// lambda in packages/bathvent_logic.yaml. See docs/state-machine.md for the
// authoritative description and the state diagram.
//
// Readings are only trusted after the duct has been flushed (a flush run of
// flush_duration_s): while the fan stands still, outside air can backflow
// through the duct and falsify the values. There is no baseline/EMA
// machinery — comfort thresholds are absolute.
//
// All persisted state lives in a module-global `BathventState` inside
// bathvent.cpp (reset via bathvent_reset_state()); inputs and tuning
// parameters are passed in as structs so the call signature stays small.
//
// NOTE: `trace` in BathventResult points to static storage and is only valid
// until the next call to `bathvent_tick()`.
//
// Host testing: compile bathvent.cpp with -DBATHVENT_HOST_TEST to stub the
// ESPHome logging (see tests/bathvent_test.cpp).
// =============================================================================

#pragma once

#include <cmath>
#include <cstdint>

// Fan stage: which relay combination is active.
enum class Stage : uint8_t {
  kOff = 0,
  kLow = 1,  // 3uF
  kMid = 2,  // 5uF
  kFull = 3  // direct
};

// Control state, persisted between ticks.
enum class FanState : uint8_t {
  kOff = 0,    // fan off, waiting for presence or max_off_time
  kFlush = 1,  // duct flush run (LOW) before the readings may be trusted
  kSniff = 2,  // sensing run (LOW): decide run vs. off from the readings
  kRun = 3     // ventilation run (MID at presence / FULL at absence)
};

// Tuning parameters, adjustable via the MQTT "number" entities.
struct BathventConfig {
  // Comfort thresholds: a value ABOVE its threshold counts as elevated.
  float humidity_threshold = 65.0f;  // % RH (absolute, not a delta)
  float voc_threshold = 150.0f;      // VOC index

  // Change check: while running, stay on as long as ANY control sensor moved
  // more than its own threshold within one check interval. The interval is
  // therefore also the minimum run time and acts as the debounce/hysteresis.
  float humidity_change_threshold = 1.0f;  // % RH
  float voc_change_threshold = 10.0f;      // VOC index
  int change_check_interval_s = 300;       // s

  // Timers.
  int max_off_time_s = 1800;      // s of idle time before a periodic flush
  int flush_duration_s = 30;      // s duct flush before the readings are trusted
  int afterrun_duration_s = 300;  // s LOW after the presence ends (in sniff)

  // Fail-safe policy: value substituted for a NAN reading. Humidity is placed
  // ABOVE its threshold (dead sensor -> ventilate); VOC BELOW (the SGP40 is
  // optional, a missing one must not force ventilation).
  float humidity_nan_value = 101.0f;
  float voc_nan_value = 0.0f;
};

// Per-tick inputs, gathered from the sensors by the caller.
struct BathventInputs {
  float humidity = NAN;   // relative humidity in percent; NAN if unavailable
  float voc = NAN;        // VOC index; NAN if unavailable
  bool light = false;     // bathroom light / presence
  uint32_t now_s = 0;     // monotonic seconds (millis() / 1000)
};

// Result of one control tick.
struct BathventResult {
  Stage stage = Stage::kOff;
  FanState state = FanState::kOff;
  bool humidity_ok = false;  // raw reading was valid (no NaN substitution)
  bool voc_ok = false;
  float humidity_used = NAN;  // reading actually evaluated (after substitution)
  float voc_used = NAN;
  // Persisted state, mirrored for MQTT visibility.
  float stored_humidity = NAN;  // reference value of the change check
  float stored_voc = NAN;
  uint32_t since_sensor_check_s = 0;  // age of the change-check reference
  uint32_t since_last_on_s = 0;       // age of the last non-off stage
  uint32_t since_afterrun_s = 0;      // age of the last presence in sniff
  uint32_t since_flush_s = 0;         // age of the current/last flush start
  const char *trace = "";  // short trace, static storage, valid until next tick
};

// Human-readable names for the "Stage" / "Fan State" text sensors.
const char *stage_name(Stage stage);
const char *state_name(FanState state);

// Run one control tick.
BathventResult bathvent_tick(const BathventInputs &inputs,
                             const BathventConfig &config);

// Drop all persisted state (host tests / explicit reset). The next tick then
// starts from kOff with every timestamp pinned to that tick's `now_s`.
void bathvent_reset_state();
