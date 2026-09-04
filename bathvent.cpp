// =============================================================================
// bathvent.cpp — Implementation of the bathroom fan control state machine.
// =============================================================================

#include "bathvent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// The drying logic is deliberately simple: in cycles of runon_duration_s the
// controller compares the current humidity with the reading from the cycle
// start. If it became smaller, the room is still drying and the run-on keeps
// going (and the baseline stays locked); otherwise it stops. No threshold —
// any decrease counts. The long cycle makes even the slow asymptotic fall near
// the outside level visible, which is why the run-on is "generous".

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
  static bool runon_active = false;   // run-on: continue after a humidity run
  static int dry_timer = 0;           // seconds since the last drying check
  static float dry_ref = NAN;         // humidity at the drying-check cycle start
  static bool drying = false;         // last check: humidity decreased over the cycle

  const bool humidity_ok = !std::isnan(in.humidity);
  const bool voc_ok = !std::isnan(in.voc);

  // --- Sensor evaluation gate ---
  // Sensor values are ONLY evaluated while the fan is actually running, and
  // only once it has been running continuously for kEvalDelayS seconds:
  //  - When the fan is OFF, outside air can backflow through the duct (e.g. a
  //    extractor hood elsewhere in the house pulls negative pressure) and
  //    falsify the humidity/VOC readings.
  //  - The first seconds after a start are skipped too, so the fan can flush
  //    the contaminated duct air and the sensor samples the real room air.
  // As soon as the fan stops, evaluation ends again (on the next tick).
  static bool fan_was_running = false;  // commanded stage was != kOff last tick
  static int run_seconds = 0;           // continuous run time (0 while stopped)
  static const int kEvalDelayS = 30;     // flush time after a start (~30 s)
  static const int kSniffSensingS = 30;  // sensing margin after the flush (afterrun/sniff floor)
  if (fan_was_running) {
    ++run_seconds;
  } else {
    run_seconds = 0;
  }
  const bool eval_on = fan_was_running && (run_seconds >= kEvalDelayS);

  // --- Run phase: ALL sensor-value processing in ONE place ---
  // Everything that consumes sensor VALUES runs here, and only while the fan
  // is actually running with the duct flushed (eval_on). While the fan is OFF
  // (or a run has not yet flushed the duct) no reading is trusted, so none of
  // this code executes — the else branch pins every sensor-driven state to
  // neutral/frozen instead. This keeps the standstill path free of sensor
  // logic: no EMA movement, no delta, no hysteresis levels, no drying/run-on.
  float hum_delta = NAN;
  if (eval_on) {
    // Hysteresis level update (shared by humidity and VOC). Rising edge
    // triggers at the threshold; falling edge only below (threshold -
    // hysteresis) to avoid oscillation around the limit.
    auto update_level = [](bool ok, float value, float threshold,
                           float hysteresis, int &level) {
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

    // Delta vs. the PRE-update baseline: the freeze decision below must
    // reflect the state before this tick's EMA movement.
    hum_delta = humidity_ok ? (in.humidity - ema) : 0.0f;
    update_level(humidity_ok, hum_delta, cfg.humidity_threshold,
                 cfg.humidity_hysteresis, hum_level);
    update_level(voc_ok, in.voc, cfg.voc_threshold, cfg.voc_hysteresis,
                 voc_level);

    // --- Cycle-based drying check (simple, generous) ---
    // Every runon_duration_s seconds compare the current humidity with the
    // value from the cycle start. If it became smaller, the room is still
    // drying: the run-on keeps going and the baseline stays locked. No
    // threshold — any decrease counts; the long cycle sees even the slow
    // asymptotic fall near the outside level.
    if (humidity_ok) {
      if (std::isnan(dry_ref)) {
        dry_ref = in.humidity;
      }
      if (++dry_timer >= cfg.runon_duration_s) {
        dry_timer = 0;
        drying = in.humidity < dry_ref;
        dry_ref = in.humidity;
        if (runon_active && !drying) {
          runon_active = false;  // no progress over the cycle -> stop
        }
      }
    } else {
      drying = false;
      runon_active = false;  // sensor loss -> no drying to follow
    }

    // --- Baseline update: locked while the room is being dried, direct fall,
    //     slow seasonal rise otherwise ---
    // The baseline is the "dry reference" (seasonal). It is LOCKED (no rise)
    // while the room is actively dried:
    //  - presence + elevated (shower/bath in progress),
    //  - the last drying check showed the humidity still decreasing (shower
    //    aftermath — same cycle signal as the run-on),
    //  - run-on still active.
    // Outside of these it RISES only slowly with the (seasonal) ema_alpha, so
    // a sustained weather-driven level is absorbed over time and the fan does
    // not fight it endlessly. It FALLS DIRECTLY whenever humidity drops below
    // it. During standstill it is frozen (see else branch below): backflow air
    // must not shift the dry reference.
    if (humidity_ok) {
      const bool locked =
          (in.light && (hum_level >= 1)) || drying || runon_active;
      if (in.humidity < ema) {
        ema = in.humidity;  // direct fall to dry air
      } else if (!locked) {
        ema = cfg.ema_alpha * in.humidity + (1.0f - cfg.ema_alpha) * ema;
      }
    }

    // --- Humidity run-on (drying continuation) ---
    // When a humidity-driven run ends (hum_level drops back below the lower
    // threshold), do NOT stop immediately: keep running (MID at presence /
    // FULL at absence — the mode that was lowering the humidity) in cycles of
    // runon_duration_s. At every cycle check, if the humidity became smaller,
    // continue; otherwise stop. This is generous: the closer the room humidity
    // gets to the outside level, the slower it falls, and the long cycle still
    // sees the slow decrease.
    if (in.mode != OpMode::kAuto) {
      runon_active = false;  // manual mode -> no run-on
    } else if (prev_hum_level >= 1 && hum_level == 0) {
      runon_active = true;  // humidity run just ended -> start run-on
    }
    prev_hum_level = hum_level;
    if (runon_active && hum_level >= 1) {
      runon_active = false;  // elevated again -> boost takes over
    }
  } else {
    // Standstill / not yet flushed: no reading is trusted. Pin every
    // sensor-driven value to neutral so no stale state from a previous run
    // leaks into the next one (levels sit at 0 = normal, EMA stays frozen).
    hum_level = 0;
    voc_level = 0;
    prev_hum_level = 0;
    dry_timer = 0;
    dry_ref = NAN;
    drying = false;
    runon_active = false;
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
    // Sniff run lasts as long as the afterrun (same parameter), but at least
    // kEvalDelayS + kSniffSensingS: a sniff starts from standstill and the
    // sensors are only evaluated after the flush delay, so a shorter run would
    // stop before it ever samples the real air ("blinder Sniff"). This floor is
    // enforced device-side — the number-entity min (afterrun_min_s in the
    // YAML) only guards new MQTT sets, not a lower value already stored on the
    // device.
    sniff_remaining =
        std::max(cfg.afterrun_duration_s, kEvalDelayS + kSniffSensingS);
    sniff_timer = 0;
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
      } else if (runon_active) {
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

  // Remember the commanded running state for the evaluation gate next tick.
  fan_was_running = (stage != Stage::kOff);

  BathventResult result;
  result.stage = stage;
  result.reason = reason;
  result.humidity_ok = humidity_ok;
  result.baseline = ema;
  result.delta = hum_delta;
  result.hum_level = hum_level;
  result.voc_level = voc_level;
  // Seconds until the next sniff run would start. sniff_timer counts idle time
  // only and resets whenever the fan is active, so while the fan runs this
  // shows a full sniff_interval (the clock restarts once idle); when idle it
  // counts down to 0, where the next sniff fires.
  result.next_sniff_s = std::max(0, sniff_sec - sniff_timer);
  result.eval_on = eval_on;
  return result;
}
