# bathvent-esphome

**Intelligente, automatisierte Badventilator-Steuerung auf Basis von ESP8266 / ESP32, ESPHome und MQTT mit erweiterter Präsenzerkennung und Schnüffel-Logik.**

## 📖 Projektbeschreibung
`bathvent-esphome` ist eine autarke, sensorgesteuerte Smart-Home-Lösung für die Badezimmerlüftung. Das Projekt nutzt eine ausgefeilte lokale Zustandsmaschine, um einen mehrstufigen Ventilator bedarfsgerecht in vier effektiven Drehzahlstufen (*Aus*, *Niedrigst*, *Mittel*, *Voll*) zu regeln – realisiert durch eine Kombination aus passiver Kondensator-Beschaltung (Lichtphase LH → 3 µF) und zwei aktiv geschalteten Relais. 

Die Besonderheit liegt in der intelligenten Kombination aus Anwesenheitserkennung (Licht), Feuchtigkeit (DHT20) und Geruch (SGP40): Während der Anwesenheit schont das System das Gehör des Nutzers durch reduzierte Leistung. Sobald das Bad verlassen wird (Abwesenheit), schaltet das System bei jeglicher Abweichung sofort auf maximale Leistung, um den Raum so schnell wie möglich zu entlüften. Die Einbindung in Hausautomationssysteme wie **OpenHAB** erfolgt nahtlos über MQTT.

---

## ⚙️ Regelungs-Matrix & Steuerungslogik

Das System steuert die Lüfterstufen nach einer strikten Prioritäten-Hierarchie, um Schimmelschutz, Geruchsbeseitigung und akustischen Komfort perfekt auszubalancieren.

### ⚡ Drehzahlstufen durch Kondensator-Beschaltung

Die Steuerung verfügt über **drei Netzspannungseingänge**:

| Eingang | Bezeichnung | Beschreibung |
| :--- | :--- | :--- |
| **L** | Dauerphase | Permanent geschaltet (230V AC) |
| **N** | Neutralleiter | Permanent geschaltet |
| **LH** | Geschaltete Lampenphase | Wird vom Lichtschalter zugeschaltet (230V AC bei Licht EIN) |

Der Ausgang zum Ventilator ist **L und N**.

#### Passive Grundlast (Niedrigst-Stufe)
Die geschaltete Lampenphase **LH** ist über einen **3 µF Kondensator** permanent mit dem Lüfterausgang L verbunden. Sobald das Licht eingeschaltet wird, läuft der Ventilator daher **automatisch und ohne aktives Zutun der Steuerung** in der niedrigsten Drehstufe. Dies stellt sicher, dass die Sensoren (DHT20, SGP40) kontinuierlich mit Raumluft aus dem Badezimmer versorgt werden – der Ventilator ist dabei jedoch akustisch nicht wahrnehmbar. Der Wert von **3 µF wurde experimentell ermittelt**.

#### Aktive Stufen (Relais-Kaskade)

Die beiden Relais sind als **Kaskade** geschaltet:

- **Relais 1 (Ein/Aus):** Schaltet die Dauerphase **L** auf den Eingang von Relais 2 durch. Ist Relais 1 AUS, ist die gesamte aktive Steuerung stromlos.
- **Relais 2 (Wechselschalter):** Ein Wechselrelais, das im **AUS-Zustand (NC)** den Strom über den **3 µF Kondensator** zum Lüfter L führt und im **EIN-Zustand (NO)** den Strom **direkt (ohne Kondensator)** durchschaltet.

| Relais 2 Zustand | Pfad | Ergebnis |
| :--- | :--- | :--- |
| **AUS (NC)** | L → Relais 1 → Relais 2 (NC) → **3 µF** → Lüfter L | Reduzierte Stufe (je nach Licht: Niedrigst oder Mittel) |
| **EIN (NO)** | L → Relais 1 → Relais 2 (NO) → **direkt** → Lüfter L | Volle Stärke |

- **Licht EIN + Relais 1 EIN + Relais 2 AUS**: Die passive 3 µF (LH) und die aktiven 3 µF (über Relais) addieren sich → $3 + 3 = 6\ \mathrm{\mu F}$ → **Mittel-Stufe**.
- **Licht AUS + Relais 1 EIN + Relais 2 AUS**: Nur die 3 µF über die Relais-Kaskade sind wirksam → **Niedrigst-Stufe** (Sensor-Versorgung im Nachlauf).
- **Relais 1 EIN + Relais 2 EIN**: Direkte Phase → **Volle Stärke**, unabhängig vom Lichtzustand.

