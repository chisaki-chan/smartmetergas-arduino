#include <SoftwareSerial.h>

SoftwareSerial sim7000(11, 13);  // RX, TX untuk SIM7000C

#define ledPinR 4
#define ledPinG 5
#define moto1 6
#define moto2 7
#define sensorPin1 2
#define sensorPin2 3
#define buttonPin 9
#define batteryPin A2

bool sensorState1 = LOW;
bool sensorState2 = LOW;

String valveState = "Close";

bool prevSensorState1 = LOW;
bool prevSensorState2 = LOW;

bool buttonPrevState = HIGH;
bool buttonState = HIGH;

const float vRef = 3.3;     // Tegangan referensi pada Arduino Pro Mini 3.3V
const float maxVoltage = 3.3;  // Tegangan maksimum baterai (misalnya 4.2V untuk Li-ion)
const float minVoltage = 0.0;  // Tegangan minimum baterai (misalnya 3.0V untuk Li-ion)
float batteryVoltage = 0.0;  // Variabel untuk menyimpan tegangan baterai
int batteryPercentage = 0;

const unsigned long oneHour = 10000;
unsigned long startTime = 0;

float counterValue = 0.0;

void setup() {
  // Inisialisasi pin
  pinMode(ledPinR, OUTPUT);  // LED Merah (D4)
  pinMode(ledPinG, OUTPUT);  // LED Hijau (D5)
  pinMode(moto1, OUTPUT);
  pinMode(moto2, OUTPUT);
  pinMode(sensorPin1, INPUT);
  pinMode(sensorPin2, INPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(batteryPin, INPUT);


  // Matikan kedua LED saat memulai
  digitalWrite(ledPinR, LOW);
  digitalWrite(ledPinG, LOW);

  // Inisialisasi komunikasi Serial untuk debugging
  Serial.begin(9600);
  delay(2000);

  // Inisialisasi SoftwareSerial untuk komunikasi dengan SIM7000C
  sim7000.begin(9600);
  delay(5000);  // Tunggu modul SIM7000C siap
  closevalve();
}

void loop() {
  buttonState = digitalRead(buttonPin);
  
  if (buttonState == LOW && buttonPrevState == HIGH) {  // Jika tombol ditekan (low) dan baru saja ditekan
    startTime = millis();            // Set waktu mulai saat tombol ditekan
    digitalWrite(ledPinG, HIGH);    
    openvalve();                     // Buka valve saat tombol ditekan
    
    // Baca sensor dan jalankan fungsi selama satu jam
    while (millis() - startTime < oneHour) {
      readsensor();                  // Jalankan fungsi baca sensor setiap loop
      batterycheck();                // Periksa baterai secara berkala
      digitalWrite(ledPinG, HIGH);
      delay(150);
      digitalWrite(ledPinG, LOW);
    }
    
    // Setelah satu jam, reset variabel dan standby
    closevalve();                    // Pastikan valve tertutup
    initializenb();
    delay(1000);
    senddata();
    delay(1000); // Tunggu sebentar sebelum mengirim perintah berikutnya
    counterValue = 0.0;              // Reset counter value
    psm();
  }

  // Saat tombol tidak ditekan, tetap standby
  if (buttonState == HIGH) {
    valveState = "Close";
    counterValue = 0.0;              // Reset counter value
    batterycheck();                  // Tetap cek status baterai selama standby
    digitalWrite(ledPinR, HIGH);
    digitalWrite(ledPinG, HIGH);
    delay(150);
    digitalWrite(ledPinR, LOW);
    digitalWrite(ledPinG, LOW);
  }

  buttonPrevState = buttonState;     // Update status tombol sebelumnya
}

//------------------------------------------------------------------------------
//Fungsi untuk menutup valve
void closevalve() {
  digitalWrite(moto1, HIGH);
  digitalWrite(moto2, LOW);
}

//Fungsi untuk membuka valve
void openvalve() {
  digitalWrite(moto1, LOW);
  digitalWrite(moto2, HIGH);
  valveState = "Open";
}

//Power Saving Mode Function
void psm () {
  sendATCommand("AT+SMDISC", "OK", 2000);
  delay(4000);
  sendATCommand("AT+CNACT=0", "OK", 2000);
  delay(4000);
}

//Initialize NB
void initializenb() {
  // Kirim perintah AT
  sendATCommand("AT","OK", 2000);
  // Set APN (sesuaikan dengan APN provider seluler Anda)
  sendATCommand("AT+CGDCONT=1,\"IP\",\"\"", "OK", 2000);
  // Aktifkan koneksi PDP
  sendATCommand("AT+CNACT=1", "OK", 5000);
  // Konfigurasi URL broker MQTT (ganti IP broker dan port)
  sendATCommand("AT+SMCONF=\"URL\",\"34.253.103.94\",1883", "OK", 2000);
  //  KEEPTIME untuk MQTT (60 detik)
  sendATCommand("AT+SMCONF=\"KEEPTIME\",60", "OK", 2000);
  // Koneksi ke broker MQTT
  sendATCommand("AT+SMCONN", "OK", 10000);
}

// Fungsi untuk mengirim perintah AT dan menunggu respons
void sendATCommand(String command, String expectedResponse, unsigned long timeout) {
  sim7000.println(command); // Kirim perintah AT ke SIM7000C
  Serial.println("Command: " + command); // Tampilkan perintah yang dikirim ke Serial Monitor
  
  unsigned long timeStart = millis();
  
  while (millis() - timeStart < timeout) {
    if (sim7000.available()) {
      String response = sim7000.readString();
      Serial.println("Response: " + response); // Tampilkan respons dari SIM7000C ke Serial Monitor
      
      if (response.indexOf(expectedResponse) >= 0) {
        return; // Jika respons sesuai, keluar dari fungsi
      }
    }
  }

  Serial.println("Error: Expected response not received.");
}

void senddata() {
  String topic = "/PGN/SmartMeter";
  
  int signalStrength;
  String signalLevel;

  readSignalStrength(signalStrength, signalLevel); // Baca kekuatan sinyal

  // Buat string JSON dengan data tambahan
  String jsonData = createJson(batteryPercentage, valveState, counterValue, signalStrength, signalLevel);

  int jsonLength = jsonData.length();

  sendATCommand("AT+SMPUB=\"" + topic + "\",\"" + String(jsonLength) + "\",0,0", ">", 2000);
  sim7000.println(jsonData);
}


String createJson(int batteryPercentage, String valveState, float counterValue, int signalStrength, String signalLevel) {
  String jsonString = "{\n";
  jsonString += "  \"condition_io\": \"" + valveState + "\",\n";
  jsonString += "  \"volume\": \"" + String(counterValue, 3) + "\",\n";
  jsonString += "  \"type_io\": \"remote\",\n";
  jsonString += "  \"battery\": \"" + String(batteryPercentage) + "\",\n";
  jsonString += "  \"signal_strength\": \"" + String(signalStrength) + " dBm\",\n";
  jsonString += "  \"signal_level\": \"" + signalLevel + "\",\n";
  jsonString += "  \"metergas_id\": \"2\"\n";
  jsonString += "}";

  return jsonString;
}


void readsensor() {
  // Read the state of the sensors
  sensorState1 = digitalRead(sensorPin1);
  sensorState2 = digitalRead(sensorPin2);

  // Check for specific state transitions and update counter
  if (sensorState1 == HIGH && sensorState2 == HIGH && prevSensorState1 == HIGH && prevSensorState2 == LOW) {
    counterValue += 0.0035;  // Approximation between 0.003 and 0.004 M³
  }
  else if (sensorState1 == LOW && sensorState2 == HIGH && prevSensorState1 == HIGH && prevSensorState2 == HIGH) {
    counterValue += 0.0015;
  }
  else if (sensorState1 == HIGH && sensorState2 == HIGH && prevSensorState1 == LOW && prevSensorState2 == HIGH) {
    counterValue += 0.0035;  // Approximation between 0.008 and 0.009 M³
  }
  else if (sensorState1 == HIGH && sensorState2 == LOW && prevSensorState1 == HIGH && prevSensorState2 == HIGH) {
    counterValue += 0.0015;  // Increment next decimal place
  }

  // // Update previous sensor states
  prevSensorState1 = sensorState1;
  prevSensorState2 = sensorState2;

  // // Print the states to the Serial Monitor
  // Serial.print("Sensor A State: ");
  // Serial.print(sensorState1);
  // Serial.print(" | Sensor B State: ");
  // Serial.println(sensorState2);

  // // Print the current counter value to the Serial Monitor
  // Serial.print("Counter Value: ");
  // Serial.print(counterValue, 3);  // Print value with 3 decimal places
  // Serial.println(" M³");

  // Simple delay to reduce data rate
  delay(500);
}

void batterycheck() {
  // Baca nilai dari pin analog A2
  int sensorValue = analogRead(batteryPin);

  // Konversi nilai analog menjadi tegangan (dalam volt)
  batteryVoltage = sensorValue * (vRef / 1023.0); // Rumus konversi ADC ke tegangan

  // Jika menggunakan voltage divider, konversikan ke tegangan sebenarnya
  // Misalnya, jika menggunakan pembagi tegangan 2:1
  batteryVoltage = batteryVoltage * 1; // Sesuaikan dengan rasio pembagi tegangan
  
  // Hitung persentase baterai berdasarkan tegangan
  batteryPercentage = calculateBatteryPercentage(batteryVoltage);

  // Tampilkan tegangan dan persentase ke Serial Monitor
  // Serial.print("Tegangan Baterai: ");
  // Serial.print(batteryVoltage);
  // Serial.println(" V");
  
  // Serial.print("Persentase Baterai: ");
  // Serial.print(batteryPercentage);
  // Serial.println(" %");
  
  delay(1000);  // Delay 1 detik sebelum membaca lagi
}

// Fungsi untuk menghitung persentase baterai berdasarkan tegangan
int calculateBatteryPercentage(float voltage) {
  // Pastikan tegangan tidak melebihi batas
  if (voltage >= maxVoltage) {
    return 100;  // Baterai penuh
  } else if (voltage <= minVoltage) {
    return 0;    // Baterai habis
  } else {
    // Persentase dihitung secara linier antara minVoltage dan maxVoltage
    return (int)((voltage - minVoltage) / (maxVoltage - minVoltage) * 100);
  }
}

void readSignalStrength(int &signalStrength, String &signalLevel) {
  sim7000.println("AT+CSQ");  // Kirim perintah untuk membaca sinyal
  delay(100);  

  while (sim7000.available()) {
    String response = sim7000.readString(); // Baca respon dari SIM7000
    Serial.println("CSQ Response: " + response);

    int csqIndex = response.indexOf("+CSQ: ");
    if (csqIndex != -1) {
      int commaIndex = response.indexOf(",", csqIndex);
      if (commaIndex != -1) {
        String rssiStr = response.substring(csqIndex + 6, commaIndex);
        int rssi = rssiStr.toInt(); // Konversi RSSI ke integer

        // Konversi RSSI ke dBm berdasarkan tabel nilai CSQ ke dBm
        if (rssi == 99) {  
          signalStrength = -999;  // Tidak ada sinyal
          signalLevel = "Unknown";
        } else {
          signalStrength = (2 * rssi) - 113;  // Konversi ke dBm
          
          // Tentukan kualitas sinyal berdasarkan nilai dBm
          if (signalStrength >= -53) {
            signalLevel = "Excellent";
          } else if (signalStrength >= -65) {
            signalLevel = "Good";
          } else if (signalStrength >= -75) {
            signalLevel = "Fair";
          } else if (signalStrength >= -85) {
            signalLevel = "Weak";
          } else {
            signalLevel = "Marginal";
          }
        }
      }
    }
  }
}
