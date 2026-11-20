#include <Wire.h>
#include <SPI.h>
#include <SoftwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <MFRC522.h>
#include <Servo.h>

// =====================================================
//  CASA DOMOTICA - ARDUINO UNO R3 (FINAL)
// =====================================================

// ---------- PINES (INTACTOS) ----------
const byte PIN_DHT = 2;
const byte PIN_SERVO = 3;
const byte PIN_ESP_RX = 4;
const byte PIN_ESP_TX = 5;
const byte PIN_RFID_RST = 6;
const byte PIN_595_DATA = 7;
const byte PIN_595_CLOCK = 8;
const byte PIN_595_LATCH = 9;
const byte PIN_RFID_SS = 10;
const byte PIN_TRIG = A0;
const byte PIN_ECHO = A1;
const byte PIN_LDR = A2;
const byte PIN_LLAMA = A3;

// ---------- BITS DEL 74HC595 ----------
const byte BIT_RGB_R = 0;      // Pin 15 (QA)
const byte BIT_RGB_G = 1;      // Pin 1  (QB)
const byte BIT_RGB_B = 2;      // Pin 2  (QC)
const byte BIT_BUZZER = 5;     // Pin 5  (QF)
const byte BIT_LED_ROJO = 6;   // Pin 6  (QG)
const byte BIT_LED_VERDE = 7;  // Pin 7  (QH)

// Cambiar a true si tu LED RGB es de Anodo Comun (pata larga a 5V)
const bool RGB_ANODO_COMUN = false;

// ---------- OBJETOS ----------
SoftwareSerial enlaceESP(PIN_ESP_RX, PIN_ESP_TX);
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(PIN_DHT, DHT11);
MFRC522 rfid(PIN_RFID_SS, PIN_RFID_RST);
Servo servoPuerta;

// ---------- CONFIGURACION ----------
const float DISTANCIA_PRESENCIA_CM = 80.0;
const unsigned long INTERVALO_ULTRASONICO = 150;
const unsigned long INTERVALO_DHT = 2500;
const unsigned long INTERVALO_LCD = 1000;
const unsigned long INTERVALO_RFID_CHECK = 3000;

const byte ANGULO_CERRADO = 0;
const byte ANGULO_ABIERTO = 90;

const byte CANTIDAD_TARJETAS = 1;
const byte TAM_UID = 4;
const byte UIDS_PERMITIDOS[CANTIDAD_TARJETAS][TAM_UID] = {
  {0xF0, 0xFE, 0xEA, 0x55}
};

// ---------- VARIABLES DE ESTADO ----------
byte salidas = 0x00;
byte salidasPrevias = 0xFF;

float temperatura = NAN;
float humedad = NAN;
float distanciaCM = -1;
int valorLuz = 0;

bool luzCasaEncendida = true; // Arranca encendida en blanco continuo
bool llama = false;
bool presencia = false;
bool puertaAbierta = false;
bool faseBuzzer = false;
bool modoSeguridad = false;
bool intrusoDetectado = false;
bool alarmaSilenciada = false;

unsigned long tUltrasonico = 0;
unsigned long tDHT = 0;
unsigned long tLCD = 0;
unsigned long tBuzzer = 0;
unsigned long tRfidCheck = 0;

const byte MAX_BUFFER_ESP = 64;
char bufferESP[MAX_BUFFER_ESP];
byte indiceESP = 0;

// ---------- REGISTRO 74HC595 ----------
void actualizar595(bool forzar = false) {
  if (!forzar && salidas == salidasPrevias) return;
  salidasPrevias = salidas;

  digitalWrite(PIN_595_LATCH, LOW);
  shiftOut(PIN_595_DATA, PIN_595_CLOCK, MSBFIRST, salidas);
  digitalWrite(PIN_595_LATCH, HIGH);
}

void ponerBit(byte bit, bool encendido) {
  bitWrite(salidas, bit, encendido ? 1 : 0);
  actualizar595();
}

void actualizarLuzBlanca() {
  bool estadoBit = RGB_ANODO_COMUN ? !luzCasaEncendida : luzCasaEncendida;
  bitWrite(salidas, BIT_RGB_R, estadoBit ? 1 : 0);
  bitWrite(salidas, BIT_RGB_G, estadoBit ? 1 : 0);
  bitWrite(salidas, BIT_RGB_B, estadoBit ? 1 : 0);
  actualizar595();
}

void enviarESP_P(const __FlashStringHelper *mensaje) {
  enlaceESP.println(mensaje);
}

// ---------- PUERTA (SERVOMOTOR) ----------
void ejecutarAperturaSegura() {
  puertaAbierta = true;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("Acceso concedido"));

  servoPuerta.write(ANGULO_ABIERTO);
  delay(3000);
  servoPuerta.write(ANGULO_CERRADO);

  puertaAbierta = false;
}

