#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Adafruit_BME280.h>
#include <EEPROM.h>

// Capteurs
Adafruit_BME280 bme1;
Adafruit_BME280 bme2;

// WiFi Access Point
const char *ssid = "Felix_UPMR";
const char *password = "12345azer!";

ESP8266WebServer server(80);

// Utilisateurs
struct Utilisateur {
  const char* login;
  const char* password;
  bool admin;
};

Utilisateur utilisateurs[] = {
  {"alice", "passalice", true},
  {"bob", "passbob", false},
  {"carol", "passcarol", false}
};
const int nbUtilisateurs = sizeof(utilisateurs) / sizeof(utilisateurs[0]);

// Session
String sessionUser = "";
bool sessionAdmin = false;

// Mesures compressées
struct MesureCompressee {
  uint8_t temperature1, humidity1, pressure1;
  uint8_t temperature2, humidity2, pressure2;
};

int maxMesures = 300;
int mesuresParHeure = 30;
MesureCompressee *mesures;
int nbMesures = 0;
unsigned long dernierTempsMesure = 0;

// EEPROM pour sauvegarde du dernier effaceur
#define EEPROM_SIZE 64
#define EEPROM_ADDR_LOGIN 0
String dernierEffaceur = "";

// Utilitaires EEPROM
void writeStringToEEPROM(int addr, const String &str) {
  int len = str.length();
  EEPROM.write(addr, len);
  for (int i = 0; i < len; i++) {
    EEPROM.write(addr + 1 + i, str[i]);
  }
  EEPROM.commit();
}

String readStringFromEEPROM(int addr) {
  int len = EEPROM.read(addr);
  char data[EEPROM_SIZE];
  for (int i = 0; i < len && i < EEPROM_SIZE - 1; i++) {
    data[i] = EEPROM.read(addr + 1 + i);
  }
  data[len] = '\0';
  return String(data);
}

// Compression / décompression
uint8_t compress(float value, float minVal, float maxVal) {
  return constrain(round((value - minVal) * 255.0 / (maxVal - minVal)), 0, 255);
}

float decompress(uint8_t compressed, float minVal, float maxVal) {
  return compressed * (maxVal - minVal) / 255.0 + minVal;
}

// Mesures
void prendreMesureEtStocker() {
  if (nbMesures >= maxMesures) return;

  float t1 = bme1.readTemperature();
  float h1 = bme1.readHumidity();
  float p1 = bme1.readPressure() / 100.0F;

  float t2 = bme2.readTemperature();
  float h2 = bme2.readHumidity();
  float p2 = bme2.readPressure() / 100.0F;

  if (isnan(t1) || isnan(h1) || isnan(p1)) return;
  if (isnan(t2) || isnan(h2) || isnan(p2)) return;

  mesures[nbMesures++] = {
    compress(t1, 10, 50), compress(h1, 0, 100), compress(p1, 800, 1100),
    compress(t2, 10, 50), compress(h2, 0, 100), compress(p2, 800, 1100)
  };
}

// Authentification
bool verifierLogin(const String& login, const String& password, bool &admin) {
  for (int i = 0; i < nbUtilisateurs; i++) {
    if (login == utilisateurs[i].login && password == utilisateurs[i].password) {
      admin = utilisateurs[i].admin;
      return true;
    }
  }
  return false;
}

// Pages
void handleRoot() {
  String html = "<h1>Serveur de Mesures</h1>";
  html += "<p><a href='/csv'>Télécharger CSV</a></p>";
  if (sessionUser != "") {
    html += "<p>Connecté en tant que : <b>" + sessionUser + "</b> [<a href='/logout'>Logout</a>]</p>";
    if (sessionAdmin) html += "<p><a href='/config'>Configurer</a> | <a href='/clear'>Effacer les mesures</a></p>";
  } else {
    html += "<p><a href='/login'>Login</a></p>";
  }
  if (dernierEffaceur != "") {
    html += "<p><em>Dernier effacement par : <b>" + dernierEffaceur + "</b></em></p>";
  }
  html += "<h2>Mesures reçues</h2><ul>";
  for (int i = 0; i < nbMesures; i++) {
    float t1 = decompress(mesures[i].temperature1, 10, 50);
    float h1 = decompress(mesures[i].humidity1, 0, 100);
    float t2 = decompress(mesures[i].temperature2, 10, 50);
    float h2 = decompress(mesures[i].humidity2, 0, 100);
    html += "<li>T1: " + String(t1, 1) + " H1: " + String(h1, 1) + " | T2: " + String(t2, 1) + " H2: " + String(h2, 1) + "</li>";
  }
  html += "</ul>";
  server.send(200, "text/html", html);
}

