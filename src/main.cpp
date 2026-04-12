// M5Stack Air Monitor
// PMS5003, BME680, SHT3X, QMP6988, SGP30, GPS
// Buffer SD hors-ligne, MQTT vers HiveMQ Cloud
// Seuils : ATMO / OMS 2021 / ANSES

#include <M5Stack.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_BME680.h>
#include <PMS.h>
#include <TinyGPS++.h>
#include "SHT3X.h"
#include "QMP6988.h"
#include <SD.h>
#include <SPI.h>
#include <Adafruit_SGP30.h>
#include "credentials.h"

// Couleurs
#define CLR_BG     0x0000
#define CLR_BAR    0x1082
#define CLR_SEP    0x3186
#define CLR_LABEL  0x8C71
#define CLR_GOOD   0x07E0   // vert
#define CLR_MED    0xFFE0   // jaune
#define CLR_BAD    0xFD20   // orange
#define CLR_DANGER 0xF800   // rouge
#define CLR_CYAN   0x07FF

// Ecran
#define BAR_H      28
#define BTN_Y      216
#define CONT_Y     (BAR_H + 1)
#define NUM_PAGES  2

// Buffer SD
#define SD_MAX_BYTES   512000
#define SD_FLUSH_BATCH 5

// Hardware
HardwareSerial  SerialPMS(1);
HardwareSerial  SerialGPS(2);
PMS             pms(SerialPMS);
PMS::DATA       pmsData;
TinyGPSPlus     gps;
Adafruit_BME680 bme;
SHT3X           sht;
QMP6988         qmp;
Adafruit_SGP30  sgp;
WiFiClientSecure wifiClient;
PubSubClient     mqtt(wifiClient);

// Donnees capteurs
struct {
    uint16_t pm1 = 0, pm25 = 0, pm10 = 0;
    float    temp = 0, humi = 0, pres = 0, alt = 0, voc = 0;
    double   lat = 0, lng = 0, gpsAlt = 0;
    int      sats = 0;
    bool     gpsValid = false;
    int      battery  = 0;
    bool     charging = false;
    uint16_t tvoc = 0, eco2 = 0;
    bool     sgpOk = false;
} d;

// UI
int  page     = 0;
bool screenOn = true;

// Timers
uint32_t tPms = 0, tBme = 0, tBat = 0, tMqtt = 0;
uint32_t tRecon = 0, tDisplay = 0, tWifiCheck = 0;
uint32_t mqttRetryDelay = 2000;
const uint32_t MAX_RETRY_DELAY = 60000;
bool sdAvailable = false;

// Prototypes
void     drawStatusBar();
void     drawBtnBar();
void     drawContent();
void     drawPageAir();
void     drawPageEnv();
void     setScreen(bool on);
uint16_t qColor(float v, float good, float med, float bad);
const char* qLabel(float v, float good, float med, float bad);
void     drawProgressBar(int x, int y, int w, int h, float ratio, uint16_t color);

// Humidite absolue pour compensation SGP30
uint32_t getAbsoluteHumidity(float t, float h) {
    float ah = 216.7f * ((h / 100.0f) * 6.112f *
        exp((17.62f * t) / (243.12f + t)) / (273.15f + t));
    return (uint32_t)(ah * 256.0f);
}

void setup() {
    M5.begin();
    M5.Power.begin();
    dacWrite(25, 0);
    M5.Speaker.mute();

    M5.Lcd.fillScreen(CLR_BG);
    M5.Lcd.setBrightness(150);

    if (SD.begin()) sdAvailable = true;

    // Capteurs serie
    SerialPMS.begin(9600, SERIAL_8N1, 36, 26);
    SerialGPS.begin(9600, SERIAL_8N1, 16, 17);
    pms.passiveMode();
    pms.wakeUp();

    // I2C
    Wire.begin(21, 22);
    sht.begin(&Wire, SHT3X_I2C_ADDR, 21, 22);
    qmp.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 21, 22);
    if (sgp.begin()) d.sgpOk = true;

    if (!bme.begin(0x77) && !bme.begin(0x76)) {
        M5.Lcd.setTextDatum(MC_DATUM);
        M5.Lcd.setTextColor(CLR_DANGER, CLR_BG);
        M5.Lcd.drawString("BME680 non detecte", 160, 120, 2);
        M5.Lcd.setTextDatum(TL_DATUM);
        delay(1500);
    }
    bme.setTemperatureOversampling(BME680_OS_8X);
    bme.setHumidityOversampling(BME680_OS_2X);
    bme.setPressureOversampling(BME680_OS_4X);
    bme.setIIRFilterSize(BME680_FILTER_SIZE_3);
    bme.setGasHeater(320, 150);

    // WiFi + MQTT
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    wifiClient.setInsecure();
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    mqtt.setKeepAlive(30);

    d.battery  = M5.Power.getBatteryLevel();
    d.charging = M5.Power.isCharging();

    M5.Lcd.fillScreen(CLR_BG);
    drawStatusBar();
    drawBtnBar();
    drawContent();
}

