#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["schemdraw>=0.23", "matplotlib"]
# ///
"""Circuit diagram for bathvent (Kaskaden-Schaltung der Relais).

Source of truth: README.md (BOM + Verdrahtungstabellen).

Layout:
  - Links:  Anschlüsse L (Dauerphase), LH (Lampenphase), N
  - Mitte:  Relais-Kaskade K1 (Master), K2 (Full), K3 (LowMid),
            NTC-Heißleiter, 3 µF / 5 µF Kondensatoren
  - Rechts: Lüfter (Spaltpolmotor) mit RC-Glied parallel
  - Unten:  Steuerteil (DC): Netzteil, Optokoppler, Relais-Module,
            ESP8266, Sensoren (DHT20/SGP40)

Anschlussnamen = README (IN/VCC/GND/OUT/SDA/SCL/VIN/5V...).
Versorgung: 5 V / 3,3 V als Vdd-Pfeil OBEN, GND als Erdungszeichen UNTEN
(an jedem IC/Modul; wird in ic() automatisch erzeugt).

Output: docs/circuit-diagram.svg (+ docs/circuit-diagram.png via Matplotlib)

Aufruf (uv run, beliebiges Verzeichnis; Dependencies per PEP 723 inline):
  uv run circuit-diagram.schemdraw.py        # aus docs/
  uv run docs/circuit-diagram.schemdraw.py   # aus dem Projekt-Root

Hinweis: `uvx` startet Tools (uv tool run), `uv run` startet Skripte --
fuer PEP-723-Skripte ist `uv run` der richtige Befehl.
"""

from pathlib import Path

import schemdraw
import schemdraw.elements as elm

# Ausgabeverzeichnis = Verzeichnis dieses Skripts (docs/), unabhaengig vom CWD,
# damit der Aufruf per uvx aus dem docs-Ordner oder aus dem Projekt-Root funktioniert.
OUT_DIR = Path(__file__).resolve().parent

UNIT = 2.0

# ---- helpers ----------------------------------------------------------------

def route(d, *pts):
    """Draw orthogonal polyline through the given (x, y) points."""
    for a, b in zip(pts, pts[1:]):
        d += elm.Line().at(a).to(b)

def dot(d, x, y):
    d += elm.Dot().at((x, y))


