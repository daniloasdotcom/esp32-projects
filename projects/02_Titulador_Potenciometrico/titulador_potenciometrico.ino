/*
  Código final atualizado: ESP32 + painel web com gráficos + tabela + último envio + gráfico de última média enviada.
*/
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <time.h>
#include <WebServer.h>
#include <ESP32Servo.h>

Preferences preferences;

// ================= VARIAVEIS SERVO =================
Servo servoMotor;      // cria o objeto servo
int servoPos = 90;     // posição inicial (centro/fechado)
#define SERVO_PIN 18   // pino de sinal do servo
int servoStep = 90;    // passo padrão em graus por clique
int servoOpenPos = 45; // Nova: Posição de abertura (esquerda)
int servoClosePos = 90; // Nova: Posição de fechamento (direita/centro)

int anguloAberturaPequena = 2; // Para diferença <= 0.5 (Pulso mais curto)
int anguloAberturaMedia = 5;   // Para diferença > 0.5 e <= 1.0 (Pulso médio)
int anguloAberturaGrande = 10; // Para diferença > 1.0 (Pulso mais longo)

bool iniciouComServo = false;  // Flag para saber como o usuário iniciou a leitura

// ================= VARIAVEIS TEMPORIZADOR SERVO =================
unsigned long servoTimerStart = 0;
unsigned long servoTimerDuration = 5000; // Duração padrão: 5 segundos (em ms)
bool servoPulseActive = false; // Indica se o pulso de teste está ativo

// Estados do servo durante o pulso
enum { SERVO_STANDBY, SERVO_OPENING, SERVO_OPEN, SERVO_CLOSING } servoState = SERVO_STANDBY;


// ================= CONFIGURAÇÕES =================
const char* ssidList[] = {"rede_exemplo_01", "senha_01"};
const char* passwordList[] = {"rede_exemplo_02", "senha_02"};
const int numNetworks = 2;
const char* serverURL = "https://script.google.com/macros/s/cole_aqui_o_link_do_script/exec";
const unsigned long intervaloEnvio = 5000;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -10800; // GMT-3
const int   daylightOffset_sec = 0; 

bool lastSendSuccess = false; // true = sucesso, false = erro

// IP fixo (válido apenas na rede principal)
IPAddress local_IP(192, 168, 1, 50);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(8, 8, 8, 8);   // Google DNS
IPAddress secondaryDNS(8, 8, 4, 4); // Google DNS secundário

// pinos
#define PH_SENSOR_PIN 34
#define BUTTON_PIN 15
#define ONE_WIRE_BUS 4

// calibração pH
const float PH_SLOPE = -3.9682;
float calibration_value = 16.5237;

// LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

// DS18B20
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// estados
// estados GERAIS DA MÁQUINA DE ESTADOS (contém estados globais)
enum { 
    AGUARDANDO_INICIO, 
    LENDO_SENSORES, 
    ENVIANDO_DADOS,       // ESTADO FALTANTE, USADO NO LOOP E ENVIARDADOS
    CONECTANDO_WIFI,      // ESTADO FALTANTE, USADO NO CONECTARWIFI
    CALIBRANDO, 
    ESTABILIZADO 
} estadoAtual;

// modos DE LEITURA ESPECÍFICOS (define como os sensores serão lidos)
enum { 
    MODO_BUSCA_PERFEITA, 
    MODO_TESTE_OSCILACAO,
    MODO_CONFIRMACAO_FINAL, // MODO FALTANTE, USADO NA LÓGICA DE ESTABILIDADE
    MODO_BUSCA_COM_SERVO    // NOVO MODO
} modoLeitura;

// temporizadores
unsigned long tempoUltimoEnvio = 0;
unsigned long tempoUltimaLeituraTemp = 0;
const unsigned long intervaloLeituraTemp = 1000;

// buffers / estabilidade
const int TAMANHO_BUFFER_ESTAB = 45;
const int LEITURAS_ADAPTATIVAS = 10; 
const int TAMANHO_CONFIRMACAO = 10;
float phBuffer[TAMANHO_BUFFER_ESTAB];
float tempBuffer[TAMANHO_BUFFER_ESTAB];
float voltBuffer[TAMANHO_BUFFER_ESTAB];
int indiceBuffer = 0;
bool bufferCompleto = false;

const float VAR_MAXIMA_PERFEITA = 0.12;
const float VAR_MAXIMA_OSCILACAO = 0.30;

int contagemEstabilidade = 0;
const int ESTABILIDADE_MAX_REQUERIDA = 3;

// Kalman
const float kalman_Q = 0.001;
float kalman_R = 0.10;
float kalman_P = 1.0;
float kalman_X = 0;

// leituras atuais
float ph_atual = 0.0;
float temp_atual = 0.0;
int totalLeituras = 0;

// confirmação final
float phConfirmacao[TAMANHO_CONFIRMACAO];
float tempConfirmacao[TAMANHO_CONFIRMACAO];
int indiceConfirmacao = 0;
float minPh45 = 0;
float maxPh45 = 0;
float minAdaptativo = 0;   // NOVO: limite inferior adaptativo
float maxAdaptativo = 0;   // NOVO: limite superior adaptativo

// último envio
float lastSentPh = 0.0;
float lastSentTemp = 0.0;
float lastSentVolt = 0.0;
unsigned long lastSentTimestamp = 0;
float lastSentPhBuffer[TAMANHO_BUFFER_ESTAB];
float lastSentTempBuffer[TAMANHO_BUFFER_ESTAB];
float lastSentVoltBuffer[TAMANHO_BUFFER_ESTAB];
int lastSentBufferSize = 0;

// Variável para armazenar o pH alvo
float phAlvo = 7.0; 

// Variáveis para armazenar as médias estáveis antes do pulso
float phEstavel = 0.0;
float tempEstavel = 0.0;
float voltEstavel = 0.0;

// Web server
WebServer server(80);

// Controle de reset para frontend
bool needResetForFrontend = false; // sinaliza para /data retornar reset=true uma vez
int prevModoLeitura = -1; // para detectar transição de modo


void salvarConfiguracao() {
  preferences.begin("ph_config", false); // Abre as preferências no namespace "ph_config" (R/W)
  
  // Salva o pH alvo
  preferences.putFloat("ph_alvo", phAlvo);
  
  // Salva outras variáveis de servo se necessário
  preferences.putInt("servo_open", servoOpenPos);
  preferences.putInt("servo_close", servoClosePos);
  preferences.putULong("servo_time", servoTimerDuration);

  preferences.putInt("abertura_peq", anguloAberturaPequena);
  preferences.putInt("abertura_med", anguloAberturaMedia);
  preferences.putInt("abertura_gde", anguloAberturaGrande);

  preferences.end(); // Fecha as preferências
  Serial.println("Configuracao salva no Flash.");
}

// ================= FUNÇÕES AUX =================
void resetKalman(float novo_R) {
  kalman_R = novo_R;
  kalman_P = 1.0;
}

float kalmanFilterVoltage(float raw_value) {
  kalman_P = kalman_P + kalman_Q;
  float K = kalman_P / (kalman_P + kalman_R);
  kalman_X = kalman_X + K * (raw_value - kalman_X);
  kalman_P = kalman_P - K * kalman_P;
  return kalman_X;
}

float lerTensaoKalman() {
  int adc_raw = analogRead(PH_SENSOR_PIN);
  float raw_volt = (float)adc_raw * 3.3 / 4095.0;
  return kalmanFilterVoltage(raw_volt);
}

float calcularVariacao(float buffer[], int size) {
  float min_val = buffer[0];
  float max_val = buffer[0];
  for (int i = 0; i < size; i++) {
    if (buffer[i] < min_val) min_val = buffer[i];
    if (buffer[i] > max_val) max_val = buffer[i];
  }
  return max_val - min_val;
}

float calcularMedia(float buffer[], int size) {
  float soma = 0;
  for (int i = 0; i < size; i++) soma += buffer[i];
  return soma / size;
}

