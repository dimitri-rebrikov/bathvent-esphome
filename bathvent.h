// =============================================================================
// bathvent.h — Hardware-independent control logic for the bathroom fan.
//
// The state machine runs in `bathvent_tick()`, called once per second from the
// ESPHome interval lambda in packages/bathvent_logic.yaml. All persistent
// state lives inside the function; inputs and tuning parameters are passed in
// as structs so the call signature stays small and self-documenting.
//
// NOTE: the `reason` pointer returned in BathventResult points to static
// storage and is only valid until the next call to `bathvent_tick()`.
// =============================================================================

#pragma once

#include <cmath>
#include <cstdint>

// Fan stage: which relay combination is active.
enum class Stage : uint8_t {
  kOff = 0,
  kLow = 1,  // 4uF
  kMid = 2,  // 6uF
  kFull = 3  // direct
};

// Operating mode, as set via the MQTT "Operation Mode" select.
enum class OpMode : uint8_t {
  kAuto = 0,
  kOff = 1,
  kLow = 2,
  kMid = 3,
  kFull = 4
};

// Tuning parameters, adjustable via the MQTT "number" entities.
struct BathventConfig {
  float humidity_threshold = 10.0f;  // percent
  float humidity_hysteresis = 3.0f;  // percent
  float voc_threshold = 150.0f;      // VOC index
  float voc_hysteresis = 10.0f;      // VOC index
  float ema_alpha = 0.0005f;         // humidity baseline smoothing
  int afterrun_duration_s = 60;      // afterrun after the light turns off
  int sniff_interval_min = 30;       // absent time before a sniff run
};

// Per-tick inputs, gathered from the sensors by the caller.
struct BathventInputs {
  float humidity = NAN;  // relative humidity in percent; NAN if unavailable
  float voc = NAN;       // VOC index; NAN if unavailable
  bool light = false;    // bathroom light / presence
  OpMode mode = OpMode::kAuto;
};

// Result of one control tick.
struct BathventResult {
  Stage stage = Stage::kOff;
  const char *reason = "Unknown";  // static storage, valid until next tick
  bool humidity_ok = false;
  float baseline = NAN;  // current humidity EMA baseline
  float delta = NAN;     // humidity delta vs. baseline
  int hum_level = 0;     // 0 = normal, 1 = elevated
  int voc_level = 0;     // 0 = normal, 1 = elevated
};

// Map an MQTT option string ("AUTO", "OFF", ...) to the OpMode enum.
OpMode parse_op_mode(const char *option);

// Human-readable name of a stage, for the "Stage" text sensor.
const char *stage_name(Stage stage);

// Run one control tick. `ema` is the persistent humidity baseline; it is passed
// in/out so the caller can keep it across reboots (restore-able ESPHome global).
BathventResult bathvent_tick(const BathventInputs &inputs,
                             const BathventConfig &config, float &ema);
