// --- Bibliothèques ---
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <Adafruit_BME280.h>
#include <Wire.h>

// --- Configuration WiFi (AP) ---
const char* ssid = "Felix_UPMR";
const char* password = "12345azer!";

// --- Configuration utilisateurs ---
struct User {
  const char* username;
  const char* password;
};

User users[] = {
  {"pm", "40964"},
  {"bob", "abcd"},
  {"charlie", "pass"}
};
const int USER_COUNT = sizeof(users) / sizeof(users[0]);

// --- Configuration serveur ---
ESP8266WebServer server(80);

// --- EEPROM ---
#define EEPROM_SIZE 64
#define EEPROM_ADDR_LOGIN 0
String dernierEffaceur = "";

// --- Capteurs BME280 ---
Adafruit_BME280 bme1;
Adafruit_BME280 bme2;

// --- Nom de l'appareil ---
const String nomEsp = "patate";

// --- Structure de mesure ---
struct MesureCompressee {
  uint8_t temperature1, humidity1, pressure1;
  uint8_t temperature2, humidity2, pressure2;
};

const int MAX_MESURES = 300;
MesureCompressee mesuresCompress[MAX_MESURES];
String mesuresHorodatage[MAX_MESURES];
int nbMesures = 0;

// --- Paramètres modifiables ---
int maxMesures = 300;
int mesuresParHeure = 30;
unsigned long dernierTempsMesure = 0;

// --- Utilitaires EEPROM ---
void writeStringToEEPROM(int addr, const String &str) {
  int len = str.length();
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) EEPROM.write(addr + 1 + i, str[i]);
  EEPROM.commit();
}

String readStringFromEEPROM(int addr) {
  int len = EEPROM.read(addr);
  char data[EEPROM_SIZE];
  for (int i = 0; i < len && i < EEPROM_SIZE - 1; i++) data[i] = EEPROM.read(addr + 1 + i);
  data[len] = '\0';
  return String(data);
}

// --- Authentification ---
bool authenticate(const String& user, const String& pass) {
  for (int i = 0; i < USER_COUNT; i++) {
    if (user == users[i].username && pass == users[i].password) return true;
  }
  return false;
}

// --- Heure simulée ---
String getHorodatage() {
  unsigned long secondes = millis() / 1000;
  int h = (secondes / 3600) % 24;
  int m = (secondes / 60) % 60;
  int s = secondes % 60;
  return String(h) + ":" + String(m) + ":" + String(s);
}

// --- Prendre mesure ---
void prendreMesure() {
  if (nbMesures >= maxMesures) return;

  float t1 = bme1.readTemperature();
  float h1 = bme1.readHumidity();
  float p1 = bme1.readPressure() / 100.0F;

  float t2 = bme2.readTemperature();
  float h2 = bme2.readHumidity();
  float p2 = bme2.readPressure() / 100.0F;

  if (isnan(t1) || isnan(h1) || isnan(p1) || isnan(t2) || isnan(h2) || isnan(p2)) return;

  mesuresCompress[nbMesures] = {
    compress(t1, 10, 50), compress(h1, 0, 100), compress(p1, 800, 1100),
    compress(t2, 10, 50), compress(h2, 0, 100), compress(p2, 800, 1100)
  };
  mesuresHorodatage[nbMesures] = getHorodatage();
  nbMesures++;
}

uint8_t compress(float value, float minVal, float maxVal) {
  return constrain(round((value - minVal) * 255.0 / (maxVal - minVal)), 0, 255);
}

float decompress(uint8_t compressed, float minVal, float maxVal) {
  return compressed * (maxVal - minVal) / 255.0 + minVal;
}

// --- Pages Web ---
String handleCsvOutput() {
  String csv = "id;temperature;humidity;pressure;timestamp\n";
  for (int i = 0; i < nbMesures; i++) {
    String timestamp = mesuresHorodatage[i];
    csv += nomEsp + "_bme1;" +
           String(decompress(mesuresCompress[i].temperature1, 10, 50), 1) + ";" +
           String(decompress(mesuresCompress[i].humidity1, 0, 100), 1) + ";" +
           String(decompress(mesuresCompress[i].pressure1, 800, 1100), 1) + ";" +
           timestamp + "\n";

    csv += nomEsp + "_bme2;" +
           String(decompress(mesuresCompress[i].temperature2, 10, 50), 1) + ";" +
           String(decompress(mesuresCompress[i].humidity2, 0, 100), 1) + ";" +
           String(decompress(mesuresCompress[i].pressure2, 800, 1100), 1) + ";" +
           timestamp + "\n";
  }
  return csv;
}

void handleRoot() {
  String html = "<h1>ESP Web Logger</h1>";
  html += "<p>Mesures enregistrées : " + String(nbMesures) + "</p>";
  html += "<p>Dernier effaceur : " + dernierEffaceur + "</p>";
  html += "<form method='POST' action='/clear'><input name='user'><input name='pass' type='password'><button>Effacer</button></form>";
  html += "<form method='POST' action='/config'><input name='user'><input name='pass' type='password'><br>Max: <input name='max'><br>Freq: <input name='freq'><button>Configurer</button></form>";
  html += "<p><a href='/csv'>Télécharger CSV</a></p>";
  server.send(200, "text/html", html);
}

void handleCsv() {
  server.send(200, "text/csv", handleCsvOutput());
}

void handleClearPost() {
  String user = server.arg("user");
  String pass = server.arg("pass");
  if (authenticate(user, pass)) {
    nbMesures = 0;
    dernierEffaceur = user;
    writeStringToEEPROM(EEPROM_ADDR_LOGIN, user);
    server.send(200, "text/plain", "Mesures effacées par " + user);
  } else {
    server.send(403, "text/plain", "Accès refusé");
  }
}

void handleConfigPost() {
  String user = server.arg("user");
  String pass = server.arg("pass");
  if (authenticate(user, pass)) {
    maxMesures = server.arg("max").toInt();
    mesuresParHeure = server.arg("freq").toInt();
    server.send(200, "text/plain", "Configuration mise à jour");
  } else {
    server.send(403, "text/plain", "Accès refusé");
  }
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  dernierEffaceur = readStringFromEEPROM(EEPROM_ADDR_LOGIN);

  Wire.begin();
  if (!bme1.begin(0x76)) Serial.println("Erreur capteur BME1");
  if (!bme2.begin(0x77)) Serial.println("Erreur capteur BME2");

  WiFi.softAP(ssid, password);

  server.on("/", handleRoot);
  server.on("/csv", handleCsv);
  server.on("/clear", HTTP_POST, handleClearPost);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - dernierTempsMesure > (3600000UL / mesuresParHeure)) {
    prendreMesure();
    dernierTempsMesure = millis();
  }
}
