// =============================================================================
// sensor_box.scad — kleines Lüftungsgehäuse für Sensoren (Schiebedeckel-Box)
// =============================================================================
// Gleiche Konstruktion wie bathvent_enclosure.scad (Schiebedeckel-Box) auf Basis
// der Bibliothek
//   ../../3d-models/schiebedeckel-box/schiebedeckel-box.scad  (benötigt BOSL2)
//
// Außenmaße: 47 × 19 × 21 mm (Breite × Tiefe × Höhe), Wandstärke 1 mm.
//
// Aussparungen (nur in der Kiste / box):
//   • Rückwand (-x): Kabel-Schlitz, 5 mm breit, von der Oberkante bis zur Mitte
//     (z = hoehe/2) für die Sensor-Kabel
//   • Lüftung: viele kleine Bohrungen durch ALLE Seiten, auch die Rückwand
//     (um den Kabel-Schlitz herum) — lange Seiten, beide Stirnseiten, Boden und
//     Deckel. Die Löcher (Ø 2,5 mm) sind klein genug für einen Druck ohne
//     Stützstrukturen; die Raster sind so verdichtet, dass zusätzliche Löcher auf
//     den Kreuzungspunkten zwischen den bestehenden Reihen sitzen (der Abstand
//     bleibt aber groß genug, damit die Löcher nicht verschmelzen).
//
// Hinweis: Die Zugkanten-Rundung der Bibliothek skaliert jetzt mit der
//   Wandstärke (min(1, wand/2)), sodass der Deckel auch bei wand = 1 mm
//   funktioniert. Für Modelle mit wand ≥ 2 bleibt alles unverändert.
//
// Rendern:
//   openscad -o sensor_box.stl  -D 'teil=1'  sensor_box.scad
//   openscad -o sensor_lid.stl  -D 'teil=2'  sensor_box.scad
// =============================================================================

include <BOSL2/std.scad>
$fn = 48;

use <../../3d-models/schiebedeckel-box/schiebedeckel-box.scad>

// ---- Teilauswahl (per -D übersteuerbar) ----
teil = 1;   // 1 = kiste, 2 = deckel

// ---- Maße (alle mm) ----
breite    = 47;    // Außenbreite (x-Richtung)
tiefe     = 19;    // Außentiefe  (y-Richtung)
hoehe     = 21;    // Außenhöhe   (z-Richtung)
wand      = 1;     // Wandstärke (auch Boden und Deckel)
nut_tiefe = 0.4;   // Führungsnuttiefe — muss < wand sein
spiel     = 0.4;   // Spiel Deckel/Nut

// ---- Deckel-Unterkante (Position der Deckel-Lüftung) ----
// boden + stauraum = hoehe - 2*wand - spiel/2  (gleiche Formel wie die Bibliothek)
deckel_unten = hoehe - 2 * wand - spiel / 2;    // 18.8

// ---- Kabelführung ----
// Schlitz in der Rückwand (-x), 5 mm breit, von der Oberkante bis zur Mitte
kabel_b = 5;            // Breite des Kabel-Schlitzes (y-Richtung)
kabel_t = hoehe / 2;    // Tiefe ab Oberkante bis zur Mitte

// ---- Lüftungslöcher ----
loch_d      = 2.5;      // Lochdurchmesser (supportfrei: klein)
loch_rand   = 4;        // Abstand der äußersten Lochreihen zu den Kanten

// Lochraster (symmetrisch, verdichtet: zusätzliche Löcher auf den
// Kreuzungspunkten zwischen den bestehenden Reihen)
x_raster  = [-18, -13.5, -9, -4.5, 0, 4.5, 9, 13.5, 18];
y_boden   = [-6, 0, 6];
y_deckel  = [-6, 0, 6];
z_laeng   = [5, 10, 15];
y_plusx   = [-4, 0, 4];      // +x-Seite: zusätzliche mittlere Reihe
z_plusx   = [5, 11, 16];
y_minusx  = [-6, 6];         // -x-Seite (Kabelführung): beidseitig des Schlitzes
z_minusx  = [5, 10, 15];

module kabel_schlitz() {
    // Schlitz durch die Rückwand (-x), von der Oberkante bis zur Mitte
    translate([ -breite/2, 0, hoehe - kabel_t ])
        cuboid([ 2*wand + 4, kabel_b, kabel_t + 2 ], anchor = BOTTOM);
}

// Löcher durch beide Längswände (y = ±tiefe/2), Achse in y-Richtung
module lueftung_laengswaende() {
    for (x = x_raster)
        for (z = z_laeng)
            translate([ x, 0, z ])
                cyl(d = loch_d, h = tiefe + 2, orient = BACK);
}

// Löcher durch die +x-Seite (Einschubseite), nur unterhalb des Einschubschlitzes
module lueftung_plusx() {
    for (y = y_plusx)
        for (z = z_plusx)
            translate([ breite/2, y, z ])
                cyl(d = loch_d, h = 2*wand + 4, orient = RIGHT);
}

// Löcher durch die -x-Seite (Rückwand mit Kabel-Schlitz), beidseitig vom Schlitz
module lueftung_minusx() {
    for (y = y_minusx)
        for (z = z_minusx)
            translate([ -breite/2, y, z ])
                cyl(d = loch_d, h = 2*wand + 4, orient = RIGHT);
}

// Löcher durch den Boden (z = 0), Achse in z-Richtung
module lueftung_boden() {
    for (x = x_raster)
        for (y = y_boden)
            translate([ x, y, 0 ])
                cyl(d = loch_d, h = 2*wand + 4, orient = TOP);
}

// Löcher durch den Deckel (vertikal), Achse in z-Richtung
module lueftung_deckel() {
    for (x = x_raster)
        for (y = y_deckel)
            translate([ x, y, deckel_unten ])
                cyl(d = loch_d, h = 2*wand + 4, orient = TOP);
}

// Deckel — wird direkt aus der Bibliothek bezogen (die Zugkanten-Rundung
// skaliert dort jetzt mit der Wandstärke, siehe Hinweis oben)
if (teil == 1) {
    difference() {
        schiebedeckel_box(
            breite = breite, tiefe = tiefe, hoehe = hoehe,
            wand = wand, nut_tiefe = nut_tiefe, spiel = spiel,
            teil = "kiste"
        );
        kabel_schlitz();
        lueftung_laengswaende();
        lueftung_plusx();
        lueftung_minusx();
        lueftung_boden();
    }
} else {
    difference() {
        schiebedeckel_box(
            breite = breite, tiefe = tiefe, hoehe = hoehe,
            wand = wand, nut_tiefe = nut_tiefe, spiel = spiel,
            teil = "deckel"
        );
        lueftung_deckel();
    }
}
