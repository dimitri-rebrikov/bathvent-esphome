# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

## Ziele

Das Projekt steuert die Lüftung eines Bads automatisch anhand von Luftfeuchte und Luftqualität. Die Lüftung soll anwesenheitsabhängig arbeiten – bei Anwesenheit niedrige bis mittlere Stufe, bei Abwesenheit volle Leistung bei überschrittener Schwelle. Schwellwerte und Zeiten sollen zur Laufzeit anpassbar sein; bei Sensorausfall soll die Lüftung weiter funktionieren.

## Umsetzungsideen / Prinzipien

Anwesenheit wird über den Lichtschalter erkannt. Bei Anwesenheit und sauberer Luft läuft der Lüfter auf der niedrigsten Stufe, um Luftfeuchte und -qualität zu ermitteln; überschreitet ein Wert die Schwelle, wird auf die mittlere Stufe geschaltet, die die Geräuschentwicklung begrenzt. Bei Abwesenheit bleibt der Lüfter bei sauberer Luft aus und lüftet bei überschrittener Schwelle mit voller Leistung. Ergänzend gibt es zwei Mechanismen: Nach dem Ausschalten des Lichts läuft der Lüfter kurz auf der niedrigsten Stufe nach; nach langer Abwesenheit mit sauberer Luft wird in Abständen ein Lauf auf der niedrigsten Stufe ausgeführt, um die aktuellen Werte zu ermitteln.

Die drei Stufen werden über eine Kaskadenschaltung realisiert: niedrige und mittlere Stufe über Serien-Kondensatoren, volle Stufe direkt. Die Kaskade verhindert den Kurzschluss der geladenen Kondensatoren, der die Relais beschädigen würde (Festkleben der Kontakte).

Fällt der Feuchtesensor (DHT20) aus, läuft der Lüfter sensorenlos weiter: bei Anwesenheit auf mittlerer Stufe, im Nachlauf und beim periodischen Lauf (Sniff) auf voller Stufe, sonst aus. Der VOC-Sensor (SGP40) ist optional: fehlt er oder antwortet er nicht, wird er ignoriert und die Regelung läuft nur über Feuchte und Licht. Schwellwerte, Hysterese und Zeiten sind über MQTT zur Laufzeit änderbar und bleiben über Neustarts erhalten.

---

## Stufen

Drei Stufen über eine Kaskadenschaltung (Serien-Kondensatoren, Lüfter = 2-Draht-Schattenpolmotor):

| Stufe | Kapazität |
| :--- | :--- |
| LOW | 3 µF |
| MID | 5 µF |
| FULL | direkt |

Die Kapazitäten 3 µF und 5 µF sind experimentell ermittelt und müssen für jeden Ventilator durch Berechnung und Ausprobieren bestimmt werden. Höhere Kombinationen (z. B. 10 µF) laufen bereits praktisch auf Vollgas.

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
| Nachlauf (Licht aus, 5 min) | LOW |
| Feuchte-Lauf beendet (untere Schwelle), trocknet noch | MID (anwesend) / FULL (abwesend), solange Feuchte je Zyklus sinkt |
| Feuchtesensor-Ausfall: Anwesenheit | MID |
| Feuchtesensor-Ausfall: Nachlauf | FULL |
| Feuchtesensor-Ausfall: Sniff | FULL |
| Feuchtesensor-Ausfall: sonst | Aus |

Hysterese (`humidity_hysteresis`, `voc_hysteresis`) verhindert Pendeln an den Schwellen. Die Feuchte-Baseline (gleitender Mittelwert) dient dem saisonalen Ausgleich: Sie wird gesperrt, solange der Raum aktiv getrocknet wird — bei Anwesenheit mit erhöhter Feuchte (Bad) und solange die Feuchte je Prüfzyklus weiter sinkt (Dusch-Nachwirkung, gleiches Signal wie der Run-on); fällt die Feuchte unter die Baseline, sinkt sie direkt auf den Trockenwert. Nur ein anhaltender, nicht fallender Anstieg (Wetter) lässt sie langsam mitwandern. Der SGP40 (VOC) ist optional: liefert er keine gültigen Werte (nicht verlötet oder nicht antwortend), greift die Regelung nur auf Feuchte und Licht zurück.

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relay Master (Ein/Aus) | GPIO14 (D5) |
| Relay LowMid (Low/Mid) | GPIO16 (D0) |
| Relay Full (Voll/Reduziert) | GPIO12 (D6) |

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/` (`bathvent/<komponente>/<object_id>/state` = lesen, `.../command` = setzen). Manueller Modus: `bathvent/select/operation_mode/command` (`AUTO`, `OFF`, `LOW`, `MID`, `FULL`). Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../command`, `restore_value: true`). Alle lesbaren und setzbaren Werte: siehe Abschnitt „MQTT“.

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins, bindet die C++-Logik per `esphome: includes:` ein)
- `bathvent.h` / `bathvent.cpp` – Hardware-unabhängige Zustandsmaschine (`bathvent_tick()`), einmal pro Sekunde aus dem Intervall-Lambda aufgerufen; statische Zustände (Level, Timer), EMA-Baseline wird als restaurierbares Global von außen durchgereicht
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Entity-Verdrahtung + 1-s-Intervall-Lambda (I/O-Anbindung an `bathvent_tick()`)
- `secrets.yaml` – Zugangsdaten (nicht committen)

