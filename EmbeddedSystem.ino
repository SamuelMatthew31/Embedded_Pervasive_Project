#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <MFRC522.h>

// ================== OLED CONFIG ==================
// Definisi ukuran layar OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C // Alamat I2C dari OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // Inisialisasi objek OLED dengan resolusi

// Wiring untuk OLED
// SDA -> D25 (GPIO25), SCL -> D26 (GPIO26) di ESP32

// ================== RFID CONFIG ==================
// Pin yang digunakan untuk komunikasi dengan MFRC522 (Reader RFID)
#define SS_PIN 21   // Pin untuk Chip Select
#define RST_PIN 22  // Pin untuk Reset
MFRC522 mfrc522(SS_PIN, RST_PIN);  // Inisialisasi objek MFRC522 dengan pin SS dan Reset

// ================== BUZZER CONFIG ==================
// Pin untuk Buzzer
#define BUZZER 27  // Pin untuk kontrol buzzer

// ================== BUTTON CONFIG ==================
// Pin untuk tombol input (masuk/keluar)
const int BTN_IN_PIN  = 32;   // Tombol untuk mode "in" (aktif LOW)
const int BTN_OUT_PIN = 33;   // Tombol untuk mode "out" (aktif LOW)
const unsigned long BTN_DEBOUNCE_MS = 50;  // Delay debouncing untuk tombol (50ms)

// ======= Tombol: state (global) =======
// Status tombol dan pembacaan terakhir untuk debouncing
int btnInState = HIGH;            
int btnOutState = HIGH;
int lastBtnInReading = HIGH;
int lastBtnOutReading = HIGH;
unsigned long lastBtnInChange = 0;
unsigned long lastBtnOutChange = 0;

// ========= LED CONFIG =========
const int LED_GREEN  = 13; // LED untuk indikator sukses
const int LED_RED    = 14; // LED untuk indikator error / stok habis

// Non-blocking blink struct
// Struktur untuk LED yang berkedip non-blocking (tidak menghalangi program lain berjalan)
struct LedBlink {
  int pin;
  bool active;          // sedang blinking
  unsigned long onMs;   // durasi ON (dalam milidetik)
  unsigned long offMs;  // durasi OFF (dalam milidetik)
  unsigned long lastToggle;  // Waktu terakhir LED toggled (diaktifkan/matikan)
  bool state;           // Status LED (ON/OFF)
  int repeat;           // Berapa kali siklus (on->off) (-1 = terus berulang)
  int cyclesDone;       // Menghitung siklus yang sudah dilakukan
};
LedBlink lbGreen = {LED_GREEN, false, 0, 0, 0, false, 0, 0};  // LED Hijau
LedBlink lbRed   = {LED_RED,   false, 0, 0, 0, false, 0, 0};  // LED Merah

// ================== DATA STRUCT ==================
// Struktur untuk menyimpan data barang di inventory
struct Item {
  String uid;   // UID dalam bentuk string (hex)
  String name;  // Nama barang (optional)
  int qty;      // Kuantitas barang
  uint8_t lastAction;  // Tindakan terakhir yang dilakukan (in/out)
};
Item inventory[100]; // Array untuk menyimpan data barang (maksimal 100 barang)
int itemCount = 0;   // Jumlah item dalam inventory

// ================== PROTOTYPES ==================
// Fungsi prototipe untuk menunjukkan pesan, menambah atau mengubah data, dan lainnya
void showMessage(const String &msg);
void beepSuccess();
void beepError();
void handleAdd(String id, int qty);
void handleSet(String id, int qty);
void handleScan(String id);
void showInventory();
String uidToString(MFRC522::Uid &uid);
void clearScanFlags(); // Reset lastAction pada semua item

// Tombol helper prototypes
void buttonsInit();
void buttonsUpdate();

// LED helper prototypes
void ledsInit();
void ledsUpdate();
void startBlink(LedBlink &lb, unsigned long onMs, unsigned long offMs, int repeats);
void ledSignalSuccess();
void ledSignalError();
void setModeLed(const String &m);

