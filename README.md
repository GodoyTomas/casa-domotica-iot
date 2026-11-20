# Casa Domótica IoT — Arduino Uno R3 + ESP8266

Sistema de automatización y seguridad domiciliaria basado en arquitectura de doble microcontrolador: **Arduino Uno R3** para control en tiempo real de sensores y actuadores, y **ESP8266** para conectividad Wi-Fi y control remoto vía bot de Telegram.

## Funcionalidades principales
- **Control de acceso RFID (RC522):** Apertura automática de puerta con servomotor (3 s) para tarjetas autorizadas y alerta de acceso denegado.
- **Evacuación automática ante incendios:** Al detectar llama, dispara alarma sonora/visual, notifica por Telegram y abre la puerta durante **10 segundos** de forma no bloqueante.
- **Modo Seguridad:** Vigilancia con sensor ultrasónico HC-SR04 (detección de intrusos a menos de 15 cm).
- **Iluminación LED RGB (luz blanca continua):** Control remoto ON/OFF desde Telegram mediante registro de desplazamiento **74HC595**.
- **Monitoreo ambiental:** Lectura de temperatura y humedad (DHT11), luminosidad (LDR) y visualización en LCD I2C 16x2.

## Arquitectura y Comunicación
Ambas placas se comunican por UART (`SoftwareSerial` a 9600 bps) mediante un protocolo de tramas:
- `CMD:` — Órdenes desde Telegram hacia el Arduino (`DOOR_OPEN`, `LIGHT_ON`, `LIGHT_OFF`, `SECURITY_ON`, `SECURITY_OFF`, `ALARM_OFF`, `STATUS`).
- `EVENT:` — Alertas inmediatas desde el Arduino (`FIRE`, `FIRE_CLEAR`, `INTRUDER`, `ACCESS_GRANTED`, `ACCESS_DENIED`).
- `STATE:` — Telemetría completa de sensores y actuadores.

## Configuración rápida
1. Clonar el repositorio.
2. Copiar `esp8266/secrets.h.example` como `esp8266/secrets.h` y completar credenciales Wi-Fi y de Telegram.
3. Cargar `arduino_uno/arduino_uno.ino` en el Arduino Uno R3.
4. Cargar `esp8266/esp8266.ino` en el ESP8266.