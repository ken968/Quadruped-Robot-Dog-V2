#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>
#include <U8g2lib.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "bitmaps.h"
#include "secrets.h"

// Menggunakan MQTT_BROKER_IP dari secrets.h
const char* mqtt_server = MQTT_BROKER_IP;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

Adafruit_PWMServoDriver pca = Adafruit_PWMServoDriver();
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

#define SERVOMIN 150
#define SERVOMAX 600

int led1 = 2; // GPIO2
int led2 = 4; // GPIO4
bool ledAktif = false;

// Variabel untuk State Machine (Non-blocking)
unsigned long prevMillisLED1 = 0, prevMillisLED2 = 0;
int stepLED1 = 0, stepLED2 = 0;

unsigned long prevMillisRobot = 0;
int robotStep = 0;
char currentCmd = ' ';
char lastCmd = ' ';

// ===== PENGATURAN SENSOR & SERVO LEHER =====
#define TRIG_PIN 18
#define ECHO_PIN 19
#define HEAD_SERVO_PIN 8 //servo leher
#define BUZZER_PIN 23

// Sudut Servo Leher
const int HEAD_KIRI = 170;
const int HEAD_TENGAH = 90;
const int HEAD_KANAN = 10;
const int JARAK_WARNING = 7;

// Variabel Mode & Logika
bool modeAuto = false;  // false = Manual, true = Autopilot
int autoStep = 0;       // Langkah state machine Autopilot
unsigned long prevMillisAuto = 0;
int distKiri = 0, distTengah = 0, distKanan = 0;

// Batas Jarak Aman (cm)
const int JARAK_AMAN = 20; 

// Fungsi baca jarak (Blocking singkat agar akurat saat scan)
int getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 25000); // Timeout 25ms
  if (duration == 0) return 100; // Jika timeout, anggap jauh
  return duration * 0.034 / 2;
}

// Variabel untuk Animasi OLED


void updateAnimasi() {
    // ... (kode pengecekan waktu animasi yang sudah ada)
    const unsigned char* const* currentAnimArray;
    int frameCount;

    // 1. Array dan Jumlah Frame yang Aktif
    if (currentCmd == 'W' || currentCmd == 'A' || currentCmd == 'S' || currentCmd == 'D') {
        currentAnimArray = epd_bitmap_allArray_GERAK;
        frameCount = FRAME_COUNT_GERAK;
    } 
    else if (currentCmd == 'E') { // <-- Logika untuk DUDUK
        currentAnimArray = epd_bitmap_allArray_DUDUK;
        frameCount = FRAME_COUNT_DUDUK;
    }
    else if (currentCmd == 'Q') { // <-- Logika untuk TIDUR
        currentAnimArray = epd_bitmap_allArray_TIDUR;
        frameCount = FRAME_COUNT_TIDUR;
    }
    else { 
        // Jika diam (' ') atau perintah lain, pakai animasi IDLE
        currentAnimArray = epd_bitmap_allArray_IDLE;
        frameCount = FRAME_COUNT_IDLE;
    }
    // 2. Mulai menggambar ke buffer
    u8g2.firstPage();
    do {
        // Gambar frame yang dipilih
        u8g2.drawXBM(0, 0, 128, 64, (const uint8_t*)pgm_read_ptr(&currentAnimArray[currentFrame]));
    } while (u8g2.nextPage()); 
    // 3. Pindah ke frame berikutnya (menggunakan array yang dipilih)
    currentFrame = (currentFrame + 1) % frameCount;
}

int angleToPulse(int ang) {
    return map(ang, 0, 180, SERVOMIN, SERVOMAX);
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("connecting to ");
  Serial.println(WIFI_SSID);

  WiFi.begin(WIFI_SSID, WIFI_PASS);

  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!mqttClient.connected()) {
    if (mqttClient.connect("Robodog_ESP32")) {
      mqttClient.subscribe("Robodog/CMD");
      Serial.println("Connected to MQTT");
    } else {
      Serial.print("Failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  Serial.print("Pesan MQTT masuk: ");
  Serial.println(message);

  // Parsing JSON dari Python
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, message);
  
  if (error) {
    Serial.print("Gagal parsing JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // 1. Ekstrak perintah Mode (Manual / Autopilot)
  if (doc.containsKey("mode")) {
    const char* mode = doc["mode"];
    if (strcmp(mode, "auto") == 0) {
      if (modeAuto == false) {
        modeAuto = true;
        currentCmd = ' '; 
        autoStep = 0;
        Serial.println("Berubah ke Mode AUTOPILOT");
      }
    } else if (strcmp(mode, "manual") == 0) {
      if (modeAuto == true) {
        modeAuto = false;
        currentCmd = ' ';
        robotStep = 0;
        pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_TENGAH)); 
        Serial.println("Berubah ke Mode MANUAL");
      }
    }
  }

  // 2. Ekstrak perintah Gerakan & Animasi (Hanya berlaku jika Manual)
  if (doc.containsKey("cmd") && !modeAuto) {
    const char* cmdStr = doc["cmd"];
    char c = cmdStr[0]; // Ambil karakter pertama (W/A/S/D/Q/E/ )
    
    // Jika ada perubahan arah, reset langkah
    if (currentCmd != c) {
      currentCmd = c;
      robotStep = 0;
      Serial.print("Gerak Manual: ");
      Serial.println(c);
    }
  }

  // 3. Ekstrak perintah LED
  if (doc.containsKey("led")) {
    bool ledState = doc["led"];
    ledAktif = ledState;
  }
}

void setup() {
    Serial.begin(115200);

    setup_wifi();
    mqttClient.setServer(mqtt_server, 1883);
    mqttClient.setCallback(mqttCallback);

    pca.begin();
    pca.setPWMFreq(50);
		u8g2.begin();
    delay(10);

		pinMode(TRIG_PIN, OUTPUT);
  	pinMode(ECHO_PIN, INPUT);
    pinMode(led1, OUTPUT);
    pinMode(led2, OUTPUT);
		pinMode(BUZZER_PIN, OUTPUT);
  	digitalWrite(BUZZER_PIN, LOW);

		pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_TENGAH));

    currentCmd = ' ';
}

void checkSerialCommands() {
  if (Serial.available() > 0) {
    char c = toupper(Serial.read());

    if (c == 'p') {
      if (modeAuto == false) {
        modeAuto = true;
        currentCmd = ' '; // Reset gerakan
        autoStep = 0;     // Mulai scan dari awal
        Serial.println("Mode diubah ke AUTOPILOT (via Serial)");
      } else {
        modeAuto = false;
        currentCmd = ' ';
        robotStep = 0;
        pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_TENGAH)); 
        Serial.println("Mode diubah ke MANUAL (via Serial)");
      }
      // Setelah switch mode, KELUAR dari fungsi
      return; 
    }

    // 2. Logika LED (Bisa diakses di mode manapun)
    else if (c == '1') { // LED ON
      ledAktif = true;
      Serial.println("Perintah: LED ON");
    } 
    else if (c == '2') { // LED OFF
      ledAktif = false;
      Serial.println("Perintah: LED OFF");
    }

    // 3. Logika Gerakan Manual (Hanya di Mode Manual)
    else if (c == 'W' || c == 'A' || c == 'D' || c == ' ' || c == 'E' || c == 'S' || c == 'Q') {
      if (modeAuto == false) {
        // Hanya izinkan perintah gerakan jika di mode MANUAL
        currentCmd = c;
        robotStep = 0; // Reset langkah robot
        Serial.print("Perintah Gerak: "); Serial.println(c);
      } else {
        // Jika di mode Autopilot, abaikan dan beri tahu
        Serial.println("Gerakan manual diabaikan (Mode Autopilot aktif).");
      }
    }
  }
}

