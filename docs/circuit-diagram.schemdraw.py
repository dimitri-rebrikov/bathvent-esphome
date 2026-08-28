#!/usr/bin/env python3
# /// script
# requires-python = ">=3.10"
# dependencies = ["schemdraw>=0.23"]
# ///
"""Circuit diagram for bathvent (Kaskaden-Schaltung der Relais).

Source of truth: README.md (BOM + Verdrahtungstabellen).

Layout:
  - Links:  Anschlüsse L (Dauerphase), LH (Lampenphase), N
  - Mitte:  Relais-Kaskade K1 (Master), K2 (Full), K3 (LowMid),
            NTC-Heißleiter, 3 µF / 5 µF Kondensatoren
  - Rechts: Lüfter (Spaltpolmotor) mit RC-Glied parallel

Output: docs/circuit-diagram.svg
"""

import schemdraw
import schemdraw.elements as elm

UNIT = 2.0

# ---- helpers ----------------------------------------------------------------

def route(d, *pts):
    """Draw orthogonal-ish polyline through the given (x, y) points."""
    for a, b in zip(pts, pts[1:]):
        d += elm.Line().at(a).to(b)

def dot(d, x, y):
    d += elm.Dot().at((x, y))


with schemdraw.Drawing(show=False) as d:
    d.config(unit=UNIT, fontsize=11)

    # =====================================================================
    #  230 V LEISTUNGSTEIL  (links -> rechts)
    # =====================================================================

    # ---- linke Anschlüsse ----
    d += elm.Dot(open=True).at((0, 12)).label('L', loc='left')
    d += elm.Dot(open=True).at((0, 6)).label('LH', loc='left')
    d += elm.Dot(open=True).at((0, 0)).label('N', loc='left')
    d += elm.Label().at((-0.4, 13.6)).label('230 V Netz', loc='right')

    # ---- Relais-Kontakte (Kaskade) ----
    # COM-Pegel y = 12,  Umschaltkontakte: b = NO (oben), c = NC (unten)
    k1 = d.add(elm.SwitchSpdt2().anchor('a').at((5, 12)).label('K1\nMaster', loc='top'))
    k2 = d.add(elm.SwitchSpdt2().anchor('a').at((11, 12)).label('K2\nFull', loc='top'))
    k3 = d.add(elm.SwitchSpdt2().anchor('a').at((17, 12)).label('K3\nLowMid', loc='top'))
    d += elm.Label().at((11, 16.0)).label(
        'Kontakte: oben = NO (angezogen), unten = NC (abgefallen)', loc='center')

    # ---- L -> K1 -> K2 -> K3 (NO-Kette, oben) ----
    route(d, (0, 12), (k1.absanchors['a'].x, k1.absanchors['a'].y))          # L -> K1 COM
    route(d, (k1.absanchors['b'].x, k1.absanchors['b'].y),                    # K1 NO -> K2 COM
             (k2.absanchors['a'].x, k1.absanchors['b'].y),
             (k2.absanchors['a'].x, k2.absanchors['a'].y))
    route(d, (k2.absanchors['b'].x, k2.absanchors['b'].y),                    # K2 NO -> K3 COM
             (k3.absanchors['a'].x, k2.absanchors['b'].y),
             (k3.absanchors['a'].x, k3.absanchors['a'].y))

    # ---- Ausgänge zur Kondensator-/NTC-Bank ----
    BUS_X = 23.0     # Lüfter-L-Sammelschiene
    SNUB_X = 24.5    # RC-Glied (parallel zum Lüfter)
    FAN_X = 27.0     # Lüfter

    # K2 NC (voll/direkt) -> NTC -> Lüfter L (unten geführt, y = 10.2)
    route(d, (k2.absanchors['c'].x, k2.absanchors['c'].y),
             (k2.absanchors['c'].x, 10.2))
    ntc = d.add(elm.Thermistor().at((k2.absanchors['c'].x, 10.2)).to((17.5, 10.2))
                .label('NTC\n10 Ω', loc='bottom'))
    route(d, (17.5, 10.2), (BUS_X, 10.2))

    # K3 NO (mid) -> 5 µF -> Lüfter L (oben, y = 12.8)
    cap_mid = d.add(elm.Capacitor2().at((k3.absanchors['b'].x, 12.8)).to((22.0, 12.8))
                    .label('5 µF', loc='top'))
    route(d, (22.0, 12.8), (BUS_X, 12.8))

    # K3 NC (low) -> 3 µF -> Lüfter L (Mitte, y = 11.2)
    cap_low = d.add(elm.Capacitor2().at((k3.absanchors['c'].x, 11.2)).to((22.0, 11.2))
                    .label('3 µF', loc='bottom'))
    route(d, (22.0, 11.2), (BUS_X, 11.2))

    # ---- Lüfterbus (vertikal) ----
    route(d, (BUS_X, 10.2), (BUS_X, 12.8))
    dot(d, BUS_X, 10.2)
    dot(d, BUS_X, 11.2)
    dot(d, BUS_X, 12.8)

    # ---- Lüfter (Motor) rechts, vertikal: oben = L, unten = N ----
    fan = d.add(elm.Motor().up().at((FAN_X, 6)))
    fan_top = (fan.absanchors['end'].x, fan.absanchors['end'].y)      # L (oben)
    fan_bot = (fan.absanchors['start'].x, fan.absanchors['start'].y)  # N (unten)
    d += elm.Label().at((FAN_X + 2.5, 6)).label('Lüfter\n(Spaltpol-\nmotor)', loc='left')

    # Lüfter L: Sammelschiene -> rechts -> oben zum Motor
    route(d, (BUS_X, 12.8), (fan_top[0], 12.8), (fan_top[0], fan_top[1]))
    # Lüfter N: N-Schiene -> rechts -> unten zum Motor
    route(d, (0, 0), (fan_bot[0], 0), (fan_bot[0], fan_bot[1]))

    # ---- N-Schiene (y = 0) ----
    route(d, (0, 0), (fan_bot[0], 0))
    dot(d, fan_bot[0], 0)

    # ---- RC-Glied parallel zum Lüfter (R + C in Reihe) ----
    route(d, (BUS_X, 12.8), (SNUB_X, 12.8))
    d.add(elm.ResistorIEC().up().at((SNUB_X, 12.8)).to((SNUB_X, 9.6)))
    d.add(elm.Capacitor2().up().at((SNUB_X, 9.6)).to((SNUB_X, 6.0)))
    route(d, (SNUB_X, 6.0), (SNUB_X, 0.0))
    dot(d, SNUB_X, 12.8)
    dot(d, SNUB_X, 0.0)
    d += elm.Label().at((SNUB_X + 0.9, 11.2)).label('100 Ω', loc='left')
    d += elm.Label().at((SNUB_X + 0.9, 7.8)).label('0,1 µF', loc='left')
    d += elm.Label().at((SNUB_X + 0.9, 4.8)).label('RC-Glied\n(parallel)', loc='left')

    # =====================================================================
    #  STEUERTEIL (DC) — Blöcke mit Netznamen an den Pins
    # =====================================================================
    d += elm.Label().at((-0.4, -2.2)).label('Steuerung (5 V DC)', loc='right')

    def ic(d, x, y, label, left, right):
        return d.add(elm.Ic(
            pins=[elm.IcPin(name=n, side='left') for n in left]
                 + [elm.IcPin(name=n, side='right') for n in right]
        ).label(label).at((x, y)))

    # Relais-Module (unter ihren Kontakten)
    ic(d, 5, -6, 'K1\nRelais-Modul', ['GPIO14 (D5)'], ['5 V', 'GND'])
    ic(d, 11, -6, 'K2\nRelais-Modul', ['GPIO12 (D6)'], ['5 V', 'GND'])
    ic(d, 17, -6, 'K3\nRelais-Modul', ['GPIO16 (D0)'], ['5 V', 'GND'])

    # Optokoppler (Lichterkennung); LH-Leitung kommt vom Anschluss
    opto = ic(d, 3, -13, 'Optokoppler\n(Licht)', ['LH', 'N'],
              ['GPIO13 (D7)', '5 V', 'GND'])
    route(d, (0, 6), (1.0, 6), (1.0, opto.absanchors['LH'].y),
             (opto.absanchors['LH'].x, opto.absanchors['LH'].y))

    # Netzteil
    ic(d, 10, -13, 'Netzteil\n5 V DC', ['L', 'N'], ['5 V', 'GND'])

    # ESP8266
    ic(d, 27, -8, 'ESP8266\nWemos D1 Mini',
       ['VIN (5 V)', 'GND', 'GPIO13 (D7)', 'GPIO14 (D5)', 'GPIO16 (D0)', 'GPIO12 (D6)'],
       ['GPIO4 (D2) SDA', 'GPIO5 (D1) SCL', '3V3'])

    # Sensoren
    ic(d, 18, -15, 'DHT20', ['SDA', 'SCL'], ['3V3', 'GND'])
    ic(d, 24, -15, 'SGP40', ['SDA', 'SCL'], ['3V3', 'GND'])

d.save('docs/circuit-diagram.svg')
print('saved docs/circuit-diagram.svg')