## MQTT

Alle Entitäten erscheinen über Discovery automatisch in Home Assistant (`mqtt.discovery: true`). Themenstruktur:

```
bathvent/<komponente>/<object_id>/state    # lesen (Zustand)
bathvent/<komponente>/<object_id>/command  # setzen (Befehl)
```

Befehle laufen über `/command` — `/set` wird ignoriert (ESPHome 2026.x). Parameter mit `restore_value: true` bleiben über Neustarts erhalten.

### Lesbare Werte

| Entity | Komponente | Topic (state) | Bedeutung | Bereich |
| :--- | :--- | :--- | :--- | :--- |
| Temperature | sensor | `bathvent/sensor/temperature/state` | Raumtemperatur | °C |
| Humidity | sensor | `bathvent/sensor/humidity/state` | relative Luftfeuchte | % |
| VOC Index | sensor | `bathvent/sensor/voc_index/state` | VOC-Index (SGP40, optional) | 1–500 |
| Humidity Baseline | number | `bathvent/number/humidity_baseline/state` | Trocken-Referenz (lesbar + setzbar) | % |
| Humidity Delta | sensor | `bathvent/sensor/humidity_delta/state` | Feuchte − Baseline | % |
| Humidity Status | sensor | `bathvent/sensor/humidity_status/state` | 0 = normal, 1 = erhöht | 0/1 |
| VOC Status | sensor | `bathvent/sensor/voc_status/state` | 0 = normal, 1 = erhöht | 0/1 |
| Light Switch | binary_sensor | `bathvent/binary_sensor/light_switch/state` | Licht / Anwesenheit | ON/OFF |
| DHT20 Status | binary_sensor | `bathvent/binary_sensor/dht20_status/state` | Feuchtesensor ok | ON/OFF |
| SGP40 Status | binary_sensor | `bathvent/binary_sensor/sgp40_status/state` | VOC-Sensor ok | ON/OFF |
| Stage | text_sensor | `bathvent/text_sensor/stage/state` | aktive Stufe | OFF/LOW/MID/FULL |
| Reason | text_sensor | `bathvent/text_sensor/reason/state` | Grund der Stufe | Text |
| Relay Master / LowMid / Full | switch | `bathvent/switch/relay_<id>/state` | Relais-Zustand | ON/OFF |

### Setzbare Werte

| Parameter | Topic (command) | Bereich | Schritt | Default | Bedeutung |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Humidity Threshold | `bathvent/number/humidity_threshold/command` | 1–30 % | 1 | 10 | Delta-Schwelle Feuchte |
| VOC Threshold | `bathvent/number/voc_threshold/command` | 101–400 | 5 | 150 | VOC-Schwelle |
| Humidity Hysteresis | `bathvent/number/humidity_hysteresis/command` | 1–10 % | 1 | 3 | Hysterese Feuchte |
| VOC Hysteresis | `bathvent/number/voc_hysteresis/command` | 1–50 | 1 | 10 | Hysterese VOC |
| Humidity EMA Alpha | `bathvent/number/humidity_ema_alpha/command` | 0.000001–0.01 | 0.000001 | 0.00001 | Baseline-Anstieg (saisonal, langsam) |
| Sniff Interval | `bathvent/number/sniff_interval/command` | 300–7200 s | 60 | 1800 | Intervall des periodischen Lüftens |
| Afterrun Duration | `bathvent/number/afterrun_duration/command` | 10–300 s | 10 | 300 | Nachlauf nach Licht aus (zugleich Sniff-Dauer) |
| Runon Duration | `bathvent/number/runon_duration/command` | 30–900 s | 30 | 300 | Trocknungs-Nachlauf (Zyklus-Check, solange Feuchte sinkt) |
| Humidity Baseline | `bathvent/number/humidity_baseline/command` | 0–100 % | 0.1 | – | Baseline (Trocken-Referenz) manuell setzen |
| Operation Mode | `bathvent/select/operation_mode/command` | – | – | AUTO | AUTO, OFF, LOW, MID, FULL |
| Relay Master / LowMid / Full | `bathvent/switch/relay_<id>/command` | – | – | – | ON/OFF (manuell) |

