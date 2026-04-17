# Schema Collegamenti Hardware – Morse Radio con Arduino

## Componenti necessari  *(modalità PTT-only — nessun audio generato)*

| Componente | Qty | Note |
|---|---|---|
| Arduino Mega 2560 | 1 | Raccomandato (3 porte seriali hardware) |
| Display LCD 20x4 con modulo I2C | 1 | Indirizzo tipico: 0x27 oppure 0x3F |
| Modulo Bluetooth HC-05 o HC-06 | 1 | Per tastiera BT |
| Optoisolatore 4N35 (o 4N25) | 1 | Isolamento galvanico PTT radio TX |
| Resistenza 1kΩ | 1 | Current limiting LED optoisolatore |
| Resistenza 10kΩ | 1 | Partitore portante RX |
| Resistenza 4.7kΩ | 1 | Partitore portante RX |
| Condensatore 100nF | 1 | Filtro anti-RF su A0 |
| Jack audio 3.5mm stereo | 1 | Solo per RX (uscita cuffie/squelch radio RX) |
| Diodo 1N4148 | 1 | Protezione tensioni inverse PTT |

---

## Schema dei PIN Arduino

```
Arduino Mega                Destinazione
─────────────────────────────────────────────
PIN 7    (DIGITAL OUT)  →  PTT radio TX (via optoisolatore)
PIN A0   (ANALOG IN)    →  Portante RX (uscita cuffie/squelch radio ricevente)
PIN 13   (DIGITAL OUT)  →  LED attività (builtin)
PIN 20 (SDA) / 21 (SCL) →  Display LCD I2C
PIN 18 (RX1) / 19 (TX1) →  Modulo Bluetooth HC-05
GND                     →  GND comune
5V                      →  Alimentazione moduli
```

---

## Circuito PTT (optoisolatore 4N35)

```
Arduino PIN 7 ──── R1 (1kΩ) ──── PIN 1 (Anodo LED interno 4N35)
                                  PIN 2 (Catodo LED interno 4N35) ──── GND Arduino

Radio PTT ──────────────────────── PIN 4 (Collettore 4N35)
                                   PIN 5 (Emettitore 4N35) ──── GND Radio
```

> L'optoisolatore isola galvanicamente l'Arduino dalla radio.
> Aggiungere un diodo 1N4148 in parallelo sul PTT radio (protezione tensioni inverse).
> Il PTT della maggior parte delle radio amatoriali si attiva collegando il pin PTT a GND.
> **Non è necessario nessun collegamento audio verso la radio TX**: il morse è
> trasmesso interamente modulando la portante RF tramite PTT (aperto = silenzio, chiuso = simbolo).

---

## Circuito RX — rilevamento portante (uscita cuffie/squelch → Arduino A0)

Quando l'altra radio trasmette (PTT chiuso), sull'uscita cuffie o squelch
appare un livello DC o un segnale audio che supera la soglia.
Quando il PTT è aperto (silenzio radio), il livello scende.
Il partitore adatta il segnale al range 0–5 V di Arduino:

```
Jack RX (cuffie/squelch) Tip ──── R1 (10kΩ) ──┬──── Arduino A0
                              │
                            R2 (4.7kΩ)
                              │
                             GND
```

> Tensione su A0 = Vin × (4.7k / (10k + 4.7k)) ≈ Vin × 0.32
> Con Vin max = 1 V → A0 riceve ~320 mV (sicuro per Arduino 5 V).
> Aggiungere C 100 nF da A0 a GND per filtrare il rumore RF.
>
> **Alternativa:** se la radio ha un'uscita squelch digitale (0/5 V),
> collegare direttamente al pin A0 tramite una resistenza da 10 kΩ in serie.

---

## Collegamento Display LCD I2C

```
Display LCD I2C    Arduino Mega
─────────────────────────────
VCC             →  5V
GND             →  GND
SDA             →  PIN 20 (SDA)
SCL             →  PIN 21 (SCL)
```

---

## Collegamento Modulo Bluetooth HC-05

```
HC-05           Arduino Mega
──────────────────────────────────
VCC          →  5V
GND          →  GND
TX (HC-05)   →  PIN 18 (RX1 Arduino)
RX (HC-05)   →  PIN 19 (TX1 Arduino) tramite partitore!
```

