# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

---

## Stufen

Drei Stufen über eine Kaskadenschaltung (Serien-Kondensatoren, Lüfter = 2-Draht-Schattenpolmotor):

| Stufe | Kapazität |
| :--- | :--- |
| LOW | 4 µF |
| MID | 6 µF |
| FULL | direkt |

Die Kapazitäten 4 µF und 6 µF sind experimentell ermittelt und müssen für jeden Ventilator durch Berechnung und Ausprobieren bestimmt werden. Höhere Kombinationen (z. B. 10 µF) laufen bereits praktisch auf Vollgas.

**Kaskade:** Drei Relais mit festen Rollen – **Master** (Ein/Aus, Motorverbindung), **Full** (Voll/Reduziert, überbrückt die gesamte Kondensator-Bank) und **LowMid** (Kondensatorwahl in der Bank). Beim Umschalten auf voll wird die Bank komplett überbrückt statt per Interlock ausgesperrt.

| Stufe | Master (Ein/Aus) | Full (Voll/Reduziert) | LowMid (Low/Mid) |
| :--- | :--- | :--- | :--- |
| OFF | aus | (egal) | (egal) |
| LOW | an | reduziert | low |
| MID | an | reduziert | mid |
| FULL | an | voll | (egal) |

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

### Grundidee: Anwesenheit vs. Abwesenheit

Die Regelung unterscheidet zwischen An- und Abwesenheit (Lichtschalter):

- **Anwesenheit:** Bei sauberer Luft läuft der Lüfter auf der niedrigsten Stufe. Diese dient der Ermittlung von Luftqualität und Feuchtigkeit (Luftumwälzung), nicht der starken Belüftung. Überschreitet ein Sensorwert die Schwelle, wird auf die mittlere Stufe geschaltet; die mittlere Stufe hält die Geräuschentwicklung bei Anwesenheit gering.
- **Abwesenheit:** Bei sauberer Luft bleibt der Lüfter aus. Überschreitet ein Sensorwert die Schwelle, wird mit voller Leistung abgelüftet.

Weitere Mechanismen:

- **Nachlauf:** Nach dem Ausschalten des Lichts läuft der Lüfter für eine kurze, konfigurierbare Dauer auf der niedrigsten Stufe weiter, um die Luftfeuchte und -qualität zu ermitteln.
- **Schnüffeln:** Nach einer längeren Zeitspanne ohne Lüfterlauf (saubere Luft, Abwesenheit) läuft der Lüfter kurz auf der niedrigsten Stufe (Schnüffellauf), um die aktuellen Werte zu ermitteln. Bei überschrittener Schwelle schaltet die Regelung auf die passende Stufe, sonst geht der Lüfter wieder aus.

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relay Master (Ein/Aus) | GPIO12 (D6) |
| Relay LowMid (Low/Mid) | GPIO16 (D0) |
| Relay Full (Voll/Reduziert) | GPIO14 (D5) |

**Kaskade:** Die Stufen sind Kombinationen der drei Relais (siehe Tabelle oben). Ein Kurzschluss der Kondensatoren ist konstruktionsbedingt ausgeschlossen, da das Full-Relais (Bypass) die gesamte Bank überbrückt.

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/`. Manueller Modus: `bathvent/select/operation_mode/set` (`AUTO`, `OFF`, `LOW`, `MID`, `FULL`). Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../set`, `restore_value: true`).

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins, bindet die C++-Logik per `esphome: includes:` ein)
- `bathvent.h` / `bathvent.cpp` – Hardware-unabhängige Zustandsmaschine (`bathvent_tick()`), einmal pro Sekunde aus dem Intervall-Lambda aufgerufen; statische Zustände (Level, Timer), EMA-Baseline wird als restaurierbares Global von außen durchgereicht
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Entity-Verdrahtung + 1-s-Intervall-Lambda (I/O-Anbindung an `bathvent_tick()`)
- `secrets.yaml` – Zugangsdaten (nicht committen)

## Hardware