// ===== FUNGSI UPDATE NON-BLOCKING =====
void updateLED() {
    if (!ledAktif) {
        digitalWrite(led1, LOW);
        digitalWrite(led2, LOW);
        return;
    }
    unsigned long now = millis();
    switch (stepLED1) {
        case 0: digitalWrite(led1, HIGH); if (now - prevMillisLED1 >= 50) { prevMillisLED1 = now; stepLED1 = 1; } break;
        case 1: digitalWrite(led1, LOW);  if (now - prevMillisLED1 >= 50) { prevMillisLED1 = now; stepLED1 = 2; } break;
        case 2: digitalWrite(led1, HIGH); if (now - prevMillisLED1 >= 50) { prevMillisLED1 = now; stepLED1 = 3; } break;
        case 3: digitalWrite(led1, LOW);  if (now - prevMillisLED1 >= 50) { prevMillisLED1 = now; stepLED1 = 4; } break;
        case 4: if (now - prevMillisLED1 >= 500) { prevMillisLED1 = now; stepLED1 = 0; } break;
    }
    switch (stepLED2) {
        case 0: digitalWrite(led2, HIGH); if (now - prevMillisLED2 >= 50) { prevMillisLED2 = now; stepLED2 = 1; } break;
        case 1: digitalWrite(led2, LOW);  if (now - prevMillisLED2 >= 50) { prevMillisLED2 = now; stepLED2 = 2; } break;
        case 2: digitalWrite(led2, HIGH); if (now - prevMillisLED2 >= 50) { prevMillisLED2 = now; stepLED2 = 3; } break;
        case 3: digitalWrite(led2, LOW);  if (now - prevMillisLED2 >= 50) { prevMillisLED2 = now; stepLED2 = 4; } break;
        case 4: if (now - prevMillisLED2 >= 700) { prevMillisLED2 = now; stepLED2 = 0; } break;
    }
}

