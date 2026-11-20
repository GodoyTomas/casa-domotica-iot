#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <SoftwareSerial.h>

extern "C" {
  #include "user_interface.h" // CPU a 160 MHz (acelera SSL de Telegram x2)
}

// =====================================================
//  CASA DOMOTICA - ESP8266 (FINAL)
// =====================================================

const char* WIFI_SSID = "UGD";
const char* WIFI_PASSWORD = "launiversidad2024";
#define BOT_TOKEN "8722537482:AAE4BSbGco2eIXamId4LrJzojEeaC3xpW2E"
const char* CHAT_ID_AUTORIZADO = "8488703972";

// Pines intactos: RX = D6, TX = D5
SoftwareSerial enlaceUNO(D6, D5);
WiFiClientSecure clienteSeguro;
UniversalTelegramBot bot(BOT_TOKEN, clienteSeguro);

const size_t MAX_LINEA_UNO = 160;
char bufferUNO[MAX_LINEA_UNO];
size_t indiceUNO = 0;

String estadoActual = "";
bool estadoFrescoRecibido = false;

// Cola de eventos criticos
bool evFuegoDetectado = false;
bool evFuegoDespejado = false;
bool evAccesoConcedido = false;
bool evAccesoDenegado = false;
bool evIntruso = false;

unsigned long ultimoChequeoTelegram = 0;
unsigned long ultimoChequeoWiFi = 0;
const unsigned long INTERVALO_TELEGRAM = 800;
const unsigned long INTERVALO_WIFI = 5000;

// =====================================================
// COMUNICACION CON ARDUINO UNO
// =====================================================

void enviarUNO(const char* comando) {
  enlaceUNO.println(comando);
}

void procesarLineaUNO(const char* linea) {
  if (linea[0] == '\0') return;

  if (strncmp(linea, "STATE:", 6) == 0) {
    estadoActual = String(linea);
    estadoFrescoRecibido = true;
  }
  else if (strncmp(linea, "EVENT:", 6) == 0) {
    const char* ev = linea + 6;
    if (strcmp(ev, "FIRE") == 0) {
      evFuegoDetectado = true;
    } else if (strcmp(ev, "FIRE_CLEAR") == 0) {
      evFuegoDespejado = true;
    } else if (strcmp(ev, "ACCESS_GRANTED") == 0) {
      evAccesoConcedido = true;
    } else if (strcmp(ev, "ACCESS_DENIED") == 0) {
      evAccesoDenegado = true;
    } else if (strcmp(ev, "INTRUDER") == 0) {
      evIntruso = true;
    }
  }
}

void recibirUNO() {
  while (enlaceUNO.available()) {
    char c = (char)enlaceUNO.read();

    if (c == '\n') {
      bufferUNO[indiceUNO] = '\0';
      while (indiceUNO > 0 && (bufferUNO[indiceUNO - 1] == '\r' || bufferUNO[indiceUNO - 1] == ' ')) {
        bufferUNO[--indiceUNO] = '\0';
      }
      if (indiceUNO > 0) {
        procesarLineaUNO(bufferUNO);
      }
      indiceUNO = 0;
    }
    else if (c != '\r' && indiceUNO < (MAX_LINEA_UNO - 1)) {
      bufferUNO[indiceUNO++] = c;
    }
  }
}

