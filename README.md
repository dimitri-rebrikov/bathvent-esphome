# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

## Ziele

Das Projekt steuert die Lüftung eines Bads automatisch anhand von Luftfeuchte und Luftqualität. Die Lüftung soll anwesenheitsabhängig arbeiten – bei Anwesenheit niedrige bis mittlere Stufe, bei Abwesenheit volle Leistung bei überschrittener Schwelle. Schwellwerte und Zeiten sollen zur Laufzeit anpassbar sein; bei Sensorausfall soll die Lüftung weiter funktionieren.

## Umsetzungsideen / Prinzipien

Anwesenheit wird über den Lichtschalter erkannt. Bei Anwesenheit und sauberer Luft läuft der Lüfter auf der niedrigsten Stufe, um Luftfeuchte und -qualität zu ermitteln; überschreitet ein Wert die Schwelle, wird auf die mittlere Stufe geschaltet, die die Geräuschentwicklung begrenzt. Bei Abwesenheit bleibt der Lüfter bei sauberer Luft aus und lüftet bei überschrittener Schwelle mit voller Leistung. Ergänzend gibt es zwei Mechanismen: Nach dem Ausschalten des Lichts läuft der Lüfter kurz auf der niedrigsten Stufe nach; nach langer Abwesenheit mit sauberer Luft wird in Abständen ein kurzer Lauf auf der niedrigsten Stufe ausgeführt, um die aktuellen Werte zu ermitteln.

Die drei Stufen werden über eine Kaskadenschaltung realisiert: niedrige und mittlere Stufe über Serien-Kondensatoren, volle Stufe direkt. Die Kaskade verhindert den Kurzschluss der geladenen Kondensatoren, der die Relais beschädigen würde (Festkleben der Kontakte).

Fällt ein Sensor aus, schaltet die Steuerung auf die höchste Stufe. Schwellwerte, Hysterese und Zeiten sind über MQTT zur Laufzeit änderbar und bleiben über Neustarts erhalten.

---

## Stufen

Drei Stufen über eine Kaskadenschaltung (Serien-Kondensatoren, Lüfter = 2-Draht-Schattenpolmotor):

| Stufe | Kapazität |
| :--- | :--- |
| LOW | 4 µF |
| MID | 6 µF |
| FULL | direkt |

Die Kapazitäten 4 µF und 6 µF sind experimentell ermittelt und müssen für jeden Ventilator durch Berechnung und Ausprobieren bestimmt werden. Höhere Kombinationen (z. B. 10 µF) laufen bereits praktisch auf Vollgas.

Zuordnung der Stufen zu den Relais:

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

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relay Master (Ein/Aus) | GPIO14 (D5) |
| Relay LowMid (Low/Mid) | GPIO16 (D0) |
| Relay Full (Voll/Reduziert) | GPIO12 (D6) |

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/`. Manueller Modus: `bathvent/select/operation_mode/set` (`AUTO`, `OFF`, `LOW`, `MID`, `FULL`). Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../set`, `restore_value: true`).

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins, bindet die C++-Logik per `esphome: includes:` ein)
- `bathvent.h` / `bathvent.cpp` – Hardware-unabhängige Zustandsmaschine (`bathvent_tick()`), einmal pro Sekunde aus dem Intervall-Lambda aufgerufen; statische Zustände (Level, Timer), EMA-Baseline wird als restaurierbares Global von außen durchgereicht
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Entity-Verdrahtung + 1-s-Intervall-Lambda (I/O-Anbindung an `bathvent_tick()`)
- `secrets.yaml` – Zugangsdaten (nicht committen)

## Hardware

Stückliste:

| # | Bauteil | Spezifikation | Menge | Funktion |
| :--: | :--- | :--- | :--: | :--- |
| 1 | ESP8266 (Wemos D1 Mini) | – | 1 | Steuerung |
| 2 | Netzteil | 5 V DC | 1 | Versorgung |
| 3 | DHT20 | Temperatur und Feuchte, I2C | 1 | Sensor |
| 4 | SGP40 | VOC-Index, I2C | 1 | Sensor |
| 5 | Optokoppler-Modul | – | 1 | Lichterkennung |
| 6 | Relais-Module (Kaskade) | Master, Full, LowMid | 3 | Stufenschaltung |
| 7 | Kondensator | 4 µF, 450 V AC | 1 | LOW-Stufe |
| 8 | Kondensator | 6 µF, 450 V AC | 1 | MID-Stufe |
| 9 | NTC-Heißleiter | 10 Ω, Kopf Ø 9 mm | 1 | Anlaufstrombegrenzung (FULL) |
| 10 | RC-Glied | 0,1 µF / 100 Ω, 0,5 W, 600 V AC | 1 | Lichtbogen-Unterdrückung (parallel zum Lüfter) |
| 11 | Lüfter | 2-Draht-Schattenpolmotor | 1 | Belüftung |

