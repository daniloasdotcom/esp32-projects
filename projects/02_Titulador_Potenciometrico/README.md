# Titulador Potenciométrico Automatizado (IoT)

Desenvolvimento de um sistema embarcado de baixo custo para titulação potenciométrica automatizada e monitoramento de pH em tempo real, baseado no microcontrolador ESP32.

## 🎯 Objetivo do Projeto
Criar uma solução acessível e de alta precisão para laboratórios de solos e química, substituindo equipamentos comerciais caros. O sistema afere o pH da solução, aplica filtros matemáticos para estabilizar a leitura e controla um servo motor (atuando como válvula de bureta) para dosar reagentes de forma proporcional até atingir o pH alvo.

## ⚙️ Arquitetura e Tecnologias Aplicadas
* **Hardware:** ESP32, Módulo Sensor de pH, Sensor de Temperatura DS18B20, Servo Motor e Display LCD I2C.
* **Firmware (C++):** 
  * Máquina de estados finitos (FSM) não-bloqueante.
  * Implementação de **Filtro de Kalman** para atenuação de ruídos no conversor A/D (leitura de tensão do eletrodo).
  * Lógica adaptativa de estabilidade (janela de amostras) para confirmação de leitura.
  * Controle de atuação proporcional: o ângulo de abertura do servo varia dinamicamente de acordo com o delta ($\Delta$) entre o pH atual e o pH alvo.
* **Interface Web Embarcada:** Servidor web assíncrono gerando HTML/JS dinâmico (com alocação em HEAP) para renderização de gráficos em tempo real via `Chart.js`.
* **Integração Cloud:** Envio automático de buffers de leitura estáveis para a nuvem via API HTTP POST (Google Sheets).
* **Redes:** Resolução mDNS local (`esp32.local`) e rotina de fallback de conexão (IP Estático x DHCP).

## 📊 Status
Concluído e em uso prático para rotinas de laboratório.