void despacharEventosPendientes() {
  if (WiFi.status() != WL_CONNECTED) return;

  if (evFuegoDetectado) {
    evFuegoDetectado = false;
    bot.sendMessage(CHAT_ID_AUTORIZADO, "🔥 ¡ALERTA MÁXIMA! Fuego detectado.", "");
    recibirUNO();
  }
  if (evFuegoDespejado) {
    evFuegoDespejado = false;
    bot.sendMessage(CHAT_ID_AUTORIZADO, "✅ Alerta de fuego cancelada.", "");
    recibirUNO();
  }
  if (evAccesoConcedido) {
    evAccesoConcedido = false;
    bot.sendMessage(CHAT_ID_AUTORIZADO, "🔓 Tarjeta autorizada. Abriendo puerta.", "");
    recibirUNO();
  }
  if (evAccesoDenegado) {
    evAccesoDenegado = false;
    bot.sendMessage(CHAT_ID_AUTORIZADO, "🚨 Intento de acceso denegado (RFID).", "");
    recibirUNO();
  }
  if (evIntruso) {
    evIntruso = false;
    bot.sendMessage(CHAT_ID_AUTORIZADO, "🚨 ¡INTRUSO DETECTADO! Algo se acercó demasiado con el modo seguridad activo. Usa 🔕 Silenciar Alarma para apagar la sirena.", "");
    recibirUNO();
  }
}

// =====================================================
// PARSEO DE DATOS Y REPORTE TELEGRAM
// =====================================================

String obtenerDato(const String &texto, const char* nombre) {
  String busqueda = String(nombre) + "=";
  int inicio = texto.indexOf(":" + busqueda);
  if (inicio < 0) inicio = texto.indexOf(";" + busqueda);
  if (inicio < 0) return "";
  inicio += busqueda.length() + 1;
  int fin = texto.indexOf(';', inicio);
  if (fin < 0) fin = texto.length();
  return texto.substring(inicio, fin);
}

String mostrarDato(const String &dato, const char* unidad) {
  if (dato == "" || dato == "NA") return "Sin dato";
  return dato + unidad;
}

void enviarEstadoTelegram(const String &chatId) {
  if (estadoActual.length() == 0) {
    bot.sendMessage(chatId, "⏳ Aguardando primera lectura del Arduino. Intenta en 2 segundos.", "");
    return;
  }

  String mensaje;
  mensaje.reserve(340);
  mensaje = "🏠 ESTADO SMART HOME\n\n";
  mensaje += "🌡 Temperatura: " + mostrarDato(obtenerDato(estadoActual, "T"), " °C");
  mensaje += "\n💧 Humedad: " + mostrarDato(obtenerDato(estadoActual, "H"), " %");

  String distancia = obtenerDato(estadoActual, "D");
  if (distancia == "" || distancia.toFloat() < 0) mensaje += "\n📏 Distancia: Sin lectura";
  else mensaje += "\n📏 Distancia: " + distancia + " cm";

  mensaje += "\n☀️ Luz (LDR): " + mostrarDato(obtenerDato(estadoActual, "L"), "");
  mensaje += "\n💡 Luz casa: " + String(obtenerDato(estadoActual, "LIGHT") == "1" ? "Encendida" : "Apagada");
  mensaje += "\n🔥 Llama: " + String(obtenerDato(estadoActual, "F") == "1" ? "🚨 ALERTA" : "Normal");
  mensaje += "\n🚶 Presencia: " + String(obtenerDato(estadoActual, "M") == "1" ? "Detectada" : "No detectada");
  mensaje += "\n🚪 Puerta: " + String(obtenerDato(estadoActual, "DOOR") == "1" ? "Abierta" : "Cerrada");
  mensaje += "\n🛡 Seguridad: " + String(obtenerDato(estadoActual, "SEC") == "1" ? "Activada" : "Desactivada");

  bot.sendMessage(chatId, mensaje, "");
}

// =====================================================
// INTERFAZ DE COMANDOS TELEGRAM
// =====================================================

void enviarTecladoPrincipal(const String &chatId, const String &encabezado) {
  String teclado =
    "[[\"📊 Estado\", \"🚪 Puerta\"],"
    "[\"💡 Luz ON\", \"🌑 Luz OFF\"],"
    "[\"🛡 Seguridad ON\", \"🛑 Seguridad OFF\"],"
    "[\"🔕 Silenciar Alarma\"]]";
  bot.sendMessageWithReplyKeyboard(chatId, encabezado, "", teclado, true);
}