## Hardware

Stückliste:

| # | Bauteil | Spezifikation | Menge | Funktion |
| :--: | :--- | :--- | :--: | :--- |
| 1 | ESP8266 (Wemos D1 Mini) | – | 1 | Steuerung |
| 2 | Netzteil | 5 V DC | 1 | Versorgung |
| 3 | DHT20 | Temperatur und Feuchte, I2C | 1 | Sensor |
| 4 | SGP40 | VOC-Index, I2C | 1 (optional) | Sensor |
| 5 | Optokoppler-Modul | – | 1 | Lichterkennung |
| 6 | Relais-Module (Kaskade) | Master, Full, LowMid | 3 | Stufenschaltung |
| 7 | Kondensator | 3 µF, 450 V AC | 1 | LOW-Stufe |
| 8 | Kondensator | 5 µF, 450 V AC | 1 | MID-Stufe |
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
| 3 | SGP40 (optional) | SDA | GPIO4 (D2) |
| 4 | SGP40 (optional) | SCL | GPIO5 (D1) |
| 5 | DHT20 | VCC | 3,3 V |
| 6 | SGP40 (optional) | VCC | 3,3 V |
| 7 | DHT20 | GND | GND |
| 8 | SGP40 (optional) | GND | GND |
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
| 6 | Relay LowMid (Low/Mid) | NO (mid) | 5 µF |
| 7 | 5 µF | – | Lüfter L |
| 8 | Relay LowMid (Low/Mid) | NC (low) | 3 µF |
| 9 | 3 µF | – | Lüfter L |
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
- Steuerlogik: Zustandsmaschine in `bathvent.h`/`bathvent.cpp` (`bathvent_tick()`), per `esphome: includes:` eingebunden, 1×/s aus dem Intervall-Lambda in `packages/bathvent_logic.yaml`; Präzedenz: Manual > Fail-Safe > Boost > Run-on > Sniff > Afterrun > Clean (Nachlauf/Sniff/Run-on nur anhebend).
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `select`-Zugriff im Lambda über `current_option()`; Entity-Namen ohne `/`.
- Stufen: `0=Aus, 1=LOW(3µF), 2=MID(5µF), 3=FULL(direkt)`. Kaskade: `relay_master` (Ein/Aus), `relay_full` (Voll/Reduziert; NC = voll/direkt via NTC, NO = reduziert/Bank), `relay_lowmid` (Low/Mid; NC = 3µF, NO = 5µF); nur `kOff` schaltet `relay_master` aus (de-energized Master = Motor aus); de-energized `relay_full` = voll.
- Sensoren: DHT20 (Feuchte, Delta zur EMA-Baseline) + SGP40 (VOC 1–500, 100 = 24h-Mittel, `store_baseline: true`, optional), Kompensation vom DHT20.
- Defaults (`number`, MQTT-setbar, `restore_value: true`): `humidity_threshold=10`, `voc_threshold=150`, `humidity_hysteresis=3`, `voc_hysteresis=10`, `humidity_ema_alpha=0.00001`, `sniff_interval=1800`, `afterrun_duration=300` (5 min, zugleich Sniff-Dauer), `runon_duration=300` (Trocknungs-Nachlauf, Zyklus-Check).
- MQTT: `bathvent/select/operation_mode/command` = `AUTO|OFF|LOW|MID|FULL`; Topics `bathvent/.../state` (lesen) + `bathvent/.../command` (setzen); Befehle NICHT über `/set`.
- Fail-Safe (nur Feuchtesensor DHT20, sensorenlos): Anwesenheit → MID, Nachlauf/Sniff → FULL, sonst Aus; fehlender/antwortloser SGP40 wird ignoriert (kein Fail-Safe).

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
