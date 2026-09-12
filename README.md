# bathvent-esphome

Automatisierte Lüftungssteuerung für das Bad auf Basis von ESPHome (ESP8266/ESP32) mit MQTT-Anbindung.

## Ziele

Das Projekt steuert die Lüftung eines Bads automatisch anhand von Luftfeuchte und Luftqualität. Die Lüftung soll anwesenheitsabhängig arbeiten – bei Anwesenheit niedrige bis mittlere Stufe, bei Abwesenheit volle Leistung bei überschrittener Schwelle. Schwellwerte und Zeiten sollen zur Laufzeit anpassbar sein; bei Sensorausfall soll die Lüftung weiter funktionieren.

## Umsetzungsideen / Prinzipien

Die Regelung ist eine kleine Zustandsmaschine mit vier Zuständen (`off`, `flush`, `sniff`, `run`) — vollständige Beschreibung inklusive Ablaufdiagramm: [`docs/state-machine.md`](docs/state-machine.md).

Anwesenheit wird über den Lichtschalter erkannt. Bei Anwesenheit startet eine Spülfahrt auf der niedrigsten Stufe; sie spült den Kanal, damit die Messwerte nicht durch zurückströmende Außenluft verfälscht sind. Danach wird gemessen: überschreitet ein Wert seine Schwelle, schaltet der Lüfter auf die mittlere Stufe (begrenzt die Geräuschentwicklung), bei Abwesenheit auf volle Stufe. Bei sauberer Luft und Anwesenheit bleibt er auf der niedrigsten Stufe, bei Abwesenheit aus. Nach dem Ausschalten des Lichts läuft er noch eine Weile auf der niedrigsten Stufe nach; nach langer Abwesenheit wird in Abständen eine Spül-/Messfahrt ausgeführt.

Die Schwellen sind **absolut** (keine gleitende Baseline): Ein Lauf endet, wenn sich in einem vollen Prüfintervall kein Sensorwert mehr signifikant ändert. Das Prüfintervall übernimmt damit die Rolle der Hysterese und begrenzt zugleich, wie oft der Lüfter bei anhaltend feuchter Außenluft anläuft (Frequenz ≈ Prüfintervall + Leerlaufzeit + Spülzeit).

Die drei Stufen werden über eine Kaskadenschaltung realisiert: niedrige und mittlere Stufe über Serien-Kondensatoren, volle Stufe direkt. Die Kaskade verhindert den Kurzschluss der geladenen Kondensatoren, der die Relais beschädigen würde (Festkleben der Kontakte).

Der Feuchtesensor (DHT20) ist der einzige fail-safe-relevante Sensor: Antwortet er nicht (`NAN`), wird sein Messwert durch einen konfigurierbaren Ersatzwert **oberhalb** der Schwelle ersetzt — der Lüfter lüftet dann defensiv weiter (bei Anwesenheit mittlere Stufe, sonst periodischer Volllast-Lauf). Der VOC-Sensor (SGP40) ist optional; sein Ersatzwert liegt **unter** der Schwelle, ein fehlender Sensor wird also ignoriert und die Regelung läuft nur über Feuchte und Licht. Alle Parameter sind über MQTT zur Laufzeit änderbar und bleiben über Neustarts erhalten.

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

Ein absoluter Schwellwert pro Sensor (`humidity_threshold`, `voc_threshold`). Ablaufdiagramm, Parameter und Namens-Mapping: [`docs/state-machine.md`](docs/state-machine.md).

| Situation | Stufe |
| :--- | :--- |
| Anwesenheit (Licht), sauber | LOW |
| Anwesenheit, über Schwelle | MID |
| Abwesenheit, über Schwelle | FULL |
| Abwesenheit, sauber | Aus (Spül-/Messfahrt alle `max_off_time`) |
| Nachlauf nach Licht aus (`afterrun_duration`) | LOW |
| Feuchtesensor-Ausfall (Ersatzwert über Schwelle) | wie „über Schwelle“: MID (anwesend) / FULL (abwesend) |
| VOC-Sensor fehlt (Ersatzwert unter Schwelle) | wird ignoriert |

Ein Lauf endet nicht an einer unteren Schwelle, sondern wenn sich in einem vollen Prüfintervall (`change_check_interval`) kein Sensorwert mehr um mehr als sein eigenes `*_change_threshold` bewegt hat. Das ist die bewusste Vereinfachung gegenüber einer gleitenden Baseline: Die Baseline war in der Praxis entweder zu hoch (Bad blieb feucht) oder zu niedrig (Lüfter lief permanent). Der Preis: Bei anhaltend feuchter Außenluft läuft der Lüfter periodisch an, statt dauerhaft durchzulaufen — die Frequenz ist über `change_check_interval`, `max_off_time` und `flush_duration` einstellbar.