void solicitarEstadoFresco() {
  estadoFrescoRecibido = false;
  enviarUNO("CMD:STATUS");

  unsigned long tInicio = millis();
  while (millis() - tInicio < 400) {
    recibirUNO();
    if (estadoFrescoRecibido) break;
    yield();
  }
}

void responderComando(const String &chatId, String texto) {
  texto.trim();

  if (texto == "/start" || texto == "/menu") {
    enviarTecladoPrincipal(chatId, "🏠 PANEL DE CONTROL SMART HOME\nSelecciona una acción:");
  }
  else if (texto == "📊 Estado" || texto == "/estado") {
    solicitarEstadoFresco();
    enviarEstadoTelegram(chatId);
  }
  else if (texto == "🚪 Puerta" || texto == "/puerta") {
    enviarUNO("CMD:DOOR_OPEN");
    bot.sendMessage(chatId, "🚪 Abriendo puerta...", "");
  }
  else if (texto == "💡 Luz ON" || texto == "/luz_on") {
    enviarUNO("CMD:LIGHT_ON");
    bot.sendMessage(chatId, "💡 Luz blanca ENCENDIDA.", "");
  }
  else if (texto == "🌑 Luz OFF" || texto == "/luz_off") {
    enviarUNO("CMD:LIGHT_OFF");
    bot.sendMessage(chatId, "🌑 Luz blanca APAGADA.", "");
  }
  else if (texto == "🛡 Seguridad ON" || texto == "/seguridad_on") {
    enviarUNO("CMD:SECURITY_ON");
    bot.sendMessage(chatId, "🛡 Modo seguridad ACTIVADO. Vigilando con el sensor ultrasónico.", "");
  }
  else if (texto == "🛑 Seguridad OFF" || texto == "/seguridad_off") {
    enviarUNO("CMD:SECURITY_OFF");
    bot.sendMessage(chatId, "🛑 Modo seguridad DESACTIVADO.", "");
  }
  else if (texto == "🔕 Silenciar Alarma" || texto == "/silenciar") {
    enviarUNO("CMD:ALARM_OFF");
    bot.sendMessage(chatId, "🔕 Buzzer silenciado.", "");
  }
  else {
    enviarTecladoPrincipal(chatId, "Usa los botones del panel de control:");
  }
}

void consultarTelegram() {
  if (millis() - ultimoChequeoTelegram < INTERVALO_TELEGRAM) return;
  ultimoChequeoTelegram = millis();

  int cantidad = bot.getUpdates(bot.last_message_received + 1);

  while (cantidad > 0) {
    for (int i = 0; i < cantidad; i++) {
      recibirUNO();
      String id_remitente = bot.messages[i].chat_id;
      String texto = bot.messages[i].text;

      if (id_remitente == CHAT_ID_AUTORIZADO) {
        responderComando(id_remitente, texto);
      }
    }
    cantidad = bot.getUpdates(bot.last_message_received + 1);
  }
}

// =====================================================
// SETUP Y LOOP
// =====================================================

void verificarWiFi() {
  if (millis() - ultimoChequeoWiFi < INTERVALO_WIFI) return;
  ultimoChequeoWiFi = millis();

  if (WiFi.status() != WL_CONNECTED) {
    WiFi.reconnect();
  }
}

void setup() {
  system_update_cpu_freq(160);

  enlaceUNO.begin(9600, SWSERIAL_8N1, D6, D5, false, 256);
  enlaceUNO.enableIntTx(false);

  WiFi.mode(WIFI_STA);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    recibirUNO();
  }

  clienteSeguro.setInsecure();
  clienteSeguro.setBufferSizes(2048, 1024);
  bot.waitForResponse = 600;

  enviarUNO("CMD:STATUS");
}

void loop() {
  recibirUNO();
  despacharEventosPendientes();
  verificarWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    consultarTelegram();
  }
  yield();
}