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
- **Feuchtigkeit (DHT20) – Delta-zur-Baseline:**
  - *Trocken/Normal:* < `humidity_delta_low` (z. B. 10 % rF über der gleitenden Baseline)
  - *Feuchtigkeit vorhanden:* Zwischen `humidity_delta_low` und `humidity_delta_high`
  - *Starke Feuchtigkeit:* \>= `humidity_delta_high` (z. B. 20 % rF über der Baseline, durch Duschen/Baden)
- **Geruch (SGP40 VOC-Index) – absolute Werte auf der SGP40-Skala:**
  - *Normal:* < `voc_index_low` (z. B. 150, leichte Erhöhung über dem 24h-Mittel von 100)
  - *Geruch vorhanden:* Zwischen `voc_index_low` und `voc_index_high`
  - *Starke Geruchsbelästigung:* \>= `voc_index_high` (z. B. 200, deutlicher Anstieg, Toilettengang)

#### Feuchte-Baseline (saisonale Selbstkalibrierung)
Statt absoluter Feuchte-Schwellwerte wird die **Abweichung von einer gleitenden Umgebungs-Baseline** gemessen. Diese Baseline wird über einen **extrem langsamen EMA** ($\alpha \approx 0{,}0005$, Zeitkonstante ~Stunden) aus der Badezimmer-Luftfeuchtigkeit gebildet. Der EMA folgt träge den jahreszeitlichen Schwankungen (Winter: ~45 %, Sommer: ~62 %), reagiert aber nicht auf kurze Dusch-Peaks. Dadurch läuft der Lüfter im Sommer nicht sinnlos, nur weil die Umgebungsluft generell feuchter ist. Die Baseline wird mit `restore_value: true` gespeichert und überlebt Stromausfälle. Der Alpha-Wert ist als `number`-Component (`humidity_ema_alpha`) über MQTT ausles- und anpassbar.

| Parameter | Default | Bedeutung |
| :--- | :--- | :--- |
| `humidity_delta_low` | 10 (% rF) | Mittel-Stufe bei Baseline + 10 % |
| `humidity_delta_high` | 20 (% rF) | Voll-Stufe bei Baseline + 20 % |

| Saison | Baseline | +10 % (Mittel) | +20 % (Voll) | Verhalten |
| :--- | :--- | :--- | :--- | :--- |
| Winter | ~45 % | 55 % | 65 % | ✅ wie bisher |
| Sommer | ~62 % | 72 % | 82 % | ✅ läuft nicht sinnlos |

#### SGP40-Baseline-Persistenz
Der SGP40 benötigt nach dem Einschalten mehrere Stunden, um seine interne VOC-Baseline zu kalibrieren. ESPHome speichert diese mit `store_baseline: true` (Standard) im Flash, sodass der Sensor nach einem Stromausfall oder Reboot nicht bei Null beginnt, sondern mit dem letzten bekannten Grundpegel weiterarbeitet. Der SGP40 normalisiert seine Werte selbst auf die Skala 1–500, wobei 100 dem 24h-Durchschnitt entspricht – ein externer EMA ist daher nicht erforderlich.

#### Zweistufige Kompensation (DHT20 hilft SGP40)

