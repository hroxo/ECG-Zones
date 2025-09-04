#include <LiquidCrystal.h>

// -------------------- LCD --------------------
LiquidCrystal lcd(12, 10, 5, 4, 3, 2); // rs,en,d4,d5,d6,d7
const uint8_t LCD_COLS = 16;
const uint8_t LCD_ROWS = 2;

// -------------------- AD8232 -----------------
const int ECG_PIN      = A0;  // Sinal analógico
const int ECG_LO_MINUS = 8;   // LO- (elétrodo solto)
const int ECG_LO_PLUS  = 7;   // LO+ (elétrodo solto)

// -------------------- Parâmetros de aquisição/algoritmo --------------
const uint16_t SAMPLE_INTERVAL_MS = 4;     // ~250 Hz
const uint16_t LCD_INTERVAL_MS    = 250;   // refresh suave
const uint16_t REFRACTORY_MS      = 300;   // período refratário (anti-duplo pico)
const uint16_t CALIBRATION_MS     = 6000;  // janela de calibração inicial

// Filtro de baseline (EMA de baixa frequência)
const float LP_ALPHA = 0.01f;              // 0.005–0.02 costuma resultar bem

// Estatística para limiar adaptativo (EMA)
const float THR_ALPHA = 0.02f;             // velocidade de adaptação do limiar
const float K_SIGMA   = 3.5f;              // “multiplicador” da dispersão (3.0–5.0)

// Suavização de BPM
const uint8_t N_AVG_BEATS = 5;             // média dos últimos N batimentos

// -------------------- Estado ------------------
unsigned long lastSampleTime = 0;
unsigned long lastLCDTime    = 0;
unsigned long startTime      = 0;
unsigned long lastPeakTime   = 0;

bool armedForPeak = true;   // histerese: esperamos nova subida cruzar o limiar

// Filtro e estatística
float lpBaseline = 0.0f;    // low-pass (baseline)
float yHP = 0.0f;           // sinal passa-alto
float rectVal = 0.0f;       // |yHP|
float mu = 0.0f;            // média EMA de |yHP|
float dev = 0.0f;           // “dispersão” EMA (aprox. desvio absoluto médio)

// BPM
float bpmAvgShown = 0.0f;
float rrBuffer[N_AVG_BEATS] = {0};
uint8_t rrCount = 0;
uint8_t rrIndex = 0;

// -------------------- Helpers -----------------
void lcdPrintFixed(uint8_t col, uint8_t row, const String &text) {
  lcd.setCursor(col, row);
  String t = text;
  while ((int)t.length() < (LCD_COLS - col)) t += ' ';
  if ((int)t.length() > (LCD_COLS - col)) t = t.substring(0, LCD_COLS - col);
  lcd.print(t);
}

const char* zonaFromBpm(float x) {
  if (x < 115)       return "Z1 Leve";
  else if (x <= 133) return "Z2 Mod.";
  else if (x <= 152) return "Z3 Int.";
  else               return "Z4 Muito";
}

void pushRR(float rr) {
  rrBuffer[rrIndex] = rr;
  rrIndex = (rrIndex + 1) % N_AVG_BEATS;
  if (rrCount < N_AVG_BEATS) rrCount++;
}

float bpmFromAvgRR() {
  if (rrCount == 0) return 0.0f;
  float sum = 0.0f;
  for (uint8_t i = 0; i < rrCount; i++) sum += rrBuffer[i];
  float rrMean = sum / rrCount;
  if (rrMean <= 0.0f) return 0.0f;
  return 60.0f / rrMean;
}

// -------------------- Setup -------------------
void setup() {
  Serial.begin(9600);

  pinMode(ECG_PIN, INPUT);
  pinMode(ECG_LO_MINUS, INPUT);
  pinMode(ECG_LO_PLUS, INPUT);

  lcd.begin(LCD_COLS, LCD_ROWS);
  lcd.clear();
  lcdPrintFixed(0, 0, "ECG / BPM Monitor");
  lcdPrintFixed(0, 1, "A iniciar...");
  delay(800);
  lcd.clear();

  // Marcar início (para janela de calibração)
  startTime = millis();
}