void loop() {
    M5.update();
    uint32_t now = millis();

    // Boutons : A=<  B=ecran  C=>
    if (M5.BtnB.wasPressed()) { setScreen(!screenOn); return; }
    if (screenOn) {
        if (M5.BtnA.wasPressed()) {
            page = (page + NUM_PAGES - 1) % NUM_PAGES;
            drawBtnBar(); drawContent();
        }
        if (M5.BtnC.wasPressed()) {
            page = (page + 1) % NUM_PAGES;
            drawBtnBar(); drawContent();
        }
    }

    // PMS5003 toutes les 2s
    if (now - tPms >= 2000) { tPms = now; pms.requestRead(); }
    if (pms.read(pmsData)) {
        d.pm1  = pmsData.PM_AE_UG_1_0;
        d.pm25 = pmsData.PM_AE_UG_2_5;
        d.pm10 = pmsData.PM_AE_UG_10_0;
    }

    // BME680 + ENV III toutes les 3s
    if (now - tBme >= 3000) {
        tBme = now;
        if (bme.performReading()) d.voc = bme.gas_resistance / 1000.0f;
        if (sht.update()) { d.temp = sht.cTemp; d.humi = sht.humidity; }
        if (qmp.update())  { d.pres = qmp.pressure / 100.0f; d.alt = qmp.altitude; }
    }

    // SGP30 + compensation humidite
    if (d.sgpOk) {
        if (d.humi > 0 && d.temp > -40)
            sgp.setHumidity(getAbsoluteHumidity(d.temp, d.humi));
        if (sgp.IAQmeasure()) { d.tvoc = sgp.TVOC; d.eco2 = sgp.eCO2; }
    }

    // GPS
    while (SerialGPS.available()) gps.encode(SerialGPS.read());
    if (gps.location.isValid()) {
        d.lat = gps.location.lat(); d.lng = gps.location.lng();
        d.gpsValid = true;
    }
    if (gps.satellites.isValid()) d.sats   = gps.satellites.value();
    if (gps.altitude.isValid())   d.gpsAlt = gps.altitude.meters();

    // Batterie toutes les 30s
    if (now - tBat >= 30000) {
        tBat = now;
        d.battery  = M5.Power.getBatteryLevel();
        d.charging = M5.Power.isCharging();
    }

    // Rafraichir l'ecran toutes les 2s
    if (screenOn && now - tDisplay >= 2000) {
        tDisplay = now;
        drawStatusBar(); drawContent();
    }

    // Reconnexion WiFi toutes les 30s si deconnecte
    if (!WiFi.isConnected() && now - tWifiCheck >= 30000) {
        tWifiCheck = now;
        WiFi.reconnect();
    }

    // Reconnexion MQTT avec backoff
    if (WiFi.isConnected()) {
        if (!mqtt.connected()) {
            if (now - tRecon >= mqttRetryDelay) {
                tRecon = now;
                if (mqtt.connect(DEVICE_ID, MQTT_USER, MQTT_PASS))
                    mqttRetryDelay = 2000;
                else
                    mqttRetryDelay = min(mqttRetryDelay * 2, MAX_RETRY_DELAY);
            }
        } else {
            mqtt.loop();
        }
    }

    // Envoi MQTT + buffer SD toutes les 10s
    if (now - tMqtt >= 10000) {
        tMqtt = now;

        char buf[600], topic[48];
        snprintf(topic, sizeof(topic), "sensors/%s/data", DEVICE_ID);
        snprintf(buf, sizeof(buf),
            "{\"pm1\":%d,\"pm25\":%d,\"pm10\":%d,"
            "\"voc\":%.1f,\"temp\":%.1f,\"humi\":%.1f,"
            "\"pres\":%.1f,\"alt\":%.1f,"
            "\"tvoc\":%d,\"eco2\":%d,"
            "\"lat\":%.6f,\"lng\":%.6f,\"gps_alt\":%.1f,\"sats\":%d,"
            "\"battery\":%d,\"charging\":%s}",
            d.pm1, d.pm25, d.pm10,
            d.voc, d.temp, d.humi, d.pres, d.alt,
            d.tvoc, d.eco2,
            d.gpsValid ? d.lat : 0.0, d.gpsValid ? d.lng : 0.0,
            d.gpsAlt, d.sats, d.battery,
            d.charging ? "true" : "false");

        if (mqtt.connected()) {
            // Vider le buffer SD par lots
            if (sdAvailable && SD.exists("/buffer.txt")) {
                File fin = SD.open("/buffer.txt", FILE_READ);
                if (fin) {
                    File fout = SD.open("/buffer_tmp.txt", FILE_WRITE);
                    int sent = 0;
                    char line[600];
                    while (fin.available()) {
                        int len = 0;
                        while (fin.available() && len < 599) {
                            char c = fin.read();
                            if (c == '\n') break;
                            line[len++] = c;
                        }
                        line[len] = '\0';
                        if (len == 0) continue;
                        if (sent < SD_FLUSH_BATCH) {
                            mqtt.publish(topic, line, false);
                            sent++;
                        } else if (fout) {
                            fout.println(line);
                        }
                    }
                    fin.close();
                    bool hasRemaining = false;
                    if (fout) { hasRemaining = fout.size() > 0; fout.close(); }
                    SD.remove("/buffer.txt");
                    if (hasRemaining)
                        SD.rename("/buffer_tmp.txt", "/buffer.txt");
                    else
                        SD.remove("/buffer_tmp.txt");
                }
            }
            mqtt.publish(topic, buf, true);
        } else if (sdAvailable) {
            long sz = 0;
            if (SD.exists("/buffer.txt")) {
                File f = SD.open("/buffer.txt", FILE_READ);
                if (f) { sz = f.size(); f.close(); }
            }
            if (sz < SD_MAX_BYTES) {
                File f = SD.open("/buffer.txt", FILE_APPEND);
                if (f) { f.println(buf); f.close(); }
            }
        }

        drawStatusBar();
    }
}

// Ecran on/off
void setScreen(bool on) {
    screenOn = on;
    if (on) {
        M5.Lcd.wakeup();
        M5.Lcd.setBrightness(150);
        M5.Lcd.fillScreen(CLR_BG);
        drawStatusBar(); drawBtnBar(); drawContent();
    } else {
        M5.Lcd.setBrightness(0);
        M5.Lcd.sleep();
    }
}

// Couleur 4 niveaux (plus c'est bas mieux c'est)
uint16_t qColor(float v, float good, float med, float bad) {
    if (v <= good) return CLR_GOOD;
    if (v <= med)  return CLR_MED;
    if (v <= bad)  return CLR_BAD;
    return CLR_DANGER;
}

// Label texte 4 niveaux
const char* qLabel(float v, float good, float med, float bad) {
    if (v <= good) return "BON";
    if (v <= med)  return "MOYEN";
    if (v <= bad)  return "MAUVAIS";
    return "DANGER";
}

// Couleur temperature (plage de confort ANSES)
uint16_t tempColor(float t) {
    if (t >= 19 && t <= 24) return CLR_GOOD;
    if (t >= 17 && t <= 27) return CLR_MED;
    if (t >= 14 && t <= 30) return CLR_BAD;
    return CLR_DANGER;
}

// Couleur humidite (plage de confort ANSES/OMS)
uint16_t humiColor(float h) {
    if (h >= 40 && h <= 60) return CLR_GOOD;
    if (h >= 30 && h <= 70) return CLR_MED;
    if (h >= 20 && h <= 80) return CLR_BAD;
    return CLR_DANGER;
}