void handleLogin() {
  String html = "<h2>Connexion</h2><form method='POST'><input name='login'><input name='password' type='password'><input type='submit' value='Login'></form>";
  server.send(200, "text/html", html);
}

void handleLoginPost() {
  String login = server.arg("login");
  String password = server.arg("password");
  bool admin;
  if (verifierLogin(login, password, admin)) {
    sessionUser = login;
    sessionAdmin = admin;
    server.sendHeader("Location", "/");
    server.send(303);
  } else {
    server.send(401, "text/plain", "Identifiants invalides");
  }
}

void handleLogout() {
  sessionUser = "";
  sessionAdmin = false;
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleClear() {
  if (!sessionAdmin) {
    server.send(403, "text/plain", "Non autorisé");
    return;
  }
  String html = "<h2>Confirmer suppression</h2><form method='POST'><input type='submit' value='Effacer'></form>";
  server.send(200, "text/html", html);
}

void handleClearPost() {
  if (!sessionAdmin) {
    server.send(403, "text/plain", "Non autorisé");
    return;
  }
  nbMesures = 0;
  dernierEffaceur = sessionUser;
  writeStringToEEPROM(EEPROM_ADDR_LOGIN, dernierEffaceur);
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleCSV() {
  String csv = "T1,H1,P1,T2,H2,P2\n";
  for (int i = 0; i < nbMesures; i++) {
    csv += String(decompress(mesures[i].temperature1, 10, 50)) + ",";
    csv += String(decompress(mesures[i].humidity1, 0, 100)) + ",";
    csv += String(decompress(mesures[i].pressure1, 800, 1100)) + ",";
    csv += String(decompress(mesures[i].temperature2, 10, 50)) + ",";
    csv += String(decompress(mesures[i].humidity2, 0, 100)) + ",";
    csv += String(decompress(mesures[i].pressure2, 800, 1100)) + "\n";
  }
  server.send(200, "text/csv", csv);
}

void handleConfig() {
  if (!sessionAdmin) {
    server.send(403, "text/plain", "Non autorisé");
    return;
  }
  String html = "<h2>Configuration</h2><form method='POST'>";
  html += "Max mesures: <input name='max' value='" + String(maxMesures) + "'><br>";
  html += "Mesures/heure: <input name='rate' value='" + String(mesuresParHeure) + "'><br>";
  html += "<input type='submit' value='Valider'></form>";
  server.send(200, "text/html", html);
}

void handleConfigPost() {
  if (!sessionAdmin) {
    server.send(403, "text/plain", "Non autorisé");
    return;
  }
  maxMesures = server.arg("max").toInt();
  mesuresParHeure = server.arg("rate").toInt();
  delete[] mesures;
  mesures = new MesureCompressee[maxMesures];
  nbMesures = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);
  dernierEffaceur = readStringFromEEPROM(EEPROM_ADDR_LOGIN);
  mesures = new MesureCompressee[maxMesures];

  WiFi.softAP(ssid, password);

  if (!bme1.begin(0x76)) Serial.println("Erreur BME1");
  if (!bme2.begin(0x77)) Serial.println("Erreur BME2");

  server.on("/", handleRoot);
  server.on("/login", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLoginPost);
  server.on("/logout", handleLogout);
  server.on("/csv", handleCSV);
  server.on("/clear", HTTP_GET, handleClear);
  server.on("/clear", HTTP_POST, handleClearPost);
  server.on("/config", HTTP_GET, handleConfig);
  server.on("/config", HTTP_POST, handleConfigPost);

  server.begin();
}

void loop() {
  server.handleClient();
  if (millis() - dernierTempsMesure > (3600000UL / mesuresParHeure)) {
    prendreMesureEtStocker();
    dernierTempsMesure = millis();
  }
}
