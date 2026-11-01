/*
  ESP32-S3 + cámara  ->  servidor para el clasificador de monedas
  --------------------------------------------------------------
  Puerto 80  : /status  (JSON con datos en tiempo real)
               /cmd?c=A (aceptada)  /cmd?c=R (rechazada)  <- los envía el HTML
  Puerto 81  : /stream  (video MJPEG que el HTML usa para reconocer monedas)

  Arduino IDE (core "esp32" de Espressif 3.x):
    Placa ............ ESP32S3 Dev Module
    PSRAM ............ OPI PSRAM (N16R8)  /  QSPI PSRAM (N8R2, N4R2)
    USB CDC On Boot .. Enabled   (para ver el Serial por el puerto USB)
    Flash Size ....... la de tu placa (ej. 16MB)
    Partition Scheme . 8MB with spiffs  (o Huge APP si tu flash es menor)
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"

// ===================== CONFIGURA ESTO =====================
const char* WIFI_SSID = "TU_WIFI";
const char* WIFI_PASS = "TU_CLAVE";

// Salidas que se activan 300 ms cuando llega A (aceptada) o R (rechazada)
#define PIN_ACEPTAR   1
#define PIN_RECHAZAR  2
#define PULSO_MS      300

// ---- Ajustes para que la cámara y el chip se calienten menos ----
#define FPS_MAX       10         // máximo de cuadros por segundo del stream (menos = más frío)
#define CAM_XCLK_HZ   10000000   // reloj de la cámara: 10 MHz (antes 20 MHz). Baja calor y ruido
#define CPU_MHZ       160        // CPU del ESP32-S3 (antes 240 MHz). 80 si aún se calienta
#define JPEG_CALIDAD  15         // 10 = mejor imagen, 63 = peor. Más alto = menos trabajo
#define TX_WIFI       WIFI_POWER_15dBm  // potencia de WiFi (menos calor; sube a 19_5dBm si se corta)

// ---- Servo guía: orienta la moneda hacia su caja ----
#define USAR_SERVO        1     // 0 = apaga todo lo del servo y queda como antes
#define PIN_SERVO         14    // libre en modelo A. Si usas el modelo B cámbialo (ej. 48)
#define SERVO_US_MIN      500   // pulso para 0 grados (microsegundos)
#define SERVO_US_MAX      2500  // pulso para 180 grados
#define SERVO_REPOSO      90    // posición de espera (grados)
#define SERVO_RECHAZADA   170   // grados cuando la moneda es rechazada
#define SERVO_RETRASO_MS  800   // tiempo desde que la cámara la ve hasta que el servo se mueve
#define SERVO_MANTENER_MS 1200  // tiempo que el servo se queda en posición antes de volver al reposo
// Grados por denominación, columnas {nueva, antigua}. AJUSTA estos valores a tu banda:
const int DENOM[5]        = {50, 100, 200, 500, 1000};
const int SERVO_ANG[5][2] = {{20,20}, {45,45}, {70,70}, {110,110}, {135,135}};

// Elige el mapa de pines de TU placa (deja solo uno en 1):
#define CAM_MODELO_A  1   // Freenove ESP32-S3 WROOM, ESP32-S3-EYE y la mayoría de ESP32-S3-CAM genéricas
#define CAM_MODELO_B  0   // Variante "CAM_LCD" (verifica el pinout de tu placa)

#define VOLTEAR_VERTICAL    0   // pon 1 si la imagen sale al revés
#define VOLTEAR_HORIZONTAL  0   // pon 1 si sale en espejo
// ==========================================================

#if CAM_MODELO_A
  #define PWDN_GPIO_NUM  -1
  #define RESET_GPIO_NUM -1
  #define XCLK_GPIO_NUM  15
  #define SIOD_GPIO_NUM  4
  #define SIOC_GPIO_NUM  5
  #define Y9_GPIO_NUM    16
  #define Y8_GPIO_NUM    17
  #define Y7_GPIO_NUM    18
  #define Y6_GPIO_NUM    12
  #define Y5_GPIO_NUM    10
  #define Y4_GPIO_NUM    8
  #define Y3_GPIO_NUM    9
  #define Y2_GPIO_NUM    11
  #define VSYNC_GPIO_NUM 6
  #define HREF_GPIO_NUM  7
  #define PCLK_GPIO_NUM  13
#elif CAM_MODELO_B
  #define PWDN_GPIO_NUM  -1
  #define RESET_GPIO_NUM -1
  #define XCLK_GPIO_NUM  40
  #define SIOD_GPIO_NUM  17
  #define SIOC_GPIO_NUM  18
  #define Y9_GPIO_NUM    39
  #define Y8_GPIO_NUM    41
  #define Y7_GPIO_NUM    42
  #define Y6_GPIO_NUM    12
  #define Y5_GPIO_NUM    3
  #define Y4_GPIO_NUM    14
  #define Y3_GPIO_NUM    47
  #define Y2_GPIO_NUM    13
  #define VSYNC_GPIO_NUM 21
  #define HREF_GPIO_NUM  38
  #define PCLK_GPIO_NUM  11
#endif

#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t srvControl = NULL;
httpd_handle_t srvStream  = NULL;

volatile uint32_t frames = 0;
volatile bool streamActivo = false;   // true mientras el HTML está viendo el video
uint32_t framesPrev = 0, fps = 0, tFps = 0;
uint32_t nAceptadas = 0, nRechazadas = 0, nComandos = 0;
char ultimoCmd = '-';
uint32_t tUltimoCmd = 0;
uint32_t finPulsoAceptar = 0, finPulsoRechazar = 0;
int frameW = 0, frameH = 0;

// ---------------------------------------------------------
bool iniciarCamara() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_7;   // canal/timer altos: el servo usa los bajos
  config.ledc_timer   = LEDC_TIMER_3;
  config.pin_d0 = Y2_GPIO_NUM;  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = CAM_XCLK_HZ;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;      // 640x480 (4:3) = 4 veces los píxeles de QVGA
  config.jpeg_quality = JPEG_CALIDAD;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;  // siempre el frame más reciente
  config.fb_count = 2;

  if (!psramFound()) {                    // sin PSRAM
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("ERROR camara 0x%x\n", err);
    return false;
  }
  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, VOLTEAR_VERTICAL);
  s->set_hmirror(s, VOLTEAR_HORIZONTAL);
  frameW = 640;
  frameH = 480;
  return true;
}

// ---------------------------------------------------------
// Servo guía (cola de eventos: varias monedas en la banda no se pisan)
struct EventoServo { uint32_t t; int ang; };
EventoServo colaServo[8];
uint8_t qIni = 0, qN = 0;
uint32_t servoRegresa = 0;
int servoAng = SERVO_REPOSO;

#if USAR_SERVO
void servoMover(int ang) {
  ang = constrain(ang, 0, 180);
  uint32_t us = SERVO_US_MIN + (uint32_t)(SERVO_US_MAX - SERVO_US_MIN) * ang / 180;
  ledcWrite(PIN_SERVO, (uint32_t)((uint64_t)us * 16383 / 20000));   // 50 Hz, 14 bits
  servoAng = ang;
}
void servoIniciar() { ledcAttach(PIN_SERVO, 50, 14); servoMover(SERVO_REPOSO); }
void servoEncolar(int ang) {
  if (qN >= 8) return;
  colaServo[(qIni + qN) % 8] = { millis() + SERVO_RETRASO_MS, ang };
  qN++;
}
void servoPorMoneda(int denom, char tipo) {   // tipo: 'N' nueva, 'A' antigua
  for (int i = 0; i < 5; i++)
    if (DENOM[i] == denom) { servoEncolar(SERVO_ANG[i][tipo == 'A' ? 1 : 0]); return; }
}
void servoLoop() {
  uint32_t ahora = millis();
  if (qN && (int32_t)(ahora - colaServo[qIni].t) >= 0) {
    servoMover(colaServo[qIni].ang);
    servoRegresa = ahora + SERVO_MANTENER_MS;
    qIni = (qIni + 1) % 8; qN--;
  } else if (servoRegresa && (int32_t)(ahora - servoRegresa) >= 0) {
    servoMover(SERVO_REPOSO);
    servoRegresa = 0;
  }
}
#else
void servoMover(int) {}
void servoIniciar() {}
void servoEncolar(int) {}
void servoPorMoneda(int, char) {}
void servoLoop() {}
#endif

// ---------------------------------------------------------
static esp_err_t streamHandler(httpd_req_t* req) {
  char part[64];
  esp_err_t res = httpd_resp_set_type(req, STREAM_TYPE);
  if (res != ESP_OK) return res;
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");  // necesario para que el HTML lea los pixeles
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");

  streamActivo = true;
  uint32_t tUltimo = 0;
  while (true) {
    camera_fb_t* fb = esp_camera_fb_get();       // siempre se recoge cada cuadro (evita FB-OVF)
    if (!fb) { res = ESP_FAIL; break; }
    uint32_t ahora = millis();
    if (ahora - tUltimo < (1000 / FPS_MAX)) {    // límite de FPS: se descarta el cuadro, no se hace pausa
      esp_camera_fb_return(fb);
      continue;
    }
    tUltimo = ahora;
    size_t hlen = snprintf(part, sizeof(part), STREAM_PART, (unsigned)fb->len);
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;     // el navegador cerró la conexión
    frames++;
  }
  streamActivo = false;
  return res;
}

static esp_err_t statusHandler(httpd_req_t* req) {
  char json[600];
  snprintf(json, sizeof(json),
    "{\"ip\":\"%s\",\"rssi\":%d,\"uptime\":%lu,\"fps\":%lu,\"w\":%d,\"h\":%d,"
    "\"heap\":%u,\"psram\":%u,\"acept\":%lu,\"rech\":%lu,\"cmds\":%lu,"
    "\"ultimo\":\"%c\",\"hace\":%lu,\"temp\":%.1f,\"srv\":%d}",
    WiFi.localIP().toString().c_str(), WiFi.RSSI(), millis() / 1000UL,
    (unsigned long)fps, frameW, frameH,
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
    (unsigned long)nAceptadas, (unsigned long)nRechazadas, (unsigned long)nComandos,
    ultimoCmd, tUltimoCmd ? (unsigned long)(millis() - tUltimoCmd) : 0UL,
    temperatureRead(), servoAng);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t cmdHandler(httpd_req_t* req) {
  char q[64] = {0}, val[8] = {0}, vd[8] = {0}, vt[4] = {0};
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK) {
    httpd_query_key_value(q, "c", val, sizeof(val));
    httpd_query_key_value(q, "d", vd, sizeof(vd));   // denominación (opcional): 50,100,200,500,1000
    httpd_query_key_value(q, "t", vt, sizeof(vt));   // tipo (opcional): N nueva, A antigua
  }
  char c = val[0];
  int denom = atoi(vd);
  bool ok = true;

  if (c == 'A') { nAceptadas++; digitalWrite(PIN_ACEPTAR, HIGH);  finPulsoAceptar  = millis() + PULSO_MS; }
  else if (c == 'R') { nRechazadas++; digitalWrite(PIN_RECHAZAR, HIGH); finPulsoRechazar = millis() + PULSO_MS; }
  else ok = false;

  if (ok && c == 'A' && denom > 0) servoPorMoneda(denom, vt[0]);
  if (ok && c == 'R') servoEncolar(SERVO_RECHAZADA);

  if (ok) {
    nComandos++; ultimoCmd = c; tUltimoCmd = millis();
    Serial.printf("CMD recibido: %c (aceptadas=%lu, rechazadas=%lu)\n", c,
                  (unsigned long)nAceptadas, (unsigned long)nRechazadas);
  }
  char out[64];
  snprintf(out, sizeof(out), "{\"ok\":%s,\"c\":\"%c\"}", ok ? "true" : "false", ok ? c : '?');
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t servoHandler(httpd_req_t* req) {   // /servo?a=90  (para calibrar los grados)
  char q[24] = {0}, val[8] = {0};
  if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK)
    httpd_query_key_value(q, "a", val, sizeof(val));
  servoMover(atoi(val));
  servoRegresa = 0;       // se queda donde lo pongas
  char out[32];
  snprintf(out, sizeof(out), "{\"srv\":%d}", servoAng);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
}

void iniciarServidores() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.max_uri_handlers = 8;
  httpd_uri_t uStatus = {"/status", HTTP_GET, statusHandler, NULL};
  httpd_uri_t uCmd    = {"/cmd",    HTTP_GET, cmdHandler,    NULL};
  httpd_uri_t uServo  = {"/servo",  HTTP_GET, servoHandler,  NULL};
  if (httpd_start(&srvControl, &config) == ESP_OK) {
    httpd_register_uri_handler(srvControl, &uStatus);
    httpd_register_uri_handler(srvControl, &uCmd);
    httpd_register_uri_handler(srvControl, &uServo);
  }
  config.server_port = 81;
  config.ctrl_port += 1;
  httpd_uri_t uStream = {"/stream", HTTP_GET, streamHandler, NULL};
  if (httpd_start(&srvStream, &config) == ESP_OK)
    httpd_register_uri_handler(srvStream, &uStream);
}

// ---------------------------------------------------------
void setup() {
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  delay(500);
  pinMode(PIN_ACEPTAR, OUTPUT);  digitalWrite(PIN_ACEPTAR, LOW);
  pinMode(PIN_RECHAZAR, OUTPUT); digitalWrite(PIN_RECHAZAR, LOW);

  servoIniciar();   // después de configurar los pines de salida
  if (!iniciarCamara()) { Serial.println("Camara fallo. Revisa el modelo de pines."); while (true) delay(1000); }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(400); Serial.print("."); }
  Serial.println();
  WiFi.setTxPower(TX_WIFI);

  if (MDNS.begin("monedas")) Serial.println("Tambien disponible como: monedas.local");
  iniciarServidores();

  Serial.println("=====================================");
  Serial.print("IP de la ESP32: "); Serial.println(WiFi.localIP());
  Serial.println("Escribe esa IP en el HTML.");
  Serial.println("=====================================");
}

void loop() {
  servoLoop();
  uint32_t ahora = millis();

  if (!streamActivo) {                        // nadie viendo el video: se vacía el buffer para evitar FB-OVF
    camera_fb_t* fb = esp_camera_fb_get();
    if (fb) esp_camera_fb_return(fb);
  }

  if (ahora - tFps >= 1000) {                 // FPS reales del stream
    fps = frames - framesPrev; framesPrev = frames; tFps = ahora;
  }
  if (finPulsoAceptar  && (int32_t)(ahora - finPulsoAceptar)  >= 0) { digitalWrite(PIN_ACEPTAR, LOW);  finPulsoAceptar  = 0; }
  if (finPulsoRechazar && (int32_t)(ahora - finPulsoRechazar) >= 0) { digitalWrite(PIN_RECHAZAR, LOW); finPulsoRechazar = 0; }

  if (WiFi.status() != WL_CONNECTED) {        // reconexión automática
    WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); delay(3000);
  }
  delay(5);
}