// ---------- ESTADO PARA ESP8266 ----------
void enviarEstado() {
  enlaceESP.print(F("STATE:T="));
  if (isnan(temperatura)) enlaceESP.print(F("NA"));
  else enlaceESP.print(temperatura, 1);

  enlaceESP.print(F(";H="));
  if (isnan(humedad)) enlaceESP.print(F("NA"));
  else enlaceESP.print(humedad, 1);

  enlaceESP.print(F(";D="));
  enlaceESP.print(distanciaCM, 0);

  enlaceESP.print(F(";L="));
  enlaceESP.print(valorLuz);

  enlaceESP.print(F(";F="));
  enlaceESP.print(llama ? 1 : 0);

  enlaceESP.print(F(";M="));
  enlaceESP.print(presencia ? 1 : 0);

  enlaceESP.print(F(";DOOR="));
  enlaceESP.print(puertaAbierta ? 1 : 0);

  enlaceESP.print(F(";SEC="));
  enlaceESP.print(modoSeguridad ? 1 : 0);

  enlaceESP.print(F(";LIGHT="));
  enlaceESP.println(luzCasaEncendida ? 1 : 0);
}

// ---------- COMANDOS DESDE ESP8266 ----------
void procesarComando(char *cmd) {
  int len = strlen(cmd);
  while (len > 0 && (cmd[len - 1] == '\r' || cmd[len - 1] == ' ' || cmd[len - 1] == '\t')) {
    cmd[--len] = '\0';
  }
  while (*cmd == ' ' || *cmd == '\t') cmd++;

  if (strcmp_P(cmd, PSTR("CMD:DOOR_OPEN")) == 0) {
    ejecutarAperturaSegura();
  } else if (strcmp_P(cmd, PSTR("CMD:LIGHT_ON")) == 0) {
    luzCasaEncendida = true;
    actualizarLuzBlanca();
  } else if (strcmp_P(cmd, PSTR("CMD:LIGHT_OFF")) == 0) {
    luzCasaEncendida = false;
    actualizarLuzBlanca();
  } else if (strcmp_P(cmd, PSTR("CMD:SECURITY_ON")) == 0) {
    modoSeguridad = true;
  } else if (strcmp_P(cmd, PSTR("CMD:SECURITY_OFF")) == 0) {
    modoSeguridad = false;
    intrusoDetectado = false;
  } else if (strcmp_P(cmd, PSTR("CMD:ALARM_OFF")) == 0) {
    intrusoDetectado = false;
    alarmaSilenciada = true;
    faseBuzzer = false;
    ponerBit(BIT_BUZZER, false);
  } else if (strcmp_P(cmd, PSTR("CMD:STATUS")) == 0) {
    enviarEstado();
  }
}

void recibirESP() {
  while (enlaceESP.available()) {
    char c = enlaceESP.read();
    if (c == '\n') {
      bufferESP[indiceESP] = '\0';
      if (indiceESP > 0) procesarComando(bufferESP);
      indiceESP = 0;
    } else if (c != '\r' && indiceESP < (MAX_BUFFER_ESP - 1)) {
      bufferESP[indiceESP++] = c;
    }
  }
}

// ---------- SENSORES ----------
void leerDHT() {
  if (millis() - tDHT < INTERVALO_DHT) return;
  tDHT = millis();
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  if (!isnan(h) && !isnan(t)) {
    humedad = h;
    temperatura = t;
  }
}

void leerUltrasonico() {
  if (millis() - tUltrasonico < INTERVALO_ULTRASONICO) return;
  tUltrasonico = millis();

  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);
  unsigned long duracion = pulseIn(PIN_ECHO, HIGH, 20000UL);

  if (duracion == 0) {
    distanciaCM = -1;
    presencia = false;
  } else {
    distanciaCM = duracion * 0.01724f;
    presencia = (distanciaCM > 0 && distanciaCM < DISTANCIA_PRESENCIA_CM);

    if (modoSeguridad && distanciaCM > 0 && distanciaCM <= 15.0 && !intrusoDetectado) {
      intrusoDetectado = true;
      alarmaSilenciada = false;
      enviarESP_P(F("EVENT:INTRUDER"));
    }
  }

  valorLuz = analogRead(PIN_LDR);
}

void leerSensorLlama() {
  bool nuevaLlama = (digitalRead(PIN_LLAMA) == LOW);
  if (nuevaLlama != llama) {
    llama = nuevaLlama;
    if (llama) {
      alarmaSilenciada = false;
      enviarESP_P(F("EVENT:FIRE"));
    } else {
      alarmaSilenciada = false;
      enviarESP_P(F("EVENT:FIRE_CLEAR"));
    }
  }
}

