#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include "secrets.h"   // copia ../secrets.example.h aqui como secrets.h (no se sube a git)

// =====================================================
// WIFI / GOOGLE SHEETS
// =====================================================
const char* WIFI_SSID  = SECRET_WIFI_SSID;
const char* WIFI_PASS  = SECRET_WIFI_PASS;
const String SCRIPT_URL = String("https://script.google.com") + SECRET_SCRIPT_PATH;

// =====================================================
// PINES
// =====================================================
const int PIN_EMG    = 32;   // ADC1 — funciona con WiFi activo
const int PIN_BUZZER = 21;   // beeps de aviso
const int PIN_INICIO = 0;    // boton BOOT de la placa inicia la prueba

// =====================================================
// PROTOCOLO: 15 x (5 s reposo + 5 s contraccion)
// =====================================================
const int      NUM_CICLOS     = 15;
const uint32_t DURACION_FASE  = 5000;   // ms
const int      TOTAL_FASES    = NUM_CICLOS * 2;

enum Estado { ESPERANDO, CUENTA_REGRESIVA, MIDIENDO };
Estado   estado        = ESPERANDO;
int      faseActual    = 0;            // par = reposo, impar = contraccion
uint32_t inicioFaseMs  = 0;
int      cuentaBeeps   = 0;

// =====================================================
// EMG
// =====================================================
const double GANANCIA       = 1.0;    // valores en mV reales en el pin
const double FC_ENVOLVENTE  = 6.0;
const int    VENTANA_RMS_MS = 200;

const uint32_t FS_HZ      = 2000;
const uint32_t PERIODO_US = 1000000UL / FS_HZ;
const uint32_t DIEZMADO   = FS_HZ / 5;

const int    MUESTRAS_RMS = (int)(FS_HZ * VENTANA_RMS_MS / 1000);
const int    MUESTRAS_MAV = 400;
const double PI_D   = 3.14159265358979323846;
const double RAIZ_2 = 1.41421356237309504880;

// Acumuladores de la fase actual
double   sumaRms = 0.0, sumaMav = 0.0, sumaEnv = 0.0;
uint32_t cuentaMuestras = 0;

struct Paquete { char etiqueta[20]; float rms, mav, env; };
QueueHandle_t colaSheets;

// Filtro
double b0, b1, b2, a1, a2;
double z1 = 0.0, z2 = 0.0;

// RMS
float  bufferCuad[MUESTRAS_RMS];
int    indiceRms = 0, llenadoRms = 0;
double sumaCuad = 0.0;

// MAV
double bufferMav[MUESTRAS_MAV];
int    indiceMav = 0, llenadoMav = 0;
double sumaMavBuf = 0.0;

// Señal
double   envActual = 0.0, rmsActual = 0.0, mavActual = 0.0;
uint32_t proximaMuestraUs = 0;
uint32_t contadorPlotter  = 0;

// =====================================================
// FILTRO BUTTERWORTH 2º orden pasa-bajas
// =====================================================
void configurarFiltro(double fc, double fs) {
  const double K    = tan(PI_D * fc / fs);
  const double Q    = 1.0 / RAIZ_2;
  const double norm = 1.0 / (1.0 + K / Q + K * K);
  b0 = K * K * norm;
  b1 = 2.0 * b0;
  b2 = b0;
  a1 = 2.0 * (K * K - 1.0) * norm;
  a2 = (1.0 - K / Q + K * K) * norm;
}

double filtrarEnvolvente(double x) {
  const double y = b0 * x + z1;
  z1 = b1 * x - a1 * y + z2;
  z2 = b2 * x - a2 * y;
  return y;
}

double actualizarRms(double x) {
  const float cuadrado = (float)(x * x);
  sumaCuad += cuadrado - bufferCuad[indiceRms];
  if (sumaCuad < 0.0) sumaCuad = 0.0;
  bufferCuad[indiceRms] = cuadrado;
  indiceRms = (indiceRms + 1) % MUESTRAS_RMS;
  if (llenadoRms < MUESTRAS_RMS) llenadoRms++;
  return sqrt(sumaCuad / llenadoRms);
}

double actualizarMav(double x) {
  sumaMavBuf += x - bufferMav[indiceMav];
  if (sumaMavBuf < 0.0) sumaMavBuf = 0.0;
  bufferMav[indiceMav] = x;
  indiceMav = (indiceMav + 1) % MUESTRAS_MAV;
  if (llenadoMav < MUESTRAS_MAV) llenadoMav++;
  return sumaMavBuf / llenadoMav;
}

// =====================================================
// ENVIAR A GOOGLE SHEETS (corre en el núcleo 0)
// =====================================================
void enviarASheets(const Paquete& p) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("# Sin WiFi, reintentando conexion");
    WiFi.reconnect();
    return;
  }

  char fecha[11] = "Sesion";
  char hora[9]   = "--:--:--";
  struct tm t;
  if (getLocalTime(&t, 100)) {
    strftime(fecha, sizeof(fecha), "%Y-%m-%d", &t);
    strftime(hora,  sizeof(hora),  "%H:%M:%S", &t);
  }

  char url[380];
  snprintf(url, sizeof(url),
    "%s?fecha=%s&hora=%s&rep=%s&rms=%.4f&mav=%.4f&env=%.4f",
    SCRIPT_URL.c_str(), fecha, hora, p.etiqueta, p.rms, p.mav, p.env);

  WiFiClientSecure cliente;
  cliente.setInsecure();
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(8000);

  if (!http.begin(cliente, url)) {
    Serial.println("# Error iniciando HTTP");
    return;
  }
  int codigo = http.GET();
  Serial.printf("# %s -> Sheets (HTTP %d) rms=%.4f mav=%.4f env=%.4f\n",
                p.etiqueta, codigo, p.rms, p.mav, p.env);
  http.end();
}

