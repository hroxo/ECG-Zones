# ECG Heart Rate Monitor with LCD Display

This project uses an **Arduino**, an **AD8232 ECG module**, and a **16x2 LCD (HD44780, parallel interface)** to monitor heart activity and display **heart rate (BPM)** and the corresponding **training zone** in real-time.

It is a simple but robust implementation that includes adaptive calibration, lead-off detection, and smoothed BPM values for more stable readings.

---

## 🎥 Demo

👉 [Watch the demo on YouTube](https://youtube.com/shorts/2aTkwlv1664?si=z3m-hgHZYgVNGOnP)

---

## Features

- 📟 **Real-time display** of current BPM on a 16x2 LCD.  
- 🫀 **Heart rate zones** automatically classified:  
  - Z1: `<115 BPM` → Light  
  - Z2: `115–133 BPM` → Moderate  
  - Z3: `134–152 BPM` → Intense  
  - Z4: `>153 BPM` → Very Intense  
- 🔌 **Lead-off detection**: warns on the LCD if electrodes are not properly attached.  
- ⚖️ **Adaptive thresholding**: automatically adjusts to noise and baseline drift.  
- 🎛 **Automatic calibration** on startup (first ~6 seconds).  
- 📉 **Noise reduction & smoothing**: uses baseline removal and moving average of RR intervals.

---

## Hardware Required

- Arduino board (e.g., **Arduino Uno**).  
- **AD8232 ECG sensor module**.  
- **16x2 LCD (HD44780, parallel interface)**.  
- 10kΩ potentiometer (for LCD contrast control).  
- Breadboard and jumper wires.  
- Electrodes for ECG acquisition.  

---

## Wiring

### LCD (HD44780, 16x2, parallel)

| LCD Pin | Arduino Pin |
|---------|-------------|
| RS      | 12          |
| EN      | 10          |
| D4      | 5           |
| D5      | 4           |
| D6      | 3           |
| D7      | 2           |
| RW      | GND         |
| VSS     | GND         |
| VDD     | +5V         |
| VO      | Potentiometer (contrast) |
| A/K     | +5V / GND (backlight) |

### AD8232 ECG Module

| AD8232 Pin | Arduino Pin |
|------------|-------------|
| OUTPUT     | A0          |
| LO+        | 7           |
| LO-        | 8           |
| 3.3V/5V    | 3.3V/5V     |
| GND        | GND         |

⚠️ Make sure **all grounds (Arduino, LCD, AD8232)** are connected together.

---

## Software Setup

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).  
2. Connect your Arduino board via USB.  
3. Clone this repository:  
   ```bash
   git clone https://github.com/hroxo/ECG-Zones.git
   cd ECG-Zones