// ---------- ALARMAS Y LEDS ----------
void actualizarAlarmas() {
  if (llama || intrusoDetectado) {
    bitWrite(salidas, BIT_LED_ROJO, 1);
    bitWrite(salidas, BIT_LED_VERDE, 0);

    if (!alarmaSilenciada) {
      if (millis() - tBuzzer >= 200) {
        tBuzzer = millis();
        faseBuzzer = !faseBuzzer;
        bitWrite(salidas, BIT_BUZZER, faseBuzzer ? 1 : 0);
      }
    } else {
      bitWrite(salidas, BIT_BUZZER, 0);
    }
    actualizar595();
  } else {
    faseBuzzer = false;
    bitWrite(salidas, BIT_BUZZER, 0);
    bitWrite(salidas, BIT_LED_ROJO, 0);
    bitWrite(salidas, BIT_LED_VERDE, 1);
    actualizar595();
  }
}

// ---------- RFID ----------
void verificarSaludRFID() {
  if (millis() - tRfidCheck < INTERVALO_RFID_CHECK) return;
  tRfidCheck = millis();

  byte version = rfid.PCD_ReadRegister(rfid.VersionReg);
  if (version == 0x00 || version == 0xFF) {
    rfid.PCD_Init();
  }
}

bool tarjetaPermitida() {
  if (rfid.uid.size != TAM_UID) return false;
  for (byte tarjeta = 0; tarjeta < CANTIDAD_TARJETAS; tarjeta++) {
    bool coincide = true;
    for (byte i = 0; i < TAM_UID; i++) {
      if (rfid.uid.uidByte[i] != UIDS_PERMITIDOS[tarjeta][i]) {
        coincide = false;
        break;
      }
    }
    if (coincide) return true;
  }
  return false;
}

void leerRFID() {
  verificarSaludRFID();
  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) return;

  if (tarjetaPermitida()) {
    enviarESP_P(F("EVENT:ACCESS_GRANTED"));
    ejecutarAperturaSegura();
  } else {
    enviarESP_P(F("EVENT:ACCESS_DENIED"));
    ponerBit(BIT_LED_ROJO, true);
    ponerBit(BIT_LED_VERDE, false);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Acceso denegado"));
    ponerBit(BIT_BUZZER, true);  delay(150);
    ponerBit(BIT_BUZZER, false); delay(100);
    ponerBit(BIT_BUZZER, true);  delay(150);
    ponerBit(BIT_BUZZER, false);
    delay(1500);
  }
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

// ---------- PANTALLA ----------
void actualizarLCD() {
  if (millis() - tLCD < INTERVALO_LCD) return;
  tLCD = millis();

  lcd.setCursor(0, 0);
  lcd.print(F("T:"));
  if (isnan(temperatura)) lcd.print(F("--.-"));
  else lcd.print(temperatura, 1);
  lcd.print(F(" H:"));
  if (isnan(humedad)) lcd.print(F("--"));
  else lcd.print(humedad, 0);
  lcd.print(F("%      "));

  lcd.setCursor(0, 1);
  lcd.print(F("D:"));
  if (distanciaCM < 0) lcd.print(F("--"));
  else lcd.print(distanciaCM, 0);
  lcd.print(F("cm      "));

  lcd.setCursor(10, 1);
  if (llama) lcd.print(F("FUEGO!"));
  else if (presencia) lcd.print(F("PRES  "));
  else lcd.print(F("OK    "));
}

// ---------- SETUP Y LOOP ----------
void setup() {
  pinMode(0, INPUT_PULLUP);

  enlaceESP.begin(9600);
  pinMode(PIN_ESP_RX, INPUT_PULLUP);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_LLAMA, INPUT);
  digitalWrite(PIN_TRIG, LOW);

  pinMode(PIN_595_DATA, OUTPUT);
  pinMode(PIN_595_CLOCK, OUTPUT);
  pinMode(PIN_595_LATCH, OUTPUT);

  salidas = 0x00;
  actualizar595(true);
  actualizarLuzBlanca(); // Enciende la luz blanca al iniciar

  Wire.begin();
  Wire.setClock(400000L);
  lcd.init();
  lcd.backlight();
  dht.begin();

  servoPuerta.attach(PIN_SERVO);
  servoPuerta.write(ANGULO_CERRADO);

  pinMode(PIN_RFID_SS, OUTPUT);
  digitalWrite(PIN_RFID_SS, HIGH);
  SPI.begin();
  rfid.PCD_Init();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(F("CASA DOMOTICA"));
  delay(1500);
}

void loop() {
  recibirESP();
  leerSensorLlama();
  leerRFID();
  leerUltrasonico();
  leerDHT();
  actualizarAlarmas();
  actualizarLCD();
}