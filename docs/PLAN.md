# Ephemeris: Plan für den Berlin-School-Generator

Stand 23.09.2026, Entwurf nach den Entscheidungen des Nutzers (Abschnitt 14). Name **Ephemeris**, Repo
`G:\Tools\VRAudio\BerlinSchoolGenerator`, Namensraum `eph::`, Präfix `EPH_`, Werkzeuge `eph_render`,
`eph_selftest`, Set-Datei `.ephset`. Code-Kommentare im Doxygen-Format, wie Phosphene.

Der Name: Eine Ephemeride ist die Tafel, die für jeden Zeitpunkt die Stellung der Himmelskörper angibt.
Ein Sequenzer ist eine solche Tafel, und die Musik, die hier entstehen soll, besteht aus Umläufen
verschiedener Länge um einen gemeinsamen Grundton, die nur selten wieder zusammenfallen.

## Stand der Umsetzung

**In Arbeit (26.09.2026): Filtermodelle auf dem Stand der Technik.** Auftrag: "noch mehr verschiedene Filtermodelle"
(SEM, SSM2040, CEM3320, SSI2140, Moog, IR3109, Korg35/MS-20, ARP 4075, Polivoks, Matrix-12, Kamm, CEM3389/3387), "weitere
interessante Ideen gerne hinzufügen", "die Filter auch in die Presets", und ausdrücklich: "das absolute SOTA in der
Filtersimulation". Keine Agenten.
Methode (SOTA, white-box ohne Hardware-Messungen): jedes Modell als Schaltungs-ODE an seinen physikalischen Nichtlinearitäten
(Differenzpaare tanh, Diodenpaare, Diodenbegrenzer, CMOS-Inverter, Slew-Rate), trapezförmig diskretisiert in
Kondensatorstrom-/TPT-Form (Zavalishin, Simper/Cytomic), die implizite Gleichung pro Sample mit Newton-Raphson gelöst
(analytische Jacobi-Matrix, strukturierte Lösung: Kaskade = bidiagonal + Rückkopplungsecke, Diodenleiter = tridiagonal +
Ecke; Warmstart aus dem letzten Sample; feste Iterationszahl, damit Vektor = Skalar bitgenau und blockgrößenunabhängig),
bei 2x (die Reihenstimmen laufen schon so). Referenzen: D'Angelo/Välimäki 2013/2014 (Moog, Generalized Ladder I/II),
Huovilainen 2004, Zavalishin "The Art of VA Filter Design", Simper (trapezoidale SVF, MNA + Newton), Holters/Zölzer 2015
(nichtlineare Zustandsraummodelle aus Schaltplänen), Stinchcombe (Ladder/Diodenleiter/MS-20-Analysen), DAFx 2022 EDP Wasp,
ADAA (Parker/Zavalishin/Le Bivic 2016, Bilbao et al. 2017, Holters 2019 für zustandsbehaftete Systeme) als Option.
Modelle (`voice.filter`, `lead.filter`/`drone.filter`, `poly.filter`; dazu `filter_mode` 0..1 und `filter_fm` 0..1):
 0 Moog-Ladder (tanh je Stufe, Newton; ersetzt die bisherige halbimplizite Leiter)
 1 Prophet OTA-Kaskade (SSM2040/SSI2140/CEM3320/CEM3389: vier gepufferte OTA-Stufen tanh(ein-aus), Resonanz-VCA mit Sättigung)
 2 Juno OTA-Kaskade (IR3109: sauberer, anderer Bassverlust)
 3 Oberheim SEM (trapezoidale SVF nach Simper mit OTA-tanh in den Integratoren; Mode morpht LP -> Notch -> HP)
 4 Xpander/Matrix-12 (Polmischung auf 1: LP4 LP2 BP2 BP4 HP2 HP4 Notch Phaser, Mode wählt)
 5 Diodenleiter (TB-303/EMS: gekoppelte Knoten über Diodenpaare, tridiagonal)
 6 Korg35/MS-20 (Sallen-Key, Diodenbegrenzer in der Rückkopplung, Stinchcombe)
 7 Polivoks (SVF aus slew-begrenzten Op-Amp-Integratoren)
 8 EDP Wasp (CMOS-Inverter-SVF, DAFx 2022) -- eigene Idee dazu
 9 Kammfilter (Verzögerung mit gedämpfter Rückkopplung, Frequenz = Cutoff, Mode = +/- Rückkopplung)
 Filter-FM (Xpander): Oszillator 1 moduliert den Cutoff in Audiorate (`filter_fm`) -- eigene Idee dazu.
Schritte: F1 Filterkern `eph/synth/Filters.h` (Templates für float und VecF, alle Modelle, Newton) mit Selbsttests
(Kleinsignal-Frequenzgang gegen die analytische Übertragungsfunktion, Selbstoszillation ~ Cutoff, Stabilität an den
Rändern, Verzerrung steigt mit Pegel); F2 in den SIMD-Kern (Modelle nur rechnen, wenn eine Spur sie nutzt), Vektortest je
Modell; F3 Parameter und Engine (Voice, Lead, Drone; drone-Tabelle mitziehen!); F4 Poly; F5 Presets (je Gruppe eine
Filterliste, z. B. Squelch Arp: Diodenleiter/Korg35/Moog; Juno Strings: Juno; Oberheim Brass: SEM; Cosmic Drip: Kamm/
Phaser) und Panel-Gruppe "Filter" mit Modellwahl; F6 Handbuchkapitel "Filters", Screenshots, Hashes, Release, APK.
Stand: F1 erledigt (Filters.h: Moog, OTA-Kaskade Prophet/Juno, Xpander-Polmischung, SEM/Polivoks/Wasp-SVF, Diodenleiter, Korg35; Newton kNewton=3; Test: Kleinsignal gegen analytisch 0.15 dB, Oszillation 0-4 % am Cutoff, Stress < 6.4x). Weiter mit F2 (Kern), Kamm und Filter-FM dort.

**Erledigt (26.09.2026): Die Oberfläche als Instrumenten-Panel.** Auftrag: GUI und Synth-Tabs gefielen nicht, Funktionsgruppen
wie in Phosphene, wichtige Encoder größer (Cutoff, Streichfett), ein stimmiges Farbschema für moderne Berlin School,
Maximieren-Knopf. Umgesetzt: `Plugin/EditorTheme.*` -- Palette aus dem Logo (Mitternachtsgrund, Pergament-Text, die fünf
Planeten als Funktionsfamilien: Bernstein Quellen, Kupfer Filter, Salbei Hüllkurven, Petrol Bewegung, Stahlblau Raum/Mix;
Sonnen-Bernstein als Akzent), ein eigenes LookAndFeel (Knopf mit Wertebogen in Familienfarbe, Schalter mit Lampe, flache
Tabs), `layoutOf()` mit den Gruppen aller Module (große Encoder mit `*`), ParamPage als Panel aus Gruppenkästen (scrollt
bei Bedarf), Editor-Körper skaliert mit dem Fenster (Phosphene-Art), Maximieren-Knopf und F11/Vollbild im Standalone.
Beobachtet: `vst3test` fiel im Release-Lauf einmal durch (unter Volllast klang das Plugin nicht rechtzeitig), allein und im
zweiten Lauf grün -- bei Wiederholung die Wartezeit des Tests erhöhen.

**Erledigt (25./26.09.2026): Gegen die Langeweile, Teil 2 -- Variationsplaner, Presets, Flächen-Synth, Wavetables.**
Auftrag des Nutzers nach der Literaturfrage ("Ja, bitte mache das so"; Wavetables aus dem AmbientSynth weiterverwenden,
"gerne auch Samples"). Befund gegen Stilguide 4.3/5.2/6.4/7.1, Garcia (MTO 11.4, 2005), 0zk und Stürtzers Waldorf-Seite:
Bass und Hauptsequenz laufen minutenlang identisch (Mutation nur mit 10-30 % je 16-Takt-Block), acht der 14
Variationstechniken fehlen, kein Break, jedes Stück mit denselben Grundklängen (der Komponist wählt keine Presets),
keine Polysynth-/Wavetable-Fläche (`Part::Pad` spielt niemand), keine langsame Klangfarben-Drift auf Flächen.
Schritte:
A. Variationsplaner je Phase: alle 8-16 Takte ein Ereignis auf Hauptsequenz (60 %), Gegenreihen (30 %) oder Bass (10 %):
   Step-Gate, Oktavversatz eines Steps, Skip/Reset (Länge L-1/L-2 für 4-8 Takte), Richtungswechsel für 4-8 Takte,
   Rückkehr zum Thema ("Grip"); Delay-Umschaltung 1-3x je Stück; ein Break vor dem Höhepunkt (2-4 Takte alle Reihen
   still, Echo-Feedback hoch, Schlagzeug still) mit Stilchance; zweite Modulationsspur je Reihe für den Decay
   (eigene Länge, `NoteEvent::decay`). Neue RackOps Gate, OctaveStep, Direction, Theme.
B. Der Komponist wählt je Stück und Synth ein Preset aus stil- und rollengerechten Gruppen (als Gesten, Knöpfe bleiben
   des Nutzers), eigener Stream und Reroll "sounds"; angezeigt auf den Synth-Seiten und im Mixer.
C. Flächen-Synth "Poly" auf `Part::Pad`: polyphon, Oszillator analog (Saw/Puls mit PWM, Verstimmung) oder Wavetable,
   Attack 1-4 s, Release 3-8 s, Ensemble-Chorus, langsame Positions-/PWM-Drift; 1024 Presets; der Komponist setzt ihn in
   Räume und Aufbauten, höchstens zwei Flächen zugleich, Oktavtrennung zu Chor und Solina (Produktionsguide).
   Wavetables: `CycleTable` (10 bandbegrenzte Stufen, Catmull-Rom) und Fft aus dem AmbientSynth übernommen; Tabellen
   aus Formeln (Classic, PWM, Sync, Formant, Vocal, Organ, Glass, Metal) plus rund 30 gesampelte aus dessen Bibliothek
   (AKWF/WaveEdit CC0, eigene Ambient-Tabellen), auf 32 Frames zu 256 Samples ausgedünnt und in den Kern kompiliert.
D. Wavetable als Oszillator-Option der Reihenstimmen; die Modulationsspur schaltet die Tabellenposition je Note
   (Stürtzers Iridium-Sequenzen).
E. Klangfarben-Drift: langsame Gesten auf Wellenform-Mix, Pulsbreite, Verstimmung und Tabellenposition.
Jeder Schritt mit Selbsttest, Referenz-Hashes, Handbuch und Commit.
Stand: A-E erledigt, Stilpegel neu kalibriert (Seeds 7+8, 10 min: Cosmic 4.9, Doom 3.7, Melodic 4.4, Modern 4.6, Drift 4.2 dB). Offen: die Streuung von Stück zu Stück durch die Presets (Modern -13.2 / -16.4 LUFS) -- ein Pegelausgleich je Preset oder ein langsamer Leveler; die CPU auf ruhiger Maschine messen (fremde Prozesse belegten sie). Und hören. (E: Klangfarben-Drift je Gegenreihe und Poly-Position; D: voice.table/table_pos/table_mod, skalar vorberechnet bei 2x und im Kernel eingeblendet, Vektor=Skalar bitgenau; CPU-Messung offen, die Maschine war durch fremde ambient_render-Prozesse belegt; C: Poly auf Part::Pad, 46 Tabellen -- 8 Formeln, 38 gesampelt -- in ~10 MB; Stem gemessen -29 dBFS neben den Strings, 10 dB unter der Hauptsequenz; B: Hand-Fix -- die Hände setzten zu Beginn jeden Regler auf ihre Ruhelage um den Knopf und überschrieben Cutoff/Decay/Resonanz der Einstellungen; jetzt um den Wert der Einstellungen). Offen nach C: Stilpegel an 10-min-Stücken neu kalibrieren (5-min-Referenzen: LRA 17-20 statt 13-14 LU, Cosmic -18.4 statt -17.3 LUFS).

**Erledigt (25.09.2026, abends): Hypnose und Groove, Nacht-Sets mit DJ-Überblendung.**
Auftrag des Nutzers: Ephemeris klingt "unfassbar langweilig", groovt nicht und wirkt nicht hypnotisch (Vergleich:
Martin Stürtzer); außerdem wie bei Phosphene ganze Sets, die eine Nacht durchlaufen, mit wechselnden Stilen und
guter DJ-Überblendung. Keine Agenten, alles im Vordergrund.

Diagnose (aus dem Code, gegen Literatur und Stilguide):
- Zu viel Zufall im Kern: Mutation 10-30 % je Zyklus (bei 1-Takt-Reihen alle 3-10 Takte eine Änderung),
  Wahrscheinlichkeits-Gates und Zufallsschritte auch auf Bass und Hauptsequenz, Akkordwechsel ab 2 Takten --
  Hypnose braucht Wiederholung über 16-32 Takte und eine Änderung zur Zeit.
- Es fehlen die Modulationssequenzer (Stürtzer: Stepic, "8 Modulationssequencer mit unterschiedlichen
  Längen"): Klangfarbe (Cutoff, Decay, Akzent) je Schritt in eigener, polymetrischer Länge -- das eigentliche
  Groove-Mittel der modernen Schule.
- Keine durchgehenden Filterfahrten als Makro-Bewegung (30-120 s Zyklen, eine Oktave), nur zufällige Handgesten.
- Zu langsam (Modern ab 90, Cosmic ab 96 BPM; Referenz 118-132) und zu lange Intros (12-14 %).
- Der Sequenzklang zu brav (Resonanz 0,35, Hüllkurve 2,5 Okt., Akzent schwach); die Sequenz zu leise gegen Pads
  und Räume (nachmessen per Stems).
- Kein Puls: Schlagzeug selten und erst ab 45 %; keine Kick/Pulsschicht.
- Aufbau additiv, aber neue Reihen setzen sofort voll ein (Guide 6.4: "beginnt mit Rests und füllt sich").

Schritte:
1. Hypnose-Kern: Mutation der Bass- und Hauptreihe selten und nur an 16-Takt-Grenzen; Gates/Zufallsschritte
   nur auf den Gegenreihen ab der zweiten; Akkorde mindestens 8 Takte (Melodic 4).
2. Modulationssequenzer: je Reihe eine Klangfarben-Spur (Länge 3/5/7/12 gegen die Notenlänge), je Schritt
   Helligkeit und Decay; `NoteEvent` trägt sie, die Stimme setzt sie je Note um.
3. Sequenzklang und Filterfahrten: Rollen-Klang der Hauptsequenz (Resonanz ~0,5, Hüllkurve 3,5 Okt.,
   Decay 150-250 ms, Akzent stark), Filterfahrten als Kompositionsgesten (irrationale Perioden, je Reihe eigene).
4. Tempo und Form: Tempi hoch (Cosmic 112-126, Melodic 118-132, Modern 118-130, Drift 100-118, Doom 92-110),
   Intros 6-9 %; Reihen setzen ausgedünnt ein und füllen sich (`RackOp::Fill`).
5. Puls: weiche Vierviertel-Kick bzw. Herzschlag ab dem zweiten Aufbau je Stil, Schlagzeug früher (ab 30 %).
6. Pegel: Sequenzen gegen Pads/Räume nachmessen (Stems) und anheben.
7. Nacht-Sets (umgesetzt als `compose.night_set`): Stile gemischt nach Energiewellen (Guide 6.3 "Wellen"), Stücke 8-15 min,
   DJ-Überlappung 16-32 Takte: abwechselnde Reihenbänke (Stück A Reihen 1-4, Stück B 5-8, Transposer jeweils in
   der anderen Bank), B übernimmt A's Tempo und rampt später, harmonische Tonartwechsel (Quinte, Quarte,
   Parallele), Bass-Übergabe über das Distanz-Makro und den Low Cut (A entfernt sich, B nähert sich), der Bordun
   gleitet in die neue Tonart. Offline (`eph_render --set`), Plugin und Quest.
Jeder Schritt mit Selbsttest, neuen Referenz-Hashes, Handbuch und Commit.
Stand: Schritte 1-7 erledigt (Puls writePulse je Stil ab dem zweiten Aufbau, Kit ab 30 %; Tape Keys -1 dB und Atmosphäre 0 dB, gemessen: Hauptsequenz -19.7, Tape -30, Atmosphäre -45.6 dBFS).
Schritt 7 umgesetzt als Schalter `compose.night_set` auf dem Konzert (`compose.concert_minutes` jetzt bis 720), composeNightSet:
Stil-Leiter Drift-Doom-Cosmic-Modern-Melodic, eine Sprosse je Stück, Wellen 80-120 min über concertArc, nie drei gleiche
in Folge. Messung am 34-min-Set zeigte zuerst 10 dB Einbruch je Übergabe (A's Gegenreihen, Flächen und B's Formpegel ab
Überlappungsbeginn); daher länger gemischt: B's Intro 8-16 Takte, Einsatz 8 Takte, Hauptsequenz füllt 16 Takte unter A's
Outro (32-40 Takte); A's Gegenreihen stoppen beim B-Bass, A's Bass Low Cut 30->250 Hz über 2 Takte und Distanz ->0.7,
A's Hauptsequenz Distanz ->0.6 ab B's Hauptsequenz; der Masterbus bleibt A's bis zu dessen Ende; Doom/Drift +1.2 dB.
Danach 3 dB sanfte Blende statt Einbruch. Tempo innerhalb Stilbereich ±6 BPM, Rampe 16 Takte je 4 BPM. Nebenbei behoben:
im Konzert blieben Regler, die ein Stück bewegt und das nächste nicht setzt (z. B. die Körnerwolke), stehen -- sie gehen
jetzt beim Stückwechsel auf den Knopf zurück. Quest: `night=8` in eph.cfg. Offen: Hören.

