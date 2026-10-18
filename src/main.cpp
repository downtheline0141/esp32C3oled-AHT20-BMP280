#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <U8g2lib.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>

#define OLED_SDA 5
#define OLED_SCL 6
#define SENS_SDA 7
#define SENS_SCL 8
#define OX 28
#define OY 24

const int NP = 5, TN = 36, PN = 18;
const unsigned long SENSE_MS = 1000, TH_MS = 30000UL, PH_MS = 600000UL, PAGE_MS = 7000;

U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA, U8X8_PIN_NONE);
Adafruit_AHTX0 aht;
Adafruit_BMP280 bmp;

float T, H, P, Td, DI;
float tMin = 999, tMax = -999, hMin = 999, hMax = -999, pMin = 9999, pMax = 0;
float tHist[TN]; int tCnt = 0;
float pHist[PN]; int pCnt = 0;
int page = 0;
unsigned long tSense, tTh, tPh, tPage;

void push(float *a, int &cnt, int n, float v) {
  if (cnt < n) a[cnt++] = v;
  else { memmove(a, a + 1, (n - 1) * sizeof(float)); a[n - 1] = v; }
}

void update() {
  sensors_event_t hum, temp;
  aht.getEvent(&hum, &temp);
  T = temp.temperature;
  H = hum.relative_humidity;
  P = bmp.readPressure() / 100.0f;
  DI = 0.81f * T + 0.01f * H * (0.99f * T - 14.3f) + 46.3f;
  float g = logf(H / 100.0f) + 17.62f * T / (243.12f + T);
  Td = 243.12f * g / (17.62f - g);
  tMin = min(tMin, T); tMax = max(tMax, T);
  hMin = min(hMin, H); hMax = max(hMax, H);
  pMin = min(pMin, P); pMax = max(pMax, P);
}

// 3時間あたりの気圧変化。データ不足(30分未満)ならNAN
float pDelta3() {
  if (pCnt < 4) return NAN;
  float hours = (pCnt - 1) / 6.0f;
  return (pHist[pCnt - 1] - pHist[0]) / hours * 3.0f;
}

const char *diLabel() {
  if (DI < 55) return "COLD";
  if (DI < 60) return "CHILLY";
  if (DI < 65) return "FINE";
  if (DI < 75) return "COMFY";
  if (DI < 80) return "WARM";
  if (DI < 85) return "HOT";
  return "SWELTER";
}

