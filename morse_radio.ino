/*
 * ============================================================
 *  MORSE RADIO TRANSCEIVER con Arduino  -  PTT ONLY
 * ============================================================
 *  Autore: progetto radioamatoriale
 *  Hardware:
 *    - Arduino Mega 2560 (raccomandato)
 *    - Display LCD 20x4 via I2C (addr 0x27)
 *    - Tastiera Bluetooth su Serial1 (pin 18 RX, 19 TX)
 *    - PTT radio TX: pin 7 (tramite optoisolatore 4N35)
 *      La trasmissione morse avviene SOLO aprendo/chiudendo il PTT.
 *      Nessun tono audio viene generato.
 *    - Ingresso portante RX: pin A0 (analogico)
 *      Rileva la presenza/assenza di portante dall'altra radio.
 *    - LED attività: pin 13
 *
 *  Display layout:
 *    Riga 0: messaggi ricevuti (scorrono verso l'alto)
 *    Riga 1: messaggi ricevuti (riga corrente in arrivo)
 *    Riga 2: testo da inviare (prima parte)
 *    Riga 3: testo da inviare (seconda parte / cursore)
 *
 *  Librerie necessarie (Library Manager):
 *    - LiquidCrystal_I2C
 * ============================================================
 */

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---- PIN DEFINITION ----------------------------------------
#define PIN_PTT       7    // PTT verso radio TX (optoisolatore)
#define PIN_AUDIO_IN  A0   // Ingresso portante RX dalla radio ricevente
#define PIN_LED       13   // LED attività (si accende durante trasmissione)

// ---- MORSE TIMING ------------------------------------------
// Velocità in WPM (Words Per Minute) - modificabile
int wpm = 15;
// Durata un punto in ms = 1200 / WPM
int dotDuration;   // calcolato in setup()
int dashDuration;  // = 3 * dot
int symbolGap;     // = 1 * dot (tra simboli della stessa lettera)
int letterGap;     // = 3 * dot
int wordGap;       // = 7 * dot

// ---- DISPLAY -----------------------------------------------
// Indirizzo I2C del display (0x27 o 0x3F a seconda del modulo)
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ---- STATO APPLICAZIONE ------------------------------------
enum AppMode { MODE_IDLE, MODE_TX, MODE_RX };
AppMode currentMode = MODE_IDLE;

// Buffer testo da inviare (max 40 caratteri = 2 righe x 20)
#define TX_BUFFER_LEN 41
char txBuffer[TX_BUFFER_LEN];
int  txBufferPos = 0;

// Buffer messaggi ricevuti per il display (2 righe da 20 char)
char rxLine0[21] = "                    ";  // riga 0 display (messaggio precedente)
char rxLine1[21] = "                    ";  // riga 1 display (messaggio corrente)
char rxCurrentMsg[41] = "";                 // messaggio RX in corso di costruzione
int  rxMsgPos = 0;

// ---- RICEZIONE MORSE (rilevamento portante) ----------------
// Soglia analogica per rilevare la portante sul pin A0 (0-1023).
// Quando l'altra radio trasmette il PTT chiuso, sul pin A0 arriva
// un segnale (rumore di fondo / portante) superiore alla soglia.
// Quando il PTT è aperto (silenzio radio) il valore scende sotto soglia.
#define RX_THRESHOLD_HIGH  600
#define RX_THRESHOLD_LOW   400

// Durate minime/massime per classificare dot/dash (ms)
// Calcolate in setup() in base al WPM
int rxDotMin, rxDotMax, rxDashMin, rxDashMax;
int rxLetterGapMin;
int rxWordGapMin;

// Stato macchina a stati per decodifica portante
bool     rxCarrierActive      = false;  // portante (PTT chiuso) in corso
unsigned long rxCarrierStart  = 0;      // timestamp inizio portante
unsigned long rxSilenceStart  = 0;      // timestamp inizio silenzio
bool     rxInSilence          = false;

// Buffer simboli morse per la lettera corrente (max 7 simboli: .)
#define MORSE_SYM_BUF 7
char rxSymbols[MORSE_SYM_BUF + 1];
int  rxSymbolCount = 0;

// ---- TABELLA CODICI MORSE ----------------------------------
// Ogni entry: carattere ASCII, stringa morse (. = punto, - = dash)
struct MorseEntry {
  char ch;
  const char* code;
};