with schemdraw.Drawing(show=False) as d:
    d.config(unit=UNIT, fontsize=11)

    # =====================================================================
    #  230 V LEISTUNGSTEIL  (links -> rechts)
    # =====================================================================

    # ---- linke Anschlüsse (Leitungen kommen nur von rechts) ----
    d += elm.Dot(open=True).at((0, 12)).label('L', loc='top')
    d += elm.Dot(open=True).at((0, 3)).label('LH', loc='top')
    d += elm.Dot(open=True).at((0, 6)).label('N', loc='top')
    d += elm.Label().at((-0.4, 13.6)).label('230 V Netz', loc='right')

    # ---- Relais-Kontakte (Kaskade) ----
    # COM-Pegel y = 12,  Umschaltkontakte: b = NO (oben), c = NC (unten)
    k1 = d.add(elm.SwitchSpdt2().anchor('a').at((5, 12)).label('K1\nMaster', loc='top'))
    k2 = d.add(elm.SwitchSpdt2().anchor('a').at((11, 12)).label('K2\nFull', loc='top'))
    k3 = d.add(elm.SwitchSpdt2().anchor('a').at((17, 12)).label('K3\nLowMid', loc='top'))
    d += elm.Label().at((11, 16.0)).label(
        'Kontakte: oben = NO (angezogen), unten = NC (abgefallen)', loc='center')

    # L -> K1 COM; K1 NO -> K2 COM; K2 NO -> K3 COM  (NO-Kette, oben)
    route(d, (0, 12), (k1.absanchors['a'].x, k1.absanchors['a'].y))
    route(d, (k1.absanchors['b'].x, k1.absanchors['b'].y),
             (k2.absanchors['a'].x, k1.absanchors['b'].y),
             (k2.absanchors['a'].x, k2.absanchors['a'].y))
    route(d, (k2.absanchors['b'].x, k2.absanchors['b'].y),
             (k3.absanchors['a'].x, k2.absanchors['b'].y),
             (k3.absanchors['a'].x, k3.absanchors['a'].y))

    # ---- Ausgänge zur Kondensator-/NTC-Bank (Lüfter-L-Sammelschiene) ----
    BUS_X = 23.0     # Lüfter-L-Sammelschiene
    FAN_X = 27.0     # Lüfter
    SNUB_X = 30.0    # RC-Glied (rechts vom Lüfter, parallel zum Motor)

    # K2 NC (voll/direkt) -> NTC -> Lüfter L
    k2c = k2.absanchors['c']
    route(d, (k2c.x, k2c.y), (k2c.x, 10.2))
    ntc = d.add(elm.Thermistor().at((k2c.x, 10.2)).to((17.5, 10.2))
                .label('NTC\n10 Ω', loc='bottom'))
    route(d, (17.5, 10.2), (BUS_X, 10.2))

    # K3 NO (mid) -> 5 µF -> Lüfter L   (direkt am NO-Kontakt)
    k3b = k3.absanchors['b']
    cap_mid = d.add(elm.Capacitor2().at((k3b.x, k3b.y)).to((22.0, k3b.y))
                    .label('5 µF', loc='top'))
    route(d, (22.0, k3b.y), (BUS_X, k3b.y))

    # K3 NC (low) -> 3 µF -> Lüfter L   (direkt am NC-Kontakt)
    k3c = k3.absanchors['c']
    cap_low = d.add(elm.Capacitor2().at((k3c.x, k3c.y)).to((22.0, k3c.y))
                    .label('3 µF', loc='bottom'))
    route(d, (22.0, k3c.y), (BUS_X, k3c.y))

    # Lüfter-L-Sammelschiene (vertikal, deckt alle Ausgänge ab)
    y_bus1 = 12.8
    route(d, (BUS_X, 10.2), (BUS_X, y_bus1))
    dot(d, BUS_X, 10.2)
    dot(d, BUS_X, k3c.y)
    dot(d, BUS_X, k3b.y)
    dot(d, BUS_X, y_bus1)

    # ---- Lüfter (Motor) rechts, vertikal: oben = L, unten = N ----
    fan = d.add(elm.Motor().up().at((FAN_X, 10)))
    fan_top = (fan.absanchors['end'].x, fan.absanchors['end'].y)      # L (oben)
    fan_bot = (fan.absanchors['start'].x, fan.absanchors['start'].y)  # N (unten)
    d += elm.Label().at((24.0, 10.0)).label('Lüfter\n(Spaltpol-\nmotor)', loc='right')

    # Lüfter L: Sammelschiene -> rechts -> oben zum Motor
    route(d, (BUS_X, y_bus1), (fan_top[0], y_bus1), (fan_top[0], fan_top[1]))
    # Lüfter N: N-Schiene -> rechts -> unten zum Motor
    route(d, (fan_bot[0], 6), (fan_bot[0], fan_bot[1]))
    dot(d, fan_bot[0], 6)

    # ---- N-Schiene (y = 6, bis unter das RC-Glied) ----
    route(d, (0, 6), (SNUB_X, 6))

    # ---- RC-Glied (R + C in Reihe) parallel zum Lüfter ----
    # Oben am Lüfter-L (Motor L), unten an die N-Schiene (Motor N)
    route(d, fan_top, (SNUB_X, fan_top[1]))
    d.add(elm.ResistorIEC().down().at((SNUB_X, fan_top[1])).to((SNUB_X, fan_top[1] - 2.4)))
    d.add(elm.Capacitor2().down().at((SNUB_X, fan_top[1] - 2.4)).to((SNUB_X, fan_top[1] - 4.8)))
    route(d, (SNUB_X, fan_top[1] - 4.8), (SNUB_X, 6.0))
    dot(d, SNUB_X, fan_top[1])
    dot(d, SNUB_X, 6.0)
    d += elm.Label().at((SNUB_X + 0.8, fan_top[1] - 1.2)).label('100 Ω', loc='left')
    d += elm.Label().at((SNUB_X + 0.8, fan_top[1] - 3.6)).label('0,1 µF', loc='left')
    d += elm.Label().at((SNUB_X + 0.8, fan_top[1] + 0.6)).label('RC-Glied\n(parallel)', loc='left')

    # =====================================================================
    #  STEUERTEIL (DC) — Versorgung OBEN (Vdd-Pfeil) / GND UNTEN (Erdung)
    #  Die Versorgungs-Fahnen werden in ic() automatisch erzeugt.
    # =====================================================================
    d += elm.Label().at((2.5, 0.4)).label('Steuerung (5 V DC)', loc='center')

    def ic(d, x, y, label, left=(), right=(), top=(), bottom=(), size=None):
        """IC/Modul: Signal-Pins links/rechts, Versorgung (top) OBEN mit
        Vdd-Fahne, GND (bottom) UNTEN mit Erdungszeichen — automatisch.
        top:    Liste von (Pinname, Fahnen-Label, Farbe)  z.B. ('VCC','5 V','red')
        bottom: Liste von Pinnamen z.B. 'GND'
        left/right: Signal-Pins, str oder (name, anchorname)."""
        def mk(it, side):
            if isinstance(it, (tuple, list)):
                return elm.IcPin(name=it[0], anchorname=it[1], side=side, lblsize=9)
            return elm.IcPin(name=it, side=side, lblsize=9)
        pins = ([mk(it, 'left') for it in left]
                + [mk(it, 'right') for it in right]
                + [elm.IcPin(name=t[0], side='top', lblsize=9) for t in top]
                + [elm.IcPin(name=b if isinstance(b, str) else b[0], side='bottom', lblsize=9)
                   for b in bottom])
        ic_ = d.add(elm.Ic(pins=pins, size=size)
                    .label(label, fontsize=10).right().at((x, y)))
        for t in top:
            vflag(d, P(ic_, t[0]), t[1], t[2])
        for b in bottom:
            gflag(d, P(ic_, b if isinstance(b, str) else b[0]))
        return ic_

    def P(ic, name):
        a = ic.absanchors[name]
        return (a.x, a.y)

    def vflag(d, xy, label, color):
        """Versorgungssymbol: Pfeil nach oben + Spannungswert."""
        x, y = xy
        d += elm.Line().at((x, y)).to((x, y + 0.7)).color(color)
        d += elm.Vdd().at((x, y + 0.7)).label(label, loc='top').color(color)

    def gflag(d, xy):
        """Gemeinsames Masse-Symbol (Erdungszeichen)."""
        x, y = xy
        d += elm.Line().at((x, y)).to((x, y - 0.7))
        d += elm.Ground().at((x, y - 0.7))

    # ---- Bauteile ----
    # Relais-Module: IN rechts (zur ESP), VCC oben (5 V), GND unten
    k1d = ic(d, 5, -2, 'K1\nRelais-Modul', right=['IN'],
             top=[('VCC', '5 V', 'red')], bottom=['GND'])
    k2d = ic(d, 11, -2, 'K2\nRelais-Modul', right=['IN'],
             top=[('VCC', '5 V', 'red')], bottom=['GND'])
    k3d = ic(d, 17, -2, 'K3\nRelais-Modul', right=['IN'],
             top=[('VCC', '5 V', 'red')], bottom=['GND'])

    # Optokoppler (Licht): LH/N links (AC), OUT rechts (zur ESP),
    # VCC oben, GND unten
    opto = ic(d, 1.5, -9, 'Optokoppler\n(Licht)',
              ['LH', 'N'], right=['OUT'],
              top=[('VCC', '5 V', 'red')], bottom=['GND'],
              size=(3.6, 3.0))

    # Netzteil 5 V DC: L/N links (AC), 5 V oben, GND unten
    psu = ic(d, 1.5, -15, 'Netzteil\n5 V DC',
             ['L', 'N'], top=[('5 V', '5 V', 'red')], bottom=['GND'],
             size=(3.6, 2.6))

    # ESP8266: GPIO links (von Relais/Optokoppler), SDA/SCL rechts (Sensoren),
    # VIN + 3V3 oben, GND unten
    esp = ic(d, 24, -4.5, 'ESP8266\nD1 Mini',
             [('GPIO12 (D6)', 'GPIO12 (D6)'), ('GPIO14 (D5)', 'GPIO14 (D5)'),
              ('GPIO16 (D0)', 'GPIO16 (D0)'), ('GPIO13 (D7)', 'GPIO13 (D7)')],
             [('SDA', 'GPIO4 (D2) SDA'), ('SCL', 'GPIO5 (D1) SCL')],
             top=[('VIN', '5 V', 'red'), ('3V3', '3,3 V', 'green')],
             bottom=['GND'],
             size=(5.6, 3.6))

    # Sensoren (I2C): rechts neben der ESP; DHT20 oben, SGP40 darunter;
    # SDA/SCL links (zur ESP), VCC oben (3,3 V), GND unten
    dht = ic(d, 31.5, -4.5, 'DHT20', ['SDA', 'SCL'],
             top=[('VCC', '3,3 V', 'green')], bottom=['GND'])
    sgp = ic(d, 31.5, -10.5, 'SGP40', ['SDA', 'SCL'],
             top=[('VCC', '3,3 V', 'green')], bottom=['GND'])

    # ---- Relais/Opto Signale -> ESP8266 (Lanes mit gutem Abstand) ----
    # Lane-Höhen (oben -> unten): D0 (-1.0), D5 (-3.5), D6 (-4.7);
    # Opto -> D7 läuft OBERHALB der Relais (y = 2.8) zur obersten ESP-GPIO.
    # K1 (Master) IN -> ESP GPIO14 (D5)
    route(d, P(k1d, 'IN'), (P(k1d, 'IN')[0] + 0.5, P(k1d, 'IN')[1]),
          (P(k1d, 'IN')[0] + 0.5, -3.5), (21.5, -3.5),
          (21.5, P(esp, 'GPIO14 (D5)')[1]), P(esp, 'GPIO14 (D5)'))
    # K2 (Full) IN -> ESP GPIO12 (D6)
    route(d, P(k2d, 'IN'), (P(k2d, 'IN')[0] + 0.5, P(k2d, 'IN')[1]),
          (P(k2d, 'IN')[0] + 0.5, -4.7), (20.5, -4.7),
          (20.5, P(esp, 'GPIO12 (D6)')[1]), P(esp, 'GPIO12 (D6)'))
    # K3 (LowMid) IN -> ESP GPIO16 (D0)
    route(d, P(k3d, 'IN'), (P(k3d, 'IN')[0] + 0.5, P(k3d, 'IN')[1]),
          (P(k3d, 'IN')[0] + 0.5, -1.0), (22.5, -1.0),
          (22.5, P(esp, 'GPIO16 (D0)')[1]), P(esp, 'GPIO16 (D0)'))
    # Optokoppler OUT -> ESP GPIO13 (D7): rechts hoch, oben lang, dann runter
    route(d, P(opto, 'OUT'), (9.0, P(opto, 'OUT')[1]), (9.0, 2.8),
          (23.2, 2.8), (23.2, P(esp, 'GPIO13 (D7)')[1]), P(esp, 'GPIO13 (D7)'))
    # GPIO-Beschriftung der Lanes (unterscheidbar machen)
    d += elm.Label().at((10.5, -3.2)).label('D5', loc='center')
    d += elm.Label().at((16.5, -4.4)).label('D6', loc='center')
    d += elm.Label().at((21.3, -0.7)).label('D0', loc='center')
    d += elm.Label().at((10.5, 3.2)).label('D7', loc='center')

    # ---- ESP8266 SDA / SCL -> I2C (zwei vertikale Bus-Säulen links der
    #      Sensoren; DHT20 oben, SGP40 darunter) ----
    X_SDA = 30.5   # SDA-Säule (links)
    X_SCL = 30.9   # SCL-Säule (rechts)
    # ESP -> Säulen
    route(d, P(esp, 'GPIO4 (D2) SDA'), (X_SDA, P(esp, 'GPIO4 (D2) SDA')[1]))
    route(d, P(esp, 'GPIO5 (D1) SCL'), (X_SCL, P(esp, 'GPIO5 (D1) SCL')[1]))
    # Säulen (vertikal)
    route(d, (X_SDA, P(dht, 'SDA')[1]), (X_SDA, P(sgp, 'SDA')[1]))          # SDA
    route(d, (X_SCL, P(esp, 'GPIO5 (D1) SCL')[1]), (X_SCL, P(sgp, 'SCL')[1]))  # SCL
    # Verbindungspunkte
    dot(d, X_SDA, P(esp, 'GPIO4 (D2) SDA')[1])
    dot(d, X_SCL, P(esp, 'GPIO5 (D1) SCL')[1])
    dot(d, X_SDA, P(dht, 'SDA')[1])
    dot(d, X_SCL, P(dht, 'SCL')[1])
    dot(d, X_SDA, P(sgp, 'SDA')[1])
    dot(d, X_SCL, P(sgp, 'SCL')[1])
    # Sensoren -> Säulen (kurze Stubs)
    route(d, (X_SDA, P(dht, 'SDA')[1]), P(dht, 'SDA'))
    route(d, (X_SCL, P(dht, 'SCL')[1]), P(dht, 'SCL'))
    route(d, (X_SDA, P(sgp, 'SDA')[1]), P(sgp, 'SDA'))
    route(d, (X_SCL, P(sgp, 'SCL')[1]), P(sgp, 'SCL'))
    d += elm.Label().at((X_SDA - 0.4, -3.1)).label('SDA', loc='right')
    d += elm.Label().at((X_SCL - 0.4, -1.0)).label('SCL', loc='right')

    # ---- AC -> DC Zuführungen (nur von rechts, ohne linke Gasse) ----
    # Optokoppler: LH und N (linke Pins bei x=1.0, von der rechten Seite anfahren)
    route(d, (0, 3), (0.5, 3), (0.5, P(opto, 'LH')[1]), P(opto, 'LH'))
    route(d, (0.8, 6), (0.8, P(opto, 'N')[1]), P(opto, 'N'))
    dot(d, 0.8, 6)
    # Netzteil: L und N (vom Netz / von der N-Schiene)
    route(d, (0.3, 12), (0.3, P(psu, 'L')[1]), P(psu, 'L'))
    dot(d, 0.3, 12)
    route(d, (0.1, 6), (0.1, P(psu, 'N')[1]), P(psu, 'N'))
    dot(d, 0.1, 6)

svg = OUT_DIR / 'circuit-diagram.svg'
png = OUT_DIR / 'circuit-diagram.png'

d.save(str(svg))
print(f'saved {svg}')

# PNG (Matplotlib-Backend) als Schnellansicht / für die README (weißer Hintergrund)
d.save(str(png), transparent=False, dpi=200)
print(f'saved {png}')