void drawPage(int p, int dx) {
  int x = OX + dx;
  char buf[24];
  u8g2.clearBuffer();

  if (p == 0) {
    u8g2.drawBox(x + 4, OY + 3, 3, 11);
    u8g2.drawDisc(x + 5, OY + 17, 4);
    u8g2.setFont(u8g2_font_logisoso16_tr);
    snprintf(buf, sizeof(buf), "%.1f", T);
    u8g2.drawStr(x + 13, OY + 19, buf);
    int w = u8g2.getStrWidth(buf);
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.drawStr(x + 15 + w, OY + 19, "C");
    u8g2.drawTriangle(x + 5, OY + 25, x + 2, OY + 31, x + 8, OY + 31);
    u8g2.drawDisc(x + 5, OY + 32, 3);
    snprintf(buf, sizeof(buf), "%.1f %%", H);
    u8g2.drawStr(x + 13, OY + 37, buf);
  } else if (p == 1) {
    u8g2.setFont(u8g2_font_logisoso16_tr);
    snprintf(buf, sizeof(buf), "%.0f", P);
    u8g2.drawStr(x + 2, OY + 18, buf);
    int w = u8g2.getStrWidth(buf);
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(x + 4 + w, OY + 18, "hPa");
    float d = pDelta3();
    const char *msg = "Wait 30min";
    if (!isnan(d)) {
      int ax = x + 56, ay = OY + 6;
      if (d >= 1.0f) { msg = "Improving"; u8g2.drawTriangle(ax, ay + 8, ax + 8, ay + 8, ax + 4, ay); }
      else if (d <= -3.0f) { msg = "Storm?"; u8g2.drawTriangle(ax, ay, ax + 8, ay, ax + 4, ay + 8); }
      else if (d <= -1.0f) { msg = "Worsening"; u8g2.drawTriangle(ax, ay, ax + 8, ay, ax + 4, ay + 8); }
      else { msg = "Stable"; u8g2.drawBox(ax, ay + 3, 8, 3); }
    }
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(x + 2, OY + 36, msg);
  } else if (p == 2) {
    u8g2.setFont(u8g2_font_5x7_tr);
    if (tCnt < 2) {
      u8g2.drawStr(x + 2, OY + 20, "Collecting...");
    } else {
      float lo = tHist[0], hi = tHist[0];
      for (int i = 1; i < tCnt; i++) { lo = min(lo, tHist[i]); hi = max(hi, tHist[i]); }
      if (hi - lo < 2.0f) { float m = (hi + lo) / 2; lo = m - 1; hi = m + 1; }
      snprintf(buf, sizeof(buf), "T %.1f-%.1f", lo, hi);
      u8g2.drawStr(x + 2, OY + 8, buf);
      for (int i = 1; i < tCnt; i++) {
        int y0 = OY + 39 - (int)((tHist[i - 1] - lo) / (hi - lo) * 26);
        int y1 = OY + 39 - (int)((tHist[i] - lo) / (hi - lo) * 26);
        u8g2.drawLine(x + 1 + (i - 1) * 2, y0, x + 1 + i * 2, y1);
      }
    }
  } else if (p == 3) {
    u8g2.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "DI %.1f", DI);
    u8g2.drawStr(x + 2, OY + 10, buf);
    u8g2.drawStr(x + 2, OY + 22, diLabel());
    snprintf(buf, sizeof(buf), "Dew %.1fC", Td);
    u8g2.drawStr(x + 2, OY + 34, buf);
  } else {
    u8g2.setFont(u8g2_font_6x10_tf);
    snprintf(buf, sizeof(buf), "T %.1f-%.1f", tMin, tMax);
    u8g2.drawStr(x + 2, OY + 10, buf);
    snprintf(buf, sizeof(buf), "H %.0f-%.0f", hMin, hMax);
    u8g2.drawStr(x + 2, OY + 22, buf);
    snprintf(buf, sizeof(buf), "P %.0f-%.0f", pMin, pMax);
    u8g2.drawStr(x + 2, OY + 34, buf);
  }

  // ページインジケータ(右端、固定位置)
  for (int i = 0; i < NP; i++) {
    if (i == p) u8g2.drawBox(OX + 70, OY + 2 + i * 8, 2, 6);
    else u8g2.drawBox(OX + 70, OY + 4 + i * 8, 2, 2);
  }
  u8g2.sendBuffer();
}

void setup() {
  u8g2.begin();
  u8g2.setFont(u8g2_font_6x10_tf);
  Wire.begin(SENS_SDA, SENS_SCL);
  bool ahtOk = aht.begin();
  bool bmpOk = bmp.begin(0x76) || bmp.begin(0x77);
  if (!ahtOk || !bmpOk) {
    u8g2.clearBuffer();
    u8g2.drawStr(OX + 2, OY + 12, ahtOk ? "AHT20 OK" : "AHT20 NG");
    u8g2.drawStr(OX + 2, OY + 24, bmpOk ? "BMP280 OK" : "BMP280 NG");
    u8g2.sendBuffer();
    while (true) delay(1000);
  }
  update();
  push(tHist, tCnt, TN, T);
  push(pHist, pCnt, PN, P);
  tSense = tTh = tPh = tPage = millis();
  drawPage(page, 0);
}

void loop() {
  unsigned long now = millis();
  if (now - tSense >= SENSE_MS) { tSense = now; update(); drawPage(page, 0); }
  if (now - tTh >= TH_MS) { tTh = now; push(tHist, tCnt, TN, T); }
  if (now - tPh >= PH_MS) { tPh = now; push(pHist, pCnt, PN, P); }
  if (now - tPage >= PAGE_MS) {
    tPage = now;
    page = (page + 1) % NP;
    for (int dx = 72; dx > 0; dx -= 12) drawPage(page, dx);
    drawPage(page, 0);
  }
}