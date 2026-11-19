// =====================================================
// EMG — Arduino UNO R4 WiFi
// Avisos en LCD 1602 I2C
// Datos: USB en vivo + resumen por USB + Google Sheets al terminar
// Protocolo: 30 x (5 s reposo + 5 s contraccion)  |  'P' = prueba corta de 10 s
// =====================================================
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFiS3.h>
#include <time.h>
#include "secrets.h"   // copia ../secrets.example.h aqui como secrets.h (no se sube a git)

// =====================================================
// WIFI / GOOGLE SHEETS
// =====================================================
const bool  USAR_WIFI   = false;   // false = solo USB (la PC manda a Sheets)
const char* WIFI_SSID   = SECRET_WIFI_SSID;
const char* WIFI_PASS   = SECRET_WIFI_PASS;
const char* SCRIPT_HOST = "script.google.com";
const char* SCRIPT_PATH = SECRET_SCRIPT_PATH;

// =====================================================
// PINES
// =====================================================
const int PIN_EMG   = A0;   // salida rectificada del EMG
const int PIN_BOTON = 2;    // boton opcional (D2 -> boton -> GND) para iniciar
// LCD 1602 I2C: SDA -> SDA, SCL -> SCL, VCC -> 5V, GND -> GND

// =====================================================
// LCD (se detecta sola en 0x27 o 0x3F)
// =====================================================
LiquidCrystal_I2C* lcd = nullptr;
bool    hayLcd = false;
uint8_t dirLcd = 0;

// =====================================================
// ADC
// =====================================================
const double VREF_MV  = 5000.0;   // referencia del R4 (5 V)
const double ADC_MAX  = 16383.0;  // 14 bits
const double GANANCIA = 1.0;      // 1 = mV reales en A0

// =====================================================
// PROCESAMIENTO
// =====================================================
const double   FC_ENVOLVENTE  = 6.0;
const int      VENTANA_RMS_MS = 200;
const uint32_t FS_HZ          = 2000;
const uint32_t PERIODO_US     = 1000000UL / FS_HZ;
const uint32_t DIEZMADO       = FS_HZ / 50;   // 50 lineas/s por USB

const int    MUESTRAS_RMS = (int)(FS_HZ * VENTANA_RMS_MS / 1000);
const int    MUESTRAS_MAV = 400;
const double PI_D   = 3.14159265358979323846;
const double RAIZ_2 = 1.41421356237309504880;

// =====================================================
// PROTOCOLO
// =====================================================
const int      NUM_CICLOS    = 30;   // 30 reposo + 30 contraccion
const int      TOTAL_FASES   = NUM_CICLOS * 2;
const uint32_t DURACION_FASE = 5000;   // ms

enum Estado { ESPERANDO, CUENTA_REGRESIVA, MIDIENDO, ENVIANDO };
Estado   estado        = ESPERANDO;
int      faseActual    = 0;      // par = reposo, impar = contraccion
uint32_t inicioFaseMs  = 0;
int      cuentaRegre   = 0;
int      ultimoSegundo = -1;
int      ciclosActual  = NUM_CICLOS;   // 'S' = 30 ciclos, 'P' = 1 ciclo (prueba de 10 s)

struct Resultado { float rms, mav, env; uint32_t ms; };   // ms = millis() al terminar la fase
Resultado resultados[TOTAL_FASES];
double promEnv[2] = {0, 0};

double   sumaRms = 0.0, sumaMav = 0.0, sumaEnv = 0.0;
uint32_t cuentaMuestras = 0;

uint32_t epochBase = 0, millisBase = 0;
WiFiSSLClient cliente;

// =====================================================
// FILTRO / RMS / MAV
// =====================================================
double b0, b1, b2, a1, a2;
double z1 = 0.0, z2 = 0.0;

float  bufferCuad[MUESTRAS_RMS];
int    indiceRms = 0, llenadoRms = 0;
double sumaCuad = 0.0;

float  bufferMav[MUESTRAS_MAV];
int    indiceMav = 0, llenadoMav = 0;
double sumaMavBuf = 0.0;

double   envActual = 0.0, rmsActual = 0.0, mavActual = 0.0;
uint32_t proximaMuestraUs = 0;
uint32_t contadorSerial   = 0;

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
  bufferMav[indiceMav] = (float)x;
  indiceMav = (indiceMav + 1) % MUESTRAS_MAV;
  if (llenadoMav < MUESTRAS_MAV) llenadoMav++;
  return sumaMavBuf / llenadoMav;
}