// =====================================================
// Setup awal (Inisialisasi perangkat keras dan konfigurasi)
void setup() {
  Serial.begin(115200);  // Memulai komunikasi serial untuk debug di monitor serial

  // Inisialisasi I2C untuk OLED dengan wiring yang telah ditentukan
  Wire.begin(25, 26);  // SDA -> GPIO25, SCL -> GPIO26

  // Inisialisasi SPI untuk RFID (gunakan pin default VSPI: SCK=18, MISO=19, MOSI=23)
  SPI.begin();

  // --- Setup OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("❌ OLED tidak terdeteksi. Cek wiring atau ubah address.");
    while (true);  // Jika OLED tidak terdeteksi, berhenti di sini
  }

  // --- Setup RFID ---
  mfrc522.PCD_Init();  // Inisialisasi RFID reader
  Serial.println("RFID RC522 siap, tempelkan kartu RFID...");
  mfrc522.PCD_DumpVersionToSerial();  // Tampilkan informasi versi RFID reader

  // --- Setup Buzzer ---
  pinMode(BUZZER, OUTPUT);  // Set buzzer sebagai output
  digitalWrite(BUZZER, LOW);  // Pastikan buzzer dalam kondisi mati

  // --- Setup Tombol ---
  buttonsInit();  // Inisialisasi tombol (mode in/out)

  // --- Setup LED ---
  ledsInit();  // Inisialisasi LED

  // --- Info awal di OLED ---
  display.clearDisplay();  // Bersihkan layar
  display.setTextSize(1);  // Ukuran teks 1x
  display.setTextColor(SSD1306_WHITE);  // Warna teks putih
  display.setCursor(0, 0);  // Posisi cursor di kiri atas
  display.println("System Ready!");
  display.display();  // Update layar OLED

  Serial.println("=== SISTEM PACKING & INVENTORY IoT ===");
  Serial.println("Mode: ketik di serial 'mode in' / 'mode out' atau gunakan tombol");
  Serial.println("Commands: add <UID> <qty> | set <UID> <qty> | reg <CODE> <qty> | list");
  Serial.println("======================================");
  showMessage("READY");  // Menampilkan pesan di layar OLED
}

// =====================================================
void loop() {
  ledsUpdate();  // Update status LED (non-blocking)

  buttonsUpdate();  // Update status tombol (mode in/out)

  // --- Cek input dari serial monitor ---
  if (Serial.available()) {
    String input = Serial.readStringUntil('\n');  // Membaca input dari serial monitor
    input.trim();  // Menghilangkan spasi di awal dan akhir

    if (input.length() > 0 && input.startsWith("mode ")) {
      mode = input.substring(5);  // Menetapkan mode berdasarkan input
      mode.trim();
      mode.toLowerCase();
      Serial.println("🔄 Mode diubah ke: " + mode);
      showMessage("MODE: " + mode);
      setModeLed(mode);  // Set LED berdasarkan mode
      clearScanFlags();
    }
    // Fungsi untuk menambah barang baru
    else if (input.startsWith("add ")) {
      int space1 = input.indexOf(' ');
      int space2 = input.indexOf(' ', space1 + 1);
      if (space2 != -1) {
        String id = input.substring(space1 + 1, space2);  // UID
        id.trim();
        int qty = input.substring(space2 + 1).toInt();  // Kuantitas
        handleAdd(id, qty);  // Menambahkan barang baru ke inventory
      }
    }
    // Fungsi untuk mengubah jumlah barang
    else if (input.startsWith("set ")) {
      int space1 = input.indexOf(' ');
      int space2 = input.indexOf(' ', space1 + 1);
      if (space2 != -1) {
        String id = input.substring(space1 + 1, space2);  // UID
        id.trim();
        int qty = input.substring(space2 + 1).toInt();  // Kuantitas
        handleSet(id, qty);  // Mengubah jumlah barang
      }
    }
    // Fungsi untuk menampilkan inventory
    else if (input.equalsIgnoreCase("list")) {
      showInventory();  // Menampilkan daftar inventory
    }
  }

  // --- Cek kartu RFID ---
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
    return;  // Tidak ada kartu terdeteksi, keluar dari loop

  // --- Handle scan UID ---
  String id = uidToString(mfrc522.uid);  // Mengonversi UID menjadi string
  lastScannedUID = id;  // Menyimpan UID terakhir yang dipindai

  Serial.print("Kartu Terdeteksi: ");
  Serial.println(id);

  ledSignalSuccess();  // Memberikan sinyal keberhasilan dengan LED

  mfrc522.PICC_DumpToSerial(&(mfrc522.uid));  // Menampilkan informasi UID kartu RFID

  handleScan(id);  // Menangani scan berdasarkan mode (in/out)

  mfrc522.PICC_HaltA();  // Menghentikan komunikasi dengan kartu RFID
}

