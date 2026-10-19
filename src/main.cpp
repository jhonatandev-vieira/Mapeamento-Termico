// ============================================================================
//  ARIA Platform — Mapeamento Térmico DS18B20
//  6 sensores DS18B20 no mesmo barramento 1-Wire (GPIO 4, 5 V, GND)
//  Envia t1-t6 para a plataforma a cada INTERVALO_MS milissegundos
//  Firmware versão: 1.0.0
// ============================================================================

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ArduinoJson.h>

// ── Versão ────────────────────────────────────────────────────────────────────
#define FIRMWARE_VERSION "1.0.0"
#define MODEL            "DS18B20-MAP"

// ── Hardware ──────────────────────────────────────────────────────────────────
#define PIN_ONE_WIRE     4    // GPIO 4 → barramento 1-Wire
#define PIN_LED_STATUS   2    // LED azul onboard (LOW = aceso)
#define MAX_SENSORS      6

// ── Intervalo de envio (ms) ───────────────────────────────────────────────────
#define INTERVALO_MS     30000   // 30 s

// ── Plataforma ────────────────────────────────────────────────────────────────
#define API_URL            ""
#define TARGET_DEVICE_ID   ""   //  Unidade Água Gelada
#define DEVICE_API_KEY     ""

// ── Credenciais WiFi ──────────────────────────────────────────────────────────
#define WIFI_SSID  "-----"
#define WIFI_PASS  "-----"

// ── OneWire + DallasTemperature ───────────────────────────────────────────────
OneWire           oneWire(PIN_ONE_WIRE);
DallasTemperature sensors(&oneWire);

// Endereços dos sensores (preenchidos no primeiro boot)
DeviceAddress     addr[MAX_SENSORS];
int               nSensors = 0;

// ── Estado ────────────────────────────────────────────────────────────────────
unsigned long     lastSend   = 0;
bool              wifiOk     = false;

// ─────────────────────────────────────────────────────────────────────────────
void connectWifi() {
    Serial.printf("[WiFi] Conectando a %s...\n", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        delay(400);
        Serial.print('.');
    }
    Serial.println();

    wifiOk = (WiFi.status() == WL_CONNECTED);
    if (wifiOk) {
        Serial.printf("[WiFi] Conectado! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("[WiFi] Falha — tentará novamente no próximo ciclo");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n[ARIA] DS18B20 Mapeamento Térmico v" FIRMWARE_VERSION);

    pinMode(PIN_LED_STATUS, OUTPUT);
    digitalWrite(PIN_LED_STATUS, HIGH); // apagado

    // Descobre sensores no barramento
    sensors.begin();
    nSensors = min((int)sensors.getDeviceCount(), MAX_SENSORS);
    Serial.printf("[1-Wire] %d sensor(es) encontrado(s)\n", nSensors);
    for (int i = 0; i < nSensors; i++) {
        sensors.getAddress(addr[i], i);
        sensors.setResolution(addr[i], 12); // 12 bits = 0.0625 °C
        Serial.printf("  [%d] %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\n", i,
            addr[i][0], addr[i][1], addr[i][2], addr[i][3],
            addr[i][4], addr[i][5], addr[i][6], addr[i][7]);
    }

    connectWifi();
}

// ─────────────────────────────────────────────────────────────────────────────
void sendTelemetry() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWifi();
        if (!wifiOk) return;
    }

    // Lê todos os sensores de uma vez
    sensors.requestTemperatures();

    // Monta JSON
    StaticJsonDocument<512> doc;
    doc["target_device_id"] = TARGET_DEVICE_ID;

    // t1-t6: valor raw = °C × 10 (mesmo que a plataforma espera)
    const char* keys[MAX_SENSORS] = {"t1","t2","t3","t4","t5","t6"};
    for (int i = 0; i < MAX_SENSORS; i++) {
        if (i < nSensors) {
            float tc = sensors.getTempC(addr[i]);
            if (tc != DEVICE_DISCONNECTED_C) {
                doc[keys[i]] = (int)round(tc * 10.0f);
            } else {
                doc[keys[i]] = nullptr; // sensor desconectado → null
            }
        } else {
            doc[keys[i]] = nullptr;    // posição sem sensor → null
        }
    }

    String body;
    serializeJson(doc, body);

    WiFiClientSecure client;
    client.setInsecure();   // aceita certificado auto-assinado / Let's Encrypt

    HTTPClient http;
    http.begin(client, API_URL);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Key", DEVICE_API_KEY);
    http.setTimeout(15000);

    int code = http.POST(body);
    if (code >= 200 && code < 300) {
        digitalWrite(PIN_LED_STATUS, LOW);  // pisca LED — OK
        delay(80);
        digitalWrite(PIN_LED_STATUS, HIGH);
        Serial.printf("[HTTP] OK %d | t1=%.1f t2=%.1f t3=%.1f t4=%.1f t5=%.1f t6=%.1f\n",
            code,
            (doc["t1"].isNull() ? NAN : (float)((int)doc["t1"]) / 10.0f),
            (doc["t2"].isNull() ? NAN : (float)((int)doc["t2"]) / 10.0f),
            (doc["t3"].isNull() ? NAN : (float)((int)doc["t3"]) / 10.0f),
            (doc["t4"].isNull() ? NAN : (float)((int)doc["t4"]) / 10.0f),
            (doc["t5"].isNull() ? NAN : (float)((int)doc["t5"]) / 10.0f),
            (doc["t6"].isNull() ? NAN : (float)((int)doc["t6"]) / 10.0f));
    } else {
        Serial.printf("[HTTP] Erro %d: %s\n", code, http.errorToString(code).c_str());
    }
    http.end();
}

// ─────────────────────────────────────────────────────────────────────────────
void loop() {
    unsigned long now = millis();
    if (now - lastSend >= INTERVALO_MS) {
        lastSend = now;
        sendTelemetry();
    }
}
