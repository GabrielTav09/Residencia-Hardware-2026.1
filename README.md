# Residencia-Hardware-2026.1

Este repositório contém os códigos e a documentação técnica para o projeto de Hardware da Residência 2026/1. O foco principal deste módulo é a implementação de um sistema de calibração automática utilizando o microcontrolador ESP32.

## 📌 Descrição do Projeto

O código principal, `Codigo_do_ESP32_CALIBRACAO_AUTOMATICA`, foi desenvolvido para gerenciar o processo de calibração de sensores de forma autônoma. Ele permite que o hardware ajuste seus parâmetros internos sem a necessidade de intervenção manual constante, garantindo maior precisão na coleta de dados e estabilidade do sistema em diferentes ambientes.

## 🚀 Funcionalidades

* **Calibração Automática:** Algoritmos para ajuste dinâmico de sensores.
* **Manipulação de Dados JSON:** Utilização da biblioteca ArduinoJson para comunicação ou armazenamento de configurações estruturadas.
* **Processamento em Tempo Real:** Aproveitamento do processamento Dual-Core do ESP32 para tarefas de hardware e calibração simultâneas.

## 🛠️ Tecnologias e Ferramentas

* **Linguagem:** C++ (Framework Arduino)
* **Plataforma de Desenvolvimento:** Arduino IDE 2.3.8
* **Placa Utilizada:** ESP32 Dev Module (Espressif Systems)
* **Versão do Core ESP32:** 3.3.8

## 📚 Bibliotecas Utilizadas

Para o correto funcionamento do código, é necessário instalar as seguintes dependências através do Gerenciador de Bibliotecas da Arduino IDE:

1.  **ArduinoJson** (por Benoit Blanchon)
    * **Versão: 7.4.1** .
    * *Finalidade:* Utilizada para serialização e desserialização de objetos JSON, facilitando a troca de mensagens e configurações de calibração.

## 🔧 Configuração e Instalação

1.  **Instalação da Placa:**
    * No Gerenciador de Placas (`Boards Manager`), certifique-se de ter o pacote **`esp32 by Espressif Systems`** instalado **(versão 3.3.8 testada)** e **`Arduino AVR Boards`** instalado **(versão 1.8.7 testada)**.
2.  **Instalação da Biblioteca:**
    * Vá na `Lateral direita` > `Include Library` > `Manage Libraries...`
    * Procure por **`ArduinoJson`** e instale a versão **`7.4.1`**
3.  **Upload do Código:**
    * Conecte seu ESP32 via USB.
    * Selecione a placa `ESP32 Dev Module`.
    * Selecione a porta serial correta.
    * Clique em **Upload** (Seta para a direita).

## 📂 Estrutura de Arquivos

* `Codigo_do_ESP32_CALIBRACAO_AUTOMATICA.ino`: Script principal com a lógica de calibração.
* `README.md`: Documentação do projeto.

---
Desenvolvido como parte da documentação de Hardware da Residência 2026/1.

## 📝Fluxograma de funcionamento da calibração** 
`Link do documento contendo o fluxograma:` https://docs.google.com/document/d/1QTFS1HhQR8ck_HtCRx5Kt2yM3TgcG6MsAOZ4cucw9zI/edit?usp=sharing


## 🛠️ Especificação de Integração (API Bluetooth BLE)

Para o desenvolvimento e acoplamento do aplicativo móvel, o Front-end deve interagir estritamente com os seguintes comandos de envio e estruturas de recebimento na característica de comunicação NUS.

### 1. Comandos de Envio (Botões do Front-end para o ESP32)
Devem ser enviados como strings de texto puro para a característica **RX** do serviço BLE.

* `START_CAL`: Aciona ou aborta o modo de calibração. Se o sistema estiver em operação normal (`IDLE`), limpa as variáveis e inicia o passo a passo mudando para `CAL_0`. Se enviado com a calibração em andamento, funciona como um botão de **Cancelar**, forçando o retorno imediato ao monitoramento normal com os coeficientes antigos.
* `CONFIRM_STEP`: Funciona como um botão de **Avançar / Confirmar**. Deve ser pressionado pelo utilizador após posicionar fisicamente o frasco de calibração solicitado na fenda do sensor. Liberta o ESP32 para colher as 800 amostras daquele ponto.
* `GET_TURBIDEZ`: Força uma requisição manual de leitura. O ESP32 responderá instantaneamente enviando o JSON de medição. *Nota: Este comando é automaticamente ignorado pelo ESP32 caso a máquina de estados de calibração esteja ativa.*

### 2. Mensagens de Notificação de Estado (ESP32 para o Front-end)
Enviadas como strings de texto puro pela característica **TX** do serviço BLE para orientar dinamicamente a interface do utilizador.

* `COLOQUE_0_NTU`: Exibir instrução em ecrã para o utilizador inserir o primeiro líquido de calibração (0 NTU) e expor o botão de confirmação.
* `LENDO...`: Emitido assim que o utilizador clica em confirmar. O Front-end deve exibir um feedback visual de carregamento (*spinner*) e bloquear interações para evitar cliques duplos durante as 800 leituras.
* `COLOQUE_100_NTU` a `COLOQUE_500_NTU`: Instruções sequenciais para a troca física dos frascos de referência padrão.
* `PROCESSANDO...`: Indica que a amostragem terminou e os cálculos matemáticos da Regressão Polinomial estão a ser processados pelo núcleo do ESP32.
* `CALIB_OK`: Notificação de sucesso. O Front-end deve fechar a interface de calibração, exibir uma mensagem de êxito e redirecionar o utilizador para o painel principal de monitoramento.
* `CALIB_CANCELADA`: Confirmação de interrupção. Disparado quando o utilizador cancela o processo a meio.

### 3. Pacote de Dados de Monitorização (Modo Normal)
Quando o sistema se encontra no estado `IDLE` (Operação de Rotina), o ESP32 transmite autonomamente a cada **2 segundos** um pacote formatado em **JSON** contendo as seguintes variáveis:

```json
{
  "turbidez": 42.15,
  "nivel": "Boa para o peixe caranha (pouco turva)",
  "timestamp": 124500
}