Bewertet wird nur im Zustand `sniff`, also nach einer Kanal-Spülung (`flush_duration`). Bei stehendem Lüfter kann Außenluft durch den Kanal zurückströmen und die Rohwerte verfälschen — deshalb wird im Standstill nichts bewertet.

Der SGP40 (VOC) ist optional: liefert er keine gültigen Werte (nicht verlötet oder nicht antwortend), greift die Regelung nur auf Feuchte und Licht zurück.

---

## ESPHome

**GPIO:**

| Funktion | Pin |
| :--- | :--- |
| Optokoppler (Licht), `inverted: true` | GPIO13 (D7) |
| Relay Master (Ein/Aus) | GPIO14 (D5) |
| Relay LowMid (Low/Mid) | GPIO16 (D0) |
| Relay Full (Voll/Reduziert) | GPIO12 (D6) |

**MQTT:** `mqtt.discovery: true`, Topics unter `bathvent/` (`bathvent/<komponente>/<object_id>/state` = lesen, `.../command` = setzen). Alle Schwellwerte und Zeiten sind `number`-Entitäten (`bathvent/number/.../command`, `restore_value: true`). Es gibt keinen manuellen Betriebsmodus mehr — die Regelung läuft immer automatisch. Alle lesbaren und setzbaren Werte: siehe Abschnitt „MQTT“.

**Dateien:**
- `bathvent.yaml` – Hauptdatei (Plattform, Pins, bindet die C++-Logik per `esphome: includes:` ein)
- `bathvent.h` / `bathvent.cpp` – Hardware-unabhängige Zustandsmaschine (`bathvent_tick()`), einmal pro Sekunde aus dem Intervall-Lambda aufgerufen; der gesamte persistente Zustand liegt in einem Modul-Global in `bathvent.cpp`
- `tests/bathvent_test.cpp` – Host-Tests der Zustandsmaschine (siehe Abschnitt „Tests“)
- `common/wifi_mqtt.yaml` – WiFi, MQTT, OTA, captive_portal
- `common/base_esp8266.yaml` – I2C-Bus
- `packages/bathvent_logic.yaml` – Entity-Verdrahtung + 1-s-Intervall-Lambda (I/O-Anbindung an `bathvent_tick()`)
- `docs/state-machine.md` – autoritative Beschreibung der Regelung (Diagramm + Parameter)
- `secrets.yaml` – Zugangsdaten (nicht committen)

## MQTT

Alle Entitäten erscheinen über Discovery automatisch in Home Assistant (`mqtt.discovery: true`). Themenstruktur:

```
bathvent/<komponente>/<object_id>/state    # lesen (Zustand)
bathvent/<komponente>/<object_id>/command  # setzen (Befehl)
```

Befehle laufen über `/command` — `/set` wird ignoriert (ESPHome 2026.x). Parameter mit `restore_value: true` bleiben über Neustarts erhalten.

### Lesbare Werte

| Entity | Komponente | Topic (state) | Bedeutung |
| :--- | :--- | :--- | :--- |
| Temperature | sensor | `bathvent/sensor/temperature/state` | Raumtemperatur (Rohwert, 5-s-Takt) |
| Humidity | sensor | `bathvent/sensor/humidity/state` | relative Luftfeuchte (Rohwert) |
| VOC Index | sensor | `bathvent/sensor/voc_index/state` | VOC-Index (SGP40, optional) |
| Fan State | text_sensor | `bathvent/text_sensor/fan_state/state` | `off` / `flush` / `sniff` / `run` |
| Stage | text_sensor | `bathvent/text_sensor/stage/state` | aktive Stufe: OFF/LOW/MID/FULL |
| Trace | text_sensor | `bathvent/text_sensor/trace/state` | kurzer Ablauf-Trace des Ticks |
| Stored Humidity | sensor | `bathvent/sensor/stored_humidity/state` | Referenzwert des Change-Checks |
| Stored VOC Index | sensor | `bathvent/sensor/stored_voc_index/state` | Referenzwert des Change-Checks |
| Last Sensor Check Age | sensor | `bathvent/sensor/last_sensor_check_age/state` | Alter des Prüfintervall-Starts (s) |
| Last On Age | sensor | `bathvent/sensor/last_on_age/state` | Alter seit der letzten aktiven Stufe (s) |
| Afterrun Age | sensor | `bathvent/sensor/afterrun_age/state` | Alter seit der letzten Anwesenheit im `sniff` (s) |
| Flush Age | sensor | `bathvent/sensor/flush_age/state` | Alter seit dem Start der Spülung (s) |
| Light Switch | binary_sensor | `bathvent/binary_sensor/light_switch/state` | Licht / Anwesenheit |
| Relay Master / LowMid / Full | switch | `bathvent/switch/relay_<id>/state` | Relais-Zustand |

