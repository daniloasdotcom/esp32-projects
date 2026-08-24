# Monitoramento IoT de Umidade do Solo (End-to-End)

Desenvolvimento de um sistema completo de telemetria para monitoramento de umidade do solo, englobando a coleta de dados via hardware (Edge Computing), armazenamento em nuvem e visualização em interface mobile multiplataforma.

## 🎯 Objetivo do Projeto
Fornecer uma ferramenta acessível e em tempo real para tomada de decisão no manejo de irrigação. O sistema coleta a umidade volumétrica do solo no campo, processa o dado localmente e o envia para a nuvem. O usuário final consome essas informações de forma intuitiva através de um aplicativo móvel dedicado.

## ⚙️ Arquitetura e Tecnologias Aplicadas
Este projeto demonstra uma integração "Full-Stack IoT", dividido nas seguintes camadas:

* **Hardware (ESP32 Firmware):** 
  * Microcontrolador ESP32 programado em C++.
  * Leitura analógica de sensor de umidade do solo.
  * Lógica de conexão Wi-Fi e rotina de envio de dados via requisições HTTP POST.
* **Frontend Mobile (Flutter):**
  * Aplicativo desenvolvido em framework Flutter (Linguagem Dart).
  * Consumo da API (HTTP GET) para resgatar os dados da nuvem.
  * Renderização de interface de usuário (UI) responsiva e gráficos dinâmicos para monitoramento contínuo dos níveis de umidade.

## ☁️ Backend: Google Apps Script (Google Sheets)
O Google Sheets atua como banco de dados estruturado e API. O script abaixo foi utilizado no Google Apps Script para receber o payload JSON do ESP32 via HTTP POST e registrar os dados na planilha:

```javascript
function doPost(e) {
  const sheet = SpreadsheetApp.getActive().getSheetByName("Setor");
  const data = JSON.parse(e.postData.contents || "{}");

  const agora = new Date();
  const valor = Number(data.valorADC);

  sheet.appendRow([agora, valor]);

  return ContentService.createTextOutput("OK")
    .setMimeType(ContentService.MimeType.TEXT);
}