PSC-, EC- und Universalmotoren verhalten sich mit Serien-Kondensatoren anders und sind nicht abgedeckt.

## Verdrahtung

**DC-Seite (Kleinspannung):**

| # | Von | Pin | Nach |
| :--: | :--- | :--- | :--- |
| 1 | DHT20 | SDA | GPIO4 (D2) |
| 2 | DHT20 | SCL | GPIO5 (D1) |
| 3 | SGP40 | SDA | GPIO4 (D2) |
| 4 | SGP40 | SCL | GPIO5 (D1) |
| 5 | DHT20 | VCC | 3,3 V |
| 6 | SGP40 | VCC | 3,3 V |
| 7 | DHT20 | GND | GND |
| 8 | SGP40 | GND | GND |
| 9 | Optokoppler (Licht) | OUT | GPIO13 (D7) |
| 10 | Optokoppler (Licht) | VCC | 5 V |
| 11 | Optokoppler (Licht) | GND | GND |
| 12 | Relay Master (Ein/Aus) | IN | GPIO14 (D5) |
| 13 | Relay LowMid (Low/Mid) | IN | GPIO16 (D0) |
| 14 | Relay Full (Voll/Reduziert) | IN | GPIO12 (D6) |
| 15 | Relais-Module | VCC | 5 V |
| 16 | Relais-Module | GND | GND |
| 17 | ESP8266 | VIN | 5 V |
| 18 | ESP8266 | GND | GND |
| 19 | Netzteil (5 V) | +5 V | 5 V (Versorgungsschiene) |
| 20 | Netzteil (5 V) | GND | GND |

Alle GND-Potenziale der DC-Seite verbinden (gemeinsame Masse). Sensoren auf 3,3 V (nicht 5 V).

**AC-Seite (230 V):**

| # | Von | Kontakt | Nach |
| :--: | :--- | :--- | :--- |
| 1 | L (Dauerphase) | – | Relay Master COM |
| 2 | Relay Master (Ein/Aus) | NO | Relay Full COM |
| 3 | Relay Full (Voll/Reduziert) | NC (voll) | NTC (10 Ω) |
| 4 | NTC (10 Ω) | – | Lüfter L |
| 5 | Relay Full (Voll/Reduziert) | NO (reduziert) | Relay LowMid COM |
| 6 | Relay LowMid (Low/Mid) | NO (mid) | 6 µF |
| 7 | 6 µF | – | Lüfter L |
| 8 | Relay LowMid (Low/Mid) | NC (low) | 4 µF |
| 9 | 4 µF | – | Lüfter L |
| 10 | RC-Glied (0,1 µF / 100 Ω) | – | Lüfter L (parallel zum Motor) |
| 11 | RC-Glied (0,1 µF / 100 Ω) | – | Lüfter N (parallel zum Motor) |
| 12 | N | – | Lüfter N |
| 13 | N | – | Netzteil (5 V) |
| 14 | N | – | Optokoppler |
| 15 | L (Dauerphase) | – | Netzteil (5 V) |
| 16 | LH (Lampenphase) | – | Optokoppler |

Kontakte: COM = gemeinsamer Kontakt (Anker), NO = Arbeitskontakt/Schließer (Relais angezogen), NC = Ruhekontakt/Öffner (Relais abgefallen). 230-V-Arbeiten nur durch Fachpersonal.

---

## KI-Metadaten (für AI-Agenten)

- ESPHome 2026.7.x, CLI via `uvx esphome`; Boards `d1_mini` / `nodemcuv2` / `esp32dev`.
- Steuerlogik: Zustandsmaschine in `bathvent.h`/`bathvent.cpp` (`bathvent_tick()`), per `esphome: includes:` eingebunden, 1×/s aus dem Intervall-Lambda in `packages/bathvent_logic.yaml`; Präzedenz: Manual > Fail-Safe > Boost > Sniff > Afterrun > Clean (Nachlauf/Sniff nur anhebend).
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `select`-Zugriff im Lambda über `current_option()`; Entity-Namen ohne `/`.
- Stufen: `0=Aus, 1=LOW(4µF), 2=MID(6µF), 3=FULL(direkt)`. Kaskade: `relay_master` (Ein/Aus), `relay_full` (Voll/Reduziert; NC = voll/direkt via NTC, NO = reduziert/Bank), `relay_lowmid` (Low/Mid; NC = 4µF, NO = 6µF); nur `kOff` schaltet `relay_master` aus (de-energized Master = Motor aus); de-energized `relay_full` = voll.
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