// Barre de progression
void drawProgressBar(int x, int y, int w, int h, float ratio, uint16_t color) {
    int filled = (int)(w * constrain(ratio, 0.0f, 1.0f));
    M5.Lcd.drawRect(x, y, w, h, CLR_LABEL);
    if (filled > 2) M5.Lcd.fillRect(x+1, y+1, filled-2, h-2, color);
    M5.Lcd.fillRect(x+1+max(0, filled-2), y+1, w-2-max(0, filled-2), h-2, CLR_BG);
}

// Barre de statut (haut)
void drawStatusBar() {
    M5.Lcd.fillRect(0, 0, 320, BAR_H, CLR_BAR);

    uint16_t sc = mqtt.connected() ? CLR_GOOD : WiFi.isConnected() ? CLR_MED : CLR_DANGER;
    M5.Lcd.fillCircle(14, BAR_H/2, 6, sc);

    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(WHITE, CLR_BAR);
    M5.Lcd.drawString("AIR MONITOR", 160, BAR_H/2, 2);

    char batBuf[12];
    snprintf(batBuf, sizeof(batBuf), d.charging ? "~%d%%" : "%d%%", d.battery);
    M5.Lcd.setTextColor(d.battery > 20 ? CLR_GOOD : CLR_DANGER, CLR_BAR);
    M5.Lcd.setTextDatum(MR_DATUM);
    M5.Lcd.drawString(batBuf, 314, BAR_H/2, 2);

    M5.Lcd.drawFastHLine(0, BAR_H, 320, CLR_SEP);
    M5.Lcd.setTextDatum(TL_DATUM);
}

// Barre des boutons (bas)
void drawBtnBar() {
    M5.Lcd.fillRect(0, BTN_Y, 320, 240-BTN_Y, CLR_BAR);
    M5.Lcd.drawFastHLine(0, BTN_Y, 320, CLR_SEP);

    const char* names[] = {"Air", "Env"};

    M5.Lcd.setTextDatum(MC_DATUM);
    M5.Lcd.setTextColor(CLR_LABEL, CLR_BAR);
    M5.Lcd.drawString("<", 54, BTN_Y+12, 2);
    M5.Lcd.setTextColor(WHITE, CLR_BAR);
    M5.Lcd.drawString(names[page], 160, BTN_Y+12, 2);
    M5.Lcd.setTextColor(CLR_LABEL, CLR_BAR);
    M5.Lcd.drawString(">", 266, BTN_Y+12, 2);
    M5.Lcd.setTextDatum(TL_DATUM);
}

// Routage des pages
void drawContent() {
    M5.Lcd.fillRect(0, CONT_Y, 320, BTN_Y-CONT_Y, CLR_BG);
    switch (page) {
        case 0: drawPageAir(); break;
        case 1: drawPageEnv(); break;
    }
}