void updateRobot() {
    unsigned long now = millis();
    if (currentCmd != lastCmd) {
        robotStep = 0;
				currentFrame = 0;
        lastCmd = currentCmd;
    }
    
    // ===== MUNDUR STATE MACHINE =====
    else if (currentCmd == 'S') {
        switch (robotStep) {
            case 0: // PERSIS seperti kode original - Step 1
                if (now - prevMillisRobot >= 250) { // Tambahkan delay 500ms pada case 0
                    pca.setPWM(4, 0, angleToPulse(120));
                    pca.setPWM(5, 0, angleToPulse(60));
                    pca.setPWM(2, 0, angleToPulse(90)); 
                    pca.setPWM(3, 0, angleToPulse(160));
                    prevMillisRobot = now; robotStep = 1;
                }
                break;
                
            case 1: // PERSIS seperti kode original - Step 2
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(6, 0, angleToPulse(45)); 
                    pca.setPWM(7, 0, angleToPulse(130));
                    pca.setPWM(0, 0, angleToPulse(157)); 
                    pca.setPWM(1, 0, angleToPulse(30));
                    prevMillisRobot = now; robotStep = 2;
                } 
                break;
                
            case 2: // PERSIS seperti kode original - Step 3a
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(2, 0, angleToPulse(30));
                    pca.setPWM(3, 0, angleToPulse(90));
                    pca.setPWM(4, 0, angleToPulse(150)); 
                    pca.setPWM(5, 0, angleToPulse(90));
                    prevMillisRobot = now; robotStep = 3;
                } 
                break;
                
            case 3: // PERSIS seperti kode original - Step 3b 
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(0, 0, angleToPulse(100));  
                    pca.setPWM(1, 0, angleToPulse(20));
                    pca.setPWM(6, 0, angleToPulse(60));  
                    pca.setPWM(7, 0, angleToPulse(120));
                    prevMillisRobot = now; robotStep = 4;
                } 
                break;
                
            case 4: // PERSIS seperti kode original - Step 4
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(4, 0, angleToPulse(130));	
                    pca.setPWM(5, 0, angleToPulse(45));
                    pca.setPWM(2, 0, angleToPulse(40));
                    pca.setPWM(3, 0, angleToPulse(150));
                    prevMillisRobot = now; robotStep = 5;
                } 
                break;
                
            case 5: // PERSIS seperti kode original - Step 5
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(0, 0, angleToPulse(160)); 
                    pca.setPWM(1, 0, angleToPulse(90));
                    pca.setPWM(6, 0, angleToPulse(30));
                    pca.setPWM(7, 0, angleToPulse(90));
                    prevMillisRobot = now; robotStep = 0; // Kembali ke step 0 untuk siklus berikutnya
                } 
                break;
        }
    }

    // ===== STANDBY STATE MACHINE =====
    if (currentCmd == ' ') {
        if (robotStep == 0) {
            pca.setPWM(0,0,angleToPulse(120)); pca.setPWM(1,0,angleToPulse(60));
            pca.setPWM(2,0,angleToPulse(60));  pca.setPWM(3,0,angleToPulse(120));
            pca.setPWM(4,0,angleToPulse(120)); pca.setPWM(5,0,angleToPulse(60));
            pca.setPWM(6,0,angleToPulse(60));  pca.setPWM(7,0,angleToPulse(120));
            robotStep = 1;
        }
    }

    // ===== SECOND MODE (DUDUK) =====
    else if (currentCmd == 'E') {
      if (robotStep == 0) {
          pca.setPWM(0, 0, angleToPulse(120)); pca.setPWM(1, 0, angleToPulse(60));
          pca.setPWM(2, 0, angleToPulse(60)); pca.setPWM(3, 0, angleToPulse(120));
          pca.setPWM(4, 0, angleToPulse(155)); pca.setPWM(5, 0, angleToPulse(20));
          pca.setPWM(6, 0, angleToPulse(20)); pca.setPWM(7, 0, angleToPulse(155));
          robotStep = 1;
      }
    }
    
    // ===== MAJU STATE MACHINE =====
    else if (currentCmd == 'W') {
        switch (robotStep) {
            case 0: // PERSIS seperti kode original - Step 1
                if (now - prevMillisRobot >= 250) { // Tambahkan delay 500ms pada case 0
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(0, 0, angleToPulse(160)); // kanan depan keatas
                    pca.setPWM(1, 0, angleToPulse(30));
                    pca.setPWM(6, 0, angleToPulse(45));  // kiri belakang keatas  
                    pca.setPWM(7, 0, angleToPulse(130));
                    prevMillisRobot = now; robotStep = 1;
                }
                break;
                
            case 1: // PERSIS seperti kode original - Step 2
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(0, 0, angleToPulse(160)); // kanan depan kedepan
                    pca.setPWM(1, 0, angleToPulse(90));
                    pca.setPWM(2, 0, angleToPulse(105)); // kiri depan kebelakang (dorongan)
                    pca.setPWM(3, 0, angleToPulse(150));
                    pca.setPWM(4, 0, angleToPulse(90));  // kanan belakang kebelakang (dorongan)
                    pca.setPWM(5, 0, angleToPulse(45));
                    prevMillisRobot = now; robotStep = 2;
                } 
                break;
                
            case 2: // PERSIS seperti kode original - Step 3a
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(4, 0, angleToPulse(145));
                    pca.setPWM(5, 0, angleToPulse(45));
                    pca.setPWM(6, 0, angleToPulse(30));  // kiri belakang kedepan
                    pca.setPWM(7, 0, angleToPulse(90));
                    prevMillisRobot = now; robotStep = 3;
                } 
                break;
                
            case 3: // PERSIS seperti kode original - Step 3b 
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(2, 0, angleToPulse(30)); // kiri depan keatas
                    pca.setPWM(3, 0, angleToPulse(160));
                    prevMillisRobot = now; robotStep = 4;
                } 
                break;
                
            case 4: // PERSIS seperti kode original - Step 4
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(0, 0, angleToPulse(90));  // kanan depan kebelakang
                    pca.setPWM(1, 0, angleToPulse(30));
                    pca.setPWM(2, 0, angleToPulse(30));  // kiri depan kedepan
                    pca.setPWM(3, 0, angleToPulse(90));
                    pca.setPWM(6, 0, angleToPulse(90));  // kiri belakang kebelakang
                    pca.setPWM(7, 0, angleToPulse(160));
                    prevMillisRobot = now; robotStep = 5;
                } 
                break;
                
            case 5: // PERSIS seperti kode original - Step 5
                if (now - prevMillisRobot >= 250) {
                    // Semua servo bergerak bersamaan seperti delay version
                    pca.setPWM(4, 0, angleToPulse(150)); // kanan belakang kedepan
                    pca.setPWM(5, 0, angleToPulse(90));
                    prevMillisRobot = now; robotStep = 0; // Kembali ke step 0 untuk siklus berikutnya
                } 
                break;
        }
    }

    // ===== BELOK KANAN STATE MACHINE =====
    else if (currentCmd == 'D') {
        switch (robotStep) {
            case 0: // Step 1: Stand
                pca.setPWM(0,0,angleToPulse(120)); pca.setPWM(1,0,angleToPulse(60));
                pca.setPWM(4,0,angleToPulse(120)); pca.setPWM(5,0,angleToPulse(60));
                pca.setPWM(6,0,angleToPulse(60)); pca.setPWM(7,0,angleToPulse(120));
                prevMillisRobot = now; robotStep = 1;
                break;
            case 1: // Step 2: Kaki kanan atas
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(2,0,angleToPulse(40)); pca.setPWM(3,0,angleToPulse(150));
                    prevMillisRobot = now; robotStep = 2;
                }
                break;
            case 2: // Step 3: Kaki kanan depan
                if (now - prevMillisRobot >= 500) {
                    pca.setPWM(2,0,angleToPulse(30)); pca.setPWM(3,0,angleToPulse(65));
                    prevMillisRobot = now; robotStep = 3;
                }
                break;
            case 3: // Step 4: Badan geser
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(0,0,angleToPulse(150)); pca.setPWM(1,0,angleToPulse(30));
                    pca.setPWM(6,0,angleToPulse(50)); pca.setPWM(7,0,angleToPulse(130));
                    prevMillisRobot = now; robotStep = 4;
                }
                break;
            case 4: // Step 5: Belakang
                if (now - prevMillisRobot >= 500) {
                    pca.setPWM(2,0,angleToPulse(100)); pca.setPWM(3,0,angleToPulse(150));
                    pca.setPWM(4,0,angleToPulse(155)); pca.setPWM(5,0,angleToPulse(115));
                    pca.setPWM(6,0,angleToPulse(110)); pca.setPWM(7,0,angleToPulse(160));
                    prevMillisRobot = now; robotStep = 5;
                }
                break;
            case 5: // Step 6: Kembali ke posisi awal
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(0,0,angleToPulse(120)); pca.setPWM(1,0,angleToPulse(60));
                    prevMillisRobot = now; robotStep = 0; // Kembali ke awal siklus belok
                }
                break;
        }
    }
    
    // ===== BELOK KIRI STATE MACHINE =====
    else if (currentCmd == 'A') {
        switch (robotStep) {
            case 0: // Step 1: Stand
                pca.setPWM(2,0,angleToPulse(60)); pca.setPWM(3,0,angleToPulse(120));
                pca.setPWM(6,0,angleToPulse(60)); pca.setPWM(7,0,angleToPulse(125));
                pca.setPWM(4,0,angleToPulse(120)); pca.setPWM(5,0,angleToPulse(60));
                prevMillisRobot = now; robotStep = 1;
                break;
            case 1: // Step 2: Kaki kiri atas
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(0,0,angleToPulse(155)); pca.setPWM(1,0,angleToPulse(20));
                    prevMillisRobot = now; robotStep = 2;
                }
                break;
            case 2: // Step 3: Kaki kiri depan
                if (now - prevMillisRobot >= 500) {
                    pca.setPWM(0,0,angleToPulse(160)); pca.setPWM(1,0,angleToPulse(90));
                    prevMillisRobot = now; robotStep = 3;
                }
                break;
            case 3: // Step 4: Badan geser
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(2,0,angleToPulse(40)); pca.setPWM(3,0,angleToPulse(150));
                    pca.setPWM(4,0,angleToPulse(130)); pca.setPWM(5,0,angleToPulse(45));
                    prevMillisRobot = now; robotStep = 4;
                }
                break;
            case 4: // Step 5: Belakang
                if (now - prevMillisRobot >= 500) {
                    pca.setPWM(0,0,angleToPulse(100)); pca.setPWM(1,0,angleToPulse(30));
                    pca.setPWM(4,0,angleToPulse(100)); pca.setPWM(5,0,angleToPulse(30));
                    pca.setPWM(6,0,angleToPulse(20)); pca.setPWM(7,0,angleToPulse(70));
                    prevMillisRobot = now; robotStep = 5;
                }
                break;
            case 5: // Step 6: Kembali ke posisi awal
                if (now - prevMillisRobot >= 250) {
                    pca.setPWM(2,0,angleToPulse(60)); pca.setPWM(3,0,angleToPulse(120));
                    prevMillisRobot = now; robotStep = 0; // Kembali ke awal siklus belok
                }
                break;
        }
    }

    // Tidur 
    else if (currentCmd == 'Q') {
      if (robotStep == 0) {
          pca.setPWM(0, 0, angleToPulse(160)); pca.setPWM(1, 0, angleToPulse(15));
          pca.setPWM(2, 0, angleToPulse(20)); pca.setPWM(3, 0, angleToPulse(155));
          pca.setPWM(4, 0, angleToPulse(160)); pca.setPWM(5, 0, angleToPulse(15));
          pca.setPWM(6, 0, angleToPulse(15)); pca.setPWM(7, 0, angleToPulse(160));
          robotStep = 1;
      }
    }
}