// Fungsi untuk mengubah UID menjadi string
String uidToString(MFRC522::Uid &uid) {
  char buf[3];
  String s = "";
  for (byte i = 0; i < uid.size; i++) {
    sprintf(buf, "%02X", uid.uidByte[i]);  // Menyusun UID dalam format heksadesimal
    s += String(buf);
  }
  return s;  // Mengembalikan UID sebagai string
}

// Fungsi untuk membersihkan status terakhir pada setiap item
void clearScanFlags() {
  for (int i = 0; i < itemCount; i++) {
    inventory[i].lastAction = 0;
  }
}

// Fungsi untuk menambahkan barang baru ke inventory
void handleAdd(String id, int qty) {
  if (itemCount >= (int)(sizeof(inventory)/sizeof(inventory[0]))) {
    Serial.println("❌ Inventory penuh!");
    beepError();
    return;
  }

  for (int i = 0; i < itemCount; i++) {
    if (inventory[i].uid == id) {
      Serial.println("⚠ Barang sudah ada! Gunakan 'set' untuk ubah stok.");
      beepError();
      return;
    }
  }

  inventory[itemCount].uid = id;
  inventory[itemCount].name = "";  // Tidak ada nama untuk item baru
  inventory[itemCount].qty = qty;
  inventory[itemCount].lastAction = 0;
  itemCount++;
  Serial.println("✅ Barang ditambahkan: " + id + " (" + String(qty) + ")");
  showMessage("ADD: " + id);
  beepSuccess();
}

// Fungsi untuk mengubah jumlah barang dalam inventory
void handleSet(String id, int qty) {
  for (int i = 0; i < itemCount; i++) {
    if (inventory[i].uid == id) {
      inventory[i].qty = qty;
      inventory[i].lastAction = 0;
      Serial.println("✅ Stok diubah: " + id + " = " + String(qty));
      showMessage("SET: " + id);
      beepSuccess();
      return;
    }
  }
  Serial.println("❌ Barang tidak ditemukan!");
  showMessage("NOT FOUND");
  beepError();
}

// Fungsi untuk menangani scan berdasarkan mode (in/out)
void handleScan(String id) {
  for (int i = 0; i < itemCount; i++) {
    if (inventory[i].uid == id) {
      String label = (inventory[i].name.length() ? (inventory[i].name + " (" + inventory[i].uid + ")") : inventory[i].uid);
      if (mode == "in") {
        if (inventory[i].lastAction == 1) {
          Serial.println("⚠ Barang sudah terscan masuk: " + label);
          showMessage("SUDAH SCAN IN");
          beepError();
        } else {
          inventory[i].qty++;
          inventory[i].lastAction = 1;
          Serial.println("✅ Masuk: " + label + " | total: " + String(inventory[i].qty));
          showMessage("IN: " + (inventory[i].name.length() ? inventory[i].name : inventory[i].uid));
          beepSuccess();
        }
      } else if (mode == "out") {
        if (inventory[i].lastAction == 2) {
          Serial.println("⚠ Barang sudah terscan keluar: " + label);
          showMessage("SUDAH SCAN OUT");
          beepError();
        } else {
          if (inventory[i].qty > 0) {
            inventory[i].qty--;
            inventory[i].lastAction = 2;
            Serial.println("✅ Keluar: " + label + " | sisa: " + String(inventory[i].qty));
            showMessage("OUT: " + (inventory[i].name.length() ? inventory[i].name : inventory[i].uid));
            beepSuccess();
          } else {
            Serial.println("❌ Stok habis: " + label);
            showMessage("EMPTY: " + (inventory[i].name.length() ? inventory[i].name : inventory[i].uid));
            beepError();
          }
        }
      } else {
        Serial.println("⚠ Mode belum ditentukan!");
        showMessage("SET MODE!");
        beepError();
      }
      return;
    }
  }

  // Jika barang tidak ditemukan
  Serial.println("❌ Barang tidak terdaftar!");
  showMessage("UNKNOWN ITEM");
  beepError();
}

// Fungsi untuk menampilkan daftar inventory di serial monitor
void showInventory() {
  Serial.println("\n=== DAFTAR INVENTORY ===");
  for (int i = 0; i < itemCount; i++) {
    String displayName = inventory[i].name.length() ? inventory[i].name + " (" + inventory[i].uid + ")" : inventory[i].uid;
    String actionNote = "";
    if (inventory[i].lastAction == 1) actionNote = " [SCANNED IN]";
    else if (inventory[i].lastAction == 2) actionNote = " [SCANNED OUT]";
    Serial.println(displayName + " : " + String(inventory[i].qty) + actionNote);
  }
  Serial.println("=========================\n");
  showMessage("SHOW INV");
}