#### Zusammenfassung der Drehzahlstufen

| Stufe | Licht | Relais 1 (Ein/Aus) | Relais 2 (Wechsel) | Kapazität | Drehzahl |
| :--- | :--- | :--- | :--- | :--- | :--- |
| AUS | AUS | AUS | egal | — | 0 |
| Niedrigst | EIN | AUS | egal | 3 µF (LH passiv) | Minimal (unhörbar) |
| Niedrigst | AUS | EIN | AUS (NC) | 3 µF (aktiv) | Minimal (Sensor-Versorgung) |
| Mittel | EIN | EIN | AUS (NC) | 3 + 3 = 6 µF | Mittel |
| Voll | EIN/AUS | EIN | EIN (NO) | Direkt (0 µF) | Maximal |

---

### 1. Definition der Sensor-Zustände
- **Feuchtigkeit (DHT20):**
  - *Trocken/Normal:* < `humidity_low` (z. B. 55%)
  - *Feuchtigkeit vorhanden:* Zwischen `humidity_low` und `humidity_high`
  - *Starke Feuchtigkeit:* \>= `humidity_high` (z. B. 68% durch Duschen/Baden)
- **Geruch (SGP40 VOC-Index-Delta):**
  - *Normal:* Stabil oder sinkend
  - *Geruch vorhanden:* Moderater, schneller Anstieg (Delta \>= 30)
  - *Starke Geruchsbelästigung:* Massiver, schlagartiger Sprung (Delta \>= 50, z. B. Toilettengang)

> 💡 **Kompensation (DHT20 hilft SGP40):** Steigt der VOC-Wert zeitgleich mit einem massiven Feuchtigkeitsanstieg, wird dies als "Wasserdampf (Duschen)" klassifiziert. Steigt der VOC-Wert isoliert bei stabiler Feuchtigkeit, wird er als "Geruch (Toilettengang/Aerosol)" gewertet.

### 2. Die Prioritäten-Hierarchie (Konfliktlösung)

| Priorität | Erkannter Zustand | Lüfter-Stufe | Beschreibung / Verhalten |
| :--- | :--- | :--- | :--- |
| **1 (Höchste)** | Anwesenheit + Starke Feuchtigkeit / Starke Gerüche | **Volle Stärke** | **Akut-Entlüftung:** Hat im Ernstfall auch bei Anwesenheit Vorrang vor dem Lärmschutz. |
| **2** | **Abwesenheit** (egal ob Nachlauf, Schnüffeln oder Standby) + Feuchtigkeit oder Geruch **bereits moderat erhöht** | **Volle Stärke** | **Effizienz-Lüftung:** Bei Abwesenheit spielt Lärm keine Rolle – bereits eine leichte Abweichung vom Idealwert wird sofort mit maximaler Power beseitigt. Dies gilt in **jeder** Abwesenheits-Phase. |
| **3** | Anwesenheit (Licht AN) + Leichte Feuchtigkeit / Leichter Geruch | **Mittel-Stufe** | **Komfort-Modus:** Standardbetrieb bei normaler Nutzung des Bades mit moderat erhöhten Messwerten. Die passive 3 µF (LH) plus die aktiven 3 µF über die Relais-Kaskade (Relais 1 EIN, Relais 2 AUS/NC) ergeben 6 µF – ausreichend Lüftung bei reduzierter Lautstärke. |
| **4** | Anwesenheit (Licht AN) + Luft sauber & trocken | **Niedrigst-Stufe** | **Grundlüftung:** Die Smart-Steuerung ist komplett inaktiv (beide Relais AUS). Der Lüfter wird allein über die passive 3 µF-Verbindung LH → Lüfter L versorgt – unhörbare Dauerlüftung zur Sensor-Versorgung während der Anwesenheit. |
| **5** | Abwesenheit + Schnüffel-Intervall aktiv | **Niedrigst-Stufe** | **Mess-Modus:** Kurzer Takt zur Lufterneuerung an den Sensoren bei Langzeit-Abwesenheit. Relais 1 EIN, Relais 2 AUS (NC) → 3 µF über Kaskade – der Lüfter läuft unhörbar. |
| **6 (Niedrigste)**| Abwesenheit + Luft sauber & trocken | **AUS** | **Standby:** Energiesparmodus. |