void runManualMode() {
  int jarakSaatIni = getDistance();

  if (jarakSaatIni <= JARAK_WARNING && jarakSaatIni > 0) {
    digitalWrite(BUZZER_PIN, HIGH); 
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }
  updateRobot(); 
}

void runAutoMode() {
  unsigned long now = millis();
  
  switch (autoStep) {
    case 0: // Persiapan: Stop Robot & Lirik KANAN
      currentCmd = ' '; // Pastikan robot diam saat scan
      updateRobot(); // Eksekusi diam
      pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_KANAN));
      prevMillisAuto = now;
      autoStep = 1;
      break;

    case 1: // Tunggu servo gerak & Baca Kanan -> Lirik TENGAH
      if (now - prevMillisAuto > 400) { // Tunggu 400ms
        distKanan = getDistance();
        pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_TENGAH));
        prevMillisAuto = now;
        autoStep = 2;
      }
      break;

    case 2: // Tunggu servo gerak & Baca Tengah -> Lirik KIRI
      if (now - prevMillisAuto > 400) {
        distTengah = getDistance();
        pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_KIRI));
        prevMillisAuto = now;
        autoStep = 3;
      }
      break;

    case 3: // Tunggu servo gerak & Baca Kiri -> Balik TENGAH & KEPUTUSAN
      if (now - prevMillisAuto > 400) {
        distKiri = getDistance();
        pca.setPWM(HEAD_SERVO_PIN, 0, angleToPulse(HEAD_TENGAH));
        
        // LOGIKA KEPUTUSAN (AI SEDERHANA)
        if (distTengah > JARAK_AMAN) {
          currentCmd = 'W'; // Jalan Maju
          autoStep = 4;     // Masuk fase jalan
        } else {
          // Ada halangan di depan
          if (distKanan > distKiri && distKanan > JARAK_AMAN) {
            currentCmd = 'D'; // Belok Kanan
          } else if (distKiri >= distKanan && distKiri > JARAK_AMAN) {
            currentCmd = 'A'; // Belok Kiri
          } else {
            currentCmd = 'S'; // Mundur (Jalan buntu)
          }
          autoStep = 4; // Masuk fase jalan
        }
        prevMillisAuto = now;
      }
      break;

    case 4: // Fase EKSEKUSI GERAKAN
      updateRobot(); // Robot bergerak sesuai currentCmd

      long durasiJalan = (currentCmd == 'W') ? 2000 : 800;
      
      if (now - prevMillisAuto > durasiJalan) {
        currentCmd = ' '; // Stop dulu
        autoStep = 0;     // Ulangi scan dari awal
      }
      break;
  }
}

// ===== LOOP UTAMA =====
void loop() {
  if(!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();

  checkSerialCommands();
  updateLED(); 

  if (modeAuto) {
    runAutoMode();   
  } else {
    runManualMode(); 
  }

  updateAnimasi(); 
}