**Nächste Schritte (Stand 25.09.2026, nachmittags).** Die Programmieraufgaben der Phasen 4 bis 7 sind
umgesetzt (die Einträge darunter); offen ist, was einen Menschen, eine Installation oder ein Gerät braucht:
1. **Hörrunde** (Nutzer): im Standalone "Compose" und "Play", oder `eph_render` (README). Fragen: Tragen die
   Modularstimmen mit der neuen Sättigung (Sigmoide statt tanh, rund 0,5 dB leiser)? Chor und Streicher der
   Tape Keys, die Hallmenge, die Formlängen, die Unterscheidbarkeit der Stile, die Tempi (die Referenzmessung
   legt für Cosmic und Melodic langsamere nahe)? Die Konjunktionen an den Formgrenzen?
2. **Quest auf dem Gerät**: `adb install -r build-quest\EphemerisQuest.apk`, mit `eph.cfg` (`mute=1` zum
   Prüfen); messen, ob zwei Kerne reichen, dann die Qualitätsstufe festlegen; `eph_vectest` und
   `eph_selftest` über adb auf dem echten NEON.
3. ~~pluginval und Inno Setup~~ erledigt am 25.09.2026: pluginval 1.0.4 (nach `ThirdParty\pluginval` geladen)
   besteht mit Strenge 10 (Scan, Öffnen, Editor, Verarbeitung bei 44,1/48/96 kHz und Blöcken von 64 bis 1024,
   Zustand, Automation, Thread-Sicherheit, Parameter-Fuzzing); als ctest `pluginval` und im Release-Skript.
   Inno Setup 7.1 baut `Deploy\out\Ephemeris-0.1.0-Setup.exe` (8,6 MB).
4. **Kalibrierung der Tape Keys** an Mellotron-Aufnahmen (dafür fehlen Aufnahmen).
5. ~~Kleinere Ideen aus 8.1~~ erledigt am 25.09.2026 (siehe unten): eigener Stil, Platte und BBD, Granular,
   Spannungsbogen über ein Konzert.

**25.09.2026, nachmittags: String Machine nach dem Vorbild Streichfett, Stilguide umgesetzt** (alle vier
Schritte erledigt; Release-Build mit pluginval Strenge 10 und Installer, Quest-APK gebaut).
Danach die übrigen Regeln des Guides: Terzrückungen zwischen Phasen (-3, +4), eigener Moll-Modus je späterer
Phase (Suitenteile, 6.5), Zufallsschritte über einen Quantizer (4.6; Anteil je Stil, Drift und Modern am
meisten), Stereobild (7.3; Bass Mitte, Gegenreihen 30-50 % abwechselnd, Lead leicht außermittig) mit
`drone.auto_pan` (Sinus 0,05 Hz), `compose.album` (Zwischenspiele von 3-5 Minuten ohne Reihen, dunkelstes
Stück in der Mitte, ätherischer Schluss; ohne Album unverändert), das zweite Echo (Modul `delay`, im Code `Module::Echo2`, Send
`row.echo2`, Zeit 1/8 oder 1/4T für die Gegenreihen ab der zweiten; `1/4T` auch im ersten Echo).
Danach der "Produktionsguide Tiefe, Weite und Transparenz" (Downloads des Nutzers, 25.09.2026), soweit er
sich auf eine Maschine übertragen lässt, die ihre Musik selbst spielt: `Loudness.h` (EBU R128 integriert,
Kurzzeit-Maximum, LRA, True Peak, PSR, PLR, Korrelation mit tiefster Sekunde über -50 dBFS, Side unter Mid;
`eph_render` druckt es für jeden Render); `low_cut` je Quelle (Bass 30, Hauptsequenz 90, Gegenreihen 200, Lead
und Tape 150, Streicher 200, Atmosphäre 120, Bordun 40, Drums 25 Hz); Summenbus mit 20-Hz-Hochpass, Side mono
unter 100 Hz (12 dB/Okt.), `master.width` über 300 Hz mit Wächter (Side höchstens 3 dB unter Mid),
`master.mono`; Echo-Wiederholungen mit Hochpass in der Schleife (`echo.low_cut` 200, `delay.low_cut` 300 Hz) und
auf 70 % Breite; `echo.duck`/`reverb.duck` (die Reihen ducken die Returns, 3 und 2 dB); Hall-Return 250 Hz bis
6,5 kHz, Pre-Delay eine 64tel im Tempo; Automation statt Kompression (Pegel und Breite entlang der Form, Pads
-3 dB unter dem Lead, Hall im Höhepunkt -40 %); Stilpegel neu (Cosmic +4,5, Doom +2, Melodic +5, Modern +5,
Drift +3,5 dB). Gemessen, 10 Minuten, Seed 7: Cosmic/Melodic/Modern -15,3 bis -15,5 LUFS, Doom -18,6,
Drift -19,0; Kurzzeit-Maximum um -12 LUFS, True Peak -1,0 dBTP, PSR 11-14, LRA 8-13 LU, tiefste Sekunde
um 0 (nur im Ausklang der Atmosphäre). Nicht umgesetzt: die drei getrennten Hall-Instanzen (Early, Plate,
großer Hall als eigene Sends), dynamische EQs, Resonanzunterdrücker, Transient-Designer, weicher Clipper.
Danach das Addon "Lehren aus dem Dark-Ambient/Drone-Guide" (Downloads, 25.09.2026): `distance` je Quelle
(Distanz-Makro: Pegel 0/-4/-10/-24 dB, Tiefpass offen/10/5/2,5 kHz, Hall-Send +0/0,2/0,6/1 bei 0/0,3/0,6/1; der
Bass nähert sich an jedem Einsatz über 45 s von 0,7 und entfernt sich im Abbau und im Ausklang), kaskadiertes
Ducking im Band 300 Hz bis 5 kHz (Reihen -> Pads -> Atmosphäre, `master.cascade` 2,5 dB), reine Intervalle im
Fundament (Detune von Bordun und Bassreihe 0), Allpass-Spreizung der Tape Keys (`tape.spread` 0,7, Side =
Allpass-Kette der Mitte, Korrelation um 0,35, Mono-Summe unverändert), Bewegung auf Mikro- und Meso-Zeitskalen
mit irrationalen Perioden (`master.motion`), Ereignisse in den sequenzlosen Teilen (Lead-Fragmente alle 20-90 s),
`master.sub_solo`, Export-Fades (2 s ein, 10 s aus, S-Kurve; 20 s Nachlauf), `eph_render --archive` (ohne
Limiter, -3 dBTP, 24 Bit), Crest-Faktor und Korrelation je Stem im Bericht, Doom und Drift auf -18 LUFS.
Gemessen (10 min, Seed 7): Cosmic/Melodic/Modern -15,4 bis -15,6 LUFS, Doom -17,9, Drift -18,1; LRA 10-15 LU,
Crest 17-20 dB, PSR 11-14. Nicht umgesetzt: der serielle Fernraum (es gibt nur einen Hall), der Sub als
eigenes Instrument mit eigenem Clipper und Multiband-Limiter, spektrales Ducking in 6-8 Bändern (hier ein
Band), Abhörpraxis und Mute-Test (Arbeitsweise, nicht Programm).
Danach Werks-Presets (`Presets.h`): je 1024 für Stimme, Lead, Bordun, Tape Keys, Streicher, Drums und Atmosphäre
(16 Gruppen zu 8 x 8, Namen Adjektiv dunkel -> hell plus Nomen der Gruppe, eindeutig je Synth), auf jeder
Synth-Seite als Liste mit Untermenüs und Pfeilen; ein Preset setzt den ganzen Klang und lässt den Mix
(Pegel, Pan, Sends, Low Cut, Distance, Auto Pan, Spread) und die Mengen der Atmosphäre in Ruhe. Stimmung:
Bordun-Detune 0-1 ct, Bass-Gruppen 0-3, sonst höchstens 12, Drift höchstens 7, Vibrato höchstens 40 ct,
Tape-Wow/Flutter/Motorlast im Bereich der Standardwerte; keine Transposition. Selbsttest `testPresets`.
Danach der serielle Fernraum und der Rest: Blend-Raum (`Module::Blend`, zweite Platte, 1,2 s, eigener Send je
Quelle, `blend.into_hall` 0,15 in den Hall, im Höhepunkt 0,05; Gegenreihen, Lead, Drums in den Blend-Raum, Pads,
Bordun, Atmosphäre in den Hall), weicher Clipper vor dem Limiter (`master.clip` 0,75 dB, ADAA nur um die
Spitzen), Limiter unter 80 Hz (`master.sub_ceiling` -6 dBFS, Linkwitz-Riley-Weiche), Punch je Reihe
(Transientenformer; Hauptsequenz 0,5), Echo ohne Leiern auf der Sequenz (Wow 0,2 ms, Flutter 0,02; Echo 2
fast frei), Benutzer-Presets mit "Save..." (Textdateien unter Ephemeris/Presets). Gemessen: -15,1 bis -15,2
LUFS, Doom/Drift -17,5; Crest 16,7-19,4 dB; Render 15-20x Echtzeit (die zweite Platte kostet rund 2 % eines
Kerns). Selbsttest `testBlendAndBus`.
Danach die vier Räume vollständig und die übrigen Punkte: frühe Reflexionen (Send A, `Module::Early`, 12 Abgriffe
je Seite bis 35 ms, 200 Hz bis 8 kHz; Gegenreihen, Lead, Drums), Shimmer (Send D, `Module::Shimmer`, lange Platte
mit Oktave nach oben in der Rückkopplung, 400 Hz bis 4 kHz; Bordun, Atmosphäre, Tape Keys in Atmosphäre, Brücken
und Ausklang), das spektrale Ducking in sechs Bändern (350 Hz bis 4,8 kHz, Q 2), der Resonanzunterdrücker auf dem
Reihen-Bus (`ResonanceTamer`, sechs Bänder Q 2,5, lokale Spitzen 4,5 dB über den Nachbarn bis 3:1,
`master.tame` 0,6). Die Reihen laufen dafür über einen eigenen Bus. Rechenzeit nach Tabelle für die Duck-Gains
und Leerlauf des Shimmers 7-8 % eines Kerns (vorher 5-6,5). Nicht umgesetzt: der Sub als eigenes Instrument
(mit dem Nutzer so beschlossen). Selbsttest `testSendsAD`.
Danach: Sends im Mixer ausklappbar ("Show sends"), Presets für alle Reihen auf einmal, eigene Presets auf den
Effektseiten (Echo + Spring, Hall; ganze Seite, volle Schlüssel). Harmonik gegen die Referenzaufnahmen gemessen
(`Tools/analyze_harmony.py`, Ergebnis `Tools/ref_harmony.json`, je Stil sechs Aufnahmen; Grundton des Basses je
2 s): Cosmic 94 % auf i und 5 von 6 statisch, Melodic Pendel mit III/VI/VII und Akkorden um 3 Takte, Modern,
Doom und Drift bewegter als angenommen (34 %, 34 %, 25 % auf i; 2-5 Takte je Akkord). Gewichte in `Harmony.cpp`
halb Guide, halb Messung; Äolisches Pendel mit III gleich häufig wie VI und VII; Melodic ab 2 Takten. Die Modi
unverändert: die fünfte Harmonische des Basses liest sich als große Terz, dafür ist das Chroma zu grob.
Auftrag des Nutzers: die String Machine mehr wie Waldorfs Streichfett (Ensemble, Mischung der Register, langsames
Animieren der Mischung) und den "Harmonie- und Stilguide Moderne Berlin School" (Downloads des Nutzers, Stand
25.09.2026) umsetzen, soweit sinnvoll. Reihenfolge und Stand:
1. **String Machine**: Register 16' Säge, 8' Säge, 4' Säge, 8' Rechteck, 4' Rechteck je Taste (Divide-down);
   `strings.registration` 0..7 morpht stufenlos durch acht Registrierungen (Violins, Violas, Cellos, Basses,
   Full, Hollow, Brass, Organ; je Mischung und Klangfarbe); `strings.animate` und `strings.animate_rate`: ein
   langsamer LFO bewegt die Registrierung (die "animierte Mischung"); `strings.ensemble_type` (Solina, Chorus,
   Wide); `strings.phaser` (vierstufig, langsamer LFO, stereo versetzt); `strings.feet` wird zur Balance der
   Fußlagen. Die Hände des Komponisten dürfen die Registrierung langsam bewegen.
2. **Harmonik nach Guide 3.x**: Akkordspur über dem Zentrum (Klassen A Statik, B Pendel, C Schleife, D
   Bass-Umdeutung; Markov-Tabelle 3.6; harmonischer Rhythmus 8-32 Takte, je Stil); die Bassreihe folgt der
   Akkordstufe diatonisch, die Gegenreihen bleiben (Bass-Umdeutung), der Bordun bleibt auf dem Zentrum;
   Akkordvokabular 3.4 (Quinte, Moll, sus2/4, madd9, Quartschichtung, VI/VII/III Dur; keine Dur-Tonika, kein
   Leitton-V7) mit Stimmführung 3.9; der Transposer nur noch als Sequenzer-Transposition +7/+5/-3 in Plateau
   und Höhepunkt (Kette Tonika-Quinte-Tonika-Quarte-Tonika, 4-16 Takte); paralleler Moduswechsel im
   Höhepunkt (Äolisch -> Dorisch); Schluss auf offener Quinte; weitere Modi (Mixolydisch, Lydisch, Lokrisch).
3. **Sequencing nach Guide 4.x**: Archetypen (Oktavpendel, Quintanker, Treppe auf/ab, Kanon, 3+1, dorischer
   Farbtupfer, phrygischer Stoß, Arpeggio-Spirale), 3-5 Tonhöhen, Schritt 1 Grundton oder Quinte, 1-3 Pausen
   je 8, Akzente 1 und 5, Schrittzahlen 8/16/12/ungerade; Ratchets (nur Plateau und Höhepunkt), Puls-
   Verdopplung im Höhepunkt, Wahrscheinlichkeits-Gates, im Abbau verliert die Sequenz Schritte.
4. **Form, Raum, Lead**: Intro-Anteile (Melodic mindestens 12 %), Schlagzeug nach 45 % und vor 90 %, Echo-
   Rückkopplung u-förmig (hoch in Atmosphäre und Ausklang, niedrig im Höhepunkt), Bass trocken, Pads mit Hall
   ohne Echo; Lead-Phrasen 4-8 Takte, Pausen 2-4, enden auf Quinte oder kleiner Terz, dorische Sexte als
   Farbe, leicht hinter dem Schlag.
Jeder Schritt mit Selbsttest, neuen Referenz-Hashes und Commit.
Stand: Schritt 1 erledigt (cc10980; Registrierung zeigt im Regler die Namen der Mischungen). Schritt 2
erledigt: `Harmony.h` (Akkordspur mit den Klassen Statik/Pendel/Schleife/Markov-Wanderung, Gewichte und
harmonischer Rhythmus je Stil; nur Stufen mit reiner Quinte; Vokabular `chordTones`; Transposer-Kette nur
mit Zügen, bei denen das Zentrum in der verschobenen Skala bleibt), `RackOp::Chord` (die Bassreihe rückt
diatonisch, die Gegenreihen bleiben), `RackOp::Scale` und `Score::scaleShifts` (paralleler Moduswechsel:
Äolisch -> Dorisch im Höhepunkt, -> Phrygisch im Abbau, Chancen je Stil), der Bordun folgt nur den Tonarten
der Phasen, Schluss auf offener Quinte (Streicher, sonst Tape Keys), `compose.scale` um Mixolydisch, Lydisch
und Lokrisch erweitert. Der Transposer läuft nur noch im Lead-Teil (Plateau) und im Höhepunkt, ohne
Mutation. Selbsttest `testHarmony`.
Schritt 3 erledigt: `Figure` in `Rack.h` (Classic, Oktavpendel, Quintanker, 3+1, Treppe auf/ab, Modusfarbe,
phrygischer Stoß, Spirale i-VI, Kanon der Vorreihe), Schritt 1 Grundton oder Quinte, 1-3 Pausen je 8 (Bass
0-2), Akzente 1 und 5 oder 3+3+2, Wahrscheinlichkeits-Gates je Stil (Cosmic/Doom 5 %, Melodic 8 %, Modern
20 %, Drift 15 %) aus eigenem Würfel je Reihe; `RackOp::Ratchet` (Lead-Teil 1 Schritt, Höhepunkt 2 plus 1 im
Bass), `RackOp::Division` (Achtelreihe im Höhepunkt in Sechzehnteln), `RackOp::Thin` (Bass verliert im Abbau
alle 4 und im Ausklang alle 2 Takte einen Schritt); Bass 8 oder 16 Schritte, Gate 60-75 %. Selbsttest
`testSequencing`.
Schritt 4 erledigt: Intro/Ausklang (Cosmic 14/10 %, Melodic 12/8 %, Modern Ausklang 10 %), Schlagzeug nur
zwischen 45 und 90 % des Stücks, Echo-Rückkopplung der Würfe u-förmig über die Energie (Ruhe +0,12, Höhepunkt
-0,08), Hall-Abklingzeit je Teil (Atmosphäre/Brücke ×1,3, Ausklang ×1,4, Höhepunkt ×0,75, Abbau ×1,1; Hand 3
des Komponisten), Bass ohne Echo und mit höchstens 0,1 Hall, Tape Keys und Streicher ohne Echo; Lead-Phrasen
4-8 Takte, Pausen 2-4, Schluss auf Quinte oder kleiner Terz, dorische Sexte in der Pentatonik, 8-23 ms hinter
dem Schlag. Selbsttest `testFormAndSpace`.