const MorseEntry MORSE_TABLE[] = {
  {'A', ".-"},   {'B', "-..."},  {'C', "-.-."},
  {'D', "-.."},  {'E', "."},     {'F', "..-."},
  {'G', "--."},  {'H', "...."},  {'I', ".."},
  {'J', ".---"}, {'K', "-.-"},   {'L', ".-.."},
  {'M', "--"},   {'N', "-."},    {'O', "---"},
  {'P', ".--."},  {'Q', "--.-"}, {'R', ".-."},
  {'S', "..."},  {'T', "-"},     {'U', "..-"},
  {'V', "...-"}, {'W', ".--"},   {'X', "-..-"},
  {'Y', "-.--"}, {'Z', "--.."},
  {'0', "-----"},{'1', ".----"}, {'2', "..---"},
  {'3', "...--"},{'4', "....-"}, {'5', "....."},
  {'6', "-...."}, {'7', "--..."}, {'8', "---.."},
  {'9', "----."}, {'.', ".-.-.-"},{',', "--..--"},
  {'?', "..--.."},{' ', " "},    {'/', "-..-."},
  {'\0', NULL}
};

// ============================================================
//  FUNZIONI UTILITY
// ============================================================

// Calcola i timing morse dal WPM corrente
void calcMorseTiming() {
  dotDuration  = 1200 / wpm;
  dashDuration = 3 * dotDuration;
  symbolGap    = dotDuration;
  letterGap    = 3 * dotDuration;
  wordGap      = 7 * dotDuration;

  // Tolleranza ±50% per la ricezione
  rxDotMin     = dotDuration  / 2;
  rxDotMax     = dotDuration  + dotDuration / 2;
  rxDashMin    = dashDuration - dotDuration;
  rxDashMax    = dashDuration + dotDuration;
  rxLetterGapMin = letterGap  - dotDuration;
  rxWordGapMin   = wordGap    - dotDuration;
}

// Aggiorna il display con i buffer correnti
void updateDisplay() {
  // --- Riga 0: messaggio RX precedente ---
  lcd.setCursor(0, 0);
  lcd.print(rxLine0);

  // --- Riga 1: messaggio RX corrente in costruzione ---
  lcd.setCursor(0, 1);
  // Mostra solo gli ultimi 20 char del messaggio corrente
  int rxLen = strlen(rxCurrentMsg);
  if (rxLen <= 20) {
    lcd.print(rxCurrentMsg);
    // Riempi il resto con spazi
    for (int i = rxLen; i < 20; i++) lcd.print(' ');
  } else {
    // Mostra gli ultimi 20 caratteri
    lcd.print(rxCurrentMsg + rxLen - 20);
  }

  // --- Riga 2: primi 20 char del buffer TX ---
  lcd.setCursor(0, 2);
  char line2[21] = "                    ";
  strncpy(line2, txBuffer, (txBufferPos > 20) ? 20 : txBufferPos);
  lcd.print(line2);

  // --- Riga 3: caratteri TX da posizione 20 in poi + cursore ---
  lcd.setCursor(0, 3);
  char line3[21] = "                    ";
  if (txBufferPos > 20) {
    int secondPart = txBufferPos - 20;
    if (secondPart > 20) secondPart = 20;
    strncpy(line3, txBuffer + 20, secondPart);
  }
  lcd.print(line3);
}

// Sposta il messaggio RX corrente in riga 0 e pulisce la riga 1
void archiveRxMessage() {
  if (strlen(rxCurrentMsg) == 0) return;
  // Riga 0 = ultimi 20 char del messaggio corrente
  int len = strlen(rxCurrentMsg);
  if (len >= 20) {
    strncpy(rxLine0, rxCurrentMsg + len - 20, 20);
  } else {
    strncpy(rxLine0, rxCurrentMsg, len);
    for (int i = len; i < 20; i++) rxLine0[i] = ' ';
  }
  rxLine0[20] = '\0';
  rxCurrentMsg[0] = '\0';
  rxMsgPos = 0;
  updateDisplay();
}

// Aggiunge un carattere al messaggio RX in costruzione
void appendRxChar(char c) {
  if (rxMsgPos < 40) {
    rxCurrentMsg[rxMsgPos++] = c;
    rxCurrentMsg[rxMsgPos]   = '\0';
  } else {
    // Buffer pieno: archivia e ricomincia
    archiveRxMessage();
    rxCurrentMsg[rxMsgPos++] = c;
    rxCurrentMsg[rxMsgPos]   = '\0';
  }
  updateDisplay();
}

// ============================================================
//  TRASMISSIONE MORSE  (solo PTT, nessun audio)
// ============================================================

// Chiude il PTT per "duration" ms (punto o trattino), poi lo apre.
// La pausa inter-simbolo (PTT aperto) è inclusa alla fine.
void txKey(int duration) {
  digitalWrite(PIN_PTT, HIGH);   // PTT chiuso → radio in TX
  digitalWrite(PIN_LED, HIGH);
  delay(duration);
  digitalWrite(PIN_PTT, LOW);    // PTT aperto → pausa morse
  digitalWrite(PIN_LED, LOW);
  delay(symbolGap);              // pausa inter-simbolo
}