// =====================================================
// LCD
// =====================================================
bool existeI2C(uint8_t dir) {
  Wire.beginTransmission(dir);
  return Wire.endTransmission() == 0;
}

void buscarLcd() {
  Wire.begin();
  const uint8_t dirs[2] = { 0x27, 0x3F };
  for (int i = 0; i < 2 && !hayLcd; i++) {
    if (existeI2C(dirs[i])) {
      dirLcd = dirs[i];
      lcd = new LiquidCrystal_I2C(dirLcd, 16, 2);
      lcd->init();
      lcd->backlight();
      hayLcd = true;
    }
  }
}

void reportarI2C() {
  if (hayLcd) { Serial.print("# LCD OK en 0x"); Serial.println(dirLcd, HEX); }
  else        Serial.println("# LCD NO detectada: revisa SDA, SCL, VCC (5V) y GND");
  Serial.print("# WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "conectado" : "desconectado");
}

// Escribe una linea completa (rellena con espacios para no usar clear)
void lcdLinea(int fila, const char* txt) {
  if (!hayLcd) return;
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", txt);
  lcd->setCursor(0, fila);
  lcd->print(buf);
}

void lcdMostrar(const char* l1, const char* l2) {
  lcdLinea(0, l1);
  lcdLinea(1, l2);
}

void pantallaFase(int fase, int seg) {
  char l1[17], l2[17];
  if (fase % 2 == 0) snprintf(l1, sizeof(l1), "RELAJA     %2d/%d", fase / 2 + 1, ciclosActual);
  else               snprintf(l1, sizeof(l1), "CONTRAE!!  %2d/%d", fase / 2 + 1, ciclosActual);
  snprintf(l2, sizeof(l2), "Quedan: %d s", seg);
  lcdMostrar(l1, l2);
}

// =====================================================
// FECHA / HORA
// =====================================================
// Convierte millis() de una fase a hora real (si hay hora de internet)
uint32_t epochDe(uint32_t ms) {
  if (epochBase == 0) return 0;
  return epochBase - (millisBase - ms) / 1000;
}

void formatearFechaHora(uint32_t epoch, char* fecha, char* hora) {
  if (epoch == 0) {
    strcpy(fecha, "Sesion");
    unsigned long s = millis() / 1000;
    snprintf(hora, 9, "%02lu:%02lu:%02lu", (s / 3600) % 24, (s / 60) % 60, s % 60);
    return;
  }
  time_t t = (time_t)epoch - 6 * 3600;   // hora centro de Mexico
  struct tm* tmv = gmtime(&t);
  strftime(fecha, 11, "%Y-%m-%d", tmv);
  strftime(hora,  9,  "%H:%M:%S", tmv);
}

// =====================================================
// GOOGLE SHEETS (al terminar, para no frenar el muestreo)
// =====================================================
void conectarWiFi() {
  if (!USAR_WIFI) return;
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("# ERROR: modulo WiFi no responde");
    return;
  }
  Serial.print("# Conectando a: "); Serial.println(WIFI_SSID);
  lcdMostrar("Conectando WiFi", WIFI_SSID);
  for (int i = 0; i < 2 && WiFi.status() != WL_CONNECTED; i++) {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    delay(3000);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("# WiFi conectado, IP: "); Serial.println(WiFi.localIP());
    for (int i = 0; i < 10 && epochBase == 0; i++) {
      epochBase = WiFi.getTime();
      if (epochBase == 0) delay(1000);
    }
    millisBase = millis();
  } else {
    Serial.println("# WiFi FALLO — los datos solo saldran por USB");
  }
}

bool enviarFila(const char* etiqueta, const Resultado& r) {
  if (WiFi.status() != WL_CONNECTED) return false;
  char fecha[11], hora[9];
  formatearFechaHora(epochDe(r.ms), fecha, hora);

  String ruta = String(SCRIPT_PATH) +
                "?fecha=" + fecha + "&hora=" + hora + "&rep=" + etiqueta +
                "&rms=" + String(r.rms, 4) +
                "&mav=" + String(r.mav, 4) +
                "&env=" + String(r.env, 4);

  if (!cliente.connect(SCRIPT_HOST, 443)) return false;
  cliente.print("GET "); cliente.print(ruta); cliente.println(" HTTP/1.1");
  cliente.print("Host: "); cliente.println(SCRIPT_HOST);
  cliente.println("Connection: close");
  cliente.println();

  bool ok = false;
  uint32_t t0 = millis();
  while (cliente.connected() && millis() - t0 < 8000) {
    if (cliente.available()) {
      String linea = cliente.readStringUntil('\n');
      ok = (linea.indexOf(" 302") > 0 || linea.indexOf(" 200") > 0);
      break;
    }
  }
  cliente.stop();
  return ok;
}