// ================= FUNÇÕES AUX =================
void conectarWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;
  estadoAtual = CONECTANDO_WIFI;
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("Conectando WiFi");

  bool connected = false;

  for (int i = 0; i < numNetworks; i++) {
    Serial.print("Tentando conectar a: ");
    Serial.println(ssidList[i]);

    // 1. Desconecta de qualquer tentativa anterior
    WiFi.disconnect(); 
    delay(100); // Pequena pausa para garantir a limpeza do estado

    // 2. Configuração de IP: DHCP ou IP Fixo
    if (String(ssidList[i]) == "MINEIROS") {
      // Configura IP fixo para MINEIROS
      if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
        Serial.println("Falha ao configurar IP fixo");
      }
    } else {
      // Configura DHCP para outras redes (Lab Solos)
      // Usar IPAddress(0,0,0,0) ou INADDR_NONE para configurar para DHCP
      WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0));
      // Ou a forma mais limpa de configurar DHCP no ESP32:
      // WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); 
    }

    // 3. Inicia a tentativa de conexão
    WiFi.begin(ssidList[i], passwordList[i]);
    
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
      delay(500);
      lcd.setCursor(15,1);
      lcd.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      Serial.print("Conectado em: ");
      Serial.println(ssidList[i]);
      break;
    } else {
      Serial.print("Falha em conectar a: ");
      Serial.println(ssidList[i]);
      // IMPORTANTE: Se falhar, desconecta imediatamente antes da próxima iteração
      WiFi.disconnect(true); // O 'true' limpa as credenciais atuais
    }
  }

  // O restante da função permanece o mesmo (configuração de tempo, mDNS, etc.)

  if (connected) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi Conectado!");

    if (MDNS.begin("esp32")) {
      Serial.println("mDNS iniciado: http://esp32.local");
    } else {
      Serial.println("Falha ao iniciar mDNS");
    }

  } else {
    Serial.println("Falha em todas redes.");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi Falhou!");
  }
  
  // Teste de DNS e retorno de estado
  IPAddress testIP;
  if(WiFi.hostByName("script.google.com", testIP)) {
      Serial.print("DNS OK! IP script.google.com: ");
      Serial.println(testIP);
  } else {
      Serial.println("Falha DNS!");
  }

  estadoAtual = AGUARDANDO_INICIO;
}

// ================= FUNÇÃO ENVIAR DADOS =================
bool enviarDados(float phMedia, float tempMedia, float voltMedia, unsigned long timestamp_ms) {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
    return false;
  }

  HTTPClient http;
  http.begin(serverURL);
  http.setTimeout(15000);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<200> doc;
  doc["timestamp"] = timestamp_ms;
  doc["ph"] = phMedia;
  doc["temp"] = tempMedia;
  doc["volt"] = voltMedia;

  String jsonStr;
  serializeJson(doc, jsonStr);

  lcd.setCursor(0,0);
  lcd.print("Enviando Dados..");
  lcd.setCursor(0,1);
  lcd.print("pH: "); lcd.print(phMedia, 2);

  int httpResponseCode = http.POST(jsonStr);

  if (httpResponseCode > 0) {
    Serial.print("POST SUCESSO: "); Serial.println(httpResponseCode);

    lastSentPh = phMedia;
    lastSentTemp = tempMedia;
    lastSentVolt = voltMedia;
    lastSentTimestamp = timestamp_ms;

    // SALVA O BUFFER QUE ORIGINOU O ENVIO
    if (modoLeitura == MODO_CONFIRMACAO_FINAL) {
      for (int i=0;i<TAMANHO_CONFIRMACAO;i++){
        lastSentPhBuffer[i] = phConfirmacao[i];
        lastSentTempBuffer[i] = tempConfirmacao[i];
        lastSentVoltBuffer[i] = voltBuffer[i]; // tensão média do buffer adaptativo
      }
      lastSentBufferSize = TAMANHO_CONFIRMACAO;
    } else {
      for (int i=0;i<TAMANHO_BUFFER_ESTAB;i++){
        lastSentPhBuffer[i] = phBuffer[i];
        lastSentTempBuffer[i] = tempBuffer[i];
        lastSentVoltBuffer[i] = voltBuffer[i];
      }
      lastSentBufferSize = TAMANHO_BUFFER_ESTAB;
    }

    lastSendSuccess = true;
    http.end();
    return true;
  } else {
    Serial.print("ERRO POST: "); Serial.println(httpResponseCode);
    lastSendSuccess = false;
    http.end();
    return false;
  }
}

void carregarConfiguracao() {
  preferences.begin("ph_config", true); // Abre as preferências em modo R (somente leitura)
  
  // Carrega o pH alvo, usando 7.0 como padrão se não for encontrado
  phAlvo = preferences.getFloat("ph_alvo", 7.0); 
  
  // Carrega outras variáveis de servo
  servoOpenPos = preferences.getInt("servo_open", 45);
  servoClosePos = preferences.getInt("servo_close", 90);
  servoTimerDuration = preferences.getULong("servo_time", 5000);

  // --- NOVAS LINHAS PARA CARREGAR OS ÂNGULOS ---
  anguloAberturaPequena = preferences.getInt("abertura_peq", 2);
  anguloAberturaMedia = preferences.getInt("abertura_med", 5);
  anguloAberturaGrande = preferences.getInt("abertura_gde", 10);

  preferences.end();
  Serial.printf("Configuracao carregada: pH Alvo = %.2f\n", phAlvo);
}

// ================= WEB: Handlers =================
void handleRoot() {
  // Página HTML com gráficos, buffers, último envio e gráfico de última média enviada
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor ESP32</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;margin:20px;text-align:center;background:#f5f7fb;color:#222}
h1{font-size:24px;margin-bottom:20px}
button{padding:15px 30px;margin:20px;font-size:18px;border:none;border-radius:8px;cursor:pointer;color:#fff}
#btnPH{background:#4CAF50} 
#btnServo{background:#2196F3} 
</style>
</head>
<body>
<h1>Escolha a Aplicação</h1>
<button id="btnPH" onclick="window.location.href='/ph'">Medidas de pH</button>
<button id="btnServo" onclick="window.location.href='/servo'">Servo motor</button>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

void handlePhPage() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Monitor pH & Temp</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@2"></script>
<style>
/* Reset básico para melhor controle de layout */
* {
    box-sizing: border-box;
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}
body {
    margin: 0; /* Remover margem padrão */
    padding: 20px;
    background-color: #eef2f7; /* Um cinza/azul claro suave */
    color: #333;
    text-align: center;
}
h1 {
    font-size: 24px;
    margin: 0 0 15px;
    color: #1a4f78; /* Azul escuro principal */
}
.logo-container {
    margin-bottom: 20px;
}
.logo {
    max-width: 180px;
    height: auto;
}
#btnBack {
    background-color: #6c757d; /* Cinza para botão de voltar */
    color: white;
    padding: 8px 14px;
    border-radius: 6px;
    cursor: pointer;
    border: none;
    margin-bottom: 15px;
    transition: background-color 0.3s;
}
#btnBack:hover {
    background-color: #5a6268;
}

/* --- Layout Principal: CSS Grid para responsividade e estrutura --- */
.dashboard-grid {
    display: grid;
    gap: 20px; /* Espaço entre os cards */
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
}

.card {
    background: #ffffff;
    padding: 20px;
    border-radius: 12px;
    box-shadow: 0 4px 10px rgba(0, 0, 0, 0.05); /* Sombra mais suave */
    text-align: left;
}

/* Estilização das métricas de tempo real */
.metric {
    padding: 5px 0;
    font-size: 16px;
    border-bottom: 1px solid #eee;
}
.metric:last-child {
    border-bottom: none;
}
.metric strong {
    color: #1a4f78;
    display: inline-block;
    width: 120px;
}
.metric span {
    font-weight: bold;
    font-size: 18px;
    color: #28a745; /* Cor de destaque para valores */
}
.small {
    font-size: 12px;
    color: #6c757d;
    margin-top: 10px;
    text-align: center;
}

