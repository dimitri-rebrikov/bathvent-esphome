// =============================================================================
// bathvent_enclosure.scad — Gehäuse für bathvent (Bad-Lüftungssteuerung)
// =============================================================================
// Schiebedeckel-Box ("Domino"-Stil) auf Basis der Bibliothek
//   ../../3d-models/schiebedeckel-box/schiebedeckel-box.scad  (benötigt BOSL2)
//
// Außenmaße: 320 × 40 × 45 mm (Breite × Tiefe × Höhe), Wandstärke 2 mm.
// Aussparungen (nur in der Kiste / box):
//   • Gegenüberliegende Seite (-x): mittig ein 5-mm-Loch (Ø) durch die Wand
//   • Einschubseite (Deckel, +x): zwei Kabel-Schlitze von der Oberkante abwärts
//       – mittig:  10 mm breit × 30 mm tief
//       – links daneben (im Spalt zwischen Kante und mittlerem Schlitz):
//                   5 mm breit × 30 mm tief
//
// Rendern:
//   openscad -o bathvent_box.stl  -D 'teil=1'  bathvent_enclosure.scad
//   openscad -o bathvent_lid.stl  -D 'teil=2'  bathvent_enclosure.scad
// =============================================================================

include <BOSL2/std.scad>
$fn = 48;

use <../../3d-models/schiebedeckel-box/schiebedeckel-box.scad>

// ---- Teilauswahl (per -D übersteuerbar) ----
teil = 1;   // 1 = kiste, 2 = deckel

// ---- Maße (alle mm) ----
breite    = 320;   // Außenbreite (x-Richtung)
tiefe     = 40;    // Außentiefe  (y-Richtung)
hoehe     = 45;    // Außenhöhe   (z-Richtung)
wand      = 2;     // Wandstärke (auch Boden und Deckel)
nut_tiefe = 0.8;     // Führungsnuttiefe — muss < wand sein
spiel     = 0.4;   // Spiel Deckel/Nut

// ---- Aussparungen ----
loch_d          = 5;       // Ø Loch, Rückwand
loch_z          = hoehe / 2;

schlitz_mitte_b = 10;      // Breite mittlerer Schlitz
schlitz_mitte_t = 30;      // Tiefe  mittlerer Schlitz (ab Oberkante)
schlitz_seite_b = 5;       // Breite seitlicher Schlitz
schlitz_seite_t = 30;      // Tiefe  seitlicher Schlitz (ab Oberkante)

module aussparungen() {
    // Loch mittig durch die Rückwand (-x), Achse in x-Richtung
    translate([ -breite/2, 0, loch_z ])
        cyl(d = loch_d, h = 2*wand + 4, orient = RIGHT);

    // Mittlerer Schlitz durch die Einschubseite (+x), von der Oberkante abwärts
    translate([ breite/2, 0, hoehe - schlitz_mitte_t ])
        cuboid([ 2*wand + 4, schlitz_mitte_b, schlitz_mitte_t + 2 ], anchor = BOTTOM);

    // Seitlicher Schlitz links vom mittleren, zentriert im Spalt zwischen
    // linker Kante (y = -tiefe/2) und mittlerem Schlitz (y = -schlitz_mitte_b/2)
    y_seite = (-tiefe/2 + -schlitz_mitte_b/2) / 2;
    translate([ breite/2, y_seite, hoehe - schlitz_seite_t ])
        cuboid([ 2*wand + 4, schlitz_seite_b, schlitz_seite_t + 2 ], anchor = BOTTOM);
}

if (teil == 1) {
    difference() {
        schiebedeckel_box(
            breite = breite, tiefe = tiefe, hoehe = hoehe,
            wand = wand, nut_tiefe = nut_tiefe, spiel = spiel,
            teil = "kiste"
        );
        aussparungen();
    }
} else {
    schiebedeckel_box(
        breite = breite, tiefe = tiefe, hoehe = hoehe,
        wand = wand, nut_tiefe = nut_tiefe, spiel = spiel,
        teil = "deckel"
    );
}