// Page Air : PM + TVOC + eCO2
void drawPageAir() {
    M5.Lcd.setTextDatum(TC_DATUM);
    M5.Lcd.setTextColor(CLR_CYAN, CLR_BG);
    M5.Lcd.drawString("QUALITE DE L'AIR", 160, CONT_Y+4, 2);

    struct { const char* lbl; uint16_t val; float good; float med; float bad; } pm[3] = {
        {"PM 1.0", d.pm1,  10, 20,  35},
        {"PM 2.5", d.pm25, 10, 25,  50},
        {"PM 10",  d.pm10, 20, 50, 100},
    };
    const int cy = CONT_Y + 24;
    const int cx[3] = {53, 160, 267};

    for (int i = 0; i < 3; i++) {
        float v = (float)pm[i].val;
        uint16_t c = qColor(v, pm[i].good, pm[i].med, pm[i].bad);
        const char* lbl = qLabel(v, pm[i].good, pm[i].med, pm[i].bad);

        M5.Lcd.setTextDatum(TC_DATUM);
        M5.Lcd.setTextColor(CLR_LABEL, CLR_BG);
        M5.Lcd.drawString(pm[i].lbl, cx[i], cy, 2);
        M5.Lcd.setTextColor(c, CLR_BG);
        M5.Lcd.drawNumber(pm[i].val, cx[i], cy+16, 4);
        M5.Lcd.setTextColor(CLR_LABEL, CLR_BG);
        M5.Lcd.drawString("ug/m3", cx[i], cy+42, 1);
        M5.Lcd.setTextColor(c, CLR_BG);
        M5.Lcd.drawString(lbl, cx[i], cy+52, 1);
    }

    int sep = CONT_Y + 92;
    M5.Lcd.drawFastHLine(10, sep, 300, CLR_SEP);

    // TVOC
    float tv = (float)d.tvoc;
    uint16_t tvC = qColor(tv, 220, 660, 2200);
    const char* tvL = qLabel(tv, 220, 660, 2200);
    char tvBuf[24];
    snprintf(tvBuf, sizeof(tvBuf), "TVOC  %d ppb", d.tvoc);

    int y1 = sep + 8;
    M5.Lcd.setTextColor(tvC, CLR_BG);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString(tvBuf, 10, y1, 2);
    M5.Lcd.setTextDatum(MR_DATUM);
    M5.Lcd.drawString(tvL, 310, y1, 2);
    drawProgressBar(10, y1+12, 300, 10, d.tvoc / 2200.0f, tvC);

    // eCO2
    float ev = (float)d.eco2;
    uint16_t eC = qColor(ev, 800, 1200, 2000);
    const char* eL = qLabel(ev, 800, 1200, 2000);
    char eBuf[24];
    snprintf(eBuf, sizeof(eBuf), "eCO2  %d ppm", d.eco2);

    int y2 = y1 + 32;
    M5.Lcd.setTextColor(eC, CLR_BG);
    M5.Lcd.setTextDatum(ML_DATUM);
    M5.Lcd.drawString(eBuf, 10, y2, 2);
    M5.Lcd.setTextDatum(MR_DATUM);
    M5.Lcd.drawString(eL, 310, y2, 2);
    drawProgressBar(10, y2+12, 300, 10, d.eco2 / 2000.0f, eC);

    M5.Lcd.setTextDatum(TL_DATUM);
}

// Page Env : temperature, humidite, pression, GPS
void drawPageEnv() {
    M5.Lcd.setTextDatum(TC_DATUM);
    M5.Lcd.setTextColor(CLR_CYAN, CLR_BG);
    M5.Lcd.drawString("ENVIRONNEMENT", 160, CONT_Y+4, 2);
    M5.Lcd.setTextDatum(TL_DATUM);

    char val0[20], val1[20], val2[20], val3[32];
    const char* labels[4];
    uint16_t colors[4];
    int fonts[4] = {4, 4, 4, 2};

    snprintf(val0, sizeof(val0), "%.1f C", d.temp);
    labels[0] = "Temperature";
    colors[0] = tempColor(d.temp);

    snprintf(val1, sizeof(val1), "%.1f %%", d.humi);
    labels[1] = "Humidite";
    colors[1] = humiColor(d.humi);

    snprintf(val2, sizeof(val2), "%.0f hPa", d.pres);
    labels[2] = "Pression";
    colors[2] = WHITE;

    if (d.gpsValid) {
        snprintf(val3, sizeof(val3), "%.4f / %.4f  %dsat", d.lat, d.lng, d.sats);
        colors[3] = CLR_GOOD;
    } else {
        snprintf(val3, sizeof(val3), "Acquisition... %d sat", d.sats);
        colors[3] = CLR_MED;
    }
    labels[3] = "GPS";

    const char* values[4] = {val0, val1, val2, val3};
    int ry = CONT_Y + 28;
    for (int i = 0; i < 4; i++) {
        int mid = ry + 19;
        M5.Lcd.setTextColor(CLR_LABEL, CLR_BG);
        M5.Lcd.setTextDatum(ML_DATUM);
        M5.Lcd.drawString(labels[i], 14, mid, 2);
        M5.Lcd.setTextColor(colors[i], CLR_BG);
        M5.Lcd.setTextDatum(MR_DATUM);
        M5.Lcd.drawString(values[i], 310, mid, fonts[i]);
        if (i < 3) M5.Lcd.drawFastHLine(10, ry+38, 300, CLR_SEP);
        ry += 38;
    }
    M5.Lcd.setTextDatum(TL_DATUM);
}