/* Botões de Controle */
.main-buttons {
    margin-top: 20px;
    padding: 15px;
    display: flex;
    flex-direction: column;
    gap: 10px;
}
.main-buttons button { 
    padding: 12px; 
    font-size: 16px; 
    border-radius: 8px; 
    cursor: pointer; 
    transition: background-color 0.3s, box-shadow 0.3s;
    box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
}
.btn-start { background-color: #28a745; color: white; } /* Verde - Iniciar Leitura */
.btn-servo { background-color: #007bff; color: white; } /* Azul - Monitoramento c/ Servo */
.btn-stop { background-color: #dc3545; color: white; }  /* Vermelho - Parar */

.btn-start:hover { background-color: #218838; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }
.btn-servo:hover { background-color: #0056b3; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }
.btn-stop:hover { background-color: #c82333; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }

/* Gráficos */
.chart-container {
    display: flex;
    gap: 15px;
    flex-wrap: wrap;
    justify-content: space-around;
}
.chart-item {
    flex: 1;
    min-width: 300px;
    max-width: 100%;
}
canvas {
    background: #fff;
    border-radius: 8px;
    border: 1px solid #eee;
}

/* Tabela */
h3 {
    margin: 6px 0 10px;
    color: #1a4f78;
    border-bottom: 2px solid #eee;
    padding-bottom: 5px;
}
.table-scroll {
    max-height: 800px; /* Altura máxima da tabela */
    overflow-y: auto;
    border: 1px solid #ddd;
    border-radius: 6px;
}
table {
    border-collapse: separate; /* Permite border-radius e espaçamento */
    width: 100%;
    font-size: 12px;
    border-spacing: 0;
}
th, td {
    border: none;
    padding: 8px 5px;
    text-align: center;
}
th {
    background-color: #f8f9fa;
    color: #495057;
    font-weight: 600;
    position: sticky; /* Cabeçalho fixo ao rolar */
    top: 0;
    z-index: 10;
}
#tableBuffers tbody tr:nth-child(even) {
    background-color: #f2f2f2; /* Efeito zebrado */
}
#tableBuffers tbody tr:hover {
    background-color: #e9ecef;
}

/* Responsividade para telas menores */
@media (max-width: 600px) {
    body {
        padding: 10px;
    }
    h1 {
        font-size: 20px;
    }
    .card {
        padding: 15px;
    }
    .dashboard-grid {
        gap: 15px;
    }
    .chart-container {
        flex-direction: column;
    }
    .main-buttons {
        flex-direction: row; /* Botões lado a lado em telas pequenas */
        flex-wrap: wrap;
        justify-content: center;
    }
    .main-buttons button {
        flex-grow: 1;
    }
}
</style>
</head>
<body>
<div class="logo-container">
  <img class="logo" 
       src="https://codigoagro.com/wp-content/uploads/2024/05/logo-300x70.png" 
       alt="Logo da Código Agro">
</div>
<h1>Monitoramento em Tempo Real - pH & Temperatura</h1>
<button id="btnBack" onclick="window.location.href='/'">← Voltar</button>

<div class="dashboard-grid">
  
  <div class="card" style="grid-column: span 1; min-width: 260px;">
    <h3>Métricas Atuais</h3>
    <div class="metrics">
        <div class="metric"><strong>pH atual:</strong> <span id="ph_now">--</span></div>
        <div class="metric"><strong>Temperatura:</strong> <span id="temp_now">--</span> °C</div>
        <div class="metric"><strong>pH Alvo:</strong> <span id="ph_alvo_now">--</span></div>
        <div class="metric"><strong>Tensão:</strong> <span id="volt_now">--</span> V</div>
        <div class="metric"><strong>Modo:</strong> <span id="modo_now">--</span></div>
        <div class="metric"><strong>Estabilidade:</strong> <span id="est_now">--</span></div>
    </div>
    <div class="small">Atualiza a cada 2s</div>
    
    <div class="main-buttons">
        <button class="btn-start" onclick="iniciarLeitura('/start')">Iniciar Leitura</button>
        <button class="btn-servo" onclick="iniciarLeitura('/start_servo_mode')">Monitoramento c/ Servo</button>
        <button class="btn-stop" onclick="pararLeitura()">Parar Leitura</button>
    </div>
  </div>

  <div class="card" style="grid-column: span 2; min-width: 320px;">
    <h3>Gráficos de Leitura</h3>
    <div class="chart-container">
        <div class="chart-item"><canvas id="chartPH" height="160"></canvas></div>
        <div class="chart-item"><canvas id="chartTemp" height="160"></canvas></div>
    </div>
  </div>

  <div class="card" style="grid-column: span 1;">
    <h3>Buffers (Últimas 45 Leituras)</h3>
    <div class="table-scroll">
      <table id="tableBuffers">
        <thead>
          <tr><th>Índice</th><th>pH</th><th>Temp</th><th>Volt</th></tr>
        </thead>
        <tbody id="buffersBody"></tbody>
      </table>
    </div>
  </div>

  <div class="card" style="grid-column: span 1; min-width: 260px;">
    <h3>Status de Estabilidade</h3>
    <div class="metric"><strong>Modo:</strong> <span id="mode_text">--</span></div>
    <div class="metric"><strong>Min pH 45:</strong> <span id="minp">--</span></div>
    <div class="metric"><strong>Max pH 45:</strong> <span id="maxp">--</span></div>
    <div class="metric"><strong>ΔpH (max-min):</strong> <span id="deltap">--</span></div>
    <div class="metric"><strong>Confirmações:</strong> <span id="conf_count">0</span>/10</div>

    <h3 style="margin-top: 15px;">Último Envio (Google Sheets)</h3>
    <div class="metric"><strong>Data/Hora:</strong> <span id="lastSentTime">--</span></div>
    <div class="metric"><strong>pH:</strong> <span id="lastSentPh">--</span></div>
    <div class="metric"><strong>Temperatura:</strong> <span id="lastSentTemp">--</span> °C</div>
    <div class="metric"><strong>Tensão:</strong> <span id="lastSentVolt">--</span> V</div>
    <div class="metric"><strong>Status:</strong> <span id="sendStatus">--</span></div>
  </div>
  
  <div class="card" style="grid-column: span 2; min-width: 320px;">
    <h3>Distribuição da Última Média Enviada</h3>
    <canvas id="chartLastSent" height="120"></canvas>
    <div id="statsLastSent" class="small" style="margin-top:10px;"></div>
  </div>

</div>

<script>
let phChart, tempChart, lastSentChart;
const maxPoints = 60;
const updateInterval = 2000;

function iniciarLeitura(url) {
    fetch(url)
    .then(response => {
        if (response.ok) {
            console.log('Leitura iniciada com sucesso. Modo: ' + url);
        } else {
            response.text().then(text => alert('Erro ao iniciar: ' + text));
        }
    })
    .catch(error => {
        alert('Erro de comunicação: ' + error);
    });
}
// DENTRO DO BLOCO <script> DA FUNÇÃO handlePhPage()

function pararLeitura() {
    fetch('/stop')
    .then(response => {
        if (response.ok) {
            alert("Leitura parada. Sistema resetado.");
        } else {
            response.text().then(text => alert('Erro ao parar: ' + text));
        }
    })
    .catch(error => {
        alert('Falha de comunicação ao tentar parar: ' + error);
    });
}

function createCharts(){
  const ctxPH = document.getElementById('chartPH').getContext('2d');
  phChart = new Chart(ctxPH, {
    type:'line',
    data:{labels:[],datasets:[{label:'pH',data:[],tension:0.2,pointRadius:1,borderWidth:2}]},
    options:{animation:false,scales:{y:{beginAtZero:false}}}
  });
  const ctxTemp = document.getElementById('chartTemp').getContext('2d');
  tempChart = new Chart(ctxTemp, {
    type:'line',
    data:{labels:[],datasets:[{label:'Temperatura (°C)',data:[],tension:0.2,pointRadius:1,borderWidth:2}]},
    options:{animation:false,scales:{y:{beginAtZero:false}}}
  });
  const ctxLast = document.getElementById('chartLastSent').getContext('2d');
  lastSentChart = new Chart(ctxLast, {
    type:'bar',
    data:{
      labels:[], 
      datasets:[{
        label:'Última Média pH',
        data:[],
        backgroundColor:'rgba(75, 192, 192, 0.6)',
        borderColor:'rgba(75, 192, 192, 1)',
        borderWidth:1
      }]
    },
    options:{
      animation:false,
      scales:{y:{beginAtZero:false},x:{title:{display:true,text:"Índice"}}},
      plugins:{annotation:{annotations:{}}}
    }
  });
}

function clearFrontendData(){
  phChart.data.labels = [];
  phChart.data.datasets[0].data = [];
  phChart.update();
  tempChart.data.labels = [];
  tempChart.data.datasets[0].data = [];
  tempChart.update();
  lastSentChart.data.labels = [];
  lastSentChart.data.datasets[0].data = [];
  lastSentChart.options.plugins.annotation.annotations = {};
  lastSentChart.update();
  document.getElementById('buffersBody').innerHTML = '';
  document.getElementById('statsLastSent').innerHTML = '';
}

function shiftAndPush(chart, label, value){
  chart.data.labels.push(label);
  chart.data.datasets[0].data.push(value);
  if (chart.data.labels.length > maxPoints){
    chart.data.labels.shift();
    chart.data.datasets[0].data.shift();
  }
  chart.update();
}

function updatePage(){
  fetch('/data')
    .then(r => r.json())
    .then(json => {
      if (json.reset === true) clearFrontendData();

      document.getElementById('ph_now').innerText = json.ph.toFixed(2);
      document.getElementById('temp_now').innerText = json.temp.toFixed(2);
      document.getElementById('ph_alvo_now').innerText = json.phAlvo.toFixed(2);
      document.getElementById('volt_now').innerText = json.volt.toFixed(3);
      document.getElementById('modo_now').innerText = json.modo_text;
      document.getElementById('est_now').innerText = json.contagemEstabilidade + '/' + json.estabilidadeRequerida;

      document.getElementById('minp').innerText = (json.minPh45===null?'--':json.minPh45.toFixed(3));
      document.getElementById('maxp').innerText = (json.maxPh45===null?'--':json.maxPh45.toFixed(3));
      document.getElementById('deltap').innerText = (json.deltaPh===null?'--':json.deltaPh.toFixed(3));
      document.getElementById('conf_count').innerText = json.indiceConfirmacao;

      // gráficos
      const ts = new Date().toLocaleTimeString();
      shiftAndPush(phChart, ts, json.ph);
      shiftAndPush(tempChart, ts, json.temp);

      // tabela buffers
      const body = document.getElementById('buffersBody');
      body.innerHTML = '';
      if (Array.isArray(json.phBuffer)){
        for (let i=0;i<json.phBuffer.length;i++){
          const tr = document.createElement('tr');
          const tdIdx = document.createElement('td'); tdIdx.innerText = i;
          const tdPh = document.createElement('td'); tdPh.innerText = (json.phBuffer[i]===null?'--':json.phBuffer[i].toFixed(3));
          const tdT = document.createElement('td'); tdT.innerText = (json.tempBuffer[i]===null?'--':json.tempBuffer[i].toFixed(2));
          const tdV = document.createElement('td'); tdV.innerText = (json.voltBuffer[i]===null?'--':json.voltBuffer[i].toFixed(3));
          tr.appendChild(tdIdx); tr.appendChild(tdPh); tr.appendChild(tdT); tr.appendChild(tdV);
          body.appendChild(tr);
        }
      }

      // último envio Google Sheets
      if (json.lastSentTimestamp && json.lastSentTimestamp != 0) {
        const date = new Date(json.lastSentTimestamp);
        document.getElementById('lastSentTime').innerText = date.toLocaleString();
        document.getElementById('lastSentPh').innerText = json.lastSentPh.toFixed(2);
        document.getElementById('lastSentTemp').innerText = json.lastSentTemp.toFixed(2);
        document.getElementById('lastSentVolt').innerText = json.lastSentVolt.toFixed(3);
      } else {
        document.getElementById('lastSentTime').innerText = '--';
        document.getElementById('lastSentPh').innerText = '--';
        document.getElementById('lastSentTemp').innerText = '--';
        document.getElementById('lastSentVolt').innerText = '--';
      }

      const statusEl = document.getElementById('sendStatus');
      statusEl.innerText = json.lastSendSuccess ? "Sucesso" : "Erro";
      statusEl.style.color = json.lastSendSuccess ? "green" : "red";

      // gráfico última média enviada + estatísticas
      if (Array.isArray(json.lastSentPhBuffer)){
        const data = json.lastSentPhBuffer.filter(v=>v!==null);
        lastSentChart.data.labels = data.map((_,i)=>i);
        lastSentChart.data.datasets[0].data = data;

        if(data.length>0){
          const mean = data.reduce((a,b)=>a+b,0)/data.length;
          const std = Math.sqrt(data.reduce((a,b)=>a+Math.pow(b-mean,2),0)/data.length);
          const min = Math.min(...data);
          const max = Math.max(...data);
          const cv = std/mean;
          const skew = (data.reduce((a,b)=>a+Math.pow((b-mean)/std,3),0)/data.length).toFixed(2);
          const kurt = (data.reduce((a,b)=>a+Math.pow((b-mean)/std,4),0)/data.length -3).toFixed(2);
          const normalityText = (Math.abs(skew)<0.5 && Math.abs(kurt)<1) ? "Aproximadamente normal" : "Não normal";

          document.getElementById('statsLastSent').innerHTML =
            `Média: ${mean.toFixed(3)} | Min: ${min.toFixed(3)} | Max: ${max.toFixed(3)} | `+
            `Desvio Padrão: ${std.toFixed(3)} | CV: ${cv.toFixed(2)} | ${normalityText}`;

          lastSentChart.options.plugins.annotation.annotations = {
            line1: { type:'line', yMin:mean, yMax:mean, borderColor:'red', borderWidth:2,
                     label:{content:'Média',enabled:true,position:'start'} }
          };
        } else lastSentChart.options.plugins.annotation.annotations = {};
        lastSentChart.update();
      }

    })
    .catch(err => console.error('Erro fetch /data', err));
}

window.onload = function() {
  createCharts();
  updatePage();
  setInterval(updatePage, updateInterval);

  // --- Slider do passo do servo ---
  const stepSlider = document.getElementById('servoStepSlider');
  const stepValueLabel = document.getElementById('stepValue');

  // Atualiza o texto ao mover o slider
  stepSlider.oninput = function() {
    stepValueLabel.innerText = this.value;
  };

  // Envia o valor escolhido ao ESP32 quando soltar o slider
  stepSlider.onchange = function() {
    fetch('/servo/step?value=' + this.value)
      .then(r => r.text())
      .then(t => console.log('Servo step atualizado para', this.value))
      .catch(e => console.error('Erro ao enviar passo do servo:', e));
  };
};

function startLeitura(){
  fetch('/start')
    .then(r => {
      if(r.ok) console.log("Leitura iniciada!");
      else console.warn("Erro ao iniciar leitura.");
    })
    .catch(e => console.error("Falha de comunicação:", e));
}

function servoLeft(){
  fetch('/servo/left')
    .then(r => r.text())
    .then(t => console.log(t))
    .catch(e => console.error("Erro:", e));
}

function servoRight(){
  fetch('/servo/right')
    .then(r => r.text())
    .then(t => console.log(t))
    .catch(e => console.error("Erro:", e));
}

function servoCenter(){
  fetch('/servo/step?value=90')
    .then(r => r.text())
    .then(t => console.log("Servo Centro:", t))
    .catch(e => console.error("Erro:", e));
}


</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", page);
}

void handleServoPage() {
    // 1. Aloca memória dinamicamente no HEAP (em vez da STACK) para 16KB.
    const size_t bufferSize = 16384; 
    char* html_buffer = (char*)malloc(bufferSize);

    if (html_buffer == NULL) {
        Serial.println("ERRO: Falha ao alocar memoria para a pagina HTML.");
        server.send(500, "text/plain", "Erro interno: Memoria insuficiente para pagina web.");
        return;
    }

    // 2. Usamos snprintf para formatar a string HTML, injetando os valores atuais das variáveis C++
    int len = snprintf(html_buffer, bufferSize, R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Controle Servo Bureta</title>
<style>
* {
    box-sizing: border-box;
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
}
body{
    margin: 0;
    padding: 20px;
    text-align: center;
    background:#eef2f7; /* Fundo suave */
    color:#333;
}
h1{
    font-size: 24px;
    margin-bottom: 20px;
    color: #1a4f78;
}
h2 {
    font-size: 18px;
    color: #007bff;
    margin-top: 0;
    margin-bottom: 15px;
    padding-bottom: 5px;
    border-bottom: 1px solid #eee;
}
button{
    padding:12px 25px;
    margin: 8px 4px;
    font-size:16px;
    border:none;
    border-radius:8px;
    cursor:pointer;
    color:#fff;
    transition: background-color 0.3s, box-shadow 0.3s;
    box-shadow: 0 2px 4px rgba(0,0,0,0.1);
}
button:hover {
    box-shadow: 0 4px 8px rgba(0,0,0,0.2);
}
#btnBack{background:#6c757d;}
#btnBack:hover {background:#5a6268;}

#btnLeft{background:#dc3545;} /* Vermelho para ABRIR (ácido) */
#btnLeft:hover {background:#c82333;}
#btnRight{background:#28a745;} /* Verde para FECHAR/Centro (neutro) */
#btnRight:hover {background:#218838;}

#btnPulse{background:#9C27B0; font-size:18px; padding:15px 30px;} /* Roxo para o Pulso */
#btnPulse:hover {background:#7c1a8a;}

.card{
    background:#fff;
    padding:20px;
    border-radius:12px;
    box-shadow:0 4px 10px rgba(0,0,0,0.05);
    margin-bottom:20px;
    text-align: left;
}
.current-pos {
    font-size: 18px;
    font-weight: bold;
    color: #007bff;
    margin-bottom: 15px;
    text-align: center;
}
.control-group {
    margin-top: 15px;
    padding-top: 10px;
    border-top: 1px dashed #eee;
}
label {
    display: block;
    margin-bottom: 5px;
    font-weight: 500;
}
input[type="range"] {
    width: 100%;
    margin: 10px 0;
}
input[type="number"] {
    padding: 8px;
    border: 1px solid #ccc;
    border-radius: 4px;
    width: 80px;
    text-align: center;
    margin-left: 10px;
}

/* Layout Responsivo: Grid para organização dos cards */
.servo-grid {
    display: grid;
    gap: 20px;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
}

@media (max-width: 768px) {
    .servo-grid {
        grid-template-columns: 1fr;
    }
}
</style>
</head>
<body>
<h1>Controle do Servo - Bureta Automatizada</h1>
<button id="btnBack" onclick="window.location.href='/'">← Voltar à Página Principal</button>

<div class="servo-grid">
    
    <div class="card">
        <h2>1. Calibração (Manual)</h2>
        <p>Ajuste as posições de ABERTURA e FECHAMENTO. Use o slider para encontrar o ponto exato.</p>
        
        <div class="current-pos">
            Posição Atual: <span id="currentPos">%d</span>°
        </div>
        
        <div style="text-align:center; margin-bottom: 15px;">
            <button id="btnLeft" onclick="servoLeft()">◀ DEFINIR ABRIR (Posição: %d°)</button>
            <button id="btnRight" onclick="servoRight()">DEFINIR FECHAR (Posição: %d°) ▶</button>
        </div>
        
        <div class="control-group">
            <label for="servoMoveSlider">Mover Servo Manualmente: <span id="posValue">%d</span>°</label>
            <input type="range" min="0" max="180" value="%d" id="servoMoveSlider">
        </div>
    </div>

    <div class="card">
        <h2>2. Pulso Único de Teste</h2>
        <p>Simula uma tiragem (pulso) completa: Abre o servo por um tempo e fecha automaticamente.</p>
        
        <div class="control-group">
            <label for="timeSlider">Tempo de Abertura (Tiragem): <span id="timeValue">%lu</span> segundos</label>
            <input type="range" min="1" max="60" value="%lu" id="timeSlider">
        </div>
        
        <div style="text-align:center; margin-top:20px">
            <button id="btnPulse" onclick="startPulse()">TESTE ÚNICO (Abrir por <span id="timeValueBtn">%lu</span>s)</button>
        </div>
    </div>

    <div class="card">
        <h2>3. Configuração de Automação</h2>
        
        <div class="control-group">
            <label for="phTargetInput">pH Alvo para Automação:</label>
            <input type="number" id="phTargetInput" min="4.0" max="10.0" step="0.01" value="%.2f" onchange="updatePhTarget(this.value)">
        </div>
        
        <p style="font-size:12px; color:#6c757d; margin-top:15px;">
            O sistema usará o Servo se o pH lido for inferior ao pH Alvo.
        </p>
    </div>

    <div class="card">
        <h2>4. Aberturas Proporcionais (Graus)</h2>
        <p>Ângulo em graus que o servo abrirá a partir da posição de FECHAMENTO (Ex: 90° - 5° = 85°).</p>
        
        <div style="display:flex; flex-direction:column; gap:10px; margin-top:15px;">
            <label>
                <strong>ΔpH &le; 0.5:</strong>
                <input type="number" id="angleSmallInput" min="1" max="90" step="1" value="%d" data-config-key="angle_small" onchange="updateAngle('angle_small', this.value)"> graus
            </label>
            <label>
                <strong>0.5 &lt; ΔpH &le; 1.0:</strong>
                <input type="number" id="angleMediumInput" min="1" max="90" step="1" value="%d" data-config-key="angle_medium" onchange="updateAngle('angle_medium', this.value)"> graus
            </label>
            <label>
                <strong>ΔpH &gt; 1.0:</strong>
                <input type="number" id="angleLargeInput" min="1" max="90" step="1" value="%d" data-config-key="angle_large" onchange="updateAngle('angle_large', this.value)"> graus
            </label>
        </div>
    </div>
    
</div>

<script>
const posSlider = document.getElementById('servoMoveSlider');
const posValueLabel = document.getElementById('posValue');
const currentPosLabel = document.getElementById('currentPos');
const timeSlider = document.getElementById('timeSlider');
const timeValueLabel = document.getElementById('timeValue');
const timeValueBtnLabel = document.getElementById('timeValueBtn');
const btnLeft = document.getElementById('btnLeft');
const btnRight = document.getElementById('btnRight');

// Função para atualizar os valores de ABRIR/FECHAR nos botões
function updateButtonPositions(openPos, closePos) {
    btnLeft.innerText = '◀ DEFINIR ABRIR (Posição: ' + openPos + '°)';
    btnRight.innerText = 'DEFINIR FECHAR (Posição: ' + closePos + '°) ▶';
}
updateButtonPositions(%d, %d); // Injeta a posição inicial nos botões

// --- Slider de Posição ---
posSlider.oninput = function() {
    posValueLabel.innerText = this.value;
    currentPosLabel.innerText = this.value;
};

posSlider.onchange = function() {
    fetch('/servo/move?pos=' + this.value)
      .then(r => r.text())
      .then(t => { 
          console.log('Posição movida:', t);
          // Tenta extrair a nova posição de ABRIR/FECHAR da resposta (se houver)
          const match = t.match(/\[OPEN: (\d+), CLOSE: (\d+)\]/);
          if (match) {
            updateButtonPositions(match[1], match[2]);
          }
      });
};

// --- Slider de Tempo ---
timeSlider.oninput = function() {
    timeValueLabel.innerText = this.value;
    timeValueBtnLabel.innerText = this.value;
};
timeSlider.onchange = function() {
    fetch('/servo/time?value=' + this.value)
      .then(r => r.text())
      .then(t => console.log('Tempo de pulso atualizado:', t));
};

// --- Função para atualizar o pH Alvo ---
function updatePhTarget(value) {
    const floatValue = parseFloat(value);
    if (floatValue >= 4.0 && floatValue <= 10.0) { 
        fetch('/servo/ph_target?value=' + floatValue)
        .then(r => r.text())
        .then(t => console.log('pH Alvo atualizado para', floatValue))
        .catch(e => console.error('Erro ao enviar pH alvo:', e));
    } else {
        alert('O pH Alvo deve estar entre 4.0 e 10.0');
    }
}

// --- NOVO: Função JavaScript para atualizar os ângulos ---
function updateAngle(key, value) {
    const angleValue = parseInt(value);
    if (angleValue >= 1 && angleValue <= 90) { 
        fetch('/servo/angle_config?key=' + key + '&value=' + angleValue)
        .then(r => r.text())
        .then(t => console.log(key + ' atualizado para', angleValue))
        .catch(e => console.error('Erro ao enviar ângulo:', e));
    } else {
        alert('O ângulo deve estar entre 1 e 90 graus.');
    }
}

// --- Funções de Controle ---
function servoLeft(){ 
    fetch('/servo/left')
    .then(r=>r.text())
    .then(t=>{ 
        console.log(t); 
        updateCurrentPos(t); 
        // Atualiza a posição de abertura no botão
        updateButtonPositions(posSlider.value, %d); // Assumindo %d é servoClosePos
    });
}

function servoRight(){ 
    fetch('/servo/right')
    .then(r=>r.text())
    .then(t=>{ 
        console.log(t); 
        updateCurrentPos(t); 
        // Atualiza a posição de fechamento no botão
        updateButtonPositions(%d, posSlider.value); // Assumindo %d é servoOpenPos
    });
}

function startPulse(){
    document.getElementById('btnPulse').disabled = true;
    document.getElementById('btnPulse').innerText = 'PULSO EM ANDAMENTO...';
    fetch('/servo/pulse')
        .then(r => {
            if (r.ok) {
                console.log("Pulso Único iniciado!");
                setTimeout(() => {
                    document.getElementById('btnPulse').disabled = false;
                    document.getElementById('btnPulse').innerText = 'TESTE ÚNICO (Abrir por ' + timeSlider.value + 's)';
                    // A lógica do ESP32 já retorna ao estado SERVO_STANDBY, mas forçamos a atualização visual da posição.
                    updateCurrentPos('FECHAR: %d'); 
                }, (timeSlider.value * 1000) + 1000); 
            } else {
                r.text().then(text => alert("Erro: " + text));
                document.getElementById('btnPulse').disabled = false;
                document.getElementById('btnPulse').innerText = 'TENTE NOVAMENTE';
            }
        })
        .catch(e => {
            console.error("Falha de comunicação:", e);
            document.getElementById('btnPulse').disabled = false;
            document.getElementById('btnPulse').innerText = 'ERRO DE COMUNICAÇÃO';
        });
}

function updateCurrentPos(response) {
    const match = response.match(/(\d+)/);
    if (match) {
        currentPosLabel.innerText = match[0];
        posSlider.value = match[0];
        posValueLabel.innerText = match[0];
    }
}
</script>
</body>
</html>
)rawliteral",
        // Parâmetros injetados na ordem dos placeholders (os %...)
        // Parâmetros da Seção 1 (Calibração)
        servoPos, servoOpenPos, servoClosePos, servoPos, servoPos, 
        
        // Parâmetros da Seção 2 (Pulso Único)
        servoTimerDuration / 1000, servoTimerDuration / 1000, servoTimerDuration / 1000,
        
        // Parâmetro da Seção 3 (pH Alvo)
        phAlvo,
        
        // Parâmetros da Seção 4 (Aberturas Proporcionais)
        anguloAberturaPequena, anguloAberturaMedia, anguloAberturaGrande,
        
        // NOVOS: Parâmetros injetados no JS para auxiliar a função updateButtonPositions
        servoOpenPos, servoClosePos, // Injeta o valor inicial nos botões
        servoClosePos, servoOpenPos, // Valores de fallback/ajuste no servoLeft/servoRight
        servoClosePos // Posição de fechamento para o pulso
    );
    
    // 3. Verifica se a string foi criada sem erro
    if (len < 0 || len >= bufferSize) {
        Serial.printf("ERRO: snprintf falhou ou buffer de %d bytes é insuficiente (precisa de %d).", bufferSize, len);
        server.send(500, "text/plain", "Erro de formatacao de pagina.");
        free(html_buffer);
        return;
    }

    // 4. Envia a string HTML formatada para o cliente
    server.send(200, "text/html", html_buffer);
    // 5. LIBERA a memória alocada no HEAP após o envio!
    free(html_buffer);
}

void handleData() {
  DynamicJsonDocument doc(8192);
  doc["ph"] = ph_atual;
  doc["temp"] = temp_atual;
  doc["volt"] = kalman_X;
  doc["phAlvo"] = phAlvo;

const char* modoStr = (modoLeitura == MODO_BUSCA_PERFEITA) ? "PERFEITO" :
                        (modoLeitura == MODO_CONFIRMACAO_FINAL) ? "ADAPTATIVO" :
                        (modoLeitura == MODO_BUSCA_COM_SERVO) ? "SERVO AUTOMÁTICO" : // <--- NOVO
                        "Aguardando botão"; // <--- Estado padrão
  doc["modo"] = modoLeitura;
  doc["modo_text"] = modoStr;
  doc["contagemEstabilidade"] = contagemEstabilidade;
  doc["estabilidadeRequerida"] = ESTABILIDADE_MAX_REQUERIDA;
  doc["indiceConfirmacao"] = indiceConfirmacao;

  if (minPh45 == 0 && maxPh45 == 0) {
    doc["minPh45"] = nullptr;
    doc["maxPh45"] = nullptr;
    doc["deltaPh"] = nullptr;
  } else {
    doc["minPh45"] = minPh45;
    doc["maxPh45"] = maxPh45;
    doc["deltaPh"] = (maxPh45 - minPh45);
  }

  // Flag reset: se needResetForFrontend true -> retorna reset=true E zera a flag (one-shot)
  doc["reset"] = needResetForFrontend ? true : false;
  if (needResetForFrontend) needResetForFrontend = false;

  // Arrays
  JsonArray arrPh = doc.createNestedArray("phBuffer");
  JsonArray arrTemp = doc.createNestedArray("tempBuffer");
  JsonArray arrVolt = doc.createNestedArray("voltBuffer");
  for (int i = 0; i < TAMANHO_BUFFER_ESTAB; i++) {
    if (phBuffer[i] == 99.0f) arrPh.add(JsonVariant()); else arrPh.add(phBuffer[i]);
    if (tempBuffer[i] == 0.0f) arrTemp.add(JsonVariant()); else arrTemp.add(tempBuffer[i]);
    if (voltBuffer[i] == 99.0f) arrVolt.add(JsonVariant()); else arrVolt.add(voltBuffer[i]);
  }

  // Último envio
  doc["lastSentPh"] = lastSentPh;
  doc["lastSentTemp"] = lastSentTemp;
  doc["lastSentVolt"] = lastSentVolt;
  doc["lastSentTimestamp"] = lastSentTimestamp;
  doc["lastSendSuccess"] = lastSendSuccess;

  JsonArray arrLastPh = doc.createNestedArray("lastSentPhBuffer");
  for (int i=0;i<lastSentBufferSize;i++){
    arrLastPh.add(lastSentPhBuffer[i]);
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

// ================= SETUP =================
// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  sensors.begin();

  // Inicialização do Buffer
  for (int i = 0; i < TAMANHO_BUFFER_ESTAB; i++) {
    phBuffer[i] = 99.0f;
    tempBuffer[i] = 0.0f;
    voltBuffer[i] = 99.0f;
  }

  // Inicialização do Kalman
  int adc_raw_init = analogRead(PH_SENSOR_PIN);
  kalman_X = (float)adc_raw_init * 3.3 / 4095.0;
  resetKalman(0.10);

  carregarConfiguracao();
  
  // Conexão WiFi
  conectarWiFi();

  // Rotas que devem vir ANTES de todas as outras rotas (Geral e JSON)
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/start", [](){
    if (estadoAtual == AGUARDANDO_INICIO) {
      iniciouComServo = false;
      indiceBuffer = 0;
      bufferCompleto = false;
      totalLeituras = 0;
      contagemEstabilidade = 0;
      modoLeitura = MODO_BUSCA_PERFEITA;
      resetKalman(0.10);
      for (int i = 0; i < TAMANHO_BUFFER_ESTAB; i++) {
        phBuffer[i] = 99.0;
        voltBuffer[i] = 99.0;
        tempBuffer[i] = 0.0;
      }
      needResetForFrontend = true;
      prevModoLeitura = modoLeitura;
      estadoAtual = LENDO_SENSORES;
      lcd.clear();
      Serial.println("\n*** INICIANDO MODO LENDO_SENSORES *** (via botão web)");
      server.send(200, "text/plain", "Leitura iniciada");
    } else {
      server.send(400, "text/plain", "Não pode iniciar agora");
    }
  });

  server.on("/start_servo_mode", [](){
    if (estadoAtual == AGUARDANDO_INICIO) {
      iniciouComServo = true;  // <- adiciona esta linha

      indiceBuffer = 0;
      bufferCompleto = false;
      totalLeituras = 0;
      contagemEstabilidade = 0;

      modoLeitura = MODO_BUSCA_COM_SERVO;
      resetKalman(0.10);
      for (int i = 0; i < TAMANHO_BUFFER_ESTAB; i++) {
        phBuffer[i] = 99.0;
        voltBuffer[i] = 99.0;
        tempBuffer[i] = 0.0;
      }
      needResetForFrontend = true;
      prevModoLeitura = modoLeitura;
      estadoAtual = LENDO_SENSORES;
      lcd.clear();
      Serial.println("\n*** INICIANDO MODO LENDO_SENSORES *** (via botão web - Modo Servo)");
      server.send(200, "text/plain", "Leitura e Monitoramento com Servo iniciados");
    } else {
      server.send(400, "text/plain", "Não pode iniciar agora");
    }
  });

  server.on("/stop", [](){
      if (estadoAtual == LENDO_SENSORES) {
          // Força a transição para o estado de espera
          estadoAtual = AGUARDANDO_INICIO; 
          
          // Opcional: Desliga o servo se estiver no meio de um pulso (segurança)
          servoPulseActive = false;
          servoState = SERVO_STANDBY;
          servoMotor.write(servoClosePos); // Fecha o servo

          Serial.println("\n*** LEITURA CANCELADA E PARADA ***");
          server.send(200, "text/plain", "Leitura parada e resetada.");
      } else {
          server.send(400, "text/plain", "Sistema já está parado.");
      }
  });
  
  // Inicializa o Servo
  servoMotor.attach(SERVO_PIN);
  servoMotor.write(servoPos);
  // --- Controle do servo: Movimento, Configuração e Pulso (ATUALIZADO) ---

  server.on("/servo/angle_config", HTTP_GET, []() {
      if (server.hasArg("key") && server.hasArg("value")) {
          String key = server.arg("key");
          int newValue = server.arg("value").toInt();
          
          if (newValue >= 1 && newValue <= 90) { // Validação de faixa de 1 a 10 graus
              if (key == "angle_small") {
                  anguloAberturaPequena = newValue;
              } else if (key == "angle_medium") {
                  anguloAberturaMedia = newValue;
              } else if (key == "angle_large") {
                  anguloAberturaGrande = newValue;
              } else {
                  server.send(400, "text/plain", "Chave de configuracao invalida.");
                  return;
              }
              salvarConfiguracao(); // Salva a alteração na memória Flash
              Serial.printf("Angulo de Abertura (%s) atualizado para %d\n", key.c_str(), newValue);
              server.send(200, "text/plain", "Angulo atualizado com sucesso.");
              return;
          }
      }
      server.send(400, "text/plain", "Parametros invalidos ou ausentes.");
  });
  server.on("/servo/ph_target", HTTP_GET, []() {
      if (server.hasArg("value")) {
          float newValue = server.arg("value").toFloat();
          if (newValue >= 4.0 && newValue <= 10.0) {
              
              // 1. Salva o valor na RAM
              phAlvo = newValue;
              
              // 2. Persiste o valor na memória não volátil (Flash/EEPROM)
              // CERTIFIQUE-SE DE QUE ESTA FUNÇÃO EXISTE E SALVA PHALVO
              // **DESCOMENTE ESTA LINHA**
              salvarConfiguracao(); 
              
              // 3. Imprime na Serial para feedback IMEDIATO (Sua confirmação visual)
              Serial.print("SUCCESS: Novo pH Alvo salvo na memória: ");
              Serial.println(phAlvo, 2); 
              
              // 4. Envia confirmação de volta para o navegador
              server.send(200, "text/plain", "pH alvo atualizado com sucesso. Valor: " + String(phAlvo, 2));
              return;
          }
      }
      server.send(400, "text/plain", "Parâmetro 'value' inválido ou ausente.");
  });
  // 1. Rota para definir o TEMPO de abertura (novo regulador)
  server.on("/servo/time", HTTP_GET, [](){
    if (!server.hasArg("value")) {
      server.send(400, "text/plain", "Parâmetro 'value' ausente");
      return;
    }
    int val = server.arg("value").toInt();
    val = constrain(val, 1, 60); // Limite de 1 a 60 segundos
    servoTimerDuration = (unsigned long)val * 1000; // Salva em milissegundos
    Serial.printf("Duração do Pulso atualizada: %d segundos\n", val);
    server.send(200, "text/plain", "Tempo atualizado para " + String(val) + "s");
  });

  // 2. Rota para iniciar o PULSO ÚNICO (novo botão de teste)
  server.on("/servo/pulse", [](){
    if (servoPulseActive) {
      server.send(400, "text/plain", "Pulso já em andamento.");
      return;
    }
    servoPulseActive = true;
    servoState = SERVO_OPENING;
    Serial.println("Pulso Único iniciado.");
    server.send(200, "text/plain", "Pulso Único iniciado.");
  });

  // 3. Rota de Movimento para Posição Específica (usada pelo slider)
  server.on("/servo/move", HTTP_GET, [](){
    if (!server.hasArg("pos")) {
      server.send(400, "text/plain", "Parâmetro 'pos' ausente");
      return;
    }
    int pos = server.arg("pos").toInt();
    pos = constrain(pos, 0, 180);
    servoMotor.write(pos);
    servoPos = pos;
    // O movimento manual do slider define as posições de ABRIR/FECHAR
    if (pos < servoClosePos) servoOpenPos = pos;
    if (pos > servoOpenPos) servoClosePos = pos;

    Serial.printf("Servo movido para %d graus. [OPEN: %d, CLOSE: %d]\n", pos, servoOpenPos, servoClosePos);
    server.send(200, "text/plain", "Servo movido para " + String(pos) + "°");
  });

  // 4. Rota para mover para esquerda (ABRIR)
  server.on("/servo/left", [](){
    servoPos = servoOpenPos; // Usa a posição de abertura calibrada
    servoMotor.write(servoPos);
    Serial.printf("Servo movido para esquerda (OPEN): %d\n", servoPos);
    server.send(200, "text/plain", "Esquerda (OPEN): " + String(servoPos));
  });

  // 5. Rota para mover para direita/centro (FECHAR)
  server.on("/servo/right", [](){
    servoPos = servoClosePos; // Usa a posição de fechamento calibrada
    servoMotor.write(servoPos);
    Serial.printf("Servo movido para direita (CLOSE): %d\n", servoPos);
    server.send(200, "text/plain", "Direita (CLOSE): " + String(servoPos));
  });

  // Rota /servo/step (mantida, pois é usada no handlePhPage também)
  server.on("/servo/step", HTTP_GET, [](){
    if (!server.hasArg("value")) {
      server.send(400, "text/plain", "Parâmetro 'value' ausente");
      return;
    }
    int val = server.arg("value").toInt();
    val = constrain(val, 1, 90); 
    servoStep = val;
    Serial.printf("Passo do servo atualizado: %d\n", servoStep);
    server.send(200, "text/plain", "Passo atualizado para " + String(servoStep) + "°");
  });
  
  // FIM: Controle do servo

  server.begin();
  Serial.print("Servidor Web pronto: ");
  Serial.println(WiFi.localIP());
  
  // --- Rotas de Páginas ---
  server.on("/ph", handlePhPage);       // Dashboard pH
  server.on("/servo", handleServoPage); // Controle do servo
}

// ================= LOOP =================
void loop() {
  server.handleClient();
  unsigned long agora = millis();
  bool estadoBotao = digitalRead(BUTTON_PIN);
  static bool ultimoEstadoBotao = HIGH;

  // --- Lógica de Pulso de Teste e Servo Automático (Não Bloqueante) ---
  if (servoPulseActive) {
    // Executa apenas a lógica do servo e ignora o resto do loop até o pulso terminar
    if (servoState == SERVO_OPENING) {
      servoMotor.write(servoOpenPos);
      servoTimerStart = agora;
      servoState = SERVO_OPEN;
      Serial.printf("Pulso Servo: Abrindo para %d graus.\n", servoOpenPos);
    } 
    else if (servoState == SERVO_OPEN) {
      if (agora - servoTimerStart >= servoTimerDuration) {
        servoState = SERVO_CLOSING;
      }
    } 
    else if (servoState == SERVO_CLOSING) {
      servoMotor.write(servoClosePos);
      servoState = SERVO_STANDBY;
      servoPulseActive = false;
      Serial.printf("Pulso Servo: Fechando para %d graus. Pulso concluído.\n", servoClosePos);

      if (modoLeitura == MODO_BUSCA_COM_SERVO) {
        Serial.println("Pulso concluído. Reiniciando automaticamente em modo Servo Automático...");
        indiceBuffer = 0;
        bufferCompleto = false;
        totalLeituras = 0;
        contagemEstabilidade = 0;
        resetKalman(0.10);
        needResetForFrontend = true;
        estadoAtual = LENDO_SENSORES;
      } else {
        estadoAtual = AGUARDANDO_INICIO;
      }
    }

    // ✅ IMPORTANTE: retorna imediatamente — nada mais do loop é executado enquanto o servo se move
    return;
  }


  // --- Botão físico para iniciar leitura padrão ---
  if (estadoBotao == LOW && ultimoEstadoBotao == HIGH) {
    delay(50);
    if (digitalRead(BUTTON_PIN) == LOW && estadoAtual == AGUARDANDO_INICIO) {
      iniciouComServo = false; // inicia sem servo se botão físico for usado
      indiceBuffer = 0;
      bufferCompleto = false;
      totalLeituras = 0;
      contagemEstabilidade = 0;
      modoLeitura = MODO_BUSCA_PERFEITA; // Modo padrão
      resetKalman(0.10);
      for (int i = 0; i < TAMANHO_BUFFER_ESTAB; i++) {
        phBuffer[i] = 99.0;
        voltBuffer[i] = 99.0;
        tempBuffer[i] = 0.0;
      }
      needResetForFrontend = true;
      prevModoLeitura = modoLeitura;
      estadoAtual = LENDO_SENSORES;
      lcd.clear();
      Serial.println("\n*** INICIANDO MODO LENDO_SENSORES *** (Botão Físico)");
    }
  }
  ultimoEstadoBotao = estadoBotao;

  // --- Máquina de Estados ---
  if (estadoAtual == AGUARDANDO_INICIO) {
    if (agora - tempoUltimaLeituraTemp >= intervaloLeituraTemp) {
      float volt = lerTensaoKalman();
      ph_atual = (PH_SLOPE * volt) + calibration_value;
      sensors.requestTemperatures();
      temp_atual = sensors.getTempCByIndex(0);
      tempoUltimaLeituraTemp = agora;

      lcd.setCursor(0,0);
      lcd.print("Aguardando Botao   ");
      lcd.setCursor(0,1);
      lcd.print("pH:");
      lcd.print(ph_atual,2);
      lcd.print(" T:");
      lcd.print(temp_atual,1);
      lcd.print((char)223);
      lcd.print("C  ");
    }
  }
  else if (estadoAtual == LENDO_SENSORES) {
    if (agora - tempoUltimaLeituraTemp >= intervaloLeituraTemp) {
      sensors.requestTemperatures();
      float tempC = sensors.getTempCByIndex(0);
      tempoUltimaLeituraTemp = agora;

      float volt = lerTensaoKalman();
      float ph_lido = (PH_SLOPE * volt) + calibration_value;

      ph_atual = ph_lido;
      temp_atual = tempC;

      phBuffer[indiceBuffer] = ph_lido;
      tempBuffer[indiceBuffer] = tempC;
      voltBuffer[indiceBuffer] = volt;
      totalLeituras++;
      indiceBuffer++;
      if (totalLeituras >= TAMANHO_BUFFER_ESTAB) bufferCompleto = true;
      if (indiceBuffer >= TAMANHO_BUFFER_ESTAB) indiceBuffer = 0;

      if (prevModoLeitura != modoLeitura) {
        needResetForFrontend = true;
        prevModoLeitura = modoLeitura;
        Serial.println("Modo mudou - solicitando reset frontend.");
      }

      // =================== Lógica de Estabilidade ===================
      if (bufferCompleto && indiceBuffer == 0) {
        float variacao_total_45 = calcularVariacao(phBuffer, TAMANHO_BUFFER_ESTAB);
        
        if (modoLeitura == MODO_BUSCA_PERFEITA || modoLeitura == MODO_BUSCA_COM_SERVO) {
          if (variacao_total_45 <= VAR_MAXIMA_PERFEITA) {
            contagemEstabilidade++;
          } else {
            contagemEstabilidade = 0;
          }

          if (contagemEstabilidade == 0 && variacao_total_45 <= VAR_MAXIMA_OSCILACAO) {
            minPh45 = phBuffer[0];
            maxPh45 = phBuffer[0];
            for (int i = 1; i < TAMANHO_BUFFER_ESTAB; i++) {
              if (phBuffer[i] < minPh45) minPh45 = phBuffer[i];
              if (phBuffer[i] > maxPh45) maxPh45 = phBuffer[i];
            }
            float diffPerfeito = maxPh45 - minPh45;
            float margem = (0.3 - diffPerfeito) / 2.0;

            minAdaptativo = minPh45 - margem;
            maxAdaptativo = maxPh45 + margem;
            
            resetKalman(0.30);
            indiceConfirmacao = 0;
            modoLeitura = MODO_CONFIRMACAO_FINAL;
            needResetForFrontend = true;
            prevModoLeitura = modoLeitura;
            Serial.println("\n>>> TRANSICAO PARA MODO ADAPTATIVO <<<");
          }
        }
      }
      else if (modoLeitura == MODO_CONFIRMACAO_FINAL) {
        if (ph_lido >= minAdaptativo && ph_lido <= maxAdaptativo) {
          phConfirmacao[indiceConfirmacao] = ph_lido;
          tempConfirmacao[indiceConfirmacao] = tempC;
          indiceConfirmacao++;

          if (indiceConfirmacao >= LEITURAS_ADAPTATIVAS) {
            contagemEstabilidade = ESTABILIDADE_MAX_REQUERIDA;
            modoLeitura = (decltype(modoLeitura))prevModoLeitura;
            Serial.printf("Confirmação Adaptativa SUCESSO. Retornando ao MODO: %d\n", modoLeitura);
          }
        } else {
          Serial.println("\n--- FALHA ADAPTATIVA: FORA DOS LIMITES ---");
          // ✅ Novo: usa a flag iniciouComServo para decidir o retorno correto
          if (iniciouComServo) {
            modoLeitura = MODO_BUSCA_COM_SERVO;
            Serial.println("RETORNANDO MODO SERVO AUTOMÁTICO (via flag de início)");
          } else {
            modoLeitura = MODO_BUSCA_PERFEITA;
            Serial.println("RETORNANDO MODO PERFEITO (modo leitura normal)");
          }
          resetKalman(0.10);
          contagemEstabilidade = 0;
          indiceConfirmacao = 0;
          needResetForFrontend = true;
          prevModoLeitura = modoLeitura;
        }
      }

      // Exibição
      lcd.setCursor(0,0);
      lcd.print("Estb. Janela: ");
      if (contagemEstabilidade < 10) {
        lcd.print(contagemEstabilidade);
        lcd.print("/");
        lcd.print(ESTABILIDADE_MAX_REQUERIDA);
      } else {
        lcd.print("OK       ");
      }
      lcd.setCursor(0,1);
      lcd.print("pH:");
      lcd.print(ph_lido,2);
      lcd.print(" T:");
      lcd.print(tempC,1);
      lcd.print((char)223);
      lcd.print("C  ");

      Serial.print("Modo: ");
      if (modoLeitura == MODO_BUSCA_PERFEITA) {
        Serial.print("PERFEITO | Estb: ");
        Serial.print(contagemEstabilidade);
      } else if (modoLeitura == MODO_CONFIRMACAO_FINAL) {
        Serial.print("ADAPTATIVO | Conf: ");
        Serial.print(indiceConfirmacao);
        Serial.print(" | Limites: [");
        Serial.print(minPh45,3);
        Serial.print(" a ");
        Serial.print(maxPh45,3);
        Serial.print("]");
      } else if (modoLeitura == MODO_BUSCA_COM_SERVO) {
        Serial.print("SERVO AUTOMÁTICO | Estb: ");
        Serial.print(contagemEstabilidade);
      } else {
        Serial.print("AGUARDANDO BOTAO");
      }
      Serial.print(" | pH Lido: ");
      Serial.println(ph_lido, 3);

      // ---------------- GATILHO FINAL PARA ENVIO ----------------
      if (contagemEstabilidade >= ESTABILIDADE_MAX_REQUERIDA) {
        if (modoLeitura == MODO_CONFIRMACAO_FINAL) {
          phEstavel = calcularMedia(phConfirmacao, ESTABILIDADE_MAX_REQUERIDA);
          tempEstavel = calcularMedia(tempConfirmacao, ESTABILIDADE_MAX_REQUERIDA);
          voltEstavel = calcularMedia(voltBuffer, TAMANHO_BUFFER_ESTAB);
        } else {
          phEstavel = calcularMedia(phBuffer, TAMANHO_BUFFER_ESTAB);
          tempEstavel = calcularMedia(tempBuffer, TAMANHO_BUFFER_ESTAB);
          voltEstavel = calcularMedia(voltBuffer, TAMANHO_BUFFER_ESTAB);
        }

        estadoAtual = ENVIANDO_DADOS;
      }
    }
  }
  else if (estadoAtual == ENVIANDO_DADOS) {
    time_t now_sec = time(NULL);
    unsigned long current_timestamp_ms = (unsigned long)now_sec * 1000;

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Media pH: ");
    lcd.print(phEstavel,2);
    lcd.setCursor(0,1);
    lcd.print("Enviando...");

    if (agora - tempoUltimoEnvio >= intervaloEnvio) {
      if (enviarDados(phEstavel, tempEstavel, voltEstavel, current_timestamp_ms)) {
        tempoUltimoEnvio = agora;
        // ================= NOVO: LÓGICA DE CONTROLE PROPORCIONAL DE ÂNGULO =================
        int angulo_abertura_decidido = 0;
        float ph_diferenca = phAlvo - phEstavel; // A diferença é positiva se o pH estiver baixo (ácido)

        Serial.print("pH Enviado: ");
        Serial.println(phEstavel, 3);
        Serial.print("pH Alvo: ");
        Serial.println(phAlvo, 3);
        Serial.printf("Diferença de pH (Alvo - Lido): %.3f\n", ph_diferenca);

        // Caso 1: Sistema iniciado com servo e pH menor que alvo
        if (iniciouComServo && ph_diferenca > 0.0) { // Se a diferença é positiva, o pH está baixo
          
            if (ph_diferenca <= 0.5) {
                angulo_abertura_decidido = anguloAberturaPequena;
                Serial.println("-> Acionamento Pequeno (diferença <= 0.5)");
            } else if (ph_diferenca <= 1.0) {
                angulo_abertura_decidido = anguloAberturaMedia;
                Serial.println("-> Acionamento Médio (0.5 < diferença <= 1.0)");
            } else { // ph_diferenca > 1.0
                angulo_abertura_decidido = anguloAberturaGrande;
                Serial.println("-> Acionamento Grande (diferença > 1.0)");
            }

            // A nova posição de abertura será a posição fechada menos o ângulo decidido
            // Exemplo: 90 (fechado) - 5 (ângulo) = 85 graus
            servoOpenPos = servoClosePos - angulo_abertura_decidido;
            servoOpenPos = constrain(servoOpenPos, 0, 180); // Garante que a posição é válida (0-180)
            
            Serial.printf("pH menor que o alvo. Acionando servo. Nova Posição de Abertura: %d graus\n", servoOpenPos);
          
            // Inicia o pulso com a nova posição de abertura (servoTimerDuration permanece o mesmo)
            servoPulseActive = true;
            servoState = SERVO_OPENING;
            modoLeitura = MODO_BUSCA_COM_SERVO; 
            estadoAtual = LENDO_SENSORES;
        }
        // ✅ Caso 2: Sistema iniciado com servo mas pH >= alvo (ou diferença negativa)
        else if (iniciouComServo) {
          Serial.println("pH maior/igual ao alvo. Mantendo modo Servo Automático.");
          modoLeitura = MODO_BUSCA_COM_SERVO;
          estadoAtual = LENDO_SENSORES;
        }
        // ✅ Caso 3: Iniciado sem servo
        else {
          Serial.println("Modo leitura normal (sem servo). Voltando para aguardando início.");
          estadoAtual = AGUARDANDO_INICIO;
        }

      } else {
        tempoUltimoEnvio = agora;
        lcd.setCursor(0,1);
        lcd.print("Erro Envio!");
        Serial.println(">>> Erro no envio <<<");
        estadoAtual = AGUARDANDO_INICIO;
      }
    }
  }

}