Die Roh-Sensoren (Temperature/Humidity/VOC) publizieren kontinuierlich (eigener
5-s-Takt) und können im Standstill durch Rückströmung im Kanal verfälscht sein.
`Fan State`, `Stage`, `Trace` sowie die Referenz- und Alterswerte kommen aus dem
1-s-Tick der Regelung; der `Trace` bewusst bei **jedem** Tick, damit der Ablauf
lückenlos nachvollziehbar ist.

### Setzbare Werte

| Parameter | Topic (command) | Bereich | Schritt | Default | Bedeutung |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Humidity Threshold | `bathvent/number/humidity_threshold/command` | 30–90 % | 1 | 65 | absolute Feuchte-Schwelle |
| VOC Threshold | `bathvent/number/voc_threshold/command` | 101–400 | 5 | 150 | VOC-Schwelle |
| Humidity Change Threshold | `bathvent/number/humidity_change_threshold/command` | 0,1–5 % | 0,1 | 1 | Mindeständerung pro Prüfintervall |
| VOC Change Threshold | `bathvent/number/voc_change_threshold/command` | 1–50 | 1 | 10 | Mindeständerung pro Prüfintervall |
| Change Check Interval | `bathvent/number/change_check_interval/command` | 60–900 s | 30 | 300 | Prüfintervall und Mindestlaufzeit |
| Max Off Time | `bathvent/number/max_off_time/command` | 300–7200 s | 60 | 1800 | Leerlauf bis zur periodischen Messfahrt |
| Flush Duration | `bathvent/number/flush_duration/command` | 15–120 s | 5 | 30 | Kanal-Spülung vor einer Messung |
| Afterrun Duration | `bathvent/number/afterrun_duration/command` | 60–600 s | 10 | 300 | Nachlauf nach Licht aus |
| Humidity NaN Value | `bathvent/number/humidity_nan_value/command` | 0–200 | 1 | 101 | Fail-safe-Ersatzwert Feuchte (über Schwelle = lüften) |
| VOC NaN Value | `bathvent/number/voc_nan_value/command` | 0–500 | 1 | 0 | Fail-safe-Ersatzwert VOC (unter Schwelle = ignorieren) |
| Relay Master / LowMid / Full | `bathvent/switch/relay_<id>/command` | – | – | – | ON/OFF (wird jeden Tick überschrieben) |

Hinweis: `restore_value: true` heißt, ein bereits auf dem Gerät gespeicherter
Wert **gewinnt** gegenüber einem geänderten Default nach dem Flashen. Neue
Defaults wirken erst nach einem Flash-Wipe oder nach einmaligem Setzen per MQTT.

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

**Schaltplan:** `docs/circuit-diagram.schemdraw.py` erzeugt `docs/circuit-diagram.svg`/`.png` (Dependencies per PEP-723-Inline-Metadaten deklariert):

```
uv run docs/circuit-diagram.schemdraw.py     # aus dem Projekt-Root
cd docs && uv run circuit-diagram.schemdraw.py   # alternativ aus dem docs-Ordner
```

Hinweis: `uvx` startet Tools (`uv tool run`), `uv run` startet Skripte – für PEP-723-Skripte ist `uv run` der richtige Befehl.

---

## KI-Metadaten (für AI-Agenten)

## Tests

Die Regelung ist hardwareunabhängig und läuft auf dem PC gegen synthetische
Messwerte und eine synthetische Uhr — Nachlauf (5 min) und periodische Messfahrt
(30 min) werden in Millisekunden geprüft, ohne zu warten:

```
g++ -std=c++17 -Wall -Wextra -DBATHVENT_HOST_TEST -I. -o tests/bathvent_test tests/bathvent_test.cpp bathvent.cpp
./tests/bathvent_test
```

Abgedeckt: Dauerbetrieb LOW/MID ohne Takten, periodischer FULL-Puls bei
Abwesenheit, Nachlauf nach Licht aus, Fail-safe bei totem Feuchtesensor,
ignorierter VOC-Sensor, Zeitstempel-Überlauf (`millis()`), Trace-Format
(Entscheidung + Actions) und Trace-Länge (keine Truncation).

---

## KI-Metadaten (für AI-Agenten)

