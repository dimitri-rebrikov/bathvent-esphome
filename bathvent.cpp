// =============================================================================
// bathvent.cpp — Implementation of the bathroom fan control state machine.
// =============================================================================

#include "bathvent.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

// Duration of one "sniff" run in seconds (short LOW stage cycle after a long
// period of absence with clean air).
constexpr int kSniffDurationS = 60;

}  // namespace

OpMode parse_op_mode(const char *option) {
  if (option == nullptr) {
    return OpMode::kAuto;
  }
  if (std::strcmp(option, "OFF") == 0) return OpMode::kOff;
  if (std::strcmp(option, "LOW") == 0) return OpMode::kLow;
  if (std::strcmp(option, "MID") == 0) return OpMode::kMid;
  if (std::strcmp(option, "FULL") == 0) return OpMode::kFull;
  return OpMode::kAuto;
}

const char *stage_name(Stage stage) {
  static const char *const kNames[] = {"OFF", "LOW", "MID", "FULL"};
  return kNames[static_cast<uint8_t>(stage)];
}

BathventResult bathvent_tick(const BathventInputs &in,
                             const BathventConfig &cfg, float &ema) {
  // Persistent controller state (survives across ticks).
  static int hum_level = 0;           // 0 = normal, 1 = elevated
  static int voc_level = 0;           // 0 = normal, 1 = elevated
  static bool light_was_on = false;
  static int afterrun_remaining = 0;  // afterrun seconds left
  static int sniff_timer = 0;         // seconds since the fan last ran
  static int sniff_remaining = 0;     // sniff run seconds left

  const bool humidity_ok = !std::isnan(in.humidity);
  const bool voc_ok = !std::isnan(in.voc);

  // --- Humidity baseline (exponential moving average) ---
  if (humidity_ok) {
    ema = cfg.ema_alpha * in.humidity + (1.0f - cfg.ema_alpha) * ema;
  }
  const float hum_delta = humidity_ok ? (in.humidity - ema) : 0.0f;

  // --- Hysteresis level update (shared by humidity and VOC) ---
  // Rising edge triggers at the threshold; falling edge only below
  // (threshold - hysteresis) to avoid oscillation around the limit.
  auto update_level = [](bool ok, float value, float threshold, float hysteresis,
                         int &level) {
    if (!ok) {
      level = 0;
      return;
    }
    if (value >= threshold) {
      level = 1;
    } else if (value >= (threshold - hysteresis) && level >= 1) {
      level = 1;  // falling: keep while above hysteresis
    } else {
      level = 0;
    }
  };

  update_level(humidity_ok, hum_delta, cfg.humidity_threshold,
               cfg.humidity_hysteresis, hum_level);
  update_level(voc_ok, in.voc, cfg.voc_threshold, cfg.voc_hysteresis,
               voc_level);

  // --- Auto base stage (elevated / presence), before overrides ---
  const bool elevated = (hum_level >= 1 || voc_level >= 1);
  const Stage auto_base =
      elevated ? (in.light ? Stage::kMid : Stage::kFull)
               : (in.light ? Stage::kLow : Stage::kOff);

  // --- Afterrun: light just turned off ---
  if (light_was_on && !in.light && afterrun_remaining == 0) {
    afterrun_remaining = cfg.afterrun_duration_s;
  }
  light_was_on = in.light;
  if (in.light) {
    afterrun_remaining = 0;  // presence -> afterrun no longer applies
  }

  // --- Sniff timer (long-term absence, clean air) ---
  const int sniff_sec = cfg.sniff_interval_s;
  if (auto_base >= Stage::kLow || afterrun_remaining > 0) {
    sniff_timer = 0;  // fan active -> reset
  } else {
    sniff_timer++;
  }
  if (sniff_timer >= sniff_sec && sniff_remaining == 0) {
    sniff_remaining = kSniffDurationS;
    sniff_timer = 0;
  }

  // --- Single decision: stage + reason together (one priority chain) ---
  // "What" (stage) and "why" (reason) are decided in ONE place so they can
  // never drift apart. Priority: Manual > Fail-safe > Boost > Sniff > Afterrun
  // > Auto (see the AUTO decision table below).
  Stage stage = Stage::kOff;
  const char *reason = "Absence";

  switch (in.mode) {
    case OpMode::kOff:
      stage = Stage::kOff;
      reason = "Manual: Off";
      break;
    case OpMode::kLow:
      stage = Stage::kLow;
      reason = "Manual: Low";
      break;
    case OpMode::kMid:
      stage = Stage::kMid;
      reason = "Manual: Mid";
      break;
    case OpMode::kFull:
      stage = Stage::kFull;
      reason = "Manual: Full";
      break;
    case OpMode::kAuto:
    default: {
      // AUTO decision table, highest priority first (first match wins):
      //   #  Condition              Stage      Reason
      //   1  humidity sensor fail   kFull      "Fail-safe: sensor"
      //   2  elevated (boost)       auto_base  "Presence/Absence: <src>"
      //   3  sniff run active       kLow       "Sniffing"
      //   4  afterrun active        kLow       "Afterrun"
      //   5  otherwise (clean air)  auto_base  "Presence"/"Absence"
      //
      // NOTE: the elevated boost (2) sits ABOVE sniff (3) and afterrun (4):
      // during an afterrun the fan must still react to rising humidity/VOC
      // instead of being held at LOW. This ordering is the precedence — do
      // not move rule 2 below rules 3/4.
      // Only the humidity sensor is safety-relevant: if it fails, run the fan
      // at FULL. The VOC sensor (SGP40) is OPTIONAL — when it is not soldered
      // or simply not responding (voc stays NAN), it is ignored here (its level
      // is already forced to 0 above) instead of triggering fail-safe.
      if (!humidity_ok) {
        stage = Stage::kFull;
        reason = "Fail-safe: sensor";
      } else if (elevated) {
        stage = auto_base;
        const char *source = (hum_level >= 1 && voc_level >= 1) ? "both"
                           : (hum_level >= 1) ? "humidity" : "voc";
        static char reason_buffer[32];
        std::snprintf(reason_buffer, sizeof(reason_buffer), "%s: %s",
                      in.light ? "Presence" : "Absence", source);
        reason = reason_buffer;
      } else if (sniff_remaining > 0) {
        stage = Stage::kLow;
        reason = "Sniffing";
      } else if (afterrun_remaining > 0) {
        stage = Stage::kLow;
        reason = "Afterrun";
      } else {
        stage = auto_base;
        reason = in.light ? "Presence" : "Absence";
      }
      break;
    }
  }

  // Decrement active timers after the decision, so stage and reason both use
  // the same pre-decrement value on this tick.
  if (afterrun_remaining > 0) afterrun_remaining--;
  if (sniff_remaining > 0) sniff_remaining--;

  BathventResult result;
  result.stage = stage;
  result.reason = reason;
  result.humidity_ok = humidity_ok;
  result.baseline = ema;
  result.delta = hum_delta;
  result.hum_level = hum_level;
  result.voc_level = voc_level;
  return result;
}
