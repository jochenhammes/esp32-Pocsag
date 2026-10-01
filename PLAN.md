# POCSAG-Empfänger: ESP32 + SX1278 – Projektplan

Ziel: eigenständiger POCSAG-Empfänger für **DAPNET auf 439,9875 MHz** (Standard 1200 Bd) mit
ESP32, externem SX1278-Modul und kleinem OLED. Der Decoder ist ein C++-Port von
`pluto_tx/pocsag_codec.py` aus [pluto-tx](https://github.com/jochenhammes/pluto-tx).

> Der Repo-Name stammt noch vom ursprünglich geplanten Heltec V3. Der wurde verworfen, weil der
> SX1262 keinen Direktmodus hat (POCSAG nur über Umwege mit dem Paketmodus) und 439,9875 MHz außerhalb
> seiner Antennenanpassung liegt.

## 1. Recherche: vorhandener Code

| Projekt | Hardware | Ansatz | Bewertung |
|---|---|---|---|
| [RadioLib `PagerClient`](https://jgromes.github.io/RadioLib/class_pager_client.html) | SX127x, CC1101, RF69, Si443x | Direktmodus: DIO1 = Bittakt (DCLK), DIO2 = Daten, Sync-Suche in Software | funktioniert, aber **keine BCH-Korrektur** (`TODO` im Code), nur eine Polarität, kein Zusammensetzen über Batch-Grenzen hinweg |
| [PocketDAPNET](https://github.com/SilentServices/PocketDAPNET) | ESP32 + SX1278 (T-Beam) | RadioLib-Direktmodus über DIO2 | GPLv3, sehr frühes Stadium (RC, 4 Commits); gute Referenz für die Verdrahtung |
| [meshpoint `pocsag_companion868`](https://github.com/javastraat/meshpoint) | TTGO (SX1276) / Heltec V3 (SX1262) | Paketmodus mit Sync-Wort, 64-Byte-Pakete | AGPL-3.0; verifiziert mit echtem DAPNET-Verkehr, aber ohne BCH und mit Lücken bei mehreren Batches |

**Entscheidung:** RadioLib nur für die Chip-Ansteuerung (FSK-Konfiguration, Direktmodus,
DCLK-Interrupt). Bit-Synchronisation und Decodierung laufen über eine eigene Implementierung nach
dem Vorbild von pluto-tx (Sync-Suche in beiden Polaritäten, BCH(31,21) mit Korrektur von 2 Bitfehlern,
Zusammensetzen über mehrere Batches). Aus GPL- oder AGPL-Projekten wird kein Code übernommen.

## 2. Hardware

- **MCU:** ESP32-DevKit (ESP32-WROOM-32). Andere ESP32-Varianten gehen auch, dann nur die Pin-Tabelle anpassen.
- **Funk:** SX1278-Modul für 433 MHz, z. B. Ai-Thinker **Ra-02** (Band 410–525 MHz, 439,9875 MHz liegt also
  im Band). Wichtig: **DIO1 und DIO2 müssen herausgeführt sein.**
- **Antenne:** 70-cm-Antenne am IPEX/SMA-Anschluss (λ/4 ≈ 16 cm Draht genügt für erste Tests).
- **Display:** kleines I²C-OLED, **Typ noch offen** → U8g2, Displaytyp per Build-Flag wählbar
  (SSD1306 128×64 / 128×32, SH1106 1,3", SSD1309 …).
- **Taster:** 1–2 Taster (Blättern / Menü), optional LED oder Piezo für Alarm bei eigener RIC.

### Vorläufige Pinbelegung (ESP32-DevKit, VSPI)

| SX1278 | ESP32 | | OLED / Bedienung | ESP32 |
|---|---|---|---|---|
| NSS | GPIO 5 | | SDA | GPIO 21 |
| SCK | GPIO 18 | | SCL | GPIO 22 |
| MISO | GPIO 19 | | Taster 1 (Blättern) | GPIO 0 (BOOT) |
| MOSI | GPIO 23 | | Taster 2 (Menü, optional) | GPIO 4 |
| RESET | GPIO 14 | | LED / Piezo (optional) | GPIO 2 / 25 |
| DIO0 | GPIO 26 | | | |
| DIO1 (DCLK) | GPIO 33 | | | |
| DIO2 (DATA) | GPIO 32 | | | |
| 3,3 V / GND | 3V3 / GND | | | |

Alle Pins stehen zentral in `include/board_config.h`. Das Ra-02 verträgt **nur 3,3 V**.

## 3. Software-Architektur

```
SX1278 (FSK, Continuous Mode, Bit-Synchronizer an)
   │ DIO1 = DCLK (steigende Flanke)  DIO2 = DATA
   ▼
ISR: Bit lesen → Ringpuffer (lock-free)         ← 1200 Interrupts/s, trivial
   ▼
Decoder-Task: BatchDecoder.pushBit()            ← Port von pocsag_codec.py
   │  Sync-Suche in beiden Polaritäten (≤2 Bitfehler), Batch-Verfolgung,
   │  BCH-Korrektur, neue Adresse nur mit ≤1 korrigiertem Bit, Nachrichtenaufbau
   ▼
Message-Queue → UI-Task (OLED, Taster, Alarm)  +  Serial (JSON-Zeilen)
```

### Funk-Konfiguration (SX1278 über RadioLib)
- `beginFSK(439.9875, 1.2, 4.5, rxBw, …)` → `receiveDirect()`, `setDirectAction()` auf DCLK.
- Kein Hardware-Sync und kein CRC. Die Sync-Suche macht die Software, dadurch werden beide Polaritäten
  gleichzeitig erkannt.
- **Frequenzfehler:** SX1278-Module haben einfache Quarze (±10–20 ppm ≈ ±4–9 kHz bei 440 MHz!).
  Deshalb:
  - eine ppm-Korrektur als Einstellung,
  - die Frequenzabweichung (FEI-Register) wird bei jedem empfangenen Ruf gelesen und angezeigt, damit man einmal
    kalibrieren kann,
  - Empfangsbandbreite anfangs großzügig (≈ 20,8 kHz) mit AFC, nach der Kalibrierung enger (12,5 kHz).
- Baudrate 512 / 1200 / 2400 einstellbar (RadioLib erlaubt bei SX127x ab 0,5 kbps). Ein Chip
  demoduliert immer nur **eine** Baudrate (anders als pluto-tx mit drei parallelen Zweigen). Optional später ein
  Scan-Modus.
- RSSI pro Ruf mitloggen.

### Decoder (`lib/pocsag/`, plattformunabhängiges C++)
1:1-Port der Logik aus `pocsag_codec.py`:
- BCH(31,21) mit Generator 0x769, Syndromtabelle für 1–2 Bitfehler, Paritätsbit
- `BatchDecoder`: Hunt/Batch/Sync-Zustände, max. 2 fehlende Syncs, Idle-Toleranz ≤3 Bit
- Text: numerisch (BCD, LSB zuerst), alpha (7 Bit, LSB zuerst), DIN 66003 (Umlaute), Auto-Modus über die
  Funktionsbits
- Zähler: Batches, Codewörter ok/defekt, Rufe mit nicht korrigierbaren Codewörtern („beschädigt“)
- Ohne Arduino-Abhängigkeiten, damit es nativ auf dem PC testbar ist.

### Ausgabe / Bedienung
- **OLED:** Statuszeile (Frequenz, Baud, RSSI, Lock-Anzeige), letzter Ruf groß (RIC, Funktion, Uhrzeit,
  Text mit Zeilenumbruch und Scrollen), Liste der letzten N Rufe.
- **Taster:** kurz = blättern / scrollen, lang = Menü (Baud, Polarität, ppm, Filter).
- **Serial (115200):** jede Nachricht als JSON-Zeile; einfache Befehle (`freq`, `baud`, `ppm`, `ric add …`).
- **NVS (Preferences):** Einstellungen und RIC-Filter dauerhaft speichern.
- **RIC-Filter / Alarm:** eigene RICs hervorheben, LED/Piezo, optional „nur eigene RICs anzeigen“.
- Zeitbasis: Uptime. Optional lassen sich DAPNET-Zeitrufe (RIC 224, `XTIME=…`) auswerten, um die Uhr zu stellen.

## 4. Projektstruktur

```
platformio.ini            env:esp32dev (Firmware), env:native (Unit-Tests)
include/board_config.h    Pins, Displaytyp, Standardwerte
lib/pocsag/               bch.{h,cpp}, batch_decoder.{h,cpp}, text.{h,cpp}
src/main.cpp              Setup, Tasks
src/radio.{h,cpp}         SX1278-Konfiguration, DCLK-ISR, Ringpuffer
src/ui.{h,cpp}            OLED + Taster
src/settings.{h,cpp}      NVS, Serial-Befehle
test/test_pocsag/         Unity-Tests (nativ)
tools/gen_vectors.py      erzeugt Testvektoren mit pluto-tx `build_transmission()`
```

## 5. Meilensteine

1. **Gerüst + Decoder + Tests (ohne Hardware)**
   PlatformIO-Projekt, C++-Port des Codecs, native Unit-Tests mit Vektoren aus pluto-tx (Bitfehler,
   invertierte Polarität, mehrere Batches, numerisch/alpha/Umlaute). CI über GitHub Actions
   (`pio test -e native`, `pio run -e esp32dev`).
2. **Funkempfang → Serial**
   SX1278 im Direktmodus, DCLK-ISR, Ausgabe der Rufe als JSON auf Serial. Test mit pluto-tx als Sender
   (geringe Leistung / Dummy-Load) und mit echtem DAPNET-Verkehr. FEI-basierte ppm-Kalibrierung.
3. **OLED + Bedienung**
   Displaytreiber über U8g2 (Typ per Flag), Anzeige, Taster, Menü, NVS, RIC-Filter, Alarm.
4. **Optional**
   Baud-Scan, Verdrahtungsskizze in der README, Gehäuse, WLAN-Weboberfläche / MQTT-Weiterleitung,
   Akkubetrieb mit Light Sleep.

## 6. Risiken / offene Punkte

- **Displaytyp offen:** durch U8g2 kein Problem. Die Auswahl erfolgt über ein Build-Flag, das Layout zuerst für 128×64.
- **Quarzfehler der SX1278-Module:** der Grund für ppm-Korrektur und FEI-Anzeige (siehe oben).
- **Polarität:** wird in Software automatisch erkannt (Sync in beiden Polaritäten).
- **512 Bd:** Bit-Synchronizer und Rx-Filter bei sehr niedriger Rate sind auf Hardware noch zu verifizieren.
- **Rechtliches:** DAPNET (Amateurfunk) ist unproblematisch. Fremde Funkrufnetze (BOS, e*Message) dürfen in
  Deutschland nicht abgehört bzw. deren Inhalte nicht weitergegeben werden; das kommt als Hinweis in die README.