> ⚡ **Zentrale Asymmetrie:** Bei **Anwesenheit** wird die Volle Stärke nur bei *stark* erhöhten Messwerten aktiviert (Lärmschutz). Bei **Abwesenheit** – egal in welcher Phase (Nachlauf, Schnüffeln, Standby) – genügen bereits *moderat* erhöhte Werte, um sofort die Volle Stärke zu schalten. Lärm spielt dann keine Rolle, die Entlüftung hat absolute Priorität.

### 3. Phasen und zeitliche Abläufe

#### A. Phase: Anwesenheit (Licht geht AN)
- **Sofortige Grundlüftung:** Sobald das Licht eingeschaltet wird, fließt über die geschaltete Lampenphase **LH** und den passiven 3 µF Kondensator Strom zum Ventilator. Der Lüfter läuft dadurch sofort in der **Niedrigst-Stufe** (unhörbar). Die Sensoren werden so ab der ersten Sekunde mit frischer Raumluft aus dem Badezimmer versorgt.
- **Normale Luft:** Solange die Sensoren saubere und trockene Luft melden, bleibt die Smart-Steuerung komplett inaktiv (beide Relais AUS). Der Ventilator läuft allein über die passive 3 µF-Verbindung in der Niedrigst-Stufe weiter.
- **Erhöhte Messwerte:** Steigen Feuchtigkeit oder Geruch moderat an, schaltet die Steuerung Relais 1 EIN und Relais 2 bleibt AUS (NC-Pfad über 3 µF) → $3 + 3 = 6\ \mathrm{\mu F}$ → **Mittel-Stufe** (Lärmvermeidung für den Menschen im Raum).
- **Ausnahme:** Tritt *starke* Feuchtigkeit oder *starke* Geruchsbelästigung auf, wird die Mittel-Stufe sofort überschrieben und auf **Volle Stärke** (direkte Phase) geschaltet.