**25.09.2026: Eigener Stil, Platte und BBD, Granular, Konzertbogen.**
- **Eigener Stil** (Modul `custom`, Style-Tab): alle Zahlen eines Profils als Parameter, "Copy <Stil> into
  Custom" als Ausgangspunkt, "Use Custom Style" schaltet ihn ein; Reihenlängen, Transposer, Tape-Satz und
  Schlagzeugmuster bleiben die des gewählten Stils (Cosmic und Drift haben keine Schlagzeugmuster). Ein
  Konzert beginnt mit ihm und morpht von ihm aus.
- **Platte** (`reverb.type`, `Plate.h`): Dattorros Plattenhall (1997), Pegel auf den Hall abgeglichen
  (−30,1 gegen −30,9 dBFS im Raum-Stem). **BBD** (`echo.type`, `Bbd.h`): 4096 Stufen, die Filter bei einem
  Drittel des Takts (längere Delays dunkler), wandernder Takt, Kompander-Sättigung und -Rauschen.
- **Granular** (`atmos.grains`, `atmos.grain_density`): eine Wolke kurzer Sinuskörner auf Skalentönen, eigener
  Zufallsstrom; der Komponist zieht sie mit der Wahrscheinlichkeit des Stils (Cosmic 60 %, Doom 30 %, Melodic
  20 %, Modern 40 %, Drift 70 %) als letzte Ziehung der Schichten und hebt sie in Atmosphäre, Brücken und
  Ausklang. Neue Referenz-Hashes (geprüft: ohne Wolke ist Melodic Seed 5 bitgleich zur alten Referenz).
- **Konzertbogen** (`compose.concert_arc`): ruhige Ränder, der Gipfel bei 60 % des Konzerts (Reihen am
  Höhepunkt, Schichten, Lead, Hände, Helligkeit, etwas Tempo).
- Selbsttests dazu; 26 ctest-Tests, dazu pluginval (Strenge 10); die APK baut mit allem (3,8 MB).

**25.09.2026: Stil-Morph, Style-Tab, Instrumentierungs-Matrix, Stems im Plugin.** `compose.morph_to`: Ein
Konzert wandert vom eigenen Stil zu einem anderen, jedes Stück mit dem Profil beim bereits gespielten Anteil
(`morphProfile`: Zahlen interpoliert, Listen und Aufzählungen vom näheren Profil; Schlagzeugmuster vom näheren
Stil); Selbsttest "style morph"; Beispiel 40 min Cosmic → Modern: drei Stücke, 116, 106, 115 BPM. Style-Tab:
die fünf Profile nebeneinander, der gewählte hervorgehoben (editierbar seit dem eigenen Stil, siehe oben). Arrange: unter den Abschnitten die Instrumentierungs-Matrix (Reihen,
Lead, Tape Keys, Strings, Drone, Drums). Export im Plugin wahlweise mit Stems. Host-Test prüft den MIDI-Weg
(eine Taste transponiert, das Modulationsrad greift die Filter).

**25.09.2026: Kaleidoscope-Kopplung (8.3), Stems, Gesten-Tab, Qualitätsstufe.**
- **Cues** (`Core/include/eph/Cue.h`): Die Engine macht beim Laden aus der Partitur Marken (Abschnitte
  englisch benannt, jeder neue Grundton, jede Konjunktion von mindestens zwei laufenden Reihen, die nicht
  gerade erst einsetzen); der Audio-Thread stempelt die Marken seines Blocks mit dem Moment, in dem man sie
  hört, und legt sie wartefrei in einen Ring; ein eigener Thread schickt sie zur rechten Zeit als OSC 1.0
  über UDP: `/eph/beat i f`, `/eph/phase s i`, `/eph/key s`, `/eph/conjunction i f`. Plugin: Parameter
  `cue.enabled` und `cue.port` (Master-Tab), Ziel `EPH_CUE_HOST` (sonst diese Maschine); Quest: `osc_host`,
  `osc_port` in `eph.cfg`; `eph_render --cues DATEI` schreibt die Marken als Text. Selbsttest: Byte-Layout
  nach der OSC-Spezifikation, Marken eines Stücks, der Tap sendet jede Marke einmal, ein echtes Datagramm
  über die Loopback-Schnittstelle.
- **Stems**: `eph_render --stems ORDNER`, eine 32-Bit-WAV je Kanalzug und eine für die Räume; ihre Summe
  ist der Mix vor dem Master (geprüft: Korrelation 0,998 mit dem Ausgang, 83 Samples Limiter-Vorlauf).
- **Gesten-Tab**: je Knopf eine Spur mit der Offset-Kurve über das Stück, nach Hand gefärbt, darüber die
  Belegung der beiden Hände (die Zwei-Hände-Regel sichtbar); Handbuch-Kapitel dazu.
- **Qualitätsstufe**: Sänger je Chortaste einstellbar (Desktop 6, Quest 3, `eph_render --quality quest`),
  der Pegel bleibt (1/√(6n), bei sechs exakt 1/6: der Desktop bitgleich).

**25.09.2026: Phase 6, Quest-Port gebaut (ungetestet auf dem Gerät).** `Quest/` nach Phosphene: OpenXR,
EGL, Oboe, Punkt-Renderer, Schrift und Hände übernommen; neu sind der Player (ein Stück wird ganz komponiert
und vor dem Audiostart geladen, "nächstes Stück" komponiert auf dem Komponisten-Thread und tauscht hinter
einer Blende, lock-frei wie Phosphenes Übergabe; der Render-Thread liest nur veröffentlichte Atomics und eine
unveränderliche Kopie der Partitur), die Hände (links Pinch Play/Stop, rechts Pinch nächstes Stück, linke
Höhe `perform.filter`, rechte Höhe `perform.throw`) und das Panel (Stück, Stil, Abschnitt, Grundton und
Skala, Zeit, Tempo, Pegel, die beiden Handwerte, Taktlampen, daneben das Orrery aus Punkten). `eph.cfg`:
mute, seed, minutes, style, set. `build_apk.ps1` baut ohne Gradle: `build-quest/EphemerisQuest.apk`,
3,6 MB (keine Daten). Dabei zwei Clang-Warnungen im Kern behoben (bitgleich). Offen: auf dem Gerät
starten, messen, die Qualitätsstufe festlegen.

**25.09.2026: Phase 7, Release-Gerüst.** `Deploy/build_release.ps1` (Build mit statischer Laufzeit, ctest,
Handbuch, Stage, Prüfung ohne dynamische MSVC-Laufzeit, SHA256SUMS, portables ZIP, Setup mit Inno Setup),
`Deploy/Ephemeris.iss` (Standalone, VST3, eph_render, Handbuch; AVX2-Prüfung), Icon aus dem Orrery
(`Deploy/make_icon.py`). Geprüft: 22 von 22 Tests, `Ephemeris-0.1.0-portable.zip` (9 MB). Inno Setup ist
auf dieser Maschine nicht installiert, das Setup selbst ist deshalb noch nicht gebaut.

**25.09.2026: Handbuch.** `Tools/manual/make_manual.py` nach Phosphene: Fließtext aus `chapters.txt`
(Englisch), Parametertabellen aus `eph_render --dump-params`, ein Bild je Tab (`docs/screenshots`), HTML
und PDF (`docs/manual/Ephemeris-Manual.pdf`, Edge headless); verweigert ein Handbuch mit einem Modul, das
auf keiner Seite steht. Mute-Knopf wie in Phosphene: `EPH_MUTE` und der Screenshot-Modus erzwingen ihn.

**25.09.2026: Leistung, zweite Runde.** Die Engine ruft einen Setter nur noch, wenn sich seine Eingabe
geändert hat (Hall, Federn, Echo, Kompressor, Limiter, die zehn Stimmen, die Kanalzüge, Tape Keys, Strings,
Drums, Atmosphäre; die Setter sind reine Funktionen ihrer Eingabe, also bitgleich), `played()` merkt sich
Knopf, Offset und Wert je Parameter, der Echo-Wurf wird in `mix()` addiert statt in die Kanalzüge
geschrieben. Die Federn rechnen ihre beiden Allpass-Ketten Stufe für Stufe verschränkt und mit FMA (neue
Referenz-Hashes). Fünf Minuten Melodic: 11,4 → 8,3 s (Minimum aus drei Läufen, im direkten Vergleich).

**25.09.2026: Perform.** Ein Modul `perform` mit vier Bedienelementen, die auf das laufende Stück wirken und in
der Grundstellung neutral sind (die Referenz-Renders bleiben bitgleich): Filter (die Hand auf den Filtern der
Reihen, ±2 Oktaven), Transpose (die Transpositionstaste, ±12 Halbtöne für jede tonale Note, die danach
beginnt), Hold (die Hände lassen los, die Gesten stehen), Echo Throw (alle Echo-Sends und die Rückkopplung
hoch). MIDI: Tasten transponieren um ihren Abstand zum mittleren C (hält bis zur nächsten Taste),
Modulationsrad = Filter, Expression = Throw, Sustain = Hold; jedes Bedienelement per Learn auf jeden
Controller, die Zuordnung liegt im Zustand. Tab "Perform" mit den Reglern und Learn-Knöpfen. Selbsttest
"perform controls". Live-Mutation einer Reihe gibt es nicht: Die Partitur ist fertig komponiert; dafür
ist das Neuwürfeln einer Einheit da.

**25.09.2026: Rack-Tab mit der Orrery-Ansicht.** Links die Reihen als Umlaufbahnen um den Grundton (die Sonne
in der Mitte ist der Transposer, ihr Buchstabe der Grundton, auf dem die Reihen gerade spielen), der Bass innen;
ein Umlauf ist ein Zyklus der Reihe, oben liegt Schritt 1. Stehen spielende Reihen dort zusammen, steigt eine
Lichtlinie auf (Konjunktion); darunter die Zyklen der Reihen und wann sich alle spielenden Reihen wieder
treffen. Gerechnet aus der Partitur, die die Engine spielt (neu: `Score::rowShapes`, die Form der Reihen, wie
der Komponist sie gesetzt hat; klingt nicht mit, die Referenz-Renders bleiben bitgleich) und der Abspielposition.
Rechts die Parameter der Reihen. Außerdem: Im Host spielt Ephemeris im Tempo des Hosts (die Partitur wird mit
dem Host-Tempo geladen, ein Tempowechsel lädt sie im Nachrichten-Thread neu); vorher sprang die Engine bei
abweichendem Tempo alle halbe Sekunde. Neuer Test `vst3test` (nach Phosphene): das VST3 wie in einer DAW
geladen, 27 Prüfungen. pluginval selbst ist nicht auf dieser Maschine.

**25.09.2026: Mixer-Tab mit Metern wie in Phosphene.** Ein Kanalzug je Quelle der Engine (Reihen 1 bis 8,
Lead, Drone, Tape Keys, Strings, Drums, Atmosphäre) mit Pan, Echo- und Hall-Send, Fader und Meter: RMS-Balken
mit 300 ms Rückfall, Spitzenlinie, die 1,5 s hält und dann mit 20 dB/s fällt, Spitze in Ziffern
(`Plugin/EditorMixer.*`, nach Phosphene). Die Engine sammelt Spitze und Quadratsumme je Kanal hinter Fader und
Pan (`Engine::setMetering`, `takeMeters`), nur lesend: Die Referenz-Renders bleiben bitgleich. Neuer
Screenshot-Schalter `EPH_SHOT_AT` (Beat), damit das Bild die Mitte eines Stücks zeigt (`docs/screenshot.png`).

**24.09.2026: Phase 5, erster Teil fertig: das Plugin.** VST3 und Standalone
(`build/Plugin/Ephemeris_artefacts/Release/`), Bild des Panels in `docs/screenshot.png`.

| Prüfstein | Ergebnis |
|---|---|
| Build VST3 + Standalone | ohne Fehler; JUCE aus Phosphenes Checkout (`ThirdParty/JUCE`, sonst Fetch) |
| Panel (`EPH_SHOT`) | Stil, Tonart, Skala, Längen, Compose, neuer Seed, Play; Neuwürfeln je Einheit; `.ephset` speichern und laden; Export WAV + MIDI; Arrange-Zeitleiste der Abschnitte mit Abspielmarke (Klick springt); Tabs aus den Parametertabellen |
| Live-Pfad gegen Offline-Render | Standalone mit `EPH_SEED=4242 EPH_PLAY=6 EPH_RECORD=...` gegen `eph_render --seed 4242`: höchstens 1,8·10⁻⁷ Abweichung über 288 000 Frames, das ist die Quantisierung des 24-Bit-WAV. Das Plugin spielt, was der Offline-Render spielt. |

Gebaut: `StoreParameter` (nach Phosphene: jeder Eintrag des `ParamStore` ist ein Host-Parameter, ohne
zweite Kopie); der Komponist auf eigenem Thread, die fertige Partitur wird auf dem Message-Thread mit
angehaltener Verarbeitung geladen (die Engine allokiert beim Laden, der Audio-Thread nie); ein zweiter
Compose-Auftrag während des Komponierens wird vorgemerkt; im Host ist die Abspielposition die Uhr
(`Engine::seek` bei Sprüngen), der Standalone hat Play/Stop; Export rendert offline auf einem Thread.

Befund: Die Wertefelder zeigten zuerst nur die Einheit. JUCE fragt den Text mit Länge 0 an, und das
heißt "ohne Grenze"; `getText` schnitt auf null Zeichen ab.

Noch offen aus Phase 5: Host-Test und pluginval, Tempokarte des Hosts (im Host gilt das Host-Tempo, die
Tempowechsel eines Stücks kommen nur über Export und MIDI), Perform-Makros, Handbuch-Generator, eine
eigene Oberfläche statt der generischen Knopfraster.

**24.09.2026: Phase 4 im Wesentlichen fertig.** `eph_render` komponiert jetzt standardmäßig ein
ganzes Stück (`--set "compose.style=Cosmic|Doom|Melodic|Modern|Drift"`, `--minutes`) oder ein Konzert
(`--concert 60`); `--reroll lead` würfelt eine Einheit neu, `--save-set`/`--set-file` speichern und laden
eine `.ephset`.

| Prüfstein | Ergebnis |
|---|---|
| Fünf Stile, je zwölf Minuten, Seed 5 | alle komponiert und gerendert, 23- bis 31-fache Echtzeit, 3,2 bis 4,3 % eines Kerns |
| Pegel je Stil (RMS) | Cosmic −18,5, Doom −23,4, Melodic −18,1, Modern −17,8, Drift −24,0 dBFS: Doom und Drift rund 5 dB leiser, wie in den Referenzen (dort −14 bis −16 gegen −20); absolut 4 bis 5 dB leiser als die Referenzen, weil der Master nur schützt und nicht verdichtet |
| Formgrammatik, fünf Stile × sechs Seeds | 30 von 30 Formen lückenlos, mit Atmo am Anfang, Ausklang am Ende, Einsatz, Aufbau und Höhepunkt in jeder Phase, Brücken dazwischen, Mindestlängen |
| Komponiertes Stück (Melodic, 10 min) | gleicher Seed, gleiches Stück; 598 s für 600; alle 9191 tonalen Noten (Reihen, Lead, Akkorde, Drone) in der Skala ihres Grundtons |
| Konzert, 40 min | vier Stücke, 2399 s, Tonartenreise |
| Kuratieren | Neuwürfeln des Leads lässt jede andere Note und jede Geste bitgleich; eine `.ephset` bringt dasselbe Stück zurück |
| Selbsttest | 54 von 54 |

Gebaut:
- **Stilprofile** (`Style.h`): Tempo, Länge, Phasenzahl, Intro- und Coda-Anteil, Tempo- und
  Tonartwechsel, Reihen am Höhepunkt und ihre Längen, Mutation, Transposer, Schichtwahrscheinlichkeiten,
  Tape-Satz, Lead-Dichte, Hände, Dunkelheit, Hall, Pegel. Tempi nach der Referenzmessung gesenkt
  (Cosmic 96–118, Doom 84–108, Melodic 104–126, Modern 90–120, Drift 80–110), Pegelversatz nach den
  gemessenen RMS-Werten.
- **Formgrammatik** (`Form.h`): Atmo, Sequenzphasen (Einsatz, ein bis drei Aufbauten, Lead,
  Höhepunkt, Abbau vor einer Brücke), Brücken, Ausklang; Längen in Takten im Tempo ihrer Phase, ein
  Spannungsbogen über alle Abschnitte; Tempowechsel am Beginn der Brücke, wo keine Reihe spielt.