// Invia un carattere in morse
void txChar(char c) {
  c = toupper(c);
  if (c == ' ') {
    // Pausa di parola (wordGap - letterGap già atteso = 4 dot extra)
    delay(wordGap - letterGap);
    return;
  }
  for (int i = 0; MORSE_TABLE[i].ch != '\0'; i++) {
    if (MORSE_TABLE[i].ch == c) {
      const char* code = MORSE_TABLE[i].code;
      for (int j = 0; code[j] != '\0'; j++) {
        if (code[j] == '.') {
          txKey(dotDuration);
        } else if (code[j] == '-') {
          txKey(dashDuration);
        }
      }
      // Pausa inter-lettera (letterGap - symbolGap già atteso)
      delay(letterGap - symbolGap);
      return;
    }
  }
  // Carattere non trovato: ignora
}

// Invia l'intero buffer TX modulando solo il PTT
void transmitBuffer() {
  if (txBufferPos == 0) return;

  currentMode = MODE_TX;
  // PTT parte già LOW; txKey() lo gestisce simbolo per simbolo.

  for (int i = 0; i < txBufferPos; i++) {
    txChar(txBuffer[i]);
  }

  // Assicurarsi che il PTT sia rilasciato a fine trasmissione
  digitalWrite(PIN_PTT, LOW);
  digitalWrite(PIN_LED, LOW);
  currentMode = MODE_IDLE;

  // Pulisce il buffer TX dopo l'invio
  txBufferPos = 0;
  txBuffer[0] = '\0';
  updateDisplay();
}

// ============================================================
//  RICEZIONE MORSE (rilevamento portante PTT)
// ============================================================
// L'altra radio trasmette in morse aprendo/chiudendo il PTT.
// Sul pin A0 arriva il segnale dell'uscita cuffie/squelch:
//   PTT chiuso  →  valore analogico > RX_THRESHOLD_HIGH
//   PTT aperto  →  valore analogico < RX_THRESHOLD_LOW

// Traduce una sequenza di simboli morse ("..-") in carattere
char morseSymbolsToChar(const char* symbols) {
  if (strcmp(symbols, " ") == 0) return ' ';
  for (int i = 0; MORSE_TABLE[i].ch != '\0'; i++) {
    if (strcmp(MORSE_TABLE[i].code, symbols) == 0) {
      return MORSE_TABLE[i].ch;
    }
  }
  return '?';  // Simbolo non riconosciuto
}

// Processa il buffer simboli morse accumulato per la lettera corrente
void processRxSymbols() {
  if (rxSymbolCount == 0) return;
  rxSymbols[rxSymbolCount] = '\0';
  char decoded = morseSymbolsToChar(rxSymbols);
  if (decoded != '\0') {
    appendRxChar(decoded);
  }
  rxSymbolCount = 0;
}

// Campionamento e decodifica portante morse (chiamata nel loop)
void processAudioRx() {
  int audioVal = analogRead(PIN_AUDIO_IN);
  unsigned long now = millis();

  // Isteresi: evita rimbalzi attorno alla soglia
  bool carrierPresent;
  if (rxCarrierActive) {
    carrierPresent = (audioVal > RX_THRESHOLD_LOW);   // soglia bassa per uscire
  } else {
    carrierPresent = (audioVal > RX_THRESHOLD_HIGH);  // soglia alta per entrare
  }

  if (carrierPresent) {
    // --- PTT CHIUSO (portante attiva) ---
    if (!rxCarrierActive) {
      // Fronte di salita: portante appena iniziata
      rxCarrierActive = true;
      rxCarrierStart  = now;

      if (rxInSilence) {
        // Calcola durata silenzio precedente
        unsigned long silenceDuration = now - rxSilenceStart;
        rxInSilence = false;

        if (silenceDuration >= (unsigned long)rxWordGapMin) {
          // Pausa di parola: termina la lettera corrente e aggiunge spazio
          processRxSymbols();
          appendRxChar(' ');
        } else if (silenceDuration >= (unsigned long)rxLetterGapMin) {
          // Pausa inter-lettera: termina la lettera corrente
          processRxSymbols();
        }
        // else: pausa inter-simbolo, si continua ad accumulare
      }
    }
    // else: portante già attiva, si misura alla discesa

  } else {
    // --- PTT APERTO (silenzio) ---
    if (rxCarrierActive) {
      // Fronte di discesa: portante appena terminata
      rxCarrierActive = false;
      rxSilenceStart  = now;
      rxInSilence     = true;

      unsigned long carrierDuration = now - rxCarrierStart;

      // Classifica il simbolo in base alla durata della portante
      if (carrierDuration >= (unsigned long)rxDashMin) {
        // DASH
        if (rxSymbolCount < MORSE_SYM_BUF) {
          rxSymbols[rxSymbolCount++] = '-';
        }
      } else if (carrierDuration >= (unsigned long)rxDotMin) {
        // DOT
        if (rxSymbolCount < MORSE_SYM_BUF) {
          rxSymbols[rxSymbolCount++] = '.';
        }
      }
      // Impulsi troppo brevi (rimbalzi/rumore) vengono ignorati
    } else if (rxInSilence) {
      // Silenzio prolungato: controlla se è fine parola
      unsigned long silenceDuration = now - rxSilenceStart;
      if (silenceDuration >= (unsigned long)(rxWordGapMin + wordGap)) {
        // Fine trasmissione: decodifica l'eventuale lettera rimasta
        if (rxSymbolCount > 0) {
          processRxSymbols();
        }
        rxInSilence = false;
      }
    }
  }
}

