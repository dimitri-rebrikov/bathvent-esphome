# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

---

## Stufen

Vier Stufen über drei Relais (Serien-Kondensatoren, Lüfter = 2-Draht-Schattenpolmotor):

| Stufe | Relais | Kapazität |
| :--- | :--- | :--- |
| Schnüffel | 4 µF | 4 µF |
| Niedrig | 6 µF | 6 µF |
| Mittel | 4 µF + 6 µF | 10 µF |
| Voll | direkt | – |

Die Kapazitäten 4 µF und 6 µF sind experimentell ermittelt und müssen für jeden Ventilator durch Berechnung und Ausprobieren bestimmt werden.

## Regelung

| Zustand | Stufe |
| :--- | :--- |
| Anwesenheit (Licht), sauber | Schnüffel |
| Anwesenheit, LOW | Niedrig |
| Anwesenheit, HIGH | Mittel (Maximum bei Anwesenheit) |
| Abwesenheit, LOW oder HIGH | Voll |
| Abwesenheit, sauber | Aus (+ Schnüffel alle `sniff_interval`) |
| Nachlauf (Licht aus, 1 min) | Schnüffel |
| Sensorausfall (einer) | Voll |

LOW/HIGH ergeben sich aus `humidity_delta_low/high` bzw. `voc_index_low/high`. Hysterese (`humidity_hysteresis`, `voc_hysteresis`) verhindert Pendeln an den Schwellen.

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relais 4 µF | GPIO12 (D6) |
| Relais 6 µF | GPIO16 (D0) |
| Relais Direkt | GPIO14 (D5) |

**Interlock:** Immer genau eine Stufe aktiv; das Direkt-Relais ist exklusiv zu den Kondensator-Relais. Implementierungsdetail – ein Verstoß ergibt die falsche Stufe (max. Voll), keine Gefahr.

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/`. Manueller Modus: `bathvent/select/mode/set` (`AUTO`, `OFF`, `SNIFF`, `LOW`, `MIDDLE`, `FULL`). Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../set`, `restore_value: true`).

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins)
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Zustandsmaschine
- `secrets.yaml` – Zugangsdaten (nicht committen)

## Hardware

- ESP8266 (Wemos D1 Mini), DHT20 + SGP40 (I2C, GPIO4/5), Optokoppler (GPIO13), 3x Relais (GPIO12/16/14), Kondensatoren 4 µF + 6 µF (450 V AC).
- Lüfter: 2-Draht-Schattenpolmotor. PSC-, EC- und Universalmotoren verhalten sich mit Serien-Kondensatoren anders und sind nicht abgedeckt.
- LH (Lampenphase) geht nur zur Lampe und zum Optokoppler – keine Kopplung zum Lüfter.

230-V-Arbeiten nur durch Fachpersonal. Netzseitig speisen alle drei Relais vom Dauerphasen-L auf Lüfter-L; Mittel = 4µF- und 6µF-Relais zusammen.

---

## KI-Metadaten (für AI-Agenten)

- ESPHome 2026.7.x, CLI via `uvx esphome`; Boards `d1_mini` / `nodemcuv2` / `esp32dev`.
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `select`-Zugriff im Lambda über `current_option()`; Entity-Namen ohne `/`.
- Stufen: `0=Aus, 1=Schnüffel(4µF), 2=Niedrig(6µF), 3=Mittel(10µF), 4=Voll(direkt)`.
- Sensoren: DHT20 (Feuchte, Delta zur EMA-Baseline) + SGP40 (VOC 1–500, 100 = 24h-Mittel, `store_baseline: true`), Kompensation vom DHT20.
- Defaults (`number`, MQTT-setbar, `restore_value: true`): `humidity_delta_low=10`, `humidity_delta_high=20`, `voc_index_low=150`, `voc_index_high=200`, `humidity_hysteresis=3`, `voc_hysteresis=10`, `humidity_ema_alpha=0.0005`, `sniff_interval=30`, `nachlauf_duration=60`.
- MQTT: `bathvent/select/mode/set` = `AUTO|OFF|SNIFF|LOW|MIDDLE|FULL`; Topics `bathvent/.../state|set`.
- Fail-Safe: jeder Sensorausfall → Voll. Lizenz MIT.

---

## Installation

Voraussetzung: ESPHome 2026.7.x (ohne lokales venv, z. B. via `uvx`). Zugangsdaten in `secrets.yaml` eintragen (nicht committen).

```
uvx esphome run bathvent.yaml --device COMx   # erstes Flashen per USB
uvx esphome run bathvent.yaml                 # danach per OTA
```

Bei CH340-Fehler `Error 31` unter Windows: Treiber v3.5.2019.1 verwenden und Windows-Driver-Updates blockieren.

## Quellen

- ESPHome: https://esphome.io/
  - OTA: https://esphome.io/components/ota.html
  - AHT10/DHT20: https://esphome.io/components/sensor/aht10.html
  - SGP4x: https://esphome.io/components/sensor/sgp4x.html
  - Select: https://esphome.io/components/select/
  - Captive Portal: https://esphome.io/components/captive_portal/
  - GPIO-Switch: https://esphome.io/components/switch/gpio.html
  - GPIO-Binary-Sensor: https://esphome.io/components/binary_sensor/gpio.html
  - Kommandozeile: https://esphome.io/guides/getting_started_command_line.html
- Wikipedia – Spaltpolmotor: https://de.wikipedia.org/wiki/Spaltpolmotor
- Wikipedia – Kondensatormotor: https://de.wikipedia.org/wiki/Kondensatormotor

## Lizenz

MIT, siehe `LICENSE`.
