#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "Sua_rede_aqui";
const char* password = "A_senha_da_rede_aquu";

String serverName = "https://script.google.com/macros/s/copie_e_cole_o_link_do_script_aqui/exec";

int sensorPin = 34;

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado!");
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    int leitura = analogRead(sensorPin);

    // Data/hora local (sem RTC, só timestamp aproximado do ESP32)
    String dataHora = String(millis() / 1000);

    HTTPClient http;
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"dataHora\":\"" + dataHora + "\",\"valorADC\":" + String(leitura) + "}";
    int httpResponseCode = http.POST(payload);

    Serial.print("Resposta: ");
    Serial.println(httpResponseCode);

    http.end();
  } else {
    Serial.println("WiFi desconectado!");
  }

  delay(60000); // envia a cada 1 min
}