- **Komponist** (`Composer.h`): Stücke auf getrennten Seed-Strömen (Form, Tempo, Reihen, Muster,
  Schichten, Lead, Akkorde, Hände); neue Muster in jeder Phase; Reihen kommen mit den Aufbauten, alle am
  Höhepunkt, die Gegenreihen gehen im Abbau; Tonartwechsel als Rack-Ereignis (`RackOp::Key`), dem Lead,
  Akkorde, Drone und Bleeps folgen; Einstellungen des Stücks (Tape-Satz, Hall, Pegel) als Schritte am
  Anfang; die Hände auf den Knöpfen dessen, was spielt, entlang des Spannungsbogens. Konzerte reihen
  Stücke mit Tonartenreise (Quarte, Quinte, Parallele, Ganzton).
- **Kuratieren und `.ephset`** (`SetFile.h`): Neuwürfel-Zähler je Einheit verschieben nur deren Strom.
- **MIDI**: Gesten als Controller (Cutoff CC 74, Resonanz 71, Decay 75 auf dem Kanal der Stimme; die
  Knöpfe des Instruments auf einer Spur "controls").
- **Schlagzeug** (`Drums.h`): ein kleines analoges Kit (Kick-Sweep, Snare, 808-Hats aus sechs Rechtecken,
  im Grundton gestimmte Toms, Rim, Shaker) mit Mustern für Melodic (Achtziger-Kit mit Fills), Modern
  (sparsam, Halftime, Rim-Offbeats) und Doom (ein einzelner Tom). Bewusste Abweichung von Abschnitt 4:
  Phosphenes Kit ist nicht übernommen, weil es an dessen Parametertabellen, Harmonie und
  Psytrance-Rhythmik hängt und hier acht Klänge spät im Stück genügen.

Noch offen aus Phase 4:
- **Composer-Thread und Ereignisring**: Die Engine bekommt die Partitur weiter als Ganzes; im Plugin wird
  sie außerhalb des Audio-Threads geladen (Phase 5).
- ~~**Konjunktionen als Formgrenzen** (6.2)~~ erledigt am 25.09.2026: Ein Aufbau endet dort, wo die
  Gegenreihe, die er hereinbringt, und die Bassreihe ihren Zyklus wieder gemeinsam beginnen (kgV der
  Zyklen, bei 13 Sechzehnteln gegen 16 alle 13 Takte), wenn das nahe am gezogenen Ende liegt (ein Viertel
  des Aufbaus, mindestens zwei Takte); der folgende Abschnitt gleicht aus (`snapToConjunctions`, Form.h).
  Selbsttest: 107 von 112 Aufbauten (fünf Stile, sechs Seeds) enden auf einer Konjunktion, die Form bleibt
  lückenlos, auf Takten, über ihren Mindestlängen und gleich lang.
- **Kalibrierung**: Tempo und Reihenlängen der Referenzen brauchen ein besseres Messverfahren
  (Tempogramm); die Schichtwahrscheinlichkeiten und Längen sind Setzungen.
  **25.09.2026: Tempogramm gebaut** (`Tools/analyze_ref.py`): lokales Tempo in 12-s-Fenstern, jedes über
  einen Kamm aus Sechzehntel, Achtel, Schlag, halbem Takt und Takt bewertet (Grosche und Müller 2011, in
  der Autokorrelationsform); je Stück der häufigste Wert und der Anteil der Fenster innerhalb 2 %. Die
  Reihenlänge aus den Tonhöhen: ein Chroma-Vektor je Sequenzerschritt (über dem Bass), die kürzeste
  Verschiebung, bei der sich das Muster deutlich über dem Median wiederholt. Am eigenen Render geprüft:
  112,4 für 112 BPM (die alte Messung fand keins), Reihen 12 und 6 (die Melodic-Reihen haben 12 Schritte,
  ihre Arpeggio-Figur hat Periode 6).
  Referenzen (je vier Stücke, Median; Tempo-Stabilität in Klammern): Cosmic 89 BPM (0,97; Profil 96–118),
  Doom 120 (0,95; Profil 84–108, vermutlich doppelte Zählung eines halbschnellen Grooves), Melodic 94,5
  (0,99; Profil 104–126), Modern 99,5 (1,0; Profil 90–120), Drift 112,7 (0,94; Profil 80–110). Reihen:
  überall eine Dreierfigur, Melodic und Doom dazu 16 und 32, Drift 8 und 16. **Nicht übernommen**: Cosmic
  und Melodic wären nach der Messung langsamer als ihre Profile, der Nutzer fand die Studie aber schon
  "sehr langsam"; die Tempi bleiben, bis die Hörrunde entscheidet.

**24.09.2026: Phase 3 im Wesentlichen fertig.** Die Skizze (`eph_render --minutes 10 --seed 3`) hat
jetzt ein kosmisches Intro aus Wind und Sweeps mit Drone, danach die Reihen; Chor-Akkorde auf den Tape
Keys ab einem Fünftel, beim Höhepunkt Wechsel auf das Streicherband; darüber die String-Machine von der
Mitte bis zum Ausklang; Bleeps in der Mitte; Hall, Federn und Master.

| Prüfstein | Ergebnis |
|---|---|
| Zehn Minuten mit allem | 21-fache Echtzeit, 4,7 % eines Kerns; Spitze −3,1 dBFS, RMS −20,7 dBFS, kein DC |
| Messung wie die Referenzen | 4,5 Helligkeitsbewegungen/min, Median 6,5 s, 0,47 Okt., Bereich 1,36 Okt., RMS −18,2 dBFS, Dynamik 5,3 dB |
| Tape Keys | ein gehaltener Ton verstummt nach dem Bandende (über 300 dB unter dem Klang), die Taste ist danach frei; der Capstan sinkt mit jeder Taste (−3 Cent bei drei, −7,5 bei sechs Tasten, eingestellt 1,5 je Taste) |
| Akkorde | jeder Ton in der Skala seines Grundtons, kein Akkord länger als ein Band hält (längster 6,97 s), die tiefste Stimme bewegt sich im Mittel 0,1 Halbtöne |
| Blockgrößen 1 / 37 / 512 | weiterhin bitgleich (mit Hall, Kompressor und Limiter) |
| Selbsttest | 45 von 45 |

Gebaut:
- **Hall**: Phosphenes FDN mit acht Linien, Sends von jeder Stimme und von den Echo-Wiederholungen. Die
  untere Grenze des Low Cut liegt jetzt bei 40 Hz statt 150, weil es hier keine phasengekoppelte Kick
  gibt. Der Return liegt so, dass die Fahne etwa 10 dB unter dem trockenen Mix steht; die erste
  Einstellung lag 20 dB darunter und war kaum hörbar.
- **Tape Keys**, das Mellotron ohne Aufnahmen (5.4): Chor (sechs Sänger je Taste mit eigener Verstimmung,
  eigenem Vibrato und Wandern, Glottis-Neigung, fünf Formanten zwischen „aah“ und „ooh“), Streicher und
  Flöte, dazu die Maschine: ein eigenes Band je Taste (Verstimmung, Pegel, Farbe, Verzögerung, fest je
  Taste und Seed), ein Capstan für alle mit Wow, Flutter und Motorlast, Andruckrolle, Bandende nach 8 s,
  Rauschen, Sättigung. Der Bandsatz wird beim Anschlag gewählt; ein Wechsel gilt ab dem nächsten.
- **Akkorde** (`Pads.h`): Dreiklang (manchmal mit Septime oder None) der Skala über dem Grundton des
  Transposers, parallel transponiert wie die Reihen; Stimmführung per vollständiger Suche mit kleinster
  Bewegung; Neuanschlag vor dem Bandende, Finger einige Millisekunden versetzt.
- **Drone**: eine weitere `ModVoice` mit der Tabelle des Leads, dunkel und langsam, auf dem Grundton.
- **Atmosphäre** (`Atmos.h`): Wind (zwei dekorrelierte Rauschen in wandernden Resonanzbändern), kosmische
  Sweeps als Poisson-Prozess, Sample-and-Hold-Bleeps im Grundton der Reihen, vor allem ins Echo.
- **String-Machine** (5.5): Divide-down wie bei der Solina, alle Oktaven eines Tons phasenstarr, 8' und
  4', Ensemble aus drei Verzögerungen mit 0,6- und 6-Hz-LFO, um ein Drittel versetzt.
- **Federn** (5.8): zwei dispersive Tanks nach Välimäki, Parker und Abel (2010), gespeist aus dem
  Echo-Send wie in einem Bandecho mit eingebauter Feder.
- **Master**: Phosphenes Bus-Kompressor (höchstens 1,5:1) und True-Peak-Limiter bei −1 dBTP als Schutz.

Befunde und Korrekturen:
- **Die tiefste Akkordstimme sprang im Test 4,9 Halbtöne.** Erst lag es an der Stimmführung (Zuordnung
  nach Position statt nach Nähe), die jetzt vollständig sucht. Danach lag es am Test selbst: Er nahm den
  zuerst angeschlagenen Ton als tiefsten, und durch den Finger-Versatz ist das irgendeine Stimme.
- **Die Tape Keys wechselten ihren Klang mitten im Ton**, als der Bandsatz umgeschaltet wurde. Jetzt
  merkt sich jede Taste den Satz, auf dem sie angeschlagen wurde.

Bewusst verschoben:
- **Schlagzeug** nach Phase 4: Phosphenes Kit hängt am Parametersystem, an der Harmonie und an einem
  Psytrance-Rhythmusmodul; gebraucht wird es nur für "Melodic" und "Modern", und es gehört mit den
  Stilprofilen zusammen.
- **Platte (Dattorro) und Phaser**: Der Hall deckt den Raum ab; der Phaser kommt, wenn die Gesten ihn
  brauchen.
- **Kalibrierung der Tape Keys an Mellotron-Aufnahmen**: Dafür fehlen noch Aufnahmen, in denen das
  Mellotron frei steht. Bis dahin sind alle Maschinenwerte erste Setzungen.

**24.09.2026: Phase 2 fertig.** `eph_render --minutes 10 --seed 3` spielt die Skizze (`Sketch.h`):
Bassreihe, Gegenreihe (13), Laufreihe (12 Achtel, eine Oktave höher), eine Transpositionsreihe von acht
Schritten zu vier Takten, ein Lead in zwei Passagen und zwei Hände auf sieben Knöpfen entlang eines
Spannungsbogens, der bei zwei Dritteln gipfelt. Die Form ist noch fest (der Komponist kommt in Phase 4),
ihr Inhalt wird gezogen.

| Prüfstein | Ergebnis |
|---|---|
| Zehn Minuten, drei Notenreihen, Transposer, Lead, Hände | 57-fache Echtzeit, 1,8 % eines Kerns; Spitze −3,0 dBFS, RMS −20,5 dBFS |
| Messung wie die Referenzen (`analyze_ref.py`) | 4,3 Helligkeitsbewegungen/min, Median 8,0 s, 0,52 Okt., Bereich 1,36 Okt. (Referenzen: 4,8–5,8/min, 8,5–9,8 s, 0,51–0,73 Okt., 1,13–2,15 Okt.; die Studie aus Phase 1: 2,2/min, 18,5 s, 0,44 Okt.) |
| Transposer | jede Note einer Notenreihe im Grundton ihres Takts (0 von 240 falsch); Progression beginnt und endet auf der Tonika |
| Hände | nie mehr als zwei zugleich, kein Sprung, jedes Ziel im Bereich des Knopfs; 5,7 Bewegungen/min im Test (Würfe und ihr Auffangen gezählt) |
| Lead | jede Note in der Skala ihres Grundtons, im Register, einstimmig außer den Glides von unten; Phrasen mit Pausen |
| Selbsttest | 38 von 38 |

Gebaut: Reihenmodus `Transposer` mit Taktteilern 1, 2 und 4 Takte; `Harmony` (Progressionen je Stil als
Markov-Kette über i, bVI, bVII, bIII, iv, v, ii, bII, Tritonus, mit Gewichten und Verweilchance);
`GestureEngine` (zwei Hände als Prozesse, Log-Normal-Dauern um 8 s, lange Fahrten, schnelle Würfe mit
Auffangen, Ziele aus dem Spannungsbogen, Pausen um 14 s); `Lead` (Pentatonik plus Skalentöne als
Durchgänge, Bogenkontur, Akkordtöne auf schweren Zählzeiten, Glide aus dem Skalenton darunter,
Motivwiederholung) und die Lead-Stimme (neunte `ModVoice` mit spät einsetzendem Vibrato).

Befunde und Korrekturen:
- **Höreindruck des Nutzers zur Studie**: "relativ dumpf und sehr langsam, aber das kommt auf den Kontext
  an". Die Voreinstellungen der ersten drei Stimmen sind um etwa zwei Drittel Oktave heller; die Hände
  bewegen sich nach der Referenzmessung statt nach der Hand der Studie.
- **Die Hände bewegten sich zuerst 8,9-mal pro Minute**: Bei der Wahl der Ruhezeit (4 s) war die zweite
  Hand in der Summe vergessen. Mit 14 s liegen beide zusammen bei etwa fünf.
- **Eine Transpositionsreihe, die aufhört, lässt die Reihen jetzt auf der Tonika** statt auf ihrem
  letzten Grundton.

**24.09.2026: Phase 1 hörbar, zum Hören beim Nutzer.** `eph_render --minutes 5 --seed 7` spielt die
Studie (`Study.h`): eine Berlin-Bassreihe (16 Sechzehntel), ab einem Zehntel eine Gegenreihe von 13
Schritten, Transposition i–i–bVI–bVII alle acht Takte nach dem Orgelpunkt, und die Hände: Filter öffnet
über eine Minute, Decay wird lang und wieder kurz, ein Echo-Wurf, eine Senke, ein Höhepunkt mit
Resonanz, das Schließen. Nie mehr als zwei Hände zugleich.

| Prüfstein | Ergebnis |
|---|---|
| Rechenzeit, zwei Reihen mit 2× Leiter, Echo | 75-fache Echtzeit, 1,3 % eines Kerns |
| Pegel | Spitze −3,3 dBFS, RMS −21,5 dBFS, kein DC |
| Spektralschwerpunkt über die Studie (10-s-Fenster) | folgt den Gesten: 68 Hz geschlossen → 185 Hz offen (80 s) → Senke 130 Hz (170 s) → Höhepunkt 211 Hz (210 s) → 49 Hz am Ende |
| Blockgrößen 1 / 37 / 512 | bitgleich über 20 s |
| Rack | Transposition nur an Schrittgrenzen; gleicher Seed, gleiche Noten; bei Mutationschance 1 genau eine Mutation je Zyklus (16 in 64 Beats); Schritt 0 klingt immer |
| Bandecho | Wiederholungen im Takt (plus 3,7 Samples Gruppenlaufzeit der Schleife je Durchgang), jede leiser (−15, −22, −28 dB) und dunkler |
| Drift (Ornstein-Uhlenbeck) | Streuung 2,75 Cent bei eingestellten 3 über eine Stunde |

Gebaut: `Rack` (acht Reihen, Längen 1 bis 32, Teiler, vier Richtungen, Transposition an Schrittgrenzen,
Schieberegister-Mutation, drei Rollen Bass/Counter/Walk), `ModVoice` (zwei frei laufende PolyBLEP-VCOs
mit OU-Drift, ADAA-übersteuerter Mixer, ZDF-Leiter bei 2×, Filter- und Amp-Hüllkurve, Glide, DC-Blocker),
`TapeEcho` (Hermite-Lesekopf mit Wow und Flutter, Tiefpass, Hochpass und tanh in der Schleife,
Ping-Pong, Bandgeschwindigkeit gleitet), die Engine mit Noten auf dem Sample-Raster, Gesten auf einem
absoluten 32-Sample-Raster, Mixer mit Pan und Echo-Send.

Befunde unterwegs, alle von Tests oder Messungen gefunden:
- **DC 0,029** am Ausgang: Ein Puls mit 30 % Tastverhältnis hat den Mittelwert 0,4, und die Leiter lässt
  ihn als Tiefpass durch. Behoben mit einem DC-Blocker bei 8 Hz, der AC-Kopplung eines Modularausgangs.
- **Blockgrößen nicht bitgleich** (erste Abweichung nach 8,3 s): Eine Stimme, die mitten in einer Spanne
  verstummte, lief je nach Spannenlänge noch weiter oder nicht, und damit Oszillatorphase und
  Drift-Raster. Jetzt entscheidet die Engine nur an Rasterpunkten und bei Noteneinsätzen, ob eine Stimme
  rechnet.
- **Bandecho startete mit der Standardzeit** und glitt erst auf die eingestellte: Die erste Einstellung
  nach `prepare` gilt jetzt sofort.

**Erste Referenzmessung** (`Tools/analyze_ref.py`, Ordner in `Tools/ref_sets.txt`, Ergebnis nur als
Statistik in `Tools/ref_stats.json`; je Profil acht Stücke, über die Alben verteilt). Das Werkzeug wurde
zuerst an der Studie geprüft, deren Inhalt bekannt ist: Tempo 117,8 statt 118 BPM getroffen; die
Wendepunkt-Erkennung der Filterfahrten fand in der ersten Fassung gar nichts und wurde korrigiert.
"Helligkeitsbewegung" ist ein Wendepunkt-zu-Wendepunkt-Lauf des Spektralschwerpunkts (150 Hz bis 8 kHz,
0,5-s-Rahmen, 4 s geglättet, Hysterese 0,25 Oktaven).