void enviarTodoASheets() {
  if (!USAR_WIFI) return;
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("# Sin WiFi: no se envio a Sheets (los datos estan arriba por USB)");
    return;
  }
  Serial.println("# Enviando 30 filas a Google Sheets...");
  int okTotal = 0;
  for (int i = 0; i < TOTAL_FASES; i++) {
    char etiqueta[20], prog[17];
    snprintf(etiqueta, sizeof(etiqueta), "%s_%d",
             (i % 2 == 0) ? "Reposo" : "Contraccion", i / 2 + 1);
    snprintf(prog, sizeof(prog), "Fila %d/%d", i + 1, TOTAL_FASES);
    lcdMostrar("Enviando Sheets", prog);

    bool ok = enviarFila(etiqueta, resultados[i]);
    if (!ok) { delay(500); ok = enviarFila(etiqueta, resultados[i]); }
    if (ok) okTotal++;
    Serial.print("# "); Serial.print(etiqueta);
    Serial.println(ok ? " -> Sheets OK" : " -> Sheets ERROR");
  }
  Serial.print("# Enviadas a Sheets: "); Serial.print(okTotal);
  Serial.print("/"); Serial.println(TOTAL_FASES);
}

// =====================================================
// PROTOCOLO
// =====================================================
void reiniciarAcumuladores() {
  sumaRms = sumaMav = sumaEnv = 0.0;
  cuentaMuestras = 0;
}

void iniciarFase(int fase) {
  ultimoSegundo = 5;
  pantallaFase(fase, 5);
  Serial.print(fase % 2 == 0 ? "# >>> REPOSO " : "# >>> CONTRACCION ");
  Serial.println(fase / 2 + 1);
}

void imprimirResumen() {
  double promR[2] = {0, 0}, promM[2] = {0, 0};
  promEnv[0] = promEnv[1] = 0;

  Serial.println();
  Serial.println("===== RESUMEN (copiar a Excel) =====");
  Serial.println("Fase,Numero,RMS_mV,MAV_mV,ENV_mV");
  for (int i = 0; i < ciclosActual * 2; i++) {
    const int tipo = i % 2;
    Serial.print(tipo == 0 ? "Reposo," : "Contraccion,");
    Serial.print(i / 2 + 1);            Serial.print(",");
    Serial.print(resultados[i].rms, 4); Serial.print(",");
    Serial.print(resultados[i].mav, 4); Serial.print(",");
    Serial.println(resultados[i].env, 4);
    promR[tipo]   += resultados[i].rms;
    promM[tipo]   += resultados[i].mav;
    promEnv[tipo] += resultados[i].env;
  }
  Serial.println();
  Serial.println("Promedio,Fase,RMS_mV,MAV_mV,ENV_mV");
  for (int tipo = 0; tipo < 2; tipo++) {
    promEnv[tipo] /= ciclosActual;
    Serial.print("Promedio,");
    Serial.print(tipo == 0 ? "Reposo," : "Contraccion,");
    Serial.print(promR[tipo] / ciclosActual, 4); Serial.print(",");
    Serial.print(promM[tipo] / ciclosActual, 4); Serial.print(",");
    Serial.println(promEnv[tipo], 4);
  }
  Serial.println("====================================");
  Serial.println();
}

