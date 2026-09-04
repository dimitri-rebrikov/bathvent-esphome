#!/usr/bin/env python3
# /// script
# dependencies = ["paho-mqtt>=1.6"]
# ///
"""Delete every retained MQTT message that belongs to the bathvent device.

Ghost entities in Home Assistant come from stale RETAINED discovery configs
under homeassistant/.../bathvent/... on the broker, and leftover state topics
under bathvent/... linger too. Deleting them in HA alone does not help - the
broker still holds the retained value and re-creates them on reconnect.

This script subscribes to the WHOLE MQTT tree (#), collects all retained
messages, and matches every topic that contains "bathvent" as a topic level
(`**/bathvent/**`): e.g. homeassistant/sensor/bathvent/humidity_delta/config,
bathvent/status, esphome/discover/bathvent.

It then:
  * lists everything it found (dry-run by default)
  * with --run: publishes an EMPTY retained message to every topic, which
    deletes the retained value on the broker
  * keeps bathvent/status (the LWT/availability topic) intact by re-publishing
    it as "online" retained

The current entities are re-created automatically by the device on its next
MQTT connect / reboot, so wiping everything is safe.

Credentials are read from secrets.yaml (ESPHome format). Run from the project
root with uv (paho-mqtt is resolved via the PEP 723 header above):

Usage:
  uv run cleanup_mqtt.py                       # dry-run (only lists topics)
  uv run cleanup_mqtt.py --run                 # actually delete
  uv run cleanup_mqtt.py --run --no-state      # only homeassistant/.../bathvent discovery
  uv run cleanup_mqtt.py --run --host 192.168.1.10 --user u --password p
"""

from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

import paho.mqtt.client as mqtt

STATUS_TOPIC = "bathvent/status"   # LWT: keep alive (online)

SETTLE_S = 5   # time to collect all retained messages after subscribing
FLUSH_S = 2    # time to let the empty retained publishes flush


def is_bathvent_topic(topic: str) -> bool:
    """True if the topic contains 'bathvent' as a whole topic level."""
    return "bathvent" in topic.split("/")


def load_secrets(path: str = "secrets.yaml") -> dict[str, str]:
    """Parse an ESPHome-style secrets.yaml into a dict (key: value)."""
    secrets: dict[str, str] = {}
    text = Path(path).read_text(encoding="utf-8")
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith("#") or ":" not in line:
            continue
        key, _, value = line.partition(":")
        secrets[key.strip()] = value.strip().strip('"').strip("'")
    return secrets


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="store_true",
                        help="actually delete retained messages (default: dry-run)")
    parser.add_argument("--no-state", action="store_true",
                        help="only clear HA discovery under homeassistant/.../bathvent/, "
                             "keep plain bathvent/# state topics")
    parser.add_argument("--host", help="broker address (overrides secrets.yaml)")
    parser.add_argument("--port", type=int, default=1883, help="broker port (default 1883)")
    parser.add_argument("--user", help="MQTT username (overrides secrets.yaml)")
    parser.add_argument("--password", help="MQTT password (overrides secrets.yaml)")
    parser.add_argument("--secrets", default="secrets.yaml",
                        help="path to secrets.yaml (default: ./secrets.yaml)")
    args = parser.parse_args()

    secrets = load_secrets(args.secrets)
    host = args.host or secrets.get("mqtt_broker")
    user = args.user if args.user is not None else secrets.get("mqtt_username")
    password = args.password if args.password is not None else secrets.get("mqtt_password")
    if not host:
        print("error: no broker configured (set mqtt_broker in secrets.yaml or pass --host)")
        return 1

    found: set[str] = set()

    def on_connect(client, userdata, flags, rc) -> None:
        if rc != 0:
            print(f"error: connect failed, rc={rc} ({mqtt.connack_string(rc)})")
            return
        print(f"connected to {host}:{args.port}")
        client.subscribe("#", qos=0)  # traverse the whole MQTT tree

    def on_message(client, userdata, msg) -> None:
        if not msg.retain:
            return  # only retained messages can be "left behind"
        if not is_bathvent_topic(msg.topic):
            return
        if args.no_state and not msg.topic.startswith("homeassistant/"):
            return
        found.add(msg.topic)

    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message
    if user is not None:
        client.username_pw_set(user, password)

    try:
        client.connect(host, args.port, keepalive=30)
    except Exception as exc:  # pragma: no cover - network errors are varied
        print(f"error: cannot connect to {host}:{args.port}: {exc}")
        return 1

    client.loop_start()
    time.sleep(SETTLE_S)

    # Split: Home Assistant discovery configs vs. plain state/other topics.
    discovery = sorted(t for t in found if t.startswith("homeassistant/"))
    other = sorted(t for t in found if not t.startswith("homeassistant/"))

    # The LWT/availability topic must stay: re-publish it as "online".
    other_nonstatus = [t for t in other if t != STATUS_TOPIC]
    status_present = STATUS_TOPIC in other

    total = len(discovery) + len(other_nonstatus)
    print(f"\nfound {total} retained bathvent topics"
          f" ({len(discovery)} discovery, {len(other_nonstatus)} state/other)")
    for t in discovery:
        print(f"  [discovery] {t}")
    for t in other_nonstatus:
        print(f"  [state]     {t}")
    if status_present:
        print(f"  [kept]      {STATUS_TOPIC} (LWT, re-published as online)")

    if not args.run:
        print("\ndry-run: nothing deleted. Re-run with --run to actually clear these topics.")
        client.disconnect()
        client.loop_stop()
        return 0

    if total == 0:
        print("\nnothing to delete.")
    else:
        for t in discovery + other_nonstatus:
            # Empty retained message = delete the retained value on the broker.
            client.publish(t, payload=None, retain=True)
        time.sleep(FLUSH_S)
        print(f"\ndeleted {total} retained message(s).")

    # Restore availability so HA keeps showing the device as online.
    client.publish(STATUS_TOPIC, payload="online", retain=True)
    time.sleep(FLUSH_S)

    client.disconnect()
    client.loop_stop()
    print("done. The current entities will be re-created by the device on its"
          " next MQTT connect / reboot.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