| Profil | Länge (Median) | Helligkeitsbewegungen | Dauer (Median) | Umfang (Median) | Helligkeitsbereich | RMS | Dynamik (3-s-RMS, 10–95 %) |
|---|---|---|---|---|---|---|---|
| Cosmic | 12,7 min | 5,8/min | 8,5 s | 0,65 Okt. | 1,41 Okt. | −13,7 dBFS | 7,7 dB |
| Doom | 8,3 min | 5,1/min | 9,0 s | 0,51 Okt. | 1,38 Okt. | −20,8 dBFS | 17,1 dB |
| Melodic | 6,6 min | 4,8/min | 9,8 s | 0,52 Okt. | 1,13 Okt. | −15,9 dBFS | 5,4 dB |
| Modern | 5,5 min | 4,9/min | 9,0 s | 0,73 Okt. | 1,77 Okt. | −14,1 dBFS | 14,2 dB |
| Drift | 6,5 min | 5,7/min | 9,0 s | 0,68 Okt. | 2,15 Okt. | −20,1 dBFS | 10,1 dB |
| *Studie (zum Vergleich)* | 5,1 min | 2,2/min | 18,5 s | 0,44 Okt. | 0,99 Okt. | −18,7 dBFS | 5,3 dB |

Lesart, mit Vorbehalt:
- **Die Referenzen bewegen ihre Helligkeit etwa doppelt so oft und um die Hälfte weiter als die Studie**,
  in allen Profilen ähnlich (4,8 bis 5,8 Bewegungen pro Minute, Median 9 s, 0,5 bis 0,7 Oktaven). Das
  Maß trennt aber Handgesten nicht von Einsätzen, Akkordwechseln und Soli, die der Studie fehlen. Es ist
  ein Ziel für das fertige Arrangement (Phase 4), keine Vorgabe für eine einzelne Filterfahrt.
- **Pegel und Dynamik** trennen die Profile deutlich: Cosmic, Melodic und Modern um −14 bis −16 dBFS RMS,
  Doom und Drift um −20 dBFS mit viel größerer Dynamik (10 bis 17 dB). Das geht in die Lautheitsziele
  der Profile (5.8).
- **Tempo und Reihenlänge** sind mit diesem Verfahren noch nicht belastbar: Die stärkste Periodizität
  liegt oft auf Achteln oder Vierteln statt auf den Sechzehnteln, und jedes Vielfache der wahren
  Reihenlänge korreliert. Die Tempowerte (Median 86 bis 108 BPM) sind deshalb nur ein Hinweis, dass die
  Tempobereiche aus 2.8 eher zu hoch angesetzt sind. Für Phase 4 braucht es eine Schätzung über das
  Onset-Muster innerhalb eines Taktes (Tempogramm) statt einer einzelnen Autokorrelationsspitze.

Noch offen in Phase 1: das Urteil des Nutzers nach dem Hören der Studie.

**24.09.2026: Phase 0 fertig.** Gebaut mit Visual Studio 2026, Release, AVX2:

| Prüfstein | Ergebnis |
|---|---|
| Build | ohne Warnungen (/W4) |
| `ctest` | 4 von 4: Selbsttest (17 Prüfungen), Vektortest AVX2, NEON-Shim, skalar |
| Tempo-Karte gegen numerisches Integral über 1053 s mit Rampe | Fehler 1,8 ns; `beatAt` invertiert `secondsAt` auf 5·10⁻¹³ Beats |
| Gesten-Kurve Minimum Jerk | monoton, symmetrisch, Ruhe an beiden Enden, Spitzengeschwindigkeit 15/8 der mittleren |
| MIDI-Tempo-Karte einer Rampe | ein Tempo-Ereignis je Beat; jeder Beat höchstens 1,9 µs neben dem Render |
| `eph_render --minutes 20 --bpm 96 --ramp-to 124` | 550 Takte, 1206,5 s Stille auf der Rampe, WAV und MIDI, 1,0 s Rechenzeit |

Gebaut: aus Phosphene (Stand 9a2f615, Herkunft im Dateikopf) `Vec.h`, `Dsp.h`, `Adaa.h`, `Halfband`,
`Clock`, `WavWriter`, `Oscillator.h`, `Ladder.h`, das Test-Gerüst und der NEON-Shim; neu das
Parametersystem mit den Modulen `compose`, `row` (acht Instanzen) und `master`, die Partitur mit Noten,
Gesten als Kurven (Offsets im normierten Knopfbereich; der Knopf bleibt, wo die Hand ihn loslässt),
Rack-Ereignissen und Markern, ein MIDI-Schreiber, das Engine-Gerüst und `eph_render`.

Abweichungen vom Plan, bewusst:
- **Kein Composer-Thread und kein Ereignisring** in Phase 0: Die Engine bekommt die Partitur als Ganzes.
  Ring und Thread kommen mit dem Komponisten (Phase 4), wie in Phosphene.
- **Kein MIDI-Leser**: Der Tempo-Test liest die Tempo-Ereignisse selbst. Den Leser aus Phosphene
  übernehmen, sobald ein Rundlauf von Noten etwas absichert (Phase 4).
- **Ein ctest-Eintrag für den ganzen Selbsttest** statt einem je Abschnitt; `eph_selftest --only`
  wählt Abschnitte aus. Die Aufteilung lohnt erst, wenn der Selbsttest lange läuft.

**23.09.2026: Planentwurf.** Grundlage: die Pläne und der Code von Noctuary
(`G:\Tools\VRAudio\AmbientSynth`) und Phosphene (`G:\Tools\VRAudio\PsytranceGenerator`), acht gezielte
Websuchen zu den Referenzkünstlern und zum Mellotron (Quellen am Ende), sonst eigenes Wissen. Was nicht
belegt ist, ist als solches markiert.

## 0. Kurzfassung

Ein Instrument, das aus einem Seed, einem Stilprofil und einem Spannungsbogen lange Berlin-School-Stücke
(8 bis 40 Minuten) und ganze Konzerte oder Alben (45 bis 120 Minuten) komponiert und in Echtzeit
synthetisiert: mehrere Sequenzer-Reihen auf analog modellierten Modularstimmen, die Gesten eines
Spielers an Filter, Echo und Transposition, ein synthetisches Mellotron, String-Machine, Flächen und
Drones, Lead-Soli mit Glide, je nach Stil ein Schlagzeug, Bandecho, Federhall und großer Raum.
Standalone und VST3 für Windows (JUCE 9), nativ auf der Quest 2, MIDI-Export aller Linien und Gesten,
Offline-Render als Determinismus-Orakel.

Die drei Entscheidungen, die alles andere bestimmen:

1. **Der Sequenzer ist ein Prozess, kein Pattern.** In Psytrance schreibt der Komponist Patterns. In
   der Berlin School ändern sich die Töne einer Sequenz kaum; was sich ändert, ist der Prozess:
   Transposition, Mutation, das Gegeneinanderlaufen von Reihen verschiedener Länge, Ein- und Ausstiege,
   und vor allem die Klangfarbe. Der Kern ist deshalb ein **Sequenzer-Rack mit Zustand** (Reihen,
   Längen, Taktteiler, Transpositionseingang, Mutation), und der Komponist spielt dieses Rack wie ein
   Musiker, statt jede Note zu schreiben. Die Partitur enthält trotzdem jede Note (Determinismus,
   MIDI-Export): Der Komponist-Thread lässt das Rack-Modell vorauslaufen und schreibt mit.
2. **Gesten sind musikalisches Material erster Klasse.** Filterfahrten über Minuten, Resonanz,
   Hüllkurven-Decay, Echo-Rückkopplung und der Griff zur Transpositionstaste sind in dieser Musik die
   eigentliche Melodie. Eine Gesten-Engine erzeugt sie als Automationskurven mit menschlichen
   Eigenschaften (Minimum-Jerk-Profil, Pausen, gelegentlich ein schneller Griff, höchstens zwei Hände
   gleichzeitig). Was bei Phosphene der rollende Bass war, ist hier der erste Prüfstein: **eine
   Sequenz, die atmet** (Abschnitt 12).
3. **Analoger Charakter als Modell, nicht als Rauschen.** Oszillator-Drift, Bandmaschinen (Echo,
   Mellotron), Eimerkettenspeicher und Federn sind physikalisch motivierte Modelle mit gemessenen
   Parametern, deterministisch aus dem Seed. Keine Samples, auch nicht für das Mellotron
   (Entscheidung des Nutzers, 23.09.2026).

Reihenfolge der Arbeit: zuerst eine Reihe auf einer Modularstimme mit Leiterfilter, Bandecho und einer
Filtergeste; dann die Polymetrie mehrerer Reihen; dann Mellotron und Raum; dann die Form.

## 1. Ziel, Rahmen, Nicht-Ziele

**Ziel.** Auf Knopfdruck ein Stück oder Konzert, das ein Kenner der Referenzkünstler als stilistisch
glaubwürdig hört: kosmisches Intro, Einsatz der Sequenz, Schichtung, Solo, Höhepunkt, Abbau, zweite
Sequenzphase, Ausklang; lange Entwicklung ohne Langeweile und ohne Beliebigkeit. Jede Einheit einzeln
sperrbar und neu würfelbar. Alles reproduzierbar aus Seed + Stil + Sperren.

**Rahmen** (wie Phosphene).
- Plattformen: Windows x64 (AVX2), Quest 2 (arm64, NEON). Linux als Nebenprodukt des frameworkfreien
  Kerns, nicht als Release-Ziel.
- Sample-Rate 44,1/48/96 kHz, Blöcke 16 bis 2048; Quest 48 kHz, 256er Blöcke (Oboe).
- Kern ohne Framework, C++20, keine Allokation im Audio-Thread.
- Kein Fast-Math; der Offline-Render ist das Orakel.

**Nicht-Ziele.**
- Kein Klon einzelner Künstler oder Stücke. Stilprofile tragen beschreibende Namen; Künstlernamen
  stehen nur in der Dokumentation als Hörreferenz.
- **Keine Samples**, auch nicht für Mellotron-Chöre und -Streicher (Entscheidung 23.09.2026).
- Keine neuronale Audio-Erzeugung im Kern (MusicGen, Stable Audio und Verwandte): über 30 Minuten
  weder steuerbar noch deterministisch, auf der Quest zu teuer. Höchstens später offline für
  Texturen.
- Kein Lautheitskrieg: Berlin School lebt von Dynamik. Der Limiter ist Schutz, nicht Klangmittel.
- Kein Cloud-Modell in der Echtzeitschleife.

## 2. Was Berlin School ausmacht (die musikalische Spezifikation)

Die Zahlen in diesem Abschnitt sind Hypothesen, bis sie in Phase 1 an Referenzmaterial gemessen sind
(`Tools/analyze_ref.py`, Abschnitt 11.4). Gespeichert werden nur Statistiken, nie Audio.

### 2.1 Die Referenzkünstler

