#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 5    // Pin SS (SDA) untuk Chip Select
#define RST_PIN 4   // Pin RST untuk Reset
#define BUZZER_PIN 12  // Pin untuk Buzzer

MFRC522 r(SS_PIN, RST_PIN);  // Inisialisasi MFRC522

void setup() {
  Serial.begin(115200);  // Memulai komunikasi serial dengan baud rate 115200
  SPI.begin(18, 19, 23, SS_PIN);  // Inisialisasi SPI: Pin SCK=18, MISO=19, MOSI=23, SS=5
  pinMode(SS_PIN, OUTPUT); 
  digitalWrite(SS_PIN, HIGH);  // Pastikan SS (SDA) HIGH saat idle

  pinMode(BUZZER_PIN, OUTPUT);  // Set pin Buzzer sebagai OUTPUT

  r.PCD_Init();  // Inisialisasi MFRC522
  Serial.println("Scan an RFID tag to read UID...");
}

void loop() {
  // Cek apakah ada tag baru yang didekatkan
  if (!r.PICC_IsNewCardPresent()) {
    return;  // Tidak ada tag baru, lanjutkan loop
  }

  if (!r.PICC_ReadCardSerial()) {
    return;  // Tidak bisa membaca kartu, lanjutkan loop
  }

  // Tampilkan UID tag yang terdeteksi
  Serial.print("UID tag: ");
  for (byte i = 0; i < r.uid.size; i++) {
    Serial.print(r.uid.uidByte[i] < 0x10 ? " 0" : " ");  // Format UID dengan 2 digit
    Serial.print(r.uid.uidByte[i], HEX);  // Tampilkan UID dalam format hexadecimal
  }
  Serial.println();  // Baru setelah UID tercetak

  // Nyalakan Buzzer sebagai indikator
  tone(BUZZER_PIN, 1000);  // Buzzer berbunyi dengan frekuensi 1000 Hz
  delay(500);  // Buzzer berbunyi selama 500 ms
  noTone(BUZZER_PIN);  // Matikan Buzzer
  delay(500);  // Tunggu sebentar sebelum membaca tag lagi

  r.PICC_HaltA();  // Berhenti setelah membaca tag
  r.PCD_StopCrypto1();  // Hentikan komunikasi dengan tag
  delay(1000);  // Jeda 1 detik sebelum membaca tag lain
}
