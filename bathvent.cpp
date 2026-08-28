// =============================================================================
// bathvent.cpp — Implementation of the bathroom fan control state machine.
// =============================================================================

#include "bathvent.h"

#include <cmath>
#include <cstdio>
#include <cstring>

// Minimum absolute humidity decrease (percent) over a run-on window that counts
// as "still drying" and extends the run-on by one more window.
constexpr float kRunonDeadbandPct = 0.3f;

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
  static int prev_hum_level = 0;      // previous hum_level (falling-edge detection)
  static int hum_runon_remaining = 0; // humidity run-on seconds left in window
  static float hum_runon_ref = NAN;   // humidity at the run-on window start

  const bool humidity_ok = !std::isnan(in.humidity);
  const bool voc_ok = !std::isnan(in.voc);

  // --- Humidity baseline (exponential moving average) ---
  // Delta is computed against the PRE-update baseline: the freeze decision
  // below must reflect the state before this tick's EMA movement.
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

  // --- Baseline update: rise-lock while present+elevated, fall direct ---
  // The baseline is the "dry reference":
  //  - it FALLS DIRECTLY to the current reading whenever humidity drops below
  //    it (no smoothing — the dry reference tracks instantly);
  //  - it RISES only with the slow ema_alpha, and only when NOT (present AND
  //    elevated): a long bath (presence) must not drag it up, but an absent,
  //    weather-driven rise (rain, etc.) is absorbed so the fan does not fight
  //    it endlessly.
  if (humidity_ok) {
    if (in.humidity < ema) {
      ema = in.humidity;  // direct fall to dry air
    } else if (!(in.light && (hum_level >= 1))) {
      ema = cfg.ema_alpha * in.humidity + (1.0f - cfg.ema_alpha) * ema;
    }
  }

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
    // Sniff run lasts as long as the afterrun (same parameter).
    sniff_remaining = cfg.afterrun_duration_s;
    sniff_timer = 0;
  }

  // --- Humidity run-on (drying continuation) ---
  // When a humidity-driven run ends (hum_level drops back below the lower
  // threshold), do NOT stop immediately: keep running for one window
  // (runon_duration_s, default 60 s) so the fan keeps pulling the moisture
  // down — even below the baseline. It continues the mode that was lowering
  // the humidity (MID at presence, FULL at absence). The window
  // countdown/extension happens in the post-decision block (so stage+reason
  // see the pre-decrement value).
  // Falling edge: a humidity run just ended -> start a new run-on window.
  if (in.mode == OpMode::kAuto && prev_hum_level >= 1 && hum_level == 0) {
    hum_runon_remaining = cfg.runon_duration_s;
    hum_runon_ref = in.humidity;
  }
  prev_hum_level = hum_level;
  if (hum_runon_remaining > 0 && hum_level >= 1) {
    hum_runon_remaining = 0;  // elevated again -> boost takes over
  }

  // --- Single decision: stage + reason together (one priority chain) ---
  // "What" (stage) and "why" (reason) are decided in ONE place so they can
  // never drift apart. Priority: Manual > Fail-safe > Boost > Run-on > Sniff >
  // Afterrun > Auto (see the AUTO decision table below).
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
      //   #  Condition             Stage                 Reason
      //   1  humidity sensor fail  MID|FULL|OFF (sensorless)  "Fail-safe: presence|afterrun/sniff|absence"
      //   2  elevated (boost)       auto_base             "Presence/Absence: <src>"
      //   3  run-on active          MID|FULL (by presence)  "Run-on"
      //   4  sniff run active       kLow                  "Sniffing"
      //   5  afterrun active        kLow                  "Afterrun"
      //   6  otherwise (clean air)  auto_base             "Presence"/"Absence"
      //
      // NOTE: the elevated boost (2) sits ABOVE run-on (3), sniff (4) and
      // afterrun (5): during a run-on/nachlauf the fan must still react to
      // rising humidity/VOC instead of being held at a fixed stage. This
      // ordering is the precedence — do not move rule 2 below rules 3/4/5.
      // Sniff/afterrun are BOOST-ONLY LOW rules (raise OFF -> LOW only); the
      // run-on continues the drying mode (MID present / FULL absent) but is
      // still overridden by an elevated (MID/FULL) target.
      // Sensor fail-safe: without a valid humidity reading the fan falls back
      // to sensorless behaviour — presence -> MID, afterrun/sniff -> FULL, else
      // OFF (a classic bathroom fan that follows the light switch plus a
      // periodic full-speed air-exchange run). The VOC sensor (SGP40) is
      // OPTIONAL — when it is not soldered or simply not responding (voc stays
      // NAN), it is ignored here (its level is already forced to 0 above)
      // instead of triggering fail-safe.
      if (!humidity_ok) {
        if (in.light) {
          stage = Stage::kMid;
          reason = "Fail-safe: presence";
        } else if (afterrun_remaining > 0) {
          stage = Stage::kFull;
          reason = "Fail-safe: afterrun";
        } else if (sniff_remaining > 0) {
          stage = Stage::kFull;
          reason = "Fail-safe: sniff";
        } else {
          stage = Stage::kOff;
          reason = "Fail-safe: absence";
        }
      } else if (elevated) {
        stage = auto_base;
        const char *source = (hum_level >= 1 && voc_level >= 1) ? "both"
                           : (hum_level >= 1) ? "humidity" : "voc";
        static char reason_buffer[32];
        std::snprintf(reason_buffer, sizeof(reason_buffer), "%s: %s",
                      in.light ? "Presence" : "Absence", source);
        reason = reason_buffer;
      } else if (hum_runon_remaining > 0) {
        // Run-on continues the mode that was lowering the humidity: MID at
        // presence, FULL at absence (same as the elevated auto_base stage).
        stage = in.light ? Stage::kMid : Stage::kFull;
        reason = "Run-on";
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
  if (hum_runon_remaining > 0) {
    if (--hum_runon_remaining <= 0) {
      // Window over: still drying (humidity fell below the window-start ref)?
      if (humidity_ok && in.humidity < (hum_runon_ref - kRunonDeadbandPct)) {
        hum_runon_remaining = cfg.runon_duration_s;  // extend by one window
        hum_runon_ref = in.humidity;
      }
    }
  }

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