#### B. Phase: Nachlauf & Testung (Licht geht AUS)
- Sobald das Licht erlischt, entfällt die passive 3 µF Versorgung über LH. Damit die Sensoren nicht schlagartig ohne Luftstrom dastehen, wechselt das System in die **Nachlauf-Phase**.
- Der Lüfter läuft für **1 Minute** mit **Relais 1 EIN und Relais 2 AUS (NC)** – die 3 µF über die Kaskade ergeben die **Niedrigst-Stufe** – und zwar **auch dann, wenn die Messwerte niedrig sind**. So wird sichergestellt, dass DHT20 und SGP40 weiterhin mit Messluft aus dem Badezimmer versorgt werden und valide Restfeuchte- bzw. Geruchswerte erfassen können.
- *Zweck:* Durch die fortgesetzte Ansaugung im Niedrigst-Modus (Licht AUS + Relais 1 EIN, Relais 2 AUS = 3 µF) wird stehende Luft im Gehäuse bewegt, damit die Sensoren eine aussagekräftige Messung der echten Raumluft durchführen können.
- **Sofortige Reaktion bei erhöhten Messwerten:** Signalisieren die Sensoren *während* der Nachlauf-Minute Feuchtigkeit oder Geruch, schaltet der Lüfter **sofort** auf **Volle Stärke** hoch (Priority 2 greift). Die Nachlauf-Minute wird dadurch überschrieben.
- **Am Ende der Nachlauf-Minute** (wenn keine erhöhten Werte aufgetreten sind):
  - Luft sauber & trocken? → Lüfter geht **AUS** (Relais 1 fällt ab).
  - (Der Fall „Werte erhöht" wurde bereits während der Minute behandelt – der Lüfter läuft dann bereits auf Voll und bleibt dort bis die Sollwerte erreicht sind.)

#### C. Phase: Die Schnüffel-Automatik (Langzeit-Abwesenheit)
- Ist das Bad über längere Zeit unbewohnt (Abwesenheit), startet alle 45 Minuten ein **Schnüffel-Intervall**.
- Der Lüfter schaltet sich für **1 Minute** mit **Relais 1 EIN und Relais 2 AUS (NC)** zu (3 µF über Kaskade → **Niedrigst-Stufe**), um frische Raumluft an die Sensoren zu führen.
- Signalisieren die Sensoren während dieser Minute, dass Feuchtigkeit oder Gerüche vorhanden sind (z. B. Feuchtigkeit kriecht nachträglich aus nassen Handtüchern), schaltet der Lüfter sofort hoch auf **Volle Stärke** und bleibt aktiv, bis das Bad wieder komplett trocken/geruchsfrei ist. Andernfalls schaltet er sich nach der Minute wieder ab.

## 📡 OpenHAB & MQTT-Schnittstelle

Das Projekt ist für die maximale Transparenz im Smart Home konzipiert. Der Mikrocontroller publiziert **jeden internen Zustand** fortlaufend auf dem MQTT-Broker:

1. **Sensordaten (Telemetrie):** Temperatur, relative Luftfeuchtigkeit sowie der berechnete VOC-Index und das VOC-Delta werden zyklisch gesendet.
2. **Binär-Zustände:** Der aktuelle Status des Lichtschalters (Präsenz) wird in Echtzeit übertragen.
3. **Aktor-Zustände:** Der effektive Schaltzustand wird als **Halbe-Stufe** (Relais 1 EIN, Relais 2 AUS/NC) bzw. **Volle-Stufe** (Relais 1 EIN, Relais 2 EIN/NO) oder **Aus** (Relais 1 AUS) gemeldet. Die Information, ob die Halbe-Stufe je nach Lichtzustand (LH) als Niedrigst- oder Mittel-Stärke wirkt, wird auf dieser Ebene bewusst nicht unterschieden – sie ist allein aus der Kombination mit dem Lichtzustand ableitbar.
4. **Zustandsmaschine:** Der aktuell aktive Modus der internen Logik (z. B. *Auto*, *Schnüffeln*, *Nachlauf*, *Manuell*) wird als String übertragen.

---

## 🛠 Hardware-Komponenten
- **Controller:** ESP8266 (z. B. Wemos D1 Mini, NodeMCU) oder **ESP32** (voll kompatibel)
- **Feuchtigkeit & Temperatur:** DHT20 (I2C)
- **Luftgüte / Geruchssensor:** SGP40 (I2C)
- **Aktorik:** 2x Relais – Relais 1 als Ein/Aus-Schalter (Dauerphase L durchschalten), Relais 2 als Wechselschalter (NC: über 3 µF Kondensator / NO: direkt)
- **Drehzahlstufen:** 2x 3 µF Kondensatoren (450V AC, z. B. Motor-Entstörkondensator), einer fest verdrahtet (LH → Lüfter L), einer über Relais geschaltet (L Dauerphase → Lüfter L)
- **Eingang (Präsenz):** 1x GPIO für die Lichtschalter-Erkennung (z. B. via Optokoppler/Kopplungsrelais an LH)

---

## 🔌 Verdrahtungsplan (Wiring)

Das System ist in einen sicheren Kleinspannungs-Teil (5V/3,3V DC) und einen Netzspannungs-Teil (230V AC) unterteilt. Die Sensoren, Relais und der Optokoppler sind als fertige Platinen-Module ausgeführt. Sämtliche GND-Potenziale der DC-Seite müssen miteinander verbunden sein.

### 1. Kleinspannung & Logik (DC-Seite)

Das 5V-Mini-Netzteil versorgt den ESP8266, die beiden Relais-Module und das Optokoppler-Modul parallel mit einer stabilen 5V-Schiene. Die hochempfindlichen Sensoren (DHT20 und SGP40) werden mit den intern geregelten 3,3V des ESP8266 betrieben und teilen sich die I2C-Busleitungen parallel.

| Komponente | Modul-Pin | Quelle / Ziel (ESP8266 / Netzteil) | Funktion / Beschreibung |
| :--- | :--- | :--- | :--- |
| **Netzteil (5V)** | +5V / VCC | **ESP8266 VIN / Relais 1 & 2 VCC / Opto VCC** | Zentrale 5V-Versorgung (Parallelverteilung) |
| **Netzteil (5V)** | GND / 0V | **Alle GND-Pins (Gemeinsame Masse)** | Gemeinsames Masse-Referenzpotenzial |
| **DHT20 (I2C)** | VCC | **ESP8266 3.3V** | Spannungsversorgung Sensor (3,3V Pegel) |
| **DHT20 (I2C)** | GND | **ESP8266 GND** | Masse |
| **DHT20 (I2C)** | SDA | **ESP8266 GPIO4 (D2)** | I2C Datenschnittstelle (Data) |
| **DHT20 (I2C)** | SCL | **ESP8266 GPIO5 (D1)** | I2C Taktschnittstelle (Clock) |
| **SGP40 (I2C)** | VCC | **ESP8266 3.3V** | Spannungsversorgung Sensor |
| **SGP40 (I2C)** | GND | **ESP8266 GND** | Masse |
| **SGP40 (I2C)** | SDA | **ESP8266 GPIO4 (D2)** | I2C Datenschnittstelle (Parallel zu DHT20) |
| **SGP40 (I2C)** | SCL | **ESP8266 GPIO5 (D1)** | I2C Taktschnittstelle (Parallel zu DHT20) |
| **Optokoppler** | VCC | **Netzteil +5V** | 5V Logik-Spannungsversorgung (Koppelmodul) |
| **Optokoppler** | GND | **Netzteil GND** | Masse |
| **Optokoppler** | OUT | **ESP8266 GPIO12 (D6)** | Signal-Eingang Lichtschalter (Echtzeit) |
| **Relais 1 (Ein/Aus)** | VCC | **Netzteil +5V** | 5V Spannungsversorgung für Relais-Spule 1 |
| **Relais 1 (Ein/Aus)** | GND | **Netzteil GND** | Masse |
| **Relais 1 (Ein/Aus)** | IN / SIG | **ESP8266 GPIO14 (D5)** | Signal-Ausgang: schaltet Dauerphase L auf Relais 2 durch |
| **Relais 2 (Wechsel)** | VCC | **Netzteil +5V** (Brücke von Relais 1) | 5V Spannungsversorgung für Relais-Spule 2 |
| **Relais 2 (Wechsel)** | GND | **Netzteil GND** (Brücke von Relais 1) | Masse |
| **Relais 2 (Wechsel)** | IN / SIG | **ESP8266 GPIO13 (D7)** | Signal-Ausgang: NC=3 µF (reduziert) / NO=direkt (Voll) |

---

### 2. Netzspannung & Last (230V AC-Seite)

> ⚠️ **WARNUNG:** Arbeiten an 230V Netzspannung dürfen nur von qualifiziertem Fachpersonal durchgeführt werden! Vor dem Öffnen oder Verdrahten immer die Sicherung freischalten und auf Spannungsfreiheit prüfen.

Die Steuerung wird mit **drei Netzspannungs-Leitungen** versorgt: **L** (Dauerphase), **N** (Neutralleiter) und **LH** (geschaltete Lampenphase vom Lichtschalter). Die beiden Relais sind als **Kaskade** geschaltet: Relais 1 trennt die gesamte aktive Steuerung vom Netz, Relais 2 ist ein Wechselschalter, der zwischen Kondensator-Pfad und Direkt-Pfad umschaltet.

#### Verdrahtung im Einzelnen:

1. **Passive Niedrigst-Stufe (fest verdrahtet, kein Relais):**
   - **LH** (geschaltete Lampenphase) → **3 µF Kondensator (450V AC)** → **Lüfter L**
   - Sobald das Licht eingeschaltet wird, fließt Strom über diesen Pfad – der Ventilator dreht automatisch und ohne Zutun der Steuerung in der Niedrigst-Stufe.

2. **Relais-Kaskade (aktive Pfade):**
   - **L** (Dauerphase) → **COM (Relais 1)** → **NO (Relais 1)** → **COM (Relais 2)**
   - **Relais 2 AUS (NC):** COM → NC → **3 µF Kondensator (450V AC)** → **Lüfter L** (reduzierte Stufe)
   - **Relais 2 EIN (NO):** COM → NO → **direkt (ohne Kondensator)** → **Lüfter L** (Volle Stärke)

3. **Neutralleiter (N):**
   - Wird vom Netz parallel an den Lüfter (**N**) sowie an die Eingänge des 5V-Mini-Netzteils und des Optokoppler-Moduls geführt.

4. **Optokoppler (Präsenzerkennung):**
   - Der Optokoppler greift die geschaltete Lampenphase **LH** ab, um den Lichtzustand in Echtzeit an den ESP zu melden.

#### Logische Schaltmatrix der Relais-Kaskade:

| Zustand | Licht | Relais 1 (Ein/Aus) | Relais 2 (Wechsel) | Kapazität | Drehzahl |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **AUS** | AUS | AUS | egal | — | 0 |
| **Niedrigst** (passiv) | EIN | AUS | egal | 3 µF (LH) | Minimal (unhörbar) |
| **Niedrigst** (Nachlauf) | AUS | EIN | AUS (NC) | 3 µF (aktiv) | Minimal |
| **Mittel** | EIN | EIN | AUS (NC) | 3 + 3 = 6 µF | Mittel |
| **Voll** | EIN/AUS | EIN | EIN (NO) | Direkt | Maximal |

> 💡 **Hinweis:** Durch die Kaskadierung ist ein Hardware-Kurzschluss konstruktiv ausgeschlossen: Ist Relais 1 AUS, fließt kein Strom über die aktiven Pfade – unabhängig von der Stellung des Relais 2. Ein zusätzlicher Interlock ist daher nicht erforderlich.

---

## 📂 Projektstruktur (Best Practice)

Um das Projekt sauber auf GitHub zu verwalten und für zukünftige Hardware-Wechsel (z. B. auf ESP32) vorzubereiten, wird eine **modulare Projektstruktur** empfohlen:

* **`secrets.yaml`**: Speichert WLAN- und MQTT-Zugangsdaten lokal (wird über `.gitignore` vom GitHub-Repository ausgeschlossen).
* **`common/`**: Enthält globale Basis-Konfigurationen (`wifi_mqtt.yaml`, `base_esp8266.yaml`).
* **`packages/bathvent_logic.yaml`**: Beheimatet die gesamte, hardwareunabhängige Steuerungs- und Zustandslogik.
* **`bathvent.yaml`**: Die schlanke Hauptdatei, die lediglich die Hardware-Plattform (ESP8266/ESP32) und die GPIO-Pins via `substitutions` definiert und die Packages modular zusammensetzt.

---

## 🚀 Für KI-Agenten / AI Agents (Metadaten)
*Diese Sektion dient LLMs und Code-Generatoren zur automatischen Pflege, Code-Erstellung und Portierung:*
- **Framework:** ESPHome
- **Plattformen:** `esp8266`, `esp32` (Multi-Architektur bereit)
- **Kommunikation:** MQTT (OpenHAB-optimiert, Zustand via JSON/Templates)
- **Sensoren:** `aht10` (Konfiguration für DHT20), `sgp40` (I2C-Bus)
- **Inputs:** `binary_sensor` (GPIO für Licht/LH, sofortige Erkennung auf Sensorebene)
- **Outputs:** 2x `switch.gpio` – Relais 1 (Ein/Aus, schaltet Dauerphase L auf Relais 2 durch), Relais 2 (Wechselschalter: NC=3 µF Kondensator, NO=direkt). Kein Interlock nötig (Kaskade verhindert hardware-seitig Kurzschlüsse).
- **Passive Beschaltung:** LH (geschaltete Lampenphase) → 3 µF Kondensator (fest verdrahtet) → Lüfter L – ermöglicht Niedrigst-Stufe ohne aktiven Relais-Eingriff bei eingeschaltetem Licht.
- **Logik-Kern:** Zeitgesteuertes Intervall (Zustandsmaschine via C++ Lambda) mit Variablen-Substitutions für Schwellenwerte.
- **Abwesenheits-Regel:** `light_switch == false` konvertiert jeden Schwellenwert-Trigger (`low` und `high`) direkt in den maximalen Output-Zustand (`relay1 = true, relay2 = true` → direkte Phase).
- **Nachlauf bei Licht AUS:** Relais 1 wird für 1 min eingeschaltet, Relais 2 bleibt AUS (NC, 3 µF) – auch bei niedrigen Messwerten, um die Sensoren weiter mit Messluft zu versorgen.
- **Lizenz:** MIT

---

## 🏗 Installation & Setup
1. **ESPHome:** ESPHome-Umgebung vorbereiten.
2. **Konfiguration:** Die `bathvent.yaml` im Repository als Basis nutzen.
3. **Plattform wählen:** - Für **ESP8266**: Den Standard-Block `esp8266: board: [dein_board]` nutzen.
   - Für **ESP32**: Den Kopfbereich auf `esp32: board: [dein_board]` ändern und die Pins anpassen.
4. **Anpassung:** WLAN-Daten, MQTT-Broker-IP und Schwellenwerte in den `substitutions` eintragen.
5. **Deployment:** Den Controller via USB oder OTA flashen.

---

## 📄 Lizenz
Dieses Projekt ist unter der **MIT-Lizenz** lizenziert. Siehe `LICENSE` für Details.

---
*Entwickelt für ein optimales, schimmelfreies Badklima bei maximalem akustischen Komfort. Feedback, Forks und Pull Requests sind herzlich willkommen!*