1. **SGP40-interne Kompensation (ESPHome-Boardmittel):** Der SGP40 wird mit Temperatur- und Feuchte-Quelle vom DHT20 kompensiert, was die Genauigkeit des VOC-Algorithmus auf Hardware-Ebene verbessert. Konkrete YAML-Konfiguration siehe [KI-Referenz](#-für-ki-agenten--ai-agents-metadaten).

2. **Keine separate Ereignis-Klassifikation nötig:** Eine Unterscheidung „Dusche vs. Toilettengang" per VOC/Feuchte-Delta-Vergleich ist **nicht erforderlich**. Der Grund: Beim Duschen steigt die Feuchte immer **vor** dem VOC-Wert – die Feuchte-Regel hat den Lüfter bereits geschaltet (Mittel- oder Voll-Stufe), bevor der SGP40 überhaupt einen Anstieg meldet. Ein zusätzlicher VOC-Trigger durch Wasserdampf würde die ohnehin schon aktive Lüfterstufe nicht ändern. DHT20 und SGP40 arbeiten daher als **unabhängige Trigger**, die Prioritätentabelle löst eventuelle Konflikte.

#### Hysterese (Schaltvermeidung bei pendelnden Werten)

Damit der Lüfter bei um die Schwellwerte pendelnden Messwerten nicht ständig zwischen den Stufen hin- und herspringt, werden separate Hysterese-Werte verwendet:

| Parameter | Default | Zweck |
| :--- | :--- | :--- |
| `humidity_hysteresis` | 3 (% rF) | Differenz für Ein-/Ausschalten an `humidity_delta_low` und `humidity_delta_high` |
| `voc_hysteresis` | 10 (Punkte) | Differenz für Ein-/Ausschalten an `voc_index_low` und `voc_index_high` |

**Beispiel Feuchte:** `humidity_delta_low = 10`, `humidity_hysteresis = 3`
- Überschreitung Baseline + 10 % → Mittel-Stufe EIN
- Unterschreitung Baseline + 7 % (10 − 3) → Mittel-Stufe AUS (zurück zu Niedrigst)

Die Hysterese-Werte sind als ESPHome `number`-Komponenten (MQTT-adjustierbar, `restore_value: true`) implementiert. Konkrete Topics siehe [KI-Referenz](#-für-ki-agenten--ai-agents-metadaten).

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

### 🛡 Sensor-Ausfall-Erkennung (Fail-Safe)

Fällt ein Sensor aus (keine I2C-Antwort, `NaN`-Werte), schaltet das System in einen sicheren Zustand, um Schimmelbildung durch unerkannte Feuchtigkeit zu verhindern:

| Ausfall | Verhalten |
| :--- | :--- |
| **DHT20 ausgefallen** | Feuchte-Trigger sind nicht auswertbar. Der Lüfter läuft dauerhaft auf **Mittel-Stufe** (Relais 1 EIN, Relais 2 AUS), bis der Sensor wieder gültige Werte liefert. Die VOC-basierte Geruchserkennung arbeitet normal weiter. |
| **SGP40 ausgefallen** | Geruchs-Trigger sind nicht auswertbar. Die Feuchte-basierte Steuerung arbeitet normal weiter. VOC-Trigger werden ignoriert, der VOC-Status wird als „unbekannt" gemeldet. |
| **Beide Sensoren ausgefallen** | Lüfter läuft dauerhaft auf **Voller Stärke** (Relais 1 EIN, Relais 2 EIN) – Worst-Case-Absicherung. |

Die Sensor-Gesundheit wird auf MQTT gemeldet (Topics siehe [KI-Referenz](#-für-ki-agenten--ai-agents-metadaten)).

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
- Ist das Bad über längere Zeit unbewohnt (Abwesenheit), startet alle **30 Minuten** (konfigurierbar via `sniff_interval`) ein **Schnüffel-Intervall**.
- **Timer-Logik:** Der 30-Min-Timer wird jedes Mal zurückgesetzt, wenn der Lüfter aktiv läuft (Voll- oder Mittel-Stufe). Erst wenn 30 Minuten lang gar keine Lüfteraktivität stattfand, wird der nächste Schnüffel ausgelöst. Dadurch wird vermieden, dass kurz nach einer aktiven Entlüftung unnötig geschnüffelt wird – die Luft ist dann ohnehin frisch.
- Der Lüfter schaltet sich für **1 Minute** mit **Relais 1 EIN und Relais 2 AUS (NC)** zu (3 µF über Kaskade → **Niedrigst-Stufe**), um frische Raumluft an die Sensoren zu führen.
- Signalisieren die Sensoren während dieser Minute, dass Feuchtigkeit oder Gerüche vorhanden sind (z. B. Feuchtigkeit kriecht nachträglich aus nassen Handtüchern), schaltet der Lüfter sofort hoch auf **Volle Stärke** und bleibt aktiv, bis das Bad wieder komplett trocken/geruchsfrei ist. Andernfalls schaltet er sich nach der Minute wieder ab.

## 📡 OpenHAB & MQTT-Schnittstelle

Das Projekt ist für die maximale Transparenz im Smart Home konzipiert. Der Mikrocontroller publiziert **jeden internen Zustand** fortlaufend auf dem MQTT-Broker:

1. **Sensordaten (Telemetrie):** Temperatur, relative Luftfeuchtigkeit, die berechnete Feuchte-Baseline sowie der SGP40 VOC-Index werden zyklisch gesendet.
2. **Binär-Zustände:** Der aktuelle Status des Lichtschalters (Präsenz) wird in Echtzeit übertragen.
3. **Aktor-Zustände:** Der effektive Schaltzustand wird als **Halbe-Stufe** (Relais 1 EIN, Relais 2 AUS/NC) bzw. **Volle-Stufe** (Relais 1 EIN, Relais 2 EIN/NO) oder **Aus** (Relais 1 AUS) gemeldet. Die Information, ob die Halbe-Stufe je nach Lichtzustand (LH) als Niedrigst- oder Mittel-Stärke wirkt, wird auf dieser Ebene bewusst nicht unterschieden – sie ist allein aus der Kombination mit dem Lichtzustand ableitbar.
4. **Zustandsmaschine:** Der aktuell aktive Modus der internen Logik (z. B. *Auto*, *Schnüffeln*, *Nachlauf*, *Manuell*) wird als String übertragen.

### Manueller Override via MQTT
Über MQTT kann der Betriebsmodus manuell überschrieben werden – nützlich z. B. für OpenHAB-Regeln oder manuelle Eingriffe. Der Modus wird per Topic gesetzt (`AUTO`, `OFF`, `HALF`, `FULL`) und der aktuelle Modus zurückgemeldet. Im manuellen Modus ist die Automatik suspendiert. Der Zustand *Manuell* wird im Zustandsmaschinen-String gemeldet. Konkrete Topics siehe [KI-Referenz](#-für-ki-agenten--ai-agents-metadaten).

### Home Assistant Auto-Discovery
Durch `mqtt.discovery: true` (in den ESPHome `mqtt:`-Einstellungen) werden alle Entitäten – Sensoren, Relais-Schalter, Schwellenwerte (`number`), Modus-Wahl (`select`) – automatisch in Home Assistant als Gerät angelegt. Keine manuelle YAML-Konfiguration nötig. Auch für OpenHAB sind die MQTT-Topics durch die ESPHome-Namenskonvention direkt verwendbar.

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
*Diese Sektion ist die **maschinenlesbare Implementierungs-Referenz**. Sie enthält alle konkreten ESPHome-Komponenten, MQTT-Topics, Parameter, Formeln und Lambda-Hinweise, die ein KI-Agent zur Code-Generierung benötigt. Die konzeptionelle Beschreibung der Logik steht in den Abschnitten oberhalb.*
- **Framework:** ESPHome (getestet mit **2026.7.4**; CLI z. B. via `uvx esphome ...` – kein lokales venv erforderlich)
- **Plattformen:** `esp8266`, `esp32` (Multi-Architektur bereit). Board: `d1_mini` (Wemos D1 Mini) oder `nodemcuv2` (NodeMCU).
- **Infrastruktur:** `wifi:` mit `ssid`/`password` via `!secret` und Fallback-AP (`ap:`), `captive_portal:` (WLAN-Neukonfiguration im AP-Modus via `http://192.168.4.1`), `i2c:` (SDA=GPIO4/D2, SCL=GPIO5/D1, interne Pullups ausreichend), `api:` (für OTA und Dashboard), `logger:` (für Debugging), `ota:` mit **`- platform: esphome`** (Port 8266, `password` via `!secret`). **⚠️ ESPHome ≥ 2026:** `ota:` ist eine Multi-Platform-Komponente und benötigt zwingend den `- platform:`-Eintrag (sonst `ota.unknown: 'ota' requires a 'platform' key`).
- **Kommunikation:** MQTT (OpenHAB-optimiert, Zustand via JSON/Templates, `mqtt.discovery: true`)
- **Sensoren:** `aht10` (Konfiguration für DHT20 mit **`variant: AHT20`**, `update_interval: 60s`). **⚠️ ESPHome ≥ 2026:** Die Option heißt `variant:` (nicht mehr `model:`); DHT20 = AHT20 im DHT-Gehäuse → `variant: AHT20`. Zusätzlich `sgp4x` (I2C-Bus, `update_interval: 60s`, interner 1Hz-Treiber). SGP40 mit `compensation`-Block (temperature_source + humidity_source vom DHT20), `store_baseline: true` (Standard, Baseline-Persistenz über Stromausfall).
- **VOC-Index:** Direkte Verwendung des SGP40-Rohwerts (Skala 1–500, 100 = 24h-Durchschnitt). Kein externer EMA nötig. Absolute Schwellwerte als `number`-Components `voc_index_low` (default 150) und `voc_index_high` (default 200).
- **Feuchte-Baseline:** Extrem langsamer EMA auf dem DHT20-Feuchtewert. Bildet die saisonale Umgebungsfeuchte ab. `globals` mit `restore_value: true`. Alpha als `number`-Component `humidity_ema_alpha` (default $0{,}0005$, Zeitkonstante ~Stunden) MQTT-adjustierbar. Die Feuchte-Trigger arbeiten als Delta zu dieser Baseline (`humidity_delta_low`, `humidity_delta_high`), nicht als absolute Werte.
- **Hysterese:** Eigene `number`-Parameter `humidity_hysteresis` (default 3 %rF) und `voc_hysteresis` (default 10 Punkte). Lambda-Logik verwendet `on_value_range`-Prinzip: Einschalten bei Überschreiten des Schwellwerts, Ausschalten erst bei Unterschreiten von `Schwellwert - Hysterese`. Verhindert Flattern bei pendelnden Messwerten.
- **Schnüffel-Timer:** `sniff_interval` (default 30 min) als `number`-Component. Timer wird bei jeder aktiven Lüfterstufe (Voll/Mittel) zurückgesetzt. Erst nach 30 min Inaktivität wird der nächste Schnüffel ausgelöst.
- **Inputs:** `binary_sensor` (GPIO für Licht/LH, active-high, sofortige Erkennung auf Sensorebene)
- **Outputs:** 2x `switch.gpio` (active-high, `restore_mode: RESTORE_DEFAULT_OFF`) – Relais 1 (Name `"Relay 1 (Ein-Aus)"`, schaltet Dauerphase L auf Relais 2 durch), Relais 2 (Name `"Relay 2 (Wechsel)"`, Wechselschalter: NC=3 µF Kondensator, NO=direkt). Kein Interlock nötig (Kaskade verhindert hardware-seitig Kurzschlüsse). **⚠️ ESPHome ≥ 2026:** Entity-Namen dürfen kein `/` enthalten (wird als URL-Separator gewertet → Warnung mit automatischem Ersatz).
- **Passive Beschaltung:** LH (geschaltete Lampenphase) → 3 µF Kondensator (fest verdrahtet) → Lüfter L – ermöglicht Niedrigst-Stufe ohne aktiven Relais-Eingriff bei eingeschaltetem Licht.
- **Logik-Kern:** `interval:`-Component mit 1s-Takt. Die Zustandsmaschine (C++ Lambda) liest in jedem Takt die zuletzt gepufferten Sensorwerte (`id(sensor).state`), wertet die Prioritätentabelle aus und setzt die Relais. Die Sensoren selbst laufen mit eigenem `update_interval` – DHT20: 60s, SGP40: 60s (SGP40 interner 1Hz-Treiber läuft unabhängig).
- **Abwesenheits-Regel:** `light_switch == false` konvertiert jeden Schwellenwert-Trigger (`low` und `high`) direkt in den maximalen Output-Zustand (`relay1 = true, relay2 = true` → direkte Phase).
- **Nachlauf bei Licht AUS:** Relais 1 wird für 1 min eingeschaltet, Relais 2 bleibt AUS (NC, 3 µF) – auch bei niedrigen Messwerten, um die Sensoren weiter mit Messluft zu versorgen.
- **Schwellenwerte via MQTT (ohne Firmware-Update):** Die Schwellenwerte (`humidity_delta_low` default 10, `humidity_delta_high` default 20, `voc_index_low` default 150, `voc_index_high` default 200), EMA-Alpha (`humidity_ema_alpha` default 0.0005), Hysterese-Werte (`humidity_hysteresis`, `voc_hysteresis`) sowie Zeitkonstanten (`sniff_interval` default 30 min, `nachlauf_duration` default 1 min) werden als ESPHome `number`-Komponenten mit `restore_value: true` implementiert. Dadurch sind sie:
  - **Über MQTT auslesbar:** Jeder `number` publiziert automatisch seinen Zustand (z. B. `bathvent/number/humidity_delta_low/state`)
  - **Über MQTT setzbar:** Per `bathvent/number/humidity_delta_low/set` <Wert> – ohne OTA-Update oder Neustart
  - **Stromausfall-sicher:** `restore_value: true` speichert den zuletzt gesetzten Wert im Flash (NVS/Preferences), sodass er nach einem Neustart erhalten bleibt
- **Manueller Override:** `select`-Component mit MQTT-Topics `bathvent/select/mode/set` (Werte: `AUTO`, `OFF`, `HALF`, `FULL`). Im manuellen Modus ist die Automatik suspendiert. **⚠️ ESPHome ≥ 2026:** Zugriff auf die aktive Option im Lambda über `id(operation_mode).current_option()` (`.state` wurde entfernt; Rückgabe ist `StringRef`, Vergleich mit String-Literal möglich).
- **Sensor-Fail-Safe:** DHT20-Ausfall → Dauer-Mittelstufe. SGP40-Ausfall → VOC-Trigger ignoriert. Beide ausgefallen → Dauer-Vollstufe. Sensor-Status als `binary_sensor` auf MQTT (`dht20_status`, `sgp40_status`).
- **Lizenz:** MIT

---

## 🏗 Installation & Setup
1. **ESPHome:** ESPHome-Umgebung vorbereiten – getestet mit **ESPHome 2026.7.x**. Ohne lokales venv, z. B. via `uvx esphome ...` (oder `uv tool install esphome`).
2. **Konfiguration:** Die `bathvent.yaml` im Repository als Basis nutzen.
3. **Plattform wählen:** - Für **ESP8266**: `esp8266: board: d1_mini` (Wemos D1 Mini) oder `nodemcuv2` (NodeMCU).
   - Für **ESP32**: `esp32: board: esp32dev` verwenden und die Pins anpassen.
4. **Anpassung:** WLAN- und MQTT-Zugangsdaten in `secrets.yaml` eintragen (ist via `.gitignore` ausgeschlossen); Schwellwerte sind zur Laufzeit per MQTT einstellbar.
5. **Deployment:** Erstes Flashen via USB: `uvx esphome run bathvent.yaml --device COMx` – danach Updates per OTA: `uvx esphome run bathvent.yaml`. *(Windows-Tipp: Bei CH340-Fehler `Error 31 (device not functioning)` älteren CH340-Treiber v3.5.2019.1 verwenden und Windows-Driver-Updates blockieren.)*

---

## 📄 Lizenz
Dieses Projekt ist unter der **MIT-Lizenz** lizenziert. Siehe `LICENSE` für Details.

---
*Entwickelt für ein optimales, schimmelfreies Badklima bei maximalem akustischen Komfort. Feedback, Forks und Pull Requests sind herzlich willkommen!*