- ESP8266 (Wemos D1 Mini), DHT20 + SGP40 (I2C, GPIO4/5), Optokoppler (GPIO13), 3x Relais in Kaskade: Master/Full/LowMid (GPIO12/16/14), Kondensatoren 4 µF + 6 µF (450 V AC).
- Lüfter: 2-Draht-Schattenpolmotor. PSC-, EC- und Universalmotoren verhalten sich mit Serien-Kondensatoren anders und sind nicht abgedeckt.
- LH (Lampenphase) geht nur zur Lampe und zum Optokoppler – keine Kopplung zum Lüfter.

## Verdrahtung

**DC-Seite (Kleinspannung):**

| Komponente | Signal | ESP8266 / Ziel |
| :--- | :--- | :--- |
| DHT20 + SGP40 (I2C, parallel) | SDA / SCL | GPIO4 (D2) / GPIO5 (D1) |
| DHT20 + SGP40 | VCC / GND | 3,3 V / GND |
| Optokoppler (Licht) | OUT / VCC / GND | GPIO13 (D7) / 5 V / GND |
| Relay Master (Ein/Aus) | IN | GPIO12 (D6) |
| Relay LowMid (Low/Mid) | IN | GPIO16 (D0) |
| Relay Full (Voll/Reduziert) | IN | GPIO14 (D5) |
| Versorgung | VIN / GND | 5 V / GND (ESP, Relais-Module, Optokoppler) |

Alle GND-Potenziale der DC-Seite verbinden. Sensoren auf 3,3 V (nicht 5 V).

**AC-Seite (230 V):**

- **L** (Dauerphase) versorgt die Kaskade; **N** → Lüfter N.
- **Relay Master (Ein/Aus):** verbindet L mit dem Lüfter – Motor ein/aus.
- **Relay Full (Voll/Reduziert):** „voll" = Direktverbindung zum Lüfter (Kondensator-Bank überbrückt); „reduziert" = Lüfter über die Kondensator-Bank.
- **Relay LowMid (Low/Mid):** wählt in der reduzierten Stellung **4 µF** (LOW) oder **6 µF** (MID).
- **LH** (Lampenphase) → nur zur Lampe und zum Optokoppler (keine Verbindung zum Lüfter).

230-V-Arbeiten nur durch Fachpersonal.

---

## KI-Metadaten (für AI-Agenten)

- ESPHome 2026.7.x, CLI via `uvx esphome`; Boards `d1_mini` / `nodemcuv2` / `esp32dev`.
- Steuerlogik: Zustandsmaschine in `bathvent.h`/`bathvent.cpp` (`bathvent_tick()`), per `esphome: includes:` eingebunden, 1×/s aus dem Intervall-Lambda in `packages/bathvent_logic.yaml`; Präzedenz: Manual > Fail-Safe > Boost > Sniff > Afterrun > Clean (Nachlauf/Sniff nur anhebend).
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `select`-Zugriff im Lambda über `current_option()`; Entity-Namen ohne `/`.
- Stufen: `0=Aus, 1=LOW(4µF), 2=MID(6µF), 3=FULL(direkt)`. Kaskade: `relay_master` (Ein/Aus), `relay_full` (Voll/Reduziert, überbrückt die Bank), `relay_lowmid` (Low/Mid); de-energized = konservativ (reduziert/low), nur `kOff` schaltet `relay_master` aus.
- Sensoren: DHT20 (Feuchte, Delta zur EMA-Baseline) + SGP40 (VOC 1–500, 100 = 24h-Mittel, `store_baseline: true`), Kompensation vom DHT20.
- Defaults (`number`, MQTT-setbar, `restore_value: true`): `humidity_threshold=10`, `voc_threshold=150`, `humidity_hysteresis=3`, `voc_hysteresis=10`, `humidity_ema_alpha=0.0005`, `sniff_interval=1800`, `afterrun_duration=60`.
- MQTT: `bathvent/select/operation_mode/set` = `AUTO|OFF|LOW|MID|FULL`; Topics `bathvent/.../state|set`.
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