| Künstler | Herkunft, Label | Kennzeichen | Beleg |
|---|---|---|---|
| **Martin Stürtzer** | Wuppertal; eigenes Label Phelios (Bandcamp), Synphaera | Ambient, atmosphärische Flächen und Drones, "neo-Berlin-School"-Sequenzen analoger Synthesizer, improvisatorisch; Dub-Elektronik-Einschlag; Livestreams mit großem Publikum. *Theta Serpentis* (2022), *Epsilon Eridani* (2022, Vinyl: Seite A Sequenzen, Seite B ein langes Ambient-Stück), *Illumination Cycle*, *Circular Oscillations* | Web |
| **Thalaron** | **Alias von Martin Stürtzer** | dunklere Spielart: Space-Ambient mit Berlin-School-Elektronik, als Berlin School und Dark Ambient geführt. *Microgravity* (2017, 8 Stücke, 58 min) | Web |
| **Syndromeda** (Danny Budts) | Belgien, seit 1997 (*Mind TRIPS*); SynGate, Cue Records | warme Flächen, melodische Sequenzen, teils psychedelische Synthesizerklänge, Gitarren-Tupfer, tiefe Atmosphären, "esoterische Meditation"; stark von der Berliner Schule geprägt. *Eternal Destination* (2018), *Serendipity* (2025) | Web |
| **Ian Boddy** | Großbritannien; Gründer des Labels DiN | modular, experimentellere Klanggestaltung, sehr viele Kollaborationen: Erik Wøllo, Markus Reuter, Robert Rich (dessen Schlafkonzerte die Vorlage von Noctuary waren), Chris Carter, Bernhard Wöstheinrich, David Wright, Andy Pickford; Arc mit Mark Shreeve | Kollaborationen aus der lokalen Sammlung; Label und Arc eigenes Wissen |
| **Dark Side of the Moog** | Klaus Schulze und Pete Namlook, Label FAX, elf Teile ab 1994, VI und VII mit Bill Laswell | lange improvisierte Sitzungen, Ambient-Drift im Wechsel mit Moog-Sequenzen | Teile und Besetzung aus der lokalen Sammlung; Stil eigenes Wissen |
| **Redshift** | Großbritannien, seit 1996; Mark Shreeve, Julian Shreeve, James Goddard (Rob Jenkins bis 2002) | "klassische 70er-Berlin-School", dunkel; drei Musiker bauen Atmosphären, Mark Shreeve sequenziert auf einem großen Moog-Modular; Mellotron (teils als digitale Samples); Moog, Oberheim, PPG, Solina, Doepfer, Analogue Systems, Modcan; Sequenzierung von Kritikern als außergewöhnlich komplex und dynamisch gelobt | Web |
| **Thorsten Quaeschning** | Solo und in Kollaborationen; daneben Leiter von Tangerine Dream seit 2015 | **Referenz sind seine Soloarbeiten und Kollaborationen, nicht Tangerine Dream** (Entscheidung 24.09.2026); lokal bisher *Synthwaves* mit Ulrich Schnauss, weitere Solo-Titel ergänzt der Nutzer. Hintergrund: Sein Live-Rig bei Tangerine Dream besteht aus Modularsystem, Rack-Synthesizern, Keyboards, iPad-Instrumenten und eigenen Sequenzer-Werkzeugen; *Raum* (2022) verbindet Echtzeit-Kompositionen mit Studioproduktion | Web (Rig, *Raum*); lokale Sammlung |
| **Runes Order** | Italien (Claudio Dondo) | Dark Ambient / Industrial mit **Anleihen der Berlin School** in einzelnen Stücken (laut Nutzer z. B. *Secret Place (The Final Chapter)*); Referenz für dunkle Intros, Flächen und den Übergang von Ambient zu Sequenz | eigenes Wissen + Hinweis des Nutzers; zwölf Alben lokal |
| **Ron Boots** | Niederlande; Gründer von Groove Unlimited (Label, Festival E-Day) | Berlin-School-Sequenzen mit dichten Atmosphären; *Detachment of Worldly Affairs* (1994, von Hörern zum Album des Jahres gewählt), *Hydrythmix* (mit dem Schlagzeuger Bas Broekhuis, "kraftvolle klassische Sequenzierung"), *Current* ("schwere rhythmische Strukturen") | Web; E-Day eigenes Wissen |
| **['ramp]** (Stephen Parsick) | Deutschland; gegründet 1996, seit 2009 Parsick allein; Name vom niederländischen "de ramp" (Katastrophe); Label doombient.music | "Doombient": dunkler, finsterer, sequenzerbasierter Stil. *Synchronize or Die* (2017, Rückkehr), Kollaborationen mit Bernhard Wöstheinrich (*Ultima Ratio*) | Web |

### 2.2 Was sie verbindet

1. **Das Sequenzer-Ostinato ist das Fundament**, nicht das Schlagzeug. Die Sequenz ist die Uhr.
2. **Mehrere Reihen gleichzeitig**, oft von verschiedener Länge, die gegeneinander laufen und erst nach
   dem kleinsten gemeinsamen Vielfachen ihrer Längen wieder zusammenfallen.
3. **Die Harmonie steckt in der Transposition**: Die ganze Sequenz wird per Tastatur oder durch eine
   langsamere Reihe verschoben. Moll und Modi (äolisch, dorisch, phrygisch), Orgelpunkt, wenige
   Wechsel.
4. **Die Klangfarbe ist die Melodie**: Filter-Cutoff, Resonanz und Decay werden über Minuten von Hand
   gefahren. Die Töne bleiben, der Klang wandert.
5. **Lange, additive Form**: Schicht um Schicht, mit einem kosmischen Intro aus Drones, Rauschen und
   Flächen, und einem Ausklang, in dem die Sequenz sich in Echos auflöst.
6. **Die Palette**: Moog-Modular und Minimoog-artige Stimmen, Mellotron-Chor und -Streicher,
   String-Machine (Solina), Wavetable (PPG), Drones.
7. **Raum**: Bandecho, großer Hall, im Stereobild verteilte Reihen.
8. **Die menschliche Hand**: Improvisation in Echtzeit (Stürtzer, Tangerine Dreams nächtliche
   Echtzeit-Kompositionen, Dark Side of the Moog), analoge Drift, kleine Unregelmäßigkeiten.

### 2.3 Tempo und Raster
- Sequenzen in Achteln oder Sechzehnteln bei etwa 100 bis 135 BPM; die langsameren, dunklen Stile
  darunter, die rhythmischen darüber (Hypothese).
- 4/4 als Rahmen, aber polymetrische Reihen (Längen wie 5, 6, 7, 9, 12, 13 gegen 16).
- Tempowechsel zwischen Sequenzphasen: Die alte Sequenz hört auf, eine neue beginnt in anderem Tempo.
  Kein Beatmatching nötig.
- Swing selten; Triolen als Ausnahme.

### 2.4 Die Sequenz
- **Die Bass-Sequenz**: Grundton-lastig mit Oktavsprüngen, 8 oder 16 Schritte (der Moog 960 hatte drei
  Reihen zu acht Schritten, verkettbar), Akzent über die Filterhüllkurve, kurzes Gate.
- **Doppelsequenzen**: zwei Reihen, um einen Schritt versetzt oder eine Oktave höher, die sich zu
  einem dichteren Muster verzahnen.
- **Echo-Sequenz**: Eine Achtelsequenz in ein Echo mit punktierter Achtel ergibt ein verzahntes
  Sechzehntelmuster. Das Echo ist Teil der Komposition, nicht nur Raum.
- **Transposition** alle 4 bis 32 Takte; moderne Varianten mit Ratchets und Wahrscheinlichkeiten.
- **Mutation**: einzelne Schritte, die sich im Lauf der Zeit ändern (Noctuarys Nah-Ebene macht das
  bereits nach dem Schieberegister der Turing Machine).

### 2.5 Harmonik
- Orgelpunkt auf dem Grundton über lange Strecken; Flächen und Mellotron halten Akkorde darüber.
- Transpositionsfolgen wie i–bVI–bVII–i, i–iv, i–bIII; in den dunklen Stilen Halbtonrückungen und
  Tritonus.
- Lead-Skalen: Moll-Pentatonik, äolisch, dorisch, phrygisch.

### 2.6 Klangfarbe und Geste
- Filterfahrten über 30 Sekunden bis mehrere Minuten, oft mit Halt und Umkehr.
- Resonanz steigt zum Höhepunkt; Decay-Änderungen verwandeln eine Sequenz vom Tupfen ins Legato.
- Echo-Rückkopplung als Geste: ein "Wurf" am Phrasenende, Selbstoszillation als Übergang.
- Phaser über String-Machine und Flächen.

### 2.7 Form eines Stücks

| Abschnitt | Inhalt | Länge (Hypothese) |
|---|---|---|
| Atmosphäre | Drones, Wind aus gefiltertem Rauschen, kosmische Sweeps, Mellotron-Chor | 1 bis 5 min |
| Einsatz | die erste Reihe, Filter geschlossen, taucht aus dem Echo auf | 0,5 bis 2 min |
| Aufbau | Filter öffnet; zweite Reihe, Bass-Sequenz, Flächen; im Stil "Melodic" das Schlagzeug | 2 bis 8 min |
| Lead | Solo über den Reihen, Glide, Bends | 2 bis 6 min |
| Höhepunkt | alle Schichten, Filter offen, viel Echo | 1 bis 4 min |
| Abbau / Wechsel | Sequenz hört auf oder transponiert, Flächen allein | 1 bis 3 min |
| zweite Sequenzphase | neue Reihe, neues Tempo oder neue Tonart | wie oben |
| Ausklang | die Sequenz löst sich in Echos und Atmosphäre auf | 1 bis 4 min |

Stücke 8 bis 30 Minuten, in den Stilen "Cosmic" und "Drift" bis 40. Ein Konzert oder Album sind drei
bis acht Stücke mit Überleitungen.

### 2.8 Worin sie sich unterscheiden: fünf Stilprofile

Ein Stilprofil ist, wie in Phosphene, ein Vektor von etwa 60 Gewichten und Bereichen (Tempo, Reihenzahl,
Längenverteilung, Transpositionsrate, Skalen, Gestentempo, Echo-Anteil, Mellotron-Anteil,
Schlagzeug, Formgewichte, Lautheitsziel). Zwischen Profilen wird interpoliert.

| Profil | Tempo | Schlagzeug | Palette und Form | Hörreferenz |
|---|---|---|---|---|
| **Cosmic** | 105 bis 125 | keins | Moog-Reihen, Mellotron-Chor und -Streicher, String-Machine, lange Intros, langsamer Aufbau | Redshift, Syndromeda, Stürtzer |
| **Doom** | 90 bis 120 | keins, selten Einzelschläge | tiefes Register, Dissonanz, Halbtonrückungen, Rauschen, schwere Flächen | ['ramp], Redshift, Thalaron, Runes Order |
| **Melodic** | 115 bis 135 | elektronisches Kit, spät im Stück | melodischere Leads, mehr Akkordwechsel, kürzere Stücke (8 bis 15 min) | Ron Boots, Syndromeda |
| **Modern** | 100 bis 130 | spärlich, modern | hybride, polierte Klanggestaltung, Wavetable und Granular, filmische Flächen | Quaeschning solo und mit Schnauss, Ian Boddy |
| **Drift** | frei, wechselnd | keins | lange Ambient-Phasen zwischen Sequenz-Episoden, Tempo- und Tonartdrift | Dark Side of the Moog, Stürtzers lange Ambient-Seiten |

Tempobereiche sind Hypothesen. Für alle fünf Profile liegen inzwischen Referenzen vor (Abschnitt 14);
es fehlt nur Redshift. Tangerine Dream bleibt bewusst draußen, auch bei Quaeschning zählen die
Soloarbeiten (Entscheidung 24.09.2026).

### 2.9 Stand der Technik (SOTA)

**Die Szene heute.** Die zeitgenössische Berlin School verbindet die Sequenzer-Tradition mit
Ambient-Flächen und moderner Produktion: Stürtzer spielt improvisierte Livestreams und veröffentlicht
auf Synphaera; Tangerine Dream kombiniert auf *Raum* Echtzeit-Kompositionen mit Studioproduktion und
arbeitet live mit Modularsystem, Rack-Synthesizern und iPad-Instrumenten. Hybride Arbeitsweisen
(Eurorack plus DAW) sind der Normalfall, nicht die Ausnahme.

**Generierung.** Berlin School ist Prozessmusik im Sinne von Steve Reichs "Music as a Gradual
Process": Die Töne sind einfach, der Prozess trägt. Dafür reichen regelbasierte Verfahren mit Zustand:
Schieberegister-Mutation (Turing Machine), Polymetrie, Euklidische Gate-Muster, Constraint-Markov-Ketten
(Pachet und Roy 2011, in Phosphene umgesetzt). Große symbolische Modelle bringen hier wenig, weil es
kaum Material gibt (kein Berlin-School-MIDI-Korpus bekannt) und weil die Sequenzen selbst kurz sind.
Neuronale Audio-Generatoren (MusicGen, Stable Audio) sind für 30 steuerbare, deterministische Minuten
in Echtzeit ungeeignet (Abschnitt 1).

**Klangsynthese.** Moog-Leiter als nichtlineares ZDF-Modell (Huovilainen 2004, Zavalishin, D'Angelo
und Välimäki 2014; in Phosphene umgesetzt), PolyBLEP-Oszillatoren, Bandmaschinen (Chowdhury 2019;
Echoplex nach Arnardottir, Abel, Smith 2008), Eimerkettenspeicher (Raffel und Smith 2010; Holters und
Parker 2018), Federhall (Välimäki, Parker, Abel 2010; Parker 2011), Plattenhall (Dattorro 1997).

**Mellotron.** Ein akademisches Modell der ganzen Maschine habe ich nicht gefunden. Kommerzielle
Emulationen spielen Samples mit modelliertem Bandlauf (Arturia Mellotron V) oder erzeugen den Klang per
FM (Tapeworm). Ephemeris geht einen dritten Weg: die Quellen synthetisch, die Bandmaschine als Modell
(5.4). Das ist das neueste und riskanteste Stück des Plans (Abschnitt 13).

## 3. Architektur

```
 Stilprofil + Seed + Spannungsbogen + Sperren
            │
            ▼
   ┌──────────────────────┐  Partitur (Noten, Gesten als Kurven,     ┌──────────────────────┐
   │  Composer-Thread     │  Rack-Ereignisse, Marken; 16+ Takte voraus)│  Audio-Thread        │
   │  Konzert → Stück →   │ ───────── lock-free Queue ───────────────▶│  Sequencer (sample-  │
   │  Phase → Rack-Modell │                                            │  genau) → Stimmen →  │
   │  + Gesten-Engine     │◀── Positionsrückmeldung, Live-Eingriffe ──│  Mixer → Raum →      │
   └──────────────────────┘                                            │  Master              │
            │                                                          └──────────────────────┘
            ▼                                                                     │
   MIDI-Export (SMF 1), .ephset, OSC-Cues                           Offline-Render (Orakel, Stems),
                                                                    Lautheitsmessung, Recorder
```

**Der Unterschied zu Phosphene.** Der Komponist lässt ein **Modell des Sequenzer-Racks** voraus
laufen: Reihen mit Zustand, Transposition, Mutation. Er schreibt dabei zweierlei in die Partitur:
Noten (für Audio und MIDI) und Rack-Ereignisse (Transposition, Längenwechsel, Mutation), damit Sperren
und Neuwürfeln auf der Ebene der Reihe arbeiten können. **Gesten** sind parametrische Kurven (Ziel,
Start, Ende, Dauer, Form) und dürfen über das Vorausfenster hinausreichen; der Audio-Thread wertet sie
aus, statt Stützpunkte zu lesen.

**Schichten im Kern (`Core/`):**
- `eph/Vec.h`: SIMD-Lanes (aus Phosphene).
- `eph/Clock.h`, `eph/Sequencer.h`: Tempo-Karte mit Tempowechseln zwischen Phasen, sample-genaue
  Ereignisse, Host-Sync.
- `eph/Score.h`: Partitur mit Noten, Gesten-Kurven, Rack-Ereignissen, Marken.
- `eph/Rack.h`: das Sequenzer-Modell (5.1), im Komponisten und in der Live-Bedienung dasselbe.
- `eph/Gesture.h`: die Gesten-Engine (6.4).
- `eph/compose/*`: Form, Harmonie, Reihen-Bau, Lead, Konzert.
- `eph/synth/*`: Modularstimme, Lead, Mellotron, String-Machine, Flächen, Atmosphäre, Schlagzeug.
- `eph/fx/*`: Bandecho, BBD, Feder, Platte/FDN, Phaser, Ensemble.
- `eph/mix/*`, `eph/Params.h`, `eph/Midi.h`.

**Threads und Determinismus** wie Phosphene: Audio allokiert nie; der Composer arbeitet in Häppchen
mit der Frist "Queue nie unter 8 Takten"; ein RNG pro Modul mit `fork()` aus dem Seed. Die analoge
Drift hängt am Seed und an der Beat-Position, nie an der Wanduhr.

## 4. Wiederverwendung

Vorschlag wie bei Phosphene: **Modulkopie, kein Link.** Jede kopierte Datei nennt im Kopf Herkunft und
Stand. Hauptquelle ist Phosphene, weil es die neuere Architektur mit Komponist, Partitur, Plugin und
Quest hat; Noctuary liefert Raum, Modulation und das Vorbild der Mutation.

| Modul | Herkunft | Einsatz hier | Anpassung |
|---|---|---|---|
| `Vec.h`, `Halfband.h`, `Adaa.h`, `Dsp.h` | Phosphene | überall | Namensraum |
| `Params.h` (Blöcke pro Modulinstanz) | Phosphene | alle Parameter | neue Blöcke `row1..8`, `voice`, `tape`, `strings` ... |
| `Clock.h`, `Score.h`, `Midi.h`, `MidiMap.h`, `SetFile.h` | Phosphene | Takt, Partitur, Export | Gesten als parametrische Kurven; Tempowechsel; `.ephset` |
| `Composer.*`, `Form.*` | Phosphene | Gerüst des Composer-Threads, Sperren, Vorauslauf | Formgrammatik für lange Stücke neu (6.2) |
| `Harmony.h` | Phosphene | Skalen, Tonartenreise, Stimmführung der Flächen | Transpositionsfolgen ergänzen |
| `Melody.*` (Constraint-Markov, Motiv-Variation) | Phosphene | Lead-Soli, melodische Reihen | neu gewichten, ohne Psytrance-Korpus |
| `Oscillator.h` (PolyBLEP) | Phosphene | VCOs | Dreieck, PWM, Hard-Sync, Drift ergänzen |
| `Ladder.h` (ZDF-Moog-Leiter, Lane-Template) | Phosphene | Hauptfilter jeder Modularstimme | 2× Oversampling über alle Reihen in Lanes |
| `Poly.h`, `WaveTable*`, `library.phoswt` | Phosphene | Flächen, Stil "Modern", PPG-artige Tabellen | Supersaw nicht als Standard |
| `Vocal.h` (Formant-Chorstimme) | Phosphene | Ausgangspunkt der Mellotron-Chorquelle | zum Ensemble erweitern, Glottis-Modell (5.4); prüfen, ob tragfähig |
| `Disperser.h` (Allpass-Dispersion) | Phosphene | Chirps des Federhalls | als Kaskade nach Parker 2011 |
| `Perc.h`, `PercKernel.h`, `Rhythm.h` | Phosphene | Schlagzeug je Stilprofil | einfachere Muster, späte Einsätze |
| `TempoDelay.h` | Phosphene | Basis des Bandechos | Bandmodell im Rückkopplungspfad (5.8) |
| `Reverb.h` (FDN, 8 Linien) | Phosphene | Hall | Modi aus Noctuary ergänzen |
| `Dynamics.*`, `Loudness.*` | Phosphene | Master, Meter | sanfter, Limiter nur als Schutz |
| `Cue.h` | Phosphene | OSC-Cues für Kaleidoscope | Adressen `/eph/...` |
| `Quality.h` | Phosphene | Qualitätsstufen Desktop/Quest | neue Stufen (9) |
| `Probe`, `Audibility`, `Rating`, `Gallery` | Phosphene | Mess- und Bewertungsgerüst | vor dem Kopieren prüfen, was davon passt |
| `Plugin/`, `Quest/`, `Deploy/`, `Tests/`, `Tools/manual`, `Tools/release` | Phosphene | Gerüste | Projektname, Pfade |
| `Near.h` Sequence (Schieberegister, Transposition mit dem Grundton, atmender Filter) | Noctuary | Vorbild der Reihen-Mutation | im `Rack` neu, die Regeln übernehmen |
| `Modulation.h` (LFOs, Lorenz, Rössler, Kuramoto) | Noctuary | langsame Drift der Gesten, gekoppelte Drift der Oszillatoren | auf das Block-Parametersystem umschreiben |
| `Effects.h` (StereoDelay, Ensemble, Reverb mit vier Modi) | Noctuary | Echo, Ensemble der String-Machine, Hall | keine |
| `Convolution.h` | Noctuary | Platten- und Hallräume mit Morph | optional |
| `Cloud.h`, `GrainRing.h` | Noctuary | kosmische Texturen im Intro, aus dem eigenen Bus | keine |
| `Voice.h` (gestrichene Saite) | Noctuary | Kandidat für die Streicherquelle des Mellotrons | prüfen |
| `Filter.h` (Formant), `ZPlane.h` | Noctuary | Vokalfilter für Chöre, kosmische Sweeps | keine |

Nicht übernommen: aus Phosphene `Acid`, `Kick`, `Bass`, `Texture`, die SFX-Bank, `TranceGate`, der
Psytrance-Korpus und die Transformer-Gewichte; aus Noctuary `ClusterBrain`, `Cosmos`, die
Preset-Bibliothek, `Memory`, `Tuning` (Berlin School ist gleichstufig; der Charakter kommt aus der
Drift).

## 5. Die Klangerzeuger

Jeder Erzeuger hat einen skalaren Referenzpfad und einen Lane-Pfad, wie in Phosphene.

### 5.1 Das Sequenzer-Rack (der Kern)

- **Reihen:** bis zu acht (Quest: vier bis sechs, gemessen zu entscheiden). Jede Reihe: Länge 1 bis
  32, Taktteiler (Viertel bis Zweiunddreißigstel, triolisch, punktiert), Richtung (vorwärts,
  rückwärts, Pendel, Zufallsschritt).
- **Pro Schritt:** Tonstufe und Oktave, Gate (an, aus, gebunden), Gate-Länge, Akzent, Slide, Ratchet
  (1 bis 4), Wahrscheinlichkeit, Skip, und eine zweite Wertreihe für Filter oder Hüllkurve (wie die
  zweite und dritte Reihe des Moog 960).
- **Transposition:** Eingang von der Transpositionsspur des Komponisten oder von einer langsamen Reihe
  (eine Reihe transponiert die andere: der Epizykel).
- **Mutation:** Schieberegister nach der Turing Machine (Whitwell 2012), wie in Noctuarys Nah-Ebene;
  dazu musikalische Operatoren: zwei Schritte tauschen, Oktave kippen, rotieren, Ton hinzufügen oder
  wegnehmen. Mutationsbudget pro Phase.
- **Synchronisation:** Reihen laufen frei gegeneinander oder starten an Phrasengrenzen neu. Der
  Komponist kennt die **Konjunktionen**, die Momente, in denen alle Reihen wieder auf dem ersten
  Schritt stehen (kgV der Längen), und legt Formwechsel bevorzugt dorthin.
- Timing sample-genau; eine winzige Takt-Unruhe als Option ("analoger Takt"), standardmäßig aus.

### 5.2 Die Modularstimme

- **Drei VCOs:** Sägezahn, Puls mit PWM, Dreieck, Sinus; Hard-Sync; VCO 3 wahlweise als langsamer
  Modulator. Dazu Rauschen.
- **Mixer mit Übersteuerung** vor dem Filter (der Mixer des Minimoog wurde bewusst übersteuert).
- **Filter:** die ZDF-Leiter aus Phosphene (24 dB, 2× Oversampling), wahlweise ein 12-dB-SVF.
- **VCA und Hüllkurven** mit RC-Kurven (exponentiell), Hüllkurve auf Cutoff, Key-Tracking.
- **Drift:** jeder VCO mit langsamer Tonhöhendrift als Ornstein-Uhlenbeck-Prozess (einige Cent), eine
  kleine Abweichung pro Note, eine "Aufwärm"-Phase am Anfang; über Noctuarys Kuramoto-Kopplung
  optional gemeinsam driftend. Die Größenordnungen sind zu messen; eine Standardliteratur dazu kenne
  ich nicht.
- **Lanes:** eine Stimme pro Reihe; acht Reihen sind ein AVX-Register für Leiter, Hüllkurven und VCA.

### 5.3 Lead

- Monophone Modularstimme mit Glide (konstante Zeit oder konstante Rate), Legato, Vibrato als Geste des
  Modulationsrads, Bends (ein Ganzton hinauf in den Ton), eigenes Echo.
- Stil "Modern": zusätzlich Wavetable-Leads aus `Poly`.
- Die Noten kommen aus dem Lead-Generator des Komponisten (6.7).

### 5.4 Das Mellotron, synthetisch

Ein Mellotron ist eine Bandmaschine pro Taste: 35 Tasten (G2 bis F5), jede mit einem eigenen Band von
etwa acht Sekunden. Der Klang ist die Aufnahme **plus** die Maschine. Beides wird modelliert:

**Die Quellen.**
- **Chor:** ein Ensemble von 8 bis 16 virtuellen Sängern pro Taste. Glottis-Anregung nach dem LF-Modell
  (Fant, Liljencrants, Lin 1985), Formantfilter (Klatt 1980) auf Vokalen wie "a" und "o", je Sänger
  eigenes Vibrato (5 bis 6 Hz), eigene Mikro-Schwankungen in Tonhöhe und Lautstärke, kleine
  Verstimmung; die Ensemble-Streuung nach Ternströms Arbeiten zum Chorklang. Phosphenes `Vocal`
  (Formant-Chorstimme) ist der Ausgangspunkt.
- **Streicher:** Ensemble aus bandbegrenzten Sägezähnen oder dem Streichermodell aus Noctuary, mit
  Korpusresonanzen.
- **Flöte:** additiv mit Atemrauschen und Anblasgeräusch.

**Die Maschine.**
- **Jede Taste hat ihr Band:** eigene Verstimmung (einige Cent), eigener Pegel, eigene Klangfarbe,
  eigene Einsatzverzögerung.
- **Bandlauf:** Wow (langsam) und Flutter (schneller) plus Zufallsanteil; Messung nach der Bewertung
  für Gleichlaufschwankungen (IEC 60386).
- **Das Bandende:** Nach etwa acht Sekunden ist der Ton vorbei, egal wie lange die Taste gehalten wird.
  Der Komponist weiß das und greift neu, wie ein Spieler.
- **Motorlast:** Sind viele Tasten gedrückt, sinkt die Tonhöhe leicht. Die Größe ist an Aufnahmen zu
  messen.
- **Kopf und Elektronik:** Höhenabfall, Head Bump, leichte Sättigung (Bandmodell nach Chowdhury 2019),
  Bandrauschen, der kurze "Ruck" der Andruckrolle beim Einsatz, ein schnelles Ende beim Loslassen.

**Kalibrierung:** Spektren, Laufschwankungen und Einsätze werden an Mellotron-Aufnahmen gemessen; nur
die Zahlen kommen ins Projekt. **Benennung:** "Mellotron" ist ein Markenname; Oberfläche und Handbuch
nennen das Instrument neutral (Vorschlag: "Tape Keys"), die Dokumentation erklärt das Vorbild.

### 5.5 String-Machine

- Ein Mastertakt teilt sich auf alle Tasten herab (die Töne sind phasenstarr, das gehört zum Klang),
  Sägezahn in 8'- und 4'-Lage, Formantfilter.
- Das Wesentliche ist das **Ensemble**: drei Eimerkettenspeicher-Chorusse, moduliert von einem langsamen
  und einem schnellen LFO mit versetzter Phase (so die verbreitete Beschreibung der Solina; im Detail zu
  prüfen). BBD-Modell nach Holters und Parker 2018; Noctuarys `Ensemble` als Ausgangspunkt.
- Phaser danach als Geste.

### 5.6 Flächen, Drones, Atmosphäre

- Flächen als `Poly`-Instanz (VA und Wavetable), Stimmführung mit minimaler Bewegung (Phosphene
  `Harmony`).
- Drones: gehaltene Modularstimmen mit langsamen Filtergesten auf dem Grundton.
- Atmosphäre: Wind aus gefiltertem Rauschen mit Gesten, kosmische Sweeps (resonanter Filter über
  Rauschen, Phaser), Weltraumklänge (FM-Blips, Ringmodulation, Sample-and-Hold auf dem Filter),
  Granular-Wolken aus dem eigenen Bus (`Cloud`).
- **Tiefenregel:** Im Band unter etwa 150 Hz spielt zu jeder Zeit nur ein Besitzer, Bass-Sequenz oder
  Drone; der Komponist vergibt ihn (angelehnt an Phosphenes Tiefenregel).

### 5.7 Schlagzeug (je Stilprofil)

- Phosphenes Kit (fünf Engines) mit einfacheren Mustern.
- "Melodic": elektronisches Kit, Einsatz erst nach dem Aufbau der Reihen.
- "Modern": spärlich, Ticks, Rimshots, gefilterte Schläge.
- "Cosmic", "Doom", "Drift": aus, höchstens einzelne gestimmte Schläge.

### 5.8 Raum und Effekte

| Effekt | Verfahren | Literatur |
|---|---|---|
| Bandecho | Verzögerung mit moduliertem Lesekopf (Wow, Flutter), Sättigung und Höhenverlust in der Rückkopplung, Head Bump, optional mehrere Köpfe; tempo-synchron (punktierte Achtel als Standard) oder frei; Rückkopplung bis zur Selbstoszillation als Geste | Arnardottir, Abel, Smith 2008 (Echoplex); Chowdhury 2019 |
| BBD-Delay und Chorus | Eimerkette mit Ein- und Ausgangsfiltern, Taktmodulation | Raffel, Smith 2010; Holters, Parker 2018 |
| Federhall | Allpass-Dispersionskaskaden für die Chirps, Phosphenes `Disperser` | Välimäki, Parker, Abel 2010; Parker 2011 |
| Plattenhall | Dattorro-Struktur | Dattorro 1997 |
| Hall | FDN aus Phosphene, Modi aus Noctuary, optional Faltung | Schlecht, Habets (in Noctuary umgesetzt) |
| Phaser | ZDF-Phaser | Zavalishin; Kiiski, Esqueda, Välimäki 2016 |

**Mixer:** Kanalzug je Reihe und Erzeuger, Stereo-Verteilung der Reihen, langsames Auto-Pan, drei Sends
(Echo, Feder/Platte, Hall). **Master:** sanfter Bus-Kompressor (Giannoulis et al. 2012), Mono unter
etwa 100 Hz, True-Peak-Limiter nur als Schutz, Lautheitsziel je Profil (Hypothese −16 bis −11 LUFS,
an Referenzen zu messen), Meter nach BS.1770.

## 6. Der Komponist

### 6.1 Ebenen
1. **Konzert/Album** (45 bis 120 min): Zahl der Stücke, Dramaturgie (Nachtkonzert, Studioalbum,
   Livestream), Profil oder Profilreise, Tonartenreise, Überleitungen.
2. **Stück** (8 bis 40 min): Form aus der Grammatik (6.2), Tonart, Tempo je Sequenzphase,
   Instrumentierung, der **Rack-Patch** (welche Reihen, welche Längen, welche Stimmen).
3. **Phase** (1 bis 8 min): Aufbau, Höhepunkt, Abbau; Dichte, Filteröffnung, Echo-Anteil aus dem
   Spannungsbogen.
4. **Geste** (Sekunden bis Minuten) und **Schritt**.

### 6.2 Form-Grammatik für lange Stücke
```
Stück       → Atmo Körper Ausklang
Körper      → Sequenzphase (Wechsel Sequenzphase)*          (Cosmic, Doom: 1 bis 3 Phasen)
            | (Ambient Sequenzepisode)+ Ambient              (Drift)
Sequenzphase→ Einsatz Aufbau+ Lead? Höhepunkt Abbau?
Wechsel     → Brücke          (Flächen und Mellotron allein, Drone)
            | Überblendung    (neue Reihe über der alten, dann die alte hinaus)
            | Transposition   (dieselbe Reihe, neue Tonart)
```
Längen in Takten aus Verteilungen je Profil (16 bis 128 Takte pro Abschnitt), nicht streng in
Zweierpotenzen. Formwechsel bevorzugt an Konjunktionen der Reihen (5.1).

### 6.3 Schichtung und Instrumentierungs-Matrix
- Typische Einsatzfolge: Drone → Atmosphäre → Reihe 1 (Filter zu) → Filter öffnet über ein bis zwei
  Minuten → Reihe 2 (verzahnt oder oktaviert) → Flächen, Mellotron → Schlagzeug (nur "Melodic",
  "Modern") → Lead → Höhepunkt.
- Constraint: höchstens ein neues Element in N Takten; jedes Element kommt mit einer Geste (Filter
  auf, Echo-Einspeisung, Einblendung), nie hart.

### 6.4 Die Gesten-Engine
- Eine Geste: Zielparameter, Start, Ende, Dauer, Form.
- **Formen:** menschliche Bewegungen folgen dem Minimum-Jerk-Profil (Flash und Hogan 1985): glatte
  S-Kurven; dazu leichtes Zittern, Halte-Plateaus, Überschwingen mit Korrektur, gelegentlich ein
  schneller Griff.
- **Zwei-Hände-Regel:** höchstens zwei Handgesten gleichzeitig (LFOs zählen nicht). Das macht die
  Bewegung glaubwürdig und verhindert, dass sich alles zugleich ändert.
- **Gesten-Grammatik:** Filter öffnet im Aufbau, taucht am Wechsel ab; Echo-Wurf am Phrasenende;
  Resonanz steigt zum Höhepunkt; Decay wandelt Staccato in Legato.
- Der Spannungsbogen setzt die Ziele; Noctuarys Lorenz- und Kuramoto-Quellen liefern die langsame
  Drift darunter.

### 6.5 Harmonie und Transposition
- Tonart pro Stück oder Sequenzphase; Transpositionsfolgen aus einer Übergangsmatrix je Profil
  (i–bVI–bVII–i, i–iv, i–bIII; Halbton und Tritonus in "Doom"), Rate 4 bis 32 Takte.
- Flächen- und Mellotron-Akkorde aus Reihentönen und Transposition, mit minimaler Stimmbewegung.
- Orgelpunkt auf dem Grundton, solange die Drone liegt.

### 6.6 Reihen-Bau
- **Bass-Reihe:** grundtonlastig, Oktavsprünge, 16 Schritte, Akzente.
- **Gegenreihe:** ungerade Länge, Akkordtöne, höhere Lage.
- **Verzahnung:** zwei Reihen um einen Schritt versetzt; **Echo-Reihe:** Achtel plus punktiertes Echo.
- Melodischer Inhalt aus Phosphenes Constraint-Markov (Skala, Ambitus, Registertrennung zwischen den
  Reihen, Konsonanz zur Transposition), Gates aus Euklidischen Mustern (Toussaint 2005).
- Längenwahl nach dem gewünschten kgV: 16 gegen 13 fällt nach 208 Schritten wieder zusammen, 16 gegen
  12 schon nach 48.

### 6.7 Lead-Soli
- Phrasen aus Constraint-Markov mit Zielkontur, Moll-Pentatonik und Modi, lange Töne mit Vibrato,
  Glide und Bends als Gesten, Motive, die wiederkehren, Frage und Antwort mit den Reihen.
- Register über den Reihen; Dichte folgt dem Spannungsbogen.

### 6.8 Spannungsbogen
Wie Phosphene nach Farbood (2012): Der Bogen steuert Filteröffnung, Zahl der Reihen, Dichte, Register,
Echo-Anteil und Lautheit.

### 6.9 Überleitungen zwischen Stücken
Über Atmosphäre: eine Mellotron- oder Drone-Brücke, die Sequenz des alten Stücks löst sich im Echo auf,
die neue beginnt in eigenem Tempo. Kein Beatmatching.

### 6.10 Sperren und Neuwürfeln
Wie Phosphene: Konzert, Stück, Phase, Reihe und Gestenspur sind einzeln sperrbar und haben je einen
eigenen Seed-Zweig. Neu würfeln ersetzt nur Ungesperrtes.

### 6.11 Lernende Anteile
Zunächst keine. Ein Berlin-School-MIDI-Korpus ist nicht bekannt (`M:\Midi` wird noch geprüft), und die
Sequenzen sind einfach genug für Regeln. Später ein Ranker über Nutzerbewertungen (Phosphenes
`Rating`), der aus mehreren Kandidaten wählt.

## 7. MIDI-Export und Dateiformate
- **SMF Format 1**, PPQ 960, Tempo-Karte mit den Tempowechseln, Marker für Phasen und Stücke.
- Ein Track je Reihe, Lead, Flächen, Mellotron, String-Machine, Schlagzeug; Transposition als eigener
  Track.
- Gesten als CC-Verläufe (74 Cutoff, 71 Resonanz, weitere frei) mit 1/32-Auflösung.
- WAV/FLAC-Stems je Erzeuger aus dem Offline-Render.
- **`.ephset`** (Text wie `.phosset`): Seed, Profil(e), Bogen, Sperren, auf Wunsch die ausgerollte
  Partitur.
- **Presets:** Klang-Presets je Stimme, Rack-Patches (Reihen, Längen, Stimmen), Stilprofile,
  Konzert-Dramaturgien.

## 8. GUI

### 8.1 Desktop (JUCE 9, Layout aus Parametern, `EPH_SHOT`-Screenshot-Modus, Sprache Englisch)

| Tab | Inhalt |
|---|---|
| **Concert** | Länge, Zahl der Stücke, Profil(e) mit Morph, Spannungsbogen, Seed, Generieren/Play/Stop, Meter |
| **Arrange** | Zeitleiste Stücke → Phasen, Instrumentierungs-Matrix, Sperren, Neuwürfeln, Sprung |
| **Rack** | die Reihen als Schrittraster mit Länge, Teiler, Mutation; dazu die **Orrery-Ansicht**: jede Reihe als Umlaufbahn, die Konjunktionen sichtbar |
| **Voices** | Modularstimme je Reihe: VCOs, Mixer, Leiter, Hüllkurven, Drift |
| **Lead** | Stimme, Glide, Vibrato, Solo-Regeln |
| **Tape Keys** | Chor/Streicher/Flöte, Bandlauf, Alter der Bänder, Motorlast |
| **Strings / Pads** | String-Machine mit Ensemble, Flächen |
| **Atmos** | Drones, Wind, Sweeps, Weltraumklänge, Granular |
| **Drums** | Kit und Muster je Profil |
| **Gestures** | Gestenspuren als Kurven, Zwei-Hände-Regel, Gestentempo |
| **Space** | Bandecho, BBD, Feder, Platte, Hall |
| **Mixer / Master** | Kanalzüge, Sends, Master, Meter |
| **Perform** | live: Filter greifen, Transpositionstaste, Reihe einfrieren, mutieren, Echo-Wurf; MIDI-Learn |
| **Export** | MIDI, Stems, `.ephset` |
| **Style** | Stilprofile ansehen, editieren, aus Referenzen kalibrieren |

### 8.2 Quest 2
Der komplette Generator läuft auf dem Gerät (Entscheidung 23.09.2026). Die Hände sind die Hände des
Spielers: linke Hand Cutoff der Reihe im Fokus, rechte Hand Transposition und Echo-Wurf, Pinch zum
Einfrieren. Die Orrery-Ansicht wird im Raum zum Planetensystem: jede Reihe eine Bahn, Konjunktionen als
Lichtlinie. Handmenü wie in Noctuary Quest.

### 8.3 Kaleidoscope-Kopplung
OSC `/eph/beat`, `/eph/phase`, `/eph/conjunction`, `/eph/key` aus `Cue.h`.

## 9. Vektorisierung
- Prinzip wie Phosphene: Structure-of-Arrays über Stimmen und Lanes, skalarer Referenzpfad als
  Orakel, bitgleiche Tests.
- **Natürliche Lane-Gruppen:** acht Reihen-Stimmen (Leiter, Hüllkurven, VCA in einem AVX-Register),
  Sänger eines Mellotron-Chors, Stimmen der String-Machine, FDN.
- **Umgesetzt (25.09.2026):** die Modularstimmen (Reihen, Lead, Drone) als `ModVoiceBank` mit dem
  Lane-Kernel `VoiceKernel.h`; siehe "Stand der Umsetzung".
- **Grober CPU-Rahmen** (Desktop, ein Kern, 48 kHz, in Phase 1 bis 3 zu prüfen):

| Modul | Ziel |
|---|---|
| Acht Reihen-Stimmen (2× Oversampling) | < 3 % |
| Lead | < 0,5 % |
| Mellotron (Chor, 8 Tasten × 12 Sänger) | < 4 % |
| String-Machine + Ensemble | < 1,5 % |
| Flächen, Drones, Atmosphäre | < 3 % |
| Schlagzeug | < 1 % |
| Echo, BBD, Feder, Platte, Hall, Master | < 4 % |
| Summe Höhepunkt | < 17 % |

- **Quest-Stufe:** Reihen 8 → 4 bis 6, Sänger 12 → 4 bis 6, Oversampling nur an der Leiter,
  Faltung aus. Werte auf dem Gerät messen.

## 10. Plattformen und Build
- Aufbau wie Phosphene: `Core/`, `Plugin/` (JUCE 9, VST3 + Standalone), `Quest/`, `Tools/render`
  (`eph_render`: Offline-Render, `--bench`, `--midi`, `--stems`, `--set-file`, `--seed`, `--style`),
  `Tools/*.py`, `Tests/`, `Deploy/`, `docs/`.
- CMake-Optionen `EPH_BUILD_PLUGIN`, `EPH_BUILD_TOOLS`, `EPH_AVX2`, `EPH_STATIC_RUNTIME`; kein
  Fast-Math.
- **VST3:** Host-Playhead als Takt. Die eigenen Tempowechsel eines Stücks gehen im Host nicht auf;
  Vorschlag: dort gilt das Host-Tempo, und die Tempo-Karte kommt über den MIDI-Export in die DAW
  (Entscheidung offen, Abschnitt 14).
- **Standalone:** eigene Uhr, ASIO/WASAPI, Recorder, Exporte.
- **Quest:** `EPH_MUTE=1` für Tests.

## 11. Tests und Messungen

### 11.1 Selbsttest (`eph_selftest`, Muster Phosphene)
- Oszillatoren: Aliasing; Drift beschränkt und bei gleichem Seed bitgleich.
- Leiter: Cutoff, Selbstoszillation, Lane-Pfad gegen skalar.
- Rack: Konjunktionen nach kgV, Transposition, Mutation deterministisch, Sperren bitgleich.
- Gesten: Minimum-Jerk-Profil, Zwei-Hände-Regel nie verletzt.
- Mellotron: Bandende nach der eingestellten Zeit, Spektrum der Laufschwankungen im Ziel, Motorlast
  monoton in der Tastenzahl.
- Bandecho: Abfall und Höhenverlust je Wiederholung; Feder: Gruppenlaufzeit der Dispersion.
- Form-Constraints, MIDI-Rundlauf, Lautheit und True Peak.

### 11.2 Vektor-, Host- und Build-Tests
Aus Phosphene übernommen: `vectest` in drei Builds, `hosttest`, `vst3test`, `questguard`, pluginval
auf Strenge 10.

### 11.3 Hörprüfung
Solo-Renders je Erzeuger und Phase; A/B-Blindvergleich zweier Seeds.

### 11.4 Referenzanalyse (`Tools/analyze_ref.py`)
Tempo aus der Periodizität der Onsets; **Reihenlängen** aus der Autokorrelation der Onset-Muster;
Filterfahrten als Verlauf des Spektralschwerpunkts (über Leistung, nicht Betrag: Messfalle aus
Noctuary) mit Zeitkonstanten; Abschnittsgrenzen nach Foote (2000); Transpositionsrate aus dem
Chroma-Grundton; Zahl der Schichten über die Zeit; Lautheit und LRA. Nur Statistiken werden
gespeichert.

## 12. Phasen und Meilensteine

Aufwand in Arbeitstagen nach der Erfahrung mit Phosphene; die Reihenfolge ist verbindlicher als die
Zahlen.

| Phase | Inhalt | Prüfstein | Tage |
|---|---|---|---|
| **0 Gerüst** | Repo, CMake, Modulkopie aus Phosphene, Parametersystem, Clock, Partitur mit Gesten-Kurven, `eph_render`, Selbsttest-Skelett | `eph_render` gibt Stille mit Tempo-Karte aus; Vec-Tests grün | 2 |
| **1 Eine Sequenz, die atmet** | Rack mit ein bis zwei Reihen, Modularstimme mit Drift, Leiter mit 2× OS, Bandecho, eine Filtergeste, erste Referenzmessung | fünf Minuten einer Sequenz, deren Filter sich glaubwürdig bewegt; erste CPU-Zahlen | 4 |
| **2 Polymetrie und Gesten** | acht Reihen, Transpositionsreihe, Mutation, Konjunktionen, Gesten-Engine mit Zwei-Hände-Regel, Harmonie, Lead mit Glide | zehn Minuten mit drei Reihen und Transposition; Rack-Tests grün | 5 |
| **3 Mellotron und Raum** | synthetisches Mellotron (Chor, Streicher, Flöte, Bandmaschine), String-Machine mit BBD-Ensemble, Flächen, Drones, Atmosphäre, Feder, Platte, Hall, Schlagzeug | Hörvergleich gegen Mellotron-Aufnahmen; Maschinen-Tests grün | 6 |
| **4 Komponist** | Formgrammatik, Instrumentierungs-Matrix, Spannungsbogen, Lead-Soli, Konzert mit Überleitungen, Sperren, fünf Stilprofile, Kalibrierung, `.ephset`, MIDI | 60-Minuten-Konzert aus einem Seed; Determinismus; MIDI in einer DAW geöffnet | 6 |
| **5 GUI** | Tabs, Orrery-Ansicht, Arrange, Perform, Handbuch-Generator | Standalone und VST3 bedienbar; pluginval grün | 6 |
| **6 Quest** | NDK-Build, Qualitätsstufen, Performer-Oberfläche, Bahnen im Raum | Konzert läuft auf der Quest 2 unter 30 % eines Kerns | 4 |
| **7 Qualität und Release** | Hörrunden, Nachkalibrierung, Cues, Installer, Handbuch | v1.0 | 5 |

Nach Phase 1 gibt es den ersten hörbaren Prüfstein, nach Phase 4 ist das Produkt inhaltlich komplett.
Die GUI kommt spät, weil das Layout aus den Parametern entsteht.

## 13. Risiken
1. **Musikalische Qualität über lange Dauer** (größtes Risiko): Langeweile oder Beliebigkeit.
   Gegenmittel: früher Prüfstein, gemessene Entwicklungsraten der Referenzen, Sperren und Neuwürfeln,
   später der Ranker.
2. **Glaubwürdigkeit des synthetischen Mellotrons**, besonders des Chors: Es gibt kein Vorbild. Gegenmittel:
   Messung gegen Aufnahmen, früher Hörvergleich in Phase 3, notfalls ein ehrlich "Mellotron-artiges"
   Instrument statt einer Kopie.
3. **Referenzmaterial ungleich verteilt**: "Modern" und "Drift" sind reich belegt, "Doom" hat nur
   zwei ['ramp]-Alben mit Sequenzen (14). Redshift fehlt. Gegenmittel:
   je Profil gleich viele Stücke in die Kalibrierung nehmen, damit Boddy nicht alles dominiert.
4. **Tempowechsel im Host**: Hosts geben das Tempo vor (10).
5. **Quest-Budget**: Chor-Ensembles und acht Reihen mit Oversampling. Gegenmittel: Qualitätsstufen ab
   Phase 1, Messung auf dem Gerät.
6. **Rechtliches**: Künstlernamen nur in der Dokumentation, keine Fremd-Samples, "Mellotron" nicht als
   Produktbezeichnung in der Oberfläche.
7. **Name**: vor dem Release prüfen, ob "Ephemeris" als Audio-Software schon vergeben ist.

## 14. Entscheidungen des Nutzers (23.09.2026)
1. **Name:** Ephemeris.
2. **Mellotron:** synthetisch nach dem Stand der Technik, keine Samples.
3. **Quest:** ja, der komplette Generator auf dem Gerät.
4. **Schlagzeug:** je nach Stilprofil.
5. **Plattformen:** VST3 und Standalone wie die anderen Generatoren.
6. **Referenzmaterial** (Bestand 24.09.2026, alles FLAC oder MP3; gespeichert werden nur Messwerte):

   | Profil | Aufnahmen | Ort |
   |---|---|---|
   | Cosmic | Martin Stürtzer: *Celestial Tides*, *Protostar*, *Timelapse*; Syndromeda: *Connected!*, *In Touch With The Stars*, *The Alien Abduction Phenomenon*, *XXX* | `G:\Downloads\JDownloader\Israbox`; NAS |
   | Doom | ['ramp]: *Frozen Radios*, *Nodular*; Runes Order: zwölf Alben, dazu *Secret Place (The Final Chapter)* mit Berlin-School-Anleihen | NAS; `C:\Users\Rene\Desktop\Kandidaten\Pop - Kopie` |
   | Melodic | Ron Boots: *Area Movement*, *Backgrounds*, *Close, But Not Touching*, *Different Stories And Twisted Tales*, *Standing In The Rain*; mit Frank Klare *Monumental Dreams*; mit Harold van der Heijden *Of Desolate Places And Urban Jungles* | NAS |
   | Modern | Ian Boddy: 19 Soloalben und 17 Kollaborationen (u. a. mit Erik Wøllo, Markus Reuter, Robert Rich); dazu *Transmissions* (Boddy/Wøllo); Thorsten Quaeschning und Ulrich Schnauss: *Synthwaves* | NAS; `G:\Downloads\JDownloader\Israbox` |
   | Drift | The Dark Side of the Moog I bis XI, *The Evolution of The Dark Side of the Moog*; Pete Namlooks weitere Reihen als Umfeld | NAS |

   NAS-Pfad: `\\192.168.178.75\SambaFestplatte\Musik\Alben\`. Es fehlt Redshift. Die Zuordnung
   Profil → Ordner steht in einer Textdatei (`Tools/ref_sets.txt`), die `analyze_ref.py` liest.
7. **Kein Tangerine Dream als Referenz** (24.09.2026): Der Nutzer mag die klassischen Alben nicht, und
   auch bei Quaeschning zählen die Soloarbeiten und Kollaborationen (*Synthwaves* mit Ulrich Schnauss),
   nicht seine Tangerine-Dream-Alben. Tangerine Dream geht weder in die Kalibrierung noch in die
   Hörreferenzen.

Die vier zunächst offenen Punkte hat der Nutzer am 24.09.2026 so bestätigt:
- Modulkopie statt Link, wie bei Phosphene.
- GUI und Handbuch auf Englisch.
- Keine lernenden Anteile zu Beginn; später ein Ranker.
- Im Host gilt das Host-Tempo; Tempowechsel kommen über den MIDI-Export.


## 15. Literatur (Auswahl, je Baustein)

- Reich, S. (1968). Music as a Gradual Process. In: Writings on Music 1965–2000. (Prozessmusik)
- Whitwell, T. (2012). Turing Machine. Music Thing Modular, offene Hardware. (Schieberegister-Sequenzer)
- Toussaint, G. (2005). The Euclidean Algorithm Generates Traditional Musical Rhythms. BRIDGES.
- Pachet, F.; Roy, P. (2011). Markov Constraints: Steerable Generation of Markov Sequences. Constraints.
- Farbood, M. (2012). A Parametric, Temporal Model of Musical Tension. Music Perception.
- Flash, T.; Hogan, N. (1985). The Coordination of Arm Movements: An Experimentally Confirmed Mathematical Model. Journal of Neuroscience 5(7). (Minimum Jerk, Gesten)
- Uhlenbeck, G. E.; Ornstein, L. S. (1930). On the Theory of the Brownian Motion. Physical Review 36. (Drift)
- Välimäki, V.; Huovilainen, A. (2007). Antialiasing Oscillators in Subtractive Synthesis. IEEE Signal Processing Magazine 24(2).
- Stilson, T.; Smith, J. O. (1996). Analyzing the Moog VCF with Considerations for Digital Implementation. ICMC.
- Huovilainen, A. (2004). Non-Linear Digital Implementation of the Moog Ladder Filter. DAFx.
- D'Angelo, S.; Välimäki, V. (2014). Generalized Moog Ladder Filter, Part II. IEEE/ACM TASLP.
- Zavalishin, V. The Art of VA Filter Design. Native Instruments.
- Fant, G.; Liljencrants, J.; Lin, Q. (1985). A Four-Parameter Model of Glottal Flow. STL-QPSR 4/1985.
- Klatt, D. H. (1980). Software for a Cascade/Parallel Formant Synthesizer. JASA 67(3).
- Ternström, S. (2003). Choir Acoustics: An Overview of Scientific Research Published to Date. International Journal of Research in Choral Singing 1(1).
- Chowdhury, J. (2019). Real-Time Physical Modelling for Analog Tape Machines. DAFx.
- Arnardottir, S.; Abel, J. S.; Smith, J. O. (2008). A Digital Model of the Echoplex Tape Delay. AES 125th Convention.
- Raffel, C.; Smith, J. O. (2010). Practical Modeling of Bucket-Brigade Device Circuits. DAFx.
- Holters, M.; Parker, J. (2018). A Combined Model for a Bucket Brigade Device and Its Input and Output Filters. DAFx.
- Välimäki, V.; Parker, J.; Abel, J. S. (2010). Parametric Spring Reverberation Effect. JAES 58(7/8).
- Parker, J. (2011). Efficient Dispersion Generation Structures for Spring Reverb Emulation. EURASIP Journal on Advances in Signal Processing.
- Dattorro, J. (1997). Effect Design, Part 1: Reverberator and Other Filters. JAES 45(9).
- Kiiski, R.; Esqueda, F.; Välimäki, V. (2016). Time-Variant Gray-Box Modeling of a Phaser Pedal. DAFx.
- Giannoulis, D.; Massberg, M.; Reiss, J. D. (2012). Digital Dynamic Range Compressor Design. JAES.
- Foote, J. (2000). Automatic Audio Segmentation Using a Measure of Novelty. ICME.
- IEC 60386 (Messung von Gleichlaufschwankungen); ITU-R BS.1770-4, EBU R 128 (Lautheit).

## Quellen der Recherche (23.09.2026)

- Thalaron, *Microgravity*: https://phelios.bandcamp.com/album/microgravity
- Martin Stürtzer, *Theta Serpentis*: https://phelios.bandcamp.com/album/theta-serpentis
- Martin Stürtzer, *Epsilon Eridani*: https://phelios.bandcamp.com/album/epsilon-eridani
- Martin Stürtzer, *Illumination Cycle*: https://synphaera.bandcamp.com/album/illumination-cycle
- Martin Stürtzer, Veröffentlichungen 2023: https://martinstuertzer.de/releases2023/
- Syndromeda, *Mind TRIPS*: https://syndromeda-syngate.bandcamp.com/album/mind-trips
- Syndromeda bei Cue Records: https://www.cue-records.com/A-Z-Artists/S/Syndromeda/?language=en
- Syndromeda, *Eternal Destination* (Rezension): https://www.synthsequences.com/post/syndromeda-eternal-destination-2018
- ['ramp], *Synchronize or Die*: https://doombientmusic.bandcamp.com/album/synchronize-or-die
- ['ramp], *Synchronize or Die* (Rezension): https://www.synthsequences.com/post/ramp-synchronize-or-die-2017
- ['ramp] und Bernhard Wöstheinrich, *Ultima Ratio*: https://ramp1.bandcamp.com/album/ultima-ratio
- Tangerine Dream, *Raum* (Kscope): https://kscopemusic.com/tangerine-dream-raum/
- Tangerine Dream, Live-Rig mit Thorsten Quaeschning: https://www.synthtopia.com/content/2022/03/24/tangerine-dream-live-rig-tour-with-thorsten-quaeschning/
- Ron Boots, *Detachment of Worldly Affairs*: https://ronboots.bandcamp.com/album/detachment-of-worldly-affairs
- Ron Boots und Bas Broekhuis, *Hydrythmix*: https://ronboots.bandcamp.com/album/ron-boots-bas-broekhuis-hydrythmix
- Ron Boots (Rezensionen): https://eer-music.com/EER_music_reviews/Ron_Boots.html
- Redshift (Wikipedia): https://en.wikipedia.org/wiki/Redshift_(group)
- Redshift (ProgArchives): https://www.progarchives.com/artist.asp?id=3142
- Arturia Mellotron V: https://www.arturia.com/products/software-instruments/mellotron-v/overview
- Mellotron-Plugins im Überblick (Tapeworm u. a.): https://hiphopmakers.com/best-free-mellotron-vst-plugins