// -------------------- Loop --------------------
void loop() {
  unsigned long now = millis();

  // ---- Amostragem a ~250 Hz ----
  if (now - lastSampleTime < SAMPLE_INTERVAL_MS) {
    // Atualização do LCD pode acontecer mesmo que não haja nova amostra
    if (now - lastLCDTime >= LCD_INTERVAL_MS) {
      updateLCD();
      lastLCDTime = now;
    }
    return;
  }
  lastSampleTime += SAMPLE_INTERVAL_MS; // avança “em grelha” para reduzir jitter

  // ---- Verificar elétrodos (AD8232 LO+/LO- altos = solto) ----
  bool leadsOff = (digitalRead(ECG_LO_MINUS) == HIGH) || (digitalRead(ECG_LO_PLUS) == HIGH);
  if (leadsOff) {
    // Reset suave de estado (não perdemos totalmente a adaptação)
    armedForPeak = true;
    lastPeakTime = 0;
    rrCount = 0; rrIndex = 0;
    bpmAvgShown = 0.0f;

    if (now - lastLCDTime >= LCD_INTERVAL_MS) {
      lcdPrintFixed(0, 0, "Eletrodos soltos!");
      lcdPrintFixed(0, 1, "Verifica ligacoes");
      lastLCDTime = now;
    }
    return;
  }

  // ---- Leitura do sinal e pré-processamento ----
  int raw = analogRead(ECG_PIN);     // 0–1023

  // 1) low-pass (baseline) por EMA simples
  lpBaseline += LP_ALPHA * ((float)raw - lpBaseline);

  // 2) high-pass = sinal - baseline
  yHP = (float)raw - lpBaseline;

  // 3) retificação
  rectVal = fabs(yHP);

  // 4) estatística adaptativa para limiar
  float diff = rectVal - mu;
  mu  += THR_ALPHA * diff;                 // média EMA
  dev += THR_ALPHA * (fabs(diff) - dev);   // “dispersão” EMA (aprox. MAD)

  float threshold = mu + K_SIGMA * dev;    // limiar dinâmico

  // ---- Calibração inicial: só aprende, não deteta picos ----
  if (now - startTime < CALIBRATION_MS) {
    if (now - lastLCDTime >= LCD_INTERVAL_MS) {
      lcdPrintFixed(0, 0, "A calibrar...");
      lcdPrintFixed(0, 1, "Mantem-te quieto");
      lastLCDTime = now;
    }
    return;
  }

  // ---- Deteção de picos com histerese + refratário ----
  bool above = (rectVal > threshold);

  // Transição de subida (armado) => possível pico
  if (armedForPeak && above) {
    // Verificar refratário
    if (lastPeakTime == 0 || (now - lastPeakTime) >= REFRACTORY_MS) {
      // RR e BPM
      if (lastPeakTime != 0) {
        float rr = (now - lastPeakTime) / 1000.0f;
        // filtro grosseiro de RR (30–220 BPM)
        if (rr > 0.27f && rr < 2.0f) {
          pushRR(rr);
          bpmAvgShown = bpmFromAvgRR();

          // debug opcional
          Serial.print("BPM: ");
          Serial.println(bpmAvgShown, 1);
        }
      }
      lastPeakTime = now;
      armedForPeak = false; // desarma até voltarmos a ficar abaixo do limiar
    }
  }

  // Re-arma quando cair abaixo do limiar
  if (!armedForPeak && !above) {
    armedForPeak = true;
  }

  // ---- Atualização do LCD ----
  if (now - lastLCDTime >= LCD_INTERVAL_MS) {
    updateLCD();
    lastLCDTime = now;
  }
}

// -------------------- LCD Update --------------------
void updateLCD() {
  if (bpmAvgShown > 0.0f) {
    char buf[17];
    dtostrf(bpmAvgShown, 0, 1, buf); // 1 casa decimal
    String l0 = "BPM: ";
    l0 += String(buf);
    lcdPrintFixed(0, 0, l0);

    String l1 = "Zona: ";
    l1 += zonaFromBpm(bpmAvgShown);
    lcdPrintFixed(0, 1, l1);
  } else {
    lcdPrintFixed(0, 0, "A calcular BPM...");
    lcdPrintFixed(0, 1, "Respira normal");
  }
}
