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

***Fluxograma de funcionamento da calibração** 

   **USUÁRIO / APLICATIVO (Front-end)**       **SOFTWARE / ESP32 (Firmware)**
   +--------------------------------+       +---------------------------------+
   |                                |       |                                 |
   |  1. Clica em "START_CAL"       |------>|  Muda estadoAtual para CAL_0    |
   |                                |       |  Envia via BLE: "COLOQUE_0_NTU" |
   |                                |       +---------------------------------+
   |  2. Insere o Frasco de 0 NTU   |                       |
   |     e clica em "CONFIRM_STEP"  |------>|  Recebe CONFIRM_STEP            |
   |                                |       |  Mede Voltagem Média (800x)     |
   |                                |       |  Salva no array leiturasV[0]    |
   |                                |       |  Avança para CAL_100            |
   |                                |       |  Envia via BLE: "COLOQUE_100_NTU"
   |                                |       +---------------------------------+
   |                                |                       |
   |               :                |                       :
   |    [ Repete o mesmo processo   |            [ Repete o mesmo processo    |
   |      para 100, 200, 300, 400 ] |              para 100, 200, 300, 400 ]  |
   |               :                |                       :
   |                                |                       |
   |  3. Insere o Frasco de 500 NTU |       +---------------------------------+
   |     e clica em "CONFIRM_STEP"  |------>|  Recebe CONFIRM_STEP            |
   |                                |       |  Mede Voltagem Média (800x)     |
   |                                |       |  Salva no array leiturasV[5]    |
   |                                |       |  Avança para PROCESSAR          |
   |                                |       +---------------------------------+
   |                                |                       |
   |                                |                       v
   |                                |       +---------------------------------+
   |                                |       |  FUNÇÃO calcularNovaCurva()     |
   |                                |       |  - Executa Mínimos Quadrados    |
   |  4. Recebe o feedback na tela  |       |  - Aplica Regra de Cramer       |
   |     "Calibração Concluída!"    |<------|  - Salva a, b, c na Flash (NVS) |
   |                                |       |  - Retorna estadoAtual para IDLE|
   +--------------------------------+       +---------------------------------+
                                                            |
                                                            v
                                            [ Retoma Medição Normal de NTU ]
