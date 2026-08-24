#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>

// ====== CONFIGURÁVEIS ======
const char* WIFI_SSID = "sua_rede_aqui";
const char* WIFI_PASS = "a_senha_aqui";
const char* SCRIPT_URL = "https://script.google.com/macros/s/cole_aqui_o_link_do_script/exec"; // Web App URL

const int ADC_PIN = 34; // GPIO34 (entrada apenas)
const unsigned long SAMPLE_MS = 3000; // Intervalo de leitura (3 segundos)

// ====== VARIÁVEIS DE ESTADO DO SISTEMA ======
volatile bool running = false;         // Se a leitura está ativa ou parada
String currentSensor = "Sensor01";     // Sensor atualmente selecionado
unsigned long lastSample = 0;          // Último tempo de amostragem
int lastValue = -1;                    // Último valor lido
int readingsCount = 0;                 // Contador de leituras concluídas
bool useReadingsLimit = false;         // Se deve usar o limite de leituras
int readingsLimit = 5;                 // O valor do limite de leituras (padrão 5)

WebServer server(80);

const char* HTML = R"HTML(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>Sensor Capacitivo ESP32</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 24px; background-color: #f4f4f9; color: #333; }
    .container { max-width: 520px; margin: 20px auto; } 
    .card { padding: 20px; border: 1px solid #ddd; border-radius: 12px; background-color: #fff; box-shadow: 0 4px 8px rgba(0,0,0,0.1); }
    h2 { color: #333; margin-top: 0; text-align: center; } 
    button { padding: 10px 16px; margin-right: 8px; cursor: pointer; border: none; border-radius: 6px; transition: background-color 0.3s; }
    #startBtn { background-color: #4CAF50; color: white; }
    #startBtn:hover { background-color: #45a049; }
    #stopBtn { background-color: #f44336; color: white; }
    #stopBtn:hover { background-color: #d32f2f; }
    select, input[type="checkbox"] { padding: 8px; border: 1px solid #ccc; border-radius: 4px; }
    .row { margin: 12px 0; display: flex; align-items: center; }
    .row label { margin-right: 10px; }
    .badge { display:inline-block; padding:4px 8px; border-radius:8px; background:#eee; font-weight: bold; margin-left: 5px; }
    #state { font-weight: bold; color: #1e88e5; }
    #readingsInfo { margin-top: 15px; padding-top: 10px; border-top: 1px dashed #eee; }
    .logo { 
      display: block; 
      margin: 0 auto 20px auto; 
      max-width: 200px; 
      height: auto; 
    }
    /* NOVO ESTILO PARA O RODAPÉ */
    .footer {
      text-align: center;
      margin-top: 20px;
      font-size: 0.8em;
      color: #777;
    }
    .footer a {
      color: #1e88e5;
      text-decoration: none;
    }
    .footer a:hover {
      text-decoration: underline;
    }
  </style>
</head>
<body>
  <div class="container"> 
    <img src="https://codigoagro.com/wp-content/uploads/2024/05/logo-300x70.png" alt="Código Agro Logo" class="logo"> 
    <div class="card">
      <h2>Leitura do Sensor Capacitivo</h2>
      
      <div class="row">
        <label for="sensor">Sensor Atual:</label>
        <select id="sensor">
          <option>Sensor01</option>
          <option>Sensor02</option>
          <option>Sensor03</option>
          <option>Sensor04</option>
          <option>Sensor05</option>
        </select>
        <span id="sensorBadge" class="badge"></span>
      </div>
      
      <div class="row">
          <input type="checkbox" id="useLimit" onchange="updateLimitSettings()">
          <label for="useLimit">Limitar número de leituras a:</label>
          <select id="readingsLimit" onchange="updateLimitSettings()" disabled>
              <option value="1">1</option>
              <option value="2">2</option>
              <option value="5" selected>5</option>
              <option value="10">10</option>
              <option value="20">20</option>
          </select>
      </div>

      <div class="row">
        <button id="startBtn">Iniciar leitura</button>
        <button id="stopBtn">Parar leitura</button>
      </div>
      
      <div class="row">Estado: <strong id="state">-</strong></div>
      <div class="row">Último valor: <strong id="last">-</strong></div>
      
      <div id="readingsInfo">
          Total de Leituras Feitas: <strong id="count">0</strong>
          / Limite: <strong id="limitDisplay">∞</strong>
      </div>

    </div>
    
    <div class="footer">
      Desenvolvido por <a href="http://Daniloas.com" target="_blank">Danilo Andrade</a>
    </div>
    </div> 
<script>
// Função para buscar o status atual do ESP32
async function getStatus(){
  try {
    const r = await fetch('/status');
    const j = await r.json();
    
    // Atualiza estado e valor
    document.getElementById('state').textContent = j.running ? 'Ativa' : 'Parada';
    document.getElementById('last').textContent = j.lastValue ?? '-';
    
    // Atualiza seleção de sensor
    document.getElementById('sensor').value = j.sensor;
    document.getElementById('sensorBadge').textContent = j.sensor;
    
    // Atualiza contagem e limite
    document.getElementById('count').textContent = j.readingsCount;
    document.getElementById('useLimit').checked = j.useReadingsLimit;
    document.getElementById('readingsLimit').value = j.readingsLimit;

    // Controla o dropdown de limite (só fica ativo se o checkbox estiver marcado)
    document.getElementById('readingsLimit').disabled = !j.useReadingsLimit;
    
    // Mostra o limite na interface
    document.getElementById('limitDisplay').textContent = j.useReadingsLimit ? j.readingsLimit : '∞';

    // Lógica para controle do botão INICIAR (Permite iniciar se não estiver rodando)
    if(j.running) {
         document.getElementById('startBtn').disabled = true;
         document.getElementById('stopBtn').disabled = false;
         document.getElementById('state').style.color = '#4CAF50'; // Verde para ativo
    } else {
         document.getElementById('startBtn').disabled = false;
         document.getElementById('stopBtn').disabled = true;
         
         // Se parou por ter atingido o limite, muda o status visual
         if (j.useReadingsLimit && j.readingsCount >= j.readingsLimit) {
            document.getElementById('state').textContent = 'Concluída';
            document.getElementById('state').style.color = '#FFA000'; // Laranja para concluído
         } else {
            document.getElementById('state').textContent = 'Parada';
            document.getElementById('state').style.color = '#1e88e5'; // Azul para parada normal
         }
    }


  } catch(e) {
    console.error("Erro ao buscar status:", e);
    document.getElementById('state').textContent = 'ERRO';
    document.getElementById('state').style.color = '#d32f2f';
  }
}

// Função genérica para enviar comandos POST
async function post(path, data){
  await fetch(path, {
      method:'POST', 
      headers:{'Content-Type':'application/json'}, 
      body: JSON.stringify(data||{})
  });
  // Depois de cada POST, busca o novo status para atualizar a tela
  await getStatus();
}

// Envia as configurações de limite (checkbox e dropdown)
async function updateLimitSettings() {
    const useLimit = document.getElementById('useLimit').checked;
    const limitValue = document.getElementById('readingsLimit').value;
    
    // Habilita/desabilita o dropdown imediatamente
    document.getElementById('readingsLimit').disabled = !useLimit;
    
    await post('/setLimit', { 
        use: useLimit, 
        limit: parseInt(limitValue) 
    });
}

// Listeners de eventos
document.getElementById('startBtn').onclick = ()=> post('/start');
document.getElementById('stopBtn').onclick = ()=> post('/stop');
document.getElementById('sensor').onchange = (e)=> post('/setSensor', { name: e.target.value });

// Inicialização e atualização periódica
getStatus();
setInterval(getStatus, 2000);
</script>
</body>
</html>
)HTML";

// ===================================
// FUNÇÕES DE SERVIDOR WEB (HANDLERS)
// ===================================

void handleRoot(){ server.send(200, "text/html", HTML); }

void handleStatus(){
  String json = "{";
  json += "\"running\":"; json += (running?"true":"false");
  json += ",\"sensor\":\"" + currentSensor + "\"";
  json += ",\"lastValue\":" + String(lastValue);
  json += ",\"readingsCount\":" + String(readingsCount);
  
  // CORREÇÃO: Usar String::concat (o operador +=) com o literal de string
  json += ",\"useReadingsLimit\":"; 
  json += (useReadingsLimit?"true":"false");
  
  json += ",\"readingsLimit\":" + String(readingsLimit);
  json += "}";
  server.send(200, "application/json", json);
}

void handleStart(){ 
    // Só inicia se não estiver rodando
    if (!running) {
        // REGRA DE ZERAMENTO 2: Zera o contador sempre que a leitura é iniciada,
        // garantindo que o novo ciclo comece do zero.
        readingsCount = 0;
        Serial.println(">>> Leitura reiniciada. Contador zerado para novo ciclo.");
        
        running = true; 
        Serial.println(">>> Leitura iniciada (running = true)"); 
    }
    server.send(200, "text/plain", "OK"); 
}

void handleStop(){ 
    running = false; 
    server.send(200, "text/plain", "OK"); 
    Serial.println(">>> Leitura parada (running = false)");
}

void handleSetSensor(){
  if (server.hasArg("plain")){
    String body = server.arg("plain");
    // Lógica simples para extrair o valor de "name" do JSON
    int i = body.indexOf("\"name\"");
    if (i>=0){
      int q1 = body.indexOf('\"', body.indexOf(':', i)+1);
      int q2 = body.indexOf('\"', q1+1);
      if (q1>=0 && q2>q1) currentSensor = body.substring(q1+1, q2);
      Serial.print(">>> Sensor alterado para: ");
      Serial.println(currentSensor);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleSetLimit(){
    if (server.hasArg("plain")){
        String body = server.arg("plain");
        
        // Busca o valor 'use' (true/false)
        int i_use = body.indexOf("\"use\"");
        bool newUseLimit = useReadingsLimit;
        if (i_use >= 0) {
            if (body.indexOf("true", i_use) > 0) newUseLimit = true;
            else newUseLimit = false;
        }

        // REGRA DE ZERAMENTO 1: Se o limite foi desativado (era true, virou false), zera o contador.
        if (useReadingsLimit && !newUseLimit) {
            readingsCount = 0;
            Serial.println(">>> Limite DESATIVADO. Contador de leituras zerado.");
        }
        
        useReadingsLimit = newUseLimit; // Atualiza o estado global

        // Busca o valor 'limit'
        int i_limit = body.indexOf("\"limit\"");
        if (i_limit >= 0) {
            String limitStr = "";
            int start = body.indexOf(':', i_limit) + 1;
            int end = body.indexOf(',', start);
            if (end == -1) end = body.indexOf('}', start); // Último elemento
            if (start > 0 && end > start) {
                limitStr = body.substring(start, end);
                readingsLimit = limitStr.toInt();
            }
        }
        
        Serial.print(">>> Limite de Leituras Atualizado: ");
        Serial.print(useReadingsLimit ? "ATIVADO" : "DESATIVADO");
        Serial.print(" | Limite: ");
        Serial.println(readingsLimit);
    }
    server.send(200, "text/plain", "OK");
}


// ===================================
// FUNÇÕES DE COMUNICAÇÃO EXTERNA
// ===================================

void postToScript(int value){
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERRO: WiFi desconectado. Postagem abortada.");
    return;
  }
  
  HTTPClient http;
  http.begin(SCRIPT_URL);
  http.addHeader("Content-Type", "application/json");
  unsigned long ms = millis();
  String payload = String("{") +
    "\"sensor\":\"" + currentSensor + "\"," +
    "\"value\":" + String(value) + "," +
    "\"deviceId\":\"ESP32-1\"," +
    "\"millis\":" + String(ms) +
  "}";
  
  Serial.print("Payload enviado: ");
  Serial.println(payload);

  int httpResponseCode = http.POST(payload);
  
  Serial.print("HTTP POST para Script. Código de Resposta: ");
  Serial.println(httpResponseCode);
  
  if (httpResponseCode > 0 && httpResponseCode == 200) {
    String response = http.getString();
    Serial.print("Resposta do Script: ");
    Serial.println(response);
  } else if (httpResponseCode < 0) {
     Serial.print("ERRO na conexão HTTP: ");
     Serial.println(http.errorToString(httpResponseCode));
  }
  
  http.end();
}

// ===================================
// SETUP E LOOP
// ===================================

void setup(){
  Serial.begin(115200);
  Serial.println("\nIniciando ESP32...");

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Conectando ao WiFi");

  int attempts = 0;
  while (WiFi.status()!=WL_CONNECTED){ 
    delay(400); 
    Serial.print("."); 
    attempts++;
    if (attempts > 50) { 
        Serial.println("\nFalha ao conectar ao WiFi. Verifique SSID e senha.");
        break; 
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi conectado!"); 
      Serial.print("IP: "); 
      Serial.println(WiFi.localIP());
  } else {
      Serial.println("\n*** Rodando sem conexão WiFi (verifique as credenciais) ***");
  }


  server.on("/", handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/start", HTTP_POST, handleStart);
  server.on("/stop", HTTP_POST, handleStop);
  server.on("/setSensor", HTTP_POST, handleSetSensor);
  server.on("/setLimit", HTTP_POST, handleSetLimit); // Novo handler
  server.begin();
  
  pinMode(ADC_PIN, INPUT);
  Serial.println("Web Server iniciado.");
}

void loop(){
  server.handleClient();
  unsigned long now = millis();
  
  // Condição para ler o sensor e postar
  if (running && now - lastSample >= SAMPLE_MS){
    lastSample = now;
    
    // 1. Realiza a leitura e postagem
    int raw = analogRead(ADC_PIN);
    lastValue = raw;

    Serial.print("Leitura: ");
    Serial.print(currentSensor);
    Serial.print(" | Valor RAW: ");
    Serial.print(raw);
    Serial.print(" | Tempo (ms): ");
    Serial.println(now);
    
    postToScript(raw);
    
    // 2. Incrementa o contador
    readingsCount++;

    // 3. Verifica o limite (Lógica de Parada)
    if (running && useReadingsLimit && readingsCount >= readingsLimit){
      running = false; // Para a leitura
      Serial.println("-------------------------------------");
      Serial.print(">>> LIMITE DE LEITURAS ATINGIDO: ");
      Serial.println(readingsLimit);
      Serial.println("-------------------------------------");
    }
  }
}
