# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

---

## Stufen

Drei Stufen über drei Relais (Serien-Kondensatoren, Lüfter = 2-Draht-Schattenpolmotor):

| Stufe | Relais | Kapazität |
| :--- | :--- | :--- |
| LOW | 4 µF | 4 µF |
| MID | 6 µF | 6 µF |
| FULL | direkt | – |

Die Kapazitäten 4 µF und 6 µF sind experimentell ermittelt und müssen für jeden Ventilator durch Berechnung und Ausprobieren bestimmt werden. Höhere Kombinationen (z. B. 10 µF) laufen bereits praktisch auf Vollgas.

## Regelung

Ein Schwellwert pro Sensor (`humidity_threshold`, `voc_threshold`):

| Zustand | Stufe |
| :--- | :--- |
| Anwesenheit (Licht), sauber | LOW |
| Anwesenheit, über Schwelle | MID |
| Abwesenheit, über Schwelle | FULL |
| Abwesenheit, sauber | Aus (+ LOW alle `sniff_interval`) |
| Nachlauf (Licht aus, 1 min) | LOW |
| Sensorausfall (einer) | FULL |

Hysterese (`humidity_hysteresis`, `voc_hysteresis`) verhindert Pendeln an den Schwellen.

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relay Low (4 µF) | GPIO12 (D6) |
| Relay Mid (6 µF) | GPIO16 (D0) |
| Relay Full (direkt) | GPIO14 (D5) |

**Interlock:** Immer genau eine Stufe aktiv; das FULL-Relais ist exklusiv zu den Kondensator-Relais. Implementierungsdetail – ein Verstoß ergibt die falsche Stufe (max. FULL), keine Gefahr.

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/`. Manueller Modus: `bathvent/select/mode/set` (`AUTO`, `OFF`, `LOW`, `MID`, `FULL`). Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../set`, `restore_value: true`).

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins)
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Zustandsmaschine
- `secrets.yaml` – Zugangsdaten (nicht committen)

## Hardware

- ESP8266 (Wemos D1 Mini), DHT20 + SGP40 (I2C, GPIO4/5), Optokoppler (GPIO13), 3x Relais LOW/MID/FULL (GPIO12/16/14), Kondensatoren 4 µF + 6 µF (450 V AC).
- Lüfter: 2-Draht-Schattenpolmotor. PSC-, EC- und Universalmotoren verhalten sich mit Serien-Kondensatoren anders und sind nicht abgedeckt.
- LH (Lampenphase) geht nur zur Lampe und zum Optokoppler – keine Kopplung zum Lüfter.

## Verdrahtung

**DC-Seite (Kleinspannung):**

| Komponente | Signal | ESP8266 / Ziel |
| :--- | :--- | :--- |
| DHT20 + SGP40 (I2C, parallel) | SDA / SCL | GPIO4 (D2) / GPIO5 (D1) |
| DHT20 + SGP40 | VCC / GND | 3,3 V / GND |
| Optokoppler (Licht) | OUT / VCC / GND | GPIO13 (D7) / 5 V / GND |
| Relay Low | IN | GPIO12 (D6) |
| Relay Mid | IN | GPIO16 (D0) |
| Relay Full | IN | GPIO14 (D5) |
| Versorgung | VIN / GND | 5 V / GND (ESP, Relais-Module, Optokoppler) |

Alle GND-Potenziale der DC-Seite verbinden. Sensoren auf 3,3 V (nicht 5 V).

**AC-Seite (230 V):**

- **L** (Dauerphase) → COM aller drei Relais.
- Relay Low (NO) → **4 µF** → Lüfter L
- Relay Mid (NO) → **6 µF** → Lüfter L
- Relay Full (NO) → **direkt** → Lüfter L
- **N** → Lüfter N.
- **LH** (Lampenphase) → nur zur Lampe und zum Optokoppler (keine Verbindung zum Lüfter).

230-V-Arbeiten nur durch Fachpersonal.

---

## KI-Metadaten (für AI-Agenten)

- ESPHome 2026.7.x, CLI via `uvx esphome`; Boards `d1_mini` / `nodemcuv2` / `esp32dev`.
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `select`-Zugriff im Lambda über `current_option()`; Entity-Namen ohne `/`.
- Stufen: `0=Aus, 1=LOW(4µF), 2=MID(6µF), 3=FULL(direkt)`.
- Sensoren: DHT20 (Feuchte, Delta zur EMA-Baseline) + SGP40 (VOC 1–500, 100 = 24h-Mittel, `store_baseline: true`), Kompensation vom DHT20.
- Defaults (`number`, MQTT-setbar, `restore_value: true`): `humidity_threshold=10`, `voc_threshold=150`, `humidity_hysteresis=3`, `voc_hysteresis=10`, `humidity_ema_alpha=0.0005`, `sniff_interval=30`, `nachlauf_duration=60`.
- MQTT: `bathvent/select/mode/set` = `AUTO|OFF|LOW|MID|FULL`; Topics `bathvent/.../state|set`.
- Fail-Safe: jeder Sensorausfall → FULL. Lizenz MIT.

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