void manejarProtocolo() {
  const uint32_t ahora = millis();

  if (estado == ESPERANDO) {
    bool pedir = (digitalRead(PIN_BOTON) == LOW);
    while (Serial.available()) {
      char c = Serial.read();
      if (c == 'S' || c == 's') { pedir = true; ciclosActual = NUM_CICLOS; }
      if (c == 'P' || c == 'p') { pedir = true; ciclosActual = 1; }   // prueba de 10 s
      if (c == 'I' || c == 'i') reportarI2C();   // diagnostico
    }
    if (pedir) {
      estado       = CUENTA_REGRESIVA;
      cuentaRegre  = 0;
      inicioFaseMs = ahora;
      Serial.println("# Prueba iniciada: cuenta regresiva 3 s");
    }
    return;
  }

  if (estado == CUENTA_REGRESIVA) {
    if (ahora - inicioFaseMs >= (uint32_t)cuentaRegre * 1000) {
      if (cuentaRegre < 3) {
        char l2[17];
        snprintf(l2, sizeof(l2), "       %d", 3 - cuentaRegre);
        lcdMostrar("Preparate...", l2);
        Serial.print("# "); Serial.print(3 - cuentaRegre); Serial.println("...");
        cuentaRegre++;
      } else {
        estado       = MIDIENDO;
        faseActual   = 0;
        inicioFaseMs = ahora;
        reiniciarAcumuladores();
        iniciarFase(faseActual);
      }
    }
    return;
  }

  if (estado != MIDIENDO) return;

  const uint32_t transcurrido = ahora - inicioFaseMs;
  if (transcurrido < DURACION_FASE) {
    int seg = 5 - (int)(transcurrido / 1000);
    if (seg != ultimoSegundo) {
      ultimoSegundo = seg;
      pantallaFase(faseActual, seg);
    }
    return;
  }

  Resultado& r = resultados[faseActual];
  if (cuentaMuestras > 0) {
    r.rms = sumaRms / cuentaMuestras;
    r.mav = sumaMav / cuentaMuestras;
    r.env = sumaEnv / cuentaMuestras;
  } else {
    r.rms = r.mav = r.env = 0;
  }
  r.ms = ahora;

  faseActual++;
  inicioFaseMs += DURACION_FASE;
  reiniciarAcumuladores();

  if (faseActual >= ciclosActual * 2) {
    Serial.println("# === PRUEBA TERMINADA ===");
    imprimirResumen();
    estado = ENVIANDO;
  } else {
    iniciarFase(faseActual);
  }
}

// =====================================================
// SETUP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(1500);

  pinMode(PIN_BOTON, INPUT_PULLUP);
  analogReadResolution(14);

  buscarLcd();
  lcdMostrar("EMG", "Iniciando...");

  configurarFiltro(FC_ENVOLVENTE, FS_HZ);
  for (int i = 0; i < MUESTRAS_RMS; i++) bufferCuad[i] = 0.0f;
  for (int i = 0; i < MUESTRAS_MAV; i++) bufferMav[i]  = 0.0f;

  Serial.println("# ================================");
  Serial.println("#  EMG (UNO R4 WiFi)");
  Serial.println("# ================================");

  reportarI2C();   // el WiFi se conecta al final, solo para enviar a Sheets

  Serial.println("# Listo. Envia 'S' para iniciar, 'I' para diagnostico.");
  lcdMostrar("EMG LISTO", "Envia S por USB");
  proximaMuestraUs = micros();
}

// =====================================================
// LOOP — muestreo a 2 kHz
// =====================================================
void loop() {
  if (estado == ENVIANDO) {
    enviarTodoASheets();   // ~1 min, el muestreo se pausa aqui
    String l1 = "Rep ENV:" + String(promEnv[0], 1) + "mV";
    String l2 = "Con ENV:" + String(promEnv[1], 1) + "mV";
    lcdMostrar(l1.c_str(), l2.c_str());
    estado = ESPERANDO;
    proximaMuestraUs = micros();
    Serial.println("# Listo para otra prueba. Envia 'S'.");
    return;
  }

  const uint32_t ahora = micros();
  if ((int32_t)(ahora - proximaMuestraUs) >= 0) {
    proximaMuestraUs += PERIODO_US;

    const double adcMv = (double)analogRead(PIN_EMG) * VREF_MV / ADC_MAX;
    const double xMv   = adcMv / GANANCIA;

    envActual = filtrarEnvolvente(xMv);
    if (envActual < 0.0) envActual = 0.0;
    rmsActual = actualizarRms(xMv);
    mavActual = actualizarMav(fabs(xMv));

    if (estado == MIDIENDO) {
      sumaRms += rmsActual;
      sumaMav += mavActual;
      sumaEnv += envActual;
      cuentaMuestras++;
    }

    // Salida USB en vivo (compatible con Serial Plotter)
    if (++contadorSerial >= DIEZMADO) {
      contadorSerial = 0;
      const int marca = (estado == MIDIENDO && (faseActual % 2 == 1)) ? 500 : 0;
      Serial.print("Raw_mV:");       Serial.print(xMv, 1);
      Serial.print(",Env_mV:");      Serial.print(envActual, 2);
      Serial.print(",RMS_mV:");      Serial.print(rmsActual, 2);
      Serial.print(",MAV_mV:");      Serial.print(mavActual, 2);
      Serial.print(",Contraccion:"); Serial.println(marca);
    }
  }

  manejarProtocolo();
}