> ATTENZIONE: HC-05 lavora a 3.3V logici sul pin RX.
> Collegare TX1 Arduino (5V) → R1 (1kΩ) → RX HC-05 → R2 (2kΩ) → GND
> oppure usare un convertitore di livello 5V→3.3V.

---

## Librerie Arduino necessarie

Installare tramite **Library Manager** (Sketch → Include Library → Manage Libraries):

1. **LiquidCrystal_I2C** di Frank de Brabander (o johnrickman)

---

## Note di configurazione

### Indirizzo I2C del display
Se il display non si accende, scansionare l'I2C con questo sketch:
```cpp
#include <Wire.h>
void setup() {
  Serial.begin(9600);
  Wire.begin();
  for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.print("Trovato device I2C: 0x");
      Serial.println(addr, HEX);
    }
  }
}
void loop() {}
```
Modificare `LiquidCrystal_I2C lcd(0x27, 20, 4);` con l'indirizzo trovato.

### Velocità trasmissione (WPM)
Modificare la variabile `int wpm = 15;` nel codice, oppure inviare il comando
`WPM:20` dalla seriale USB di Arduino IDE per cambiarla a runtime.

### Soglie di rilevamento portante
Se la decodifica RX non funziona bene, regolare nel codice:
- `RX_THRESHOLD_HIGH 600` → soglia di ingresso (PTT chiuso rilevato): abbassare se il segnale è debole
- `RX_THRESHOLD_LOW  400` → soglia di uscita (PTT aperto): deve essere < HIGH (isteresi)

Per calibrare, aprire il Serial Monitor a 9600 baud e aggiungere temporaneamente:
```cpp
Serial.println(analogRead(A0));  // nel loop()
```
Misurare il valore a radio silenziosa e a radio in TX, poi impostare le soglie a metà strada.

### Abbinamento tastiera Bluetooth
1. Mettere HC-05 in modalità AT (tenere il pulsante durante l'accensione)
2. Inviare: `AT+UART=9600,0,0` per impostare 9600 baud
3. Abbinare la tastiera BT al modulo HC-05 dal telefono/PC come tastiera

---

## Comandi seriale USB (per debug/configurazione)

Aprire il Serial Monitor di Arduino IDE a 9600 baud:

| Comando | Effetto |
|---|---|
| `WPM:15` | Imposta velocità a 15 WPM |
| `CLR` | Azzera display e buffer |
| `SEND` | Trasmette il buffer TX corrente |

---

## Layout Display durante il funzionamento

```
┌────────────────────┐
│ messaggio RX prec. │  ← Riga 0: ultimo messaggio ricevuto
│ messaggio RX attua │  ← Riga 1: messaggio in ricezione (scorre)
│ TESTO DA INVIARE.. │  ← Riga 2: primi 20 char del buffer TX
│ ..continua|        │  ← Riga 3: char 21-40 + cursore
└────────────────────┘
```

---

## Funzionamento tipico

1. Accendere il sistema — il LED rimane spento, il PTT è aperto.
2. La radio RX è sempre in ascolto: il pin A0 monitora la portante in continuazione.
3. Digitare il messaggio sulla tastiera BT (appare sulle righe 3–4 del display).
4. Premere **ENTER** per trasmettere in morse.
5. Arduino chiude/apre il PTT per ogni punto e trattino — **nessun audio generato**.
6. La radio TX trasmette la portante RF modulata on/off secondo il codice morse.
7. La radio ricevente sull'altro capo rileva la portante su A0 e decodifica il morse.
8. I caratteri decodificati appaiono in tempo reale sulle righe 1–2 del display.

---

## Diagramma temporale PTT

```
PTT  ──┐  ┌──┐     ┌────┐  ┌──  ...
    └──┘  └─────┘    └──┘
    [.] [.] [---] [---] [.]
      ↑inter-simbolo  ↑inter-lettera (3× dot)
```
- PTT HIGH (chiuso) per `dotDuration` ms  = punto  (·)
- PTT HIGH (chiuso) per `dashDuration` ms = trattino (−)
- PTT LOW  (aperto) per `symbolGap` ms    = pausa inter-simbolo (1× dot)
- PTT LOW  (aperto) per `letterGap` ms    = pausa inter-lettera (3× dot)
- PTT LOW  (aperto) per `wordGap` ms      = pausa inter-parola  (7× dot)