// ============================================================
//  GESTIONE TASTIERA BLUETOOTH
// ============================================================
// La tastiera BT è connessa a Serial1 (Arduino Mega: pin 18/19)
// Caratteri speciali:
//   ENTER (13)  → avvia trasmissione
//   BACKSPACE (8 o 127) → cancella ultimo carattere

void processKeyboard() {
  while (Serial1.available()) {
    char key = (char)Serial1.read();

    if (key == 13 || key == '\n') {
      // INVIO → trasmetti il buffer
      if (txBufferPos > 0) {
        transmitBuffer();
      }

    } else if (key == 8 || key == 127) {
      // BACKSPACE → cancella ultimo char del buffer TX
      if (txBufferPos > 0) {
        txBufferPos--;
        txBuffer[txBufferPos] = '\0';
        updateDisplay();
      }

    } else if (key >= 32 && key <= 126) {
      // Carattere stampabile: aggiunge al buffer TX
      if (txBufferPos < TX_BUFFER_LEN - 1) {
        txBuffer[txBufferPos++] = key;
        txBuffer[txBufferPos]   = '\0';
        updateDisplay();
      }
    }
  }
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  // Porta seriale di debug (USB)
  Serial.begin(9600);

  // Tastiera Bluetooth su Serial1
  // Imposta la velocità del modulo BT (di solito 9600 di default)
  Serial1.begin(9600);

  // Pin
  pinMode(PIN_PTT, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_PTT, LOW);   // PTT aperto all'avvio
  digitalWrite(PIN_LED, LOW);
  // PIN_AUDIO_IN è ingresso analogico (default, nessun pinMode necessario)

  // Calcola timing morse
  calcMorseTiming();

  // Inizializza display LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();

  // Messaggio di avvio
  lcd.setCursor(0, 0);
  lcd.print("  MORSE  RADIO PTT  ");
  lcd.setCursor(0, 1);
  lcd.print("  RX pronto...      ");
  lcd.setCursor(0, 2);
  lcd.print("Scrivi e premi ENTER");
  lcd.setCursor(0, 3);
  char wpmMsg[21];
  snprintf(wpmMsg, sizeof(wpmMsg), "WPM: %d  PTT-only", wpm);
  lcd.print(wpmMsg);

  delay(2000);
  lcd.clear();

  // Inizializza buffer
  txBuffer[0]      = '\0';
  rxCurrentMsg[0]  = '\0';
  updateDisplay();

  Serial.println("Sistema Morse Radio PTT-only pronto.");
  Serial.print("WPM: "); Serial.println(wpm);
  Serial.print("Dot: ");  Serial.print(dotDuration);  Serial.println(" ms");
  Serial.print("Dash: "); Serial.print(dashDuration); Serial.println(" ms");
  Serial.println("Nessun audio generato - trasmissione solo PTT.");
}

// ============================================================
//  LOOP PRINCIPALE
// ============================================================
void loop() {
  // Leggi eventuali comandi dalla tastiera BT
  if (currentMode == MODE_IDLE) {
    processKeyboard();
  }

  // Decodifica audio in ingresso (solo se non stiamo trasmettendo)
  if (currentMode != MODE_TX) {
    processAudioRx();
  }

  // Comandi di debug/configurazione via seriale USB
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    if (cmd.startsWith("WPM:")) {
      int newWpm = cmd.substring(4).toInt();
      if (newWpm >= 5 && newWpm <= 40) {
        wpm = newWpm;
        calcMorseTiming();
        Serial.print("WPM impostato a: "); Serial.println(wpm);
      }
    } else if (cmd == "CLR") {
      txBufferPos = 0;
      txBuffer[0] = '\0';
      rxCurrentMsg[0] = '\0';
      rxMsgPos = 0;
      lcd.clear();
      updateDisplay();
      Serial.println("Display e buffer azzerati.");
    } else if (cmd == "SEND") {
      transmitBuffer();
    }
  }

  // Piccola pausa per non sovraccaricare il campionamento analogico
  delayMicroseconds(500);
}
