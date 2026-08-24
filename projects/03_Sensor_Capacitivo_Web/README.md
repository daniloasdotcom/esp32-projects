# Aquisição Dinâmica de Dados para Sensores Capacitivos (IoT)

Desenvolvimento de um sistema de aquisição de dados configurável (datalogger IoT) para monitoramento de umidade em múltiplas unidades experimentais de laboratório. O sistema utiliza uma interface web local para controle de amostragem e um backend dinâmico em nuvem.

## 🎯 Objetivo do Projeto
Agilizar e dar precisão à coleta de dados em ensaios laboratoriais agronômicos. O sistema permite que o pesquisador insira o sensor na unidade experimental, selecione o tratamento (vaso/parcela) diretamente pelo celular e defina o limite de amostras. Os dados são enviados automaticamente para a nuvem, já tabulados na aba correspondente ao tratamento.

## ⚙️ Arquitetura do Sistema
* **Hardware e Firmware (ESP32):**
  * Leitura analógica de sensor capacitivo.
  * Servidor web assíncrono embarcado gerando uma interface de controle em HTML/JS (Single Page Application).
  * Envio de payloads JSON via requisições HTTP POST.
* **Backend em Nuvem (Google Sheets + Apps Script):**
  * Script inteligente que identifica o nome do sensor/unidade experimental e cria abas dinamicamente, mantendo o banco de dados organizado por tratamento sem intervenção manual.

## ☁️ Código do Backend (Google Apps Script)
O script abaixo processa os envios do ESP32, criando planilhas (`_getOrCreateSheet_`) automaticamente para novos sensores:

```javascript
// == CONFIG ==
const SPREADSHEET_ID = 'ID_da_planilha_aqui';
const TAB_NAMES = ['Sensor01','Sensor02','Sensor03','Sensor04','Sensor05'];

function _getOrCreateSheet_(name){
  const ss = SpreadsheetApp.openById(SPREADSHEET_ID);
  let sh = ss.getSheetByName(name);
  if (!sh) {
    sh = ss.insertSheet(name);
    sh.appendRow(['Timestamp', 'Valor_ADC', 'DeviceID', 'Millis']);
  }
  return sh;
}

function doPost(e){
  try {
    const data = JSON.parse(e.postData.contents);
    const sensor = (data.sensor || 'Sensor01')+'';
    const value  = Number(data.value || 0);
    const device = (data.deviceId || 'ESP32')+'';
    const ms     = Number(data.millis || 0);

    const sh = _getOrCreateSheet_(sensor);
    const now = new Date();

    sh.appendRow([now, value, device, ms]);

    return ContentService.createTextOutput(JSON.stringify({ok:true}))
      .setMimeType(ContentService.MimeType.JSON);
  } catch(err) {
    return ContentService.createTextOutput(JSON.stringify({ok:false, error:String(err)}))
      .setMimeType(ContentService.MimeType.JSON);
  }
}
```

📊 Status
Concluído e em operação nas rotinas laboratoriais.