void tareaSheets(void*) {
  Paquete p;
  for (;;) {
    if (xQueueReceive(colaSheets, &p, portMAX_DELAY) == pdTRUE) {
      enviarASheets(p);
    }
  }
}

// =====================================================
// PROTOCOLO
// =====================================================
void reiniciarAcumuladores() {
  sumaRms = sumaMav = sumaEnv = 0.0;
  cuentaMuestras = 0;
}

void avisoFase(int fase) {
  if (fase % 2 == 0) {
    tone(PIN_BUZZER, 800, 300);   // grave = RELAJA
    Serial.printf("# >>> FASE %d: REPOSO %d\n", fase + 1, fase / 2 + 1);
  } else {
    tone(PIN_BUZZER, 2000, 300);  // agudo = CONTRAE
    Serial.printf("# >>> FASE %d: CONTRACCION %d\n", fase + 1, fase / 2 + 1);
  }
}

void iniciarPrueba() {
  estado       = CUENTA_REGRESIVA;
  cuentaBeeps  = 0;
  inicioFaseMs = millis();
  Serial.println("# Prueba iniciada: cuenta regresiva 3 s");
}

void manejarProtocolo() {
  const uint32_t ahora = millis();

  // Iniciar con boton BOOT o con 'S' por Serial
  if (estado == ESPERANDO) {
    bool pedir = (digitalRead(PIN_INICIO) == LOW);
    while (Serial.available()) {
      char c = Serial.read();
      if (c == 'S' || c == 's') pedir = true;
    }
    if (pedir) iniciarPrueba();
    return;
  }

  if (estado == CUENTA_REGRESIVA) {
    if (ahora - inicioFaseMs >= (uint32_t)cuentaBeeps * 1000) {
      if (cuentaBeeps < 3) {
        tone(PIN_BUZZER, 1200, 100);
        Serial.printf("# %d...\n", 3 - cuentaBeeps);
        cuentaBeeps++;
      } else {
        estado       = MIDIENDO;
        faseActual   = 0;
        inicioFaseMs = ahora;
        reiniciarAcumuladores();
        avisoFase(faseActual);
      }
    }
    return;
  }

  // MIDIENDO
  if (ahora - inicioFaseMs >= DURACION_FASE) {
    Paquete p;
    snprintf(p.etiqueta, sizeof(p.etiqueta), "%s_%d",
             (faseActual % 2 == 0) ? "Reposo" : "Contraccion", faseActual / 2 + 1);
    if (cuentaMuestras > 0) {
      p.rms = sumaRms / cuentaMuestras;
      p.mav = sumaMav / cuentaMuestras;
      p.env = sumaEnv / cuentaMuestras;
    } else {
      p.rms = p.mav = p.env = 0;
    }
    xQueueSend(colaSheets, &p, 0);

    faseActual++;
    inicioFaseMs += DURACION_FASE;
    reiniciarAcumuladores();

    if (faseActual >= TOTAL_FASES) {
      estado = ESPERANDO;
      tone(PIN_BUZZER, 1500, 800);
      Serial.println("# === PRUEBA TERMINADA ===");
    } else {
      avisoFase(faseActual);
    }
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_INICIO, INPUT_PULLUP);
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_EMG, ADC_0db);   // rango sensible ~0–950 mV

  configurarFiltro(FC_ENVOLVENTE, FS_HZ);
  for (int i = 0; i < MUESTRAS_RMS; i++) bufferCuad[i] = 0.0f;
  for (int i = 0; i < MUESTRAS_MAV; i++) bufferMav[i]  = 0.0;

  Serial.println("================================");
  Serial.println(" EMG + Google Sheets (ESP32)");
  Serial.println("================================");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int intentos = 0;
  while (WiFi.status() != WL_CONNECTED && intentos < 20) {
    delay(500);
    Serial.print(".");
    intentos++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\n# WiFi conectado, IP: ");
    Serial.println(WiFi.localIP());
    configTime(-6 * 3600, 0, "pool.ntp.org", "time.google.com");
  } else {
    Serial.println("\n# WiFi FALLO — continua sin internet");
  }

  colaSheets = xQueueCreate(40, sizeof(Paquete));
  xTaskCreatePinnedToCore(tareaSheets, "sheets", 8192, NULL, 1, NULL, 0);

  proximaMuestraUs = micros();
  Serial.println("# Listo. Presiona BOOT (o envia 'S') para iniciar la prueba.");
}

// =====================================================
// LOOP — muestreo a 2 kHz (núcleo 1)
// =====================================================
void loop() {
  const uint32_t ahora = micros();

  if ((int32_t)(ahora - proximaMuestraUs) >= 0) {
    proximaMuestraUs += PERIODO_US;

    const double adcMv  = (double)analogReadMilliVolts(PIN_EMG);
    const double rectMv = adcMv / GANANCIA;

    envActual = filtrarEnvolvente(rectMv);
    if (envActual < 0.0) envActual = 0.0;
    rmsActual = actualizarRms(rectMv);
    mavActual = actualizarMav(fabs(rectMv));

    if (estado == MIDIENDO) {
      sumaRms += rmsActual;
      sumaMav += mavActual;
      sumaEnv += envActual;
      cuentaMuestras++;
    }

    if (++contadorPlotter >= DIEZMADO) {
      contadorPlotter = 0;
      Serial.print("Env_mV:");  Serial.print(envActual, 4);
      Serial.print(",RMS_mV:"); Serial.print(rmsActual, 4);
      Serial.print(",MAV_mV:"); Serial.println(mavActual, 4);
    }
  }

  manejarProtocolo();
}
