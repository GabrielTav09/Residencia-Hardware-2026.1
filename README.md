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


## 📝Fluxograma de funcionamento da calibração** 
`Link do documento contendo o fluxograma:` https://miro.com/app/board/uXjVHRYVJZc=/?share_link_id=602356522690



## 🛠️ Especificação de Integração (API Bluetooth BLE)

Para o desenvolvimento e acoplamento do aplicativo móvel, o Front-end deve interagir estritamente com os seguintes comandos de envio e estruturas de recebimento na característica de comunicação NUS.
Link de um documentos: https://docs.google.com/document/d/1QTFS1HhQR8ck_HtCRx5Kt2yM3TgcG6MsAOZ4cucw9zI/edit?usp=sharing

### ⤴ 1. Comandos de Envio (Botões do Front-end para o ESP32)
Estes comandos devem ser enviados como strings de texto puro para a característica **RX** do serviço BLE.

* `START_CAL`: Inicia o processo de calibração (caso o sistema esteja em modo de operação normal `IDLE`) ou cancela imediatamente o processo atual se ele já estiver em curso (forçando o retorno ao monitoramento e restaurando as configurações anteriores).
* `CONFIRM`: Funciona como o comando de **Avançar / Confirmar**. Deve ser enviado pelo Front-end após o utilizador posicionar fisicamente o frasco com o líquido de referência na fenda do sensor e clicar para validar, autorizando o ESP32 a capturar o ponto atual e avançar para o próximo.
* `GET_TURBIDEZ`: Solicita manualmente uma leitura de medição instantânea. O ESP32 responderá enviando o JSON com os dados de turbidez. *Nota: Este comando gerará um erro se enviado enquanto uma calibração estiver ativa.*

### ⤵ 2. Mensagens de Notificação de Estado e Erros (ESP32 para o Front-end)
Enviadas como strings de texto puro pela característica **TX** do serviço BLE para orientar dinamicamente a interface do utilizador e tratar exceções.

### A. Fluxo de Calibração e Estados normais
* `0_NTU`: Indica o início do ciclo de calibração e sinaliza para a interface instruir o utilizador a inserir o líquido de calibração padrão de 0 NTU.
* `100_NTU`: Solicita a troca física do frasco para a referência de 100 NTU.
* `200_NTU`: Solicita a troca física do frasco para a referência de 200 NTU.
* `300_NTU`: Solicita a troca física do frasco para a referência de 300 NTU.
* `400_NTU`: Solicita a troca física do frasco para a referência de 400 NTU.
* `500_NTU`: Solicita a troca física do frasco para a última referência de 500 NTU.
* `LENDO`: Emitido no momento exato em que o ESP32 realiza a captura de amostras de um ponto. O Front-end deve exibir um feedback visual de carregamento (*spinner*) e bloquear interações para evitar cliques duplos.
* `PROCESSANDO`: Enviado logo após o último ponto (500 NTU) ser confirmado, indicando que a amostragem acabou e o núcleo do ESP32 está a processar os cálculos matemáticos da Regressão Polinomial.
* `CALIB_OK`: Notificação enviada quando a calibração é concluída com sucesso. A interface do Front-end deve exibir uma mensagem de êxito e redirecionar o utilizador para o painel principal de monitoramento.
* `CALIB_CANCELADA`: Confirmação de interrupção, enviada pelo hardware quando o processo é cancelado a meio devido ao recebimento de um segundo comando `START_CAL`.

### B. Mensagens de Erro
* `ERRO_EM_CALIB`: Retornado pelo ESP32 caso o Front-end envie o comando `GET_TURBIDEZ` enquanto o sistema estiver no meio do processo de calibração. A requisição de leitura manual é ignorada para não corromper o estado do hardware.
* `ERRO_AMOSTRAGEM`: Emitido se o hardware detetar uma falha crítica ou leitura inconsistente do sensor de turbidez durante a coleta das amostras de um dos pontos. O Front-end deve alertar o utilizador para verificar a posição do frasco ou o sensor.
* `ERRO_MATEMATICO`: Enviado durante a fase `PROCESSANDO` caso os dados coletados gerem uma matriz singular ou inconsistente que impossibilite o cálculo dos coeficientes da regressão polinomial. Indica que a calibração falhou e os coeficientes antigos foram mantidos.


### 3. Pacote de Dados de Monitorização (Modo Normal)
Quando o sistema se encontra no estado `IDLE` (Operação de Rotina), o ESP32 transmite autonomamente a cada **2 segundos** um pacote formatado em **JSON** contendo as seguintes variáveis:

```json
{
  "turbidez": 42.15,
  "nivel": "Boa para o peixe caranha (pouco turva)",
  "timestamp": 124500
}
