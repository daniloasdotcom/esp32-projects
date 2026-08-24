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
* **Backend / Nuvem:** 
  * Google Sheets atuando como banco de dados e API (via Google Apps Script) para armazenamento estruturado das leituras de campo.
* **Frontend Mobile (Flutter):**
  * Aplicativo desenvolvido em framework Flutter (Linguagem Dart).
  * Consumo da API (HTTP GET) para resgatar os dados da nuvem.
  * Renderização de interface de usuário (UI) responsiva para monitoramento contínuo dos níveis de umidade.

## 📁 Estrutura do Repositório
* `/esp32_firmware`: Código-fonte em C++ para embarcar no microcontrolador.
* `/flutter_app`: Código-fonte do aplicativo móvel (Dart/Flutter).

## 📊 Status
Protótipo funcional validado.