- ESPHome 2026.7.x, CLI via `uvx esphome`; Boards `d1_mini` / `nodemcuv2` / `esp32dev`.
- Steuerlogik: Zustandsmaschine in `bathvent.h`/`bathvent.cpp` (`bathvent_tick()`), per `esphome: includes:` eingebunden, 1×/s aus dem Intervall-Lambda in `packages/bathvent_logic.yaml`. Autoritative Beschreibung + Ablaufdiagramm: `docs/state-machine.md`.
- Zustände `off`/`flush`/`sniff`/`run` (Enum `FanState`); Stufen `0=Aus, 1=LOW(3µF), 2=MID(5µF), 3=FULL(direkt)` (Enum `Stage`). Zuordnung: `run` → MID (anwesend) / FULL (abwesend); `flush`/`sniff` → LOW. Jede Stufe ≠ OFF setzt `last_on_ts`.
- Ablauf: `off` + Anwesenheit oder `max_off_time` abgelaufen → `flush`; nach `flush_duration` → `sniff`; Wert über Schwelle → `run`, sonst bei Anwesenheit Nachlauf (LOW, `afterrun_duration` nach dem letzten Presence-Tick im Sniff) und bei Abwesenheit Aus. `run` läuft, solange `now - last_sensor_check_ts <= change_check_interval`; danach entscheidet, ob sich irgendein Wert um mehr als sein eigenes `*_change_threshold` bewegt hat (ja → Referenz aktualisieren, weiter; nein → anwesend: Sniff-Entscheidung im **selben** Tick, abwesend: Aus).
- **Keine Baseline/EMA, keine Hysterese, kein manueller Modus** (bewusst entfernt: die Baseline war praktisch entweder zu hoch — Bad blieb feucht — oder zu niedrig — Dauerlauf). Das Prüfintervall ist die Dämpfung; bei anhaltend feuchter Außenluft läuft der Lüfter periodisch.
- Fail-safe = NaN-Policy: `NAN` wird durch `humidity_nan_value` (101, über der Schwelle → lüften) bzw. `voc_nan_value` (0, unter der Schwelle → ignorieren) ersetzt. Ein konstanter Ersatzwert gilt nach einem Intervall als „stabil" — bei Anwesenheit fängt das der `run`-Zweig ab, bei Abwesenheit entsteht der gewollte periodische FULL-Puls.
- Zeit: `BathventInputs::now_s = millis() / 1000`; alle Vergleiche über `(uint32_t)(now - t)` (überlaufsicher). Zustand in `g_state` (`bathvent.cpp`), Reset via `bathvent_reset_state()`; Zeitstempel werden beim ersten Tick auf `now_s` gepinnt.
- Logging: `BV_LOG(...)` = `ESP_LOGD("bathvent", ...)` bzw. No-op unter `-DBATHVENT_HOST_TEST` (mit `-DBATHVENT_HOST_LOG` auf stdout). Trace: `char[320]` in `bathvent.cpp`, pro Tick geleert und in `BathventResult::trace` zurückgegeben; im Lambda als Text-Sensor `trace` bei jedem Tick publiziert. Format `<Zustand>|<entscheidung>(<eingangsparameter>)<yes|no>|…|<action>|…` — jede Entscheidung mit Eingangsparametern und Ergebnis, danach jede Action; das Log enthält dieselben Informationen als Langtext (`decide … -> YES|NO`, `action …`). Token- und Parameter-Tabellen: `docs/state-machine.md`.
- `ota:` mit `- platform: esphome`; DHT20 als `aht10` mit `variant: AHT20`; `sgp4x` mit `voc_index`; Entity-Namen ohne `/`.
- Kaskade: `relay_master` (Ein/Aus), `relay_full` (Voll/Reduziert; NC = voll/direkt via NTC, NO = reduziert/Bank), `relay_lowmid` (Low/Mid; NC = 3µF, NO = 5µF); nur `kOff` schaltet `relay_master` aus; de-energized `relay_full` = voll.
- Sensoren: DHT20 (Feuchte **absolut**) + SGP40 (VOC 1–500, 100 = 24h-Mittel, `store_baseline: true`, optional), Kompensation vom DHT20. Roh-Sensoren publizieren im 5-s-Takt; bewertet wird nur nach der Kanal-Spülung.
- Defaults (`number`, MQTT-setbar, `restore_value: true`): `humidity_threshold=65`, `voc_threshold=150`, `humidity_change_threshold=1`, `voc_change_threshold=10`, `change_check_interval=300`, `max_off_time=1800`, `flush_duration=30`, `afterrun_duration=300`, `humidity_nan_value=101`, `voc_nan_value=0`.
- MQTT: Topics `bathvent/.../state` (lesen) + `bathvent/.../command` (setzen); Befehle NICHT über `/set`. Kein `select` mehr (kein manueller Modus).
- Tests: `g++ -std=c++17 -DBATHVENT_HOST_TEST -I. -o tests/bathvent_test tests/bathvent_test.cpp bathvent.cpp && ./tests/bathvent_test`.

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
