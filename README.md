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
* `RESET_CALIB`: Apaga a calibração salva na memória flash do ESP32 e volta imediatamente a usar a fórmula padrão (`NTU = -1666,7·V² + 2166,7·V + 650`). Após o reset, o sistema segue o fluxo normal: o monitoramento continua e uma nova calibração pode ser iniciada com `START_CAL`. *Nota: Este comando gerará um erro se enviado enquanto uma calibração estiver ativa. Para resetar no meio de uma calibração, cancele antes com `START_CAL`.*

### ⤵ 2. Mensagens de Notificação de Estado e Erros (ESP32 para o Front-end)
Enviadas como strings de texto puro pela característica **TX** do serviço BLE para orientar dinamicamente a interface do utilizador e tratar exceções.

### A. Fluxo de Calibração e Estados normais
* `0_NTU`: Indica o início do ciclo de calibração e sinaliza para a interface instruir o utilizador a inserir o líquido de calibração padrão de 0 NTU.
* `500_NTU`: Solicita a troca física do frasco para a segunda e última referência de 500 NTU.
* `LENDO`: Emitido no momento exato em que o ESP32 realiza a captura de amostras de um ponto (cerca de 0,8 s). O Front-end deve exibir um feedback visual de carregamento (*spinner*) e bloquear interações para evitar cliques duplos.
* `PROCESSANDO`: Enviado logo após o último ponto (500 NTU) ser confirmado, indicando que a amostragem acabou e o ESP32 está a calcular o ajuste da fórmula padrão (deslocamento constante igual à média dos erros nos dois pontos, limitado a ±15 NTU).
* `CALIB_OK`: Notificação enviada quando a calibração é concluída com sucesso e salva na memória flash. A calibração permanece válida mesmo após reiniciar o ESP32, até uma nova calibração ou um `RESET_CALIB`. A interface do Front-end deve exibir uma mensagem de êxito e redirecionar o utilizador para o painel principal de monitoramento.
* `CALIB_CANCELADA`: Confirmação de interrupção, enviada pelo hardware quando o processo é cancelado a meio devido ao recebimento de um segundo comando `START_CAL`. Nada é salvo e a calibração anterior continua válida.
* `INATIV`: Enviado quando a calibração é encerrada automaticamente por inatividade (1min30s sem receber `START_CAL` ou `CONFIRM`). O sistema volta ao monitoramento, nada é salvo e a calibração anterior continua válida.
* `CALIB_RESETADA`: Confirmação de que o comando `RESET_CALIB` foi executado: a calibração salva foi apagada e o sistema passou a medir com a fórmula padrão.

### B. Mensagens de Erro
* `FORA_DE_CONT`: O usuário aperta "confirmar" no app antes de ter iniciado a calibração com START_CAL, o usuário aperta "confirmar" uma 3ª vez depois que os 2 pontos já foram coletados (quando o estado já virou PROCESSAR) ou o app reconectou e reenviou um CONFIRM que estava em buffer
* `ERRO_CALIB`: O usuário está no meio de uma calibração e outra tela do app tenta puxar uma leitura normal ao mesmo tempo, o app tem um timer automático de GET_TURBIDEZ que não foi pausado ao entrar no fluxo de calibração, ou foi enviado RESET_CALIB durante uma calibração
* `CALIB_ERRO`: A calibração falhou no cálculo final porque a tensão da amostra de 0 NTU não ficou pelo menos 0,01 V acima da tensão da amostra de 500 NTU. Acontece quando as amostras foram trocadas, a mesma amostra foi usada nos dois pontos, o sensor está sujo ou mal encaixado, ou o sensor está desconectado. Nada é salvo e a calibração anterior continua válida
* `ERRO_RESET`: O ESP32 não conseguiu acessar a memória flash para apagar a calibração após um RESET_CALIB (situação rara). A calibração atual continua em uso; o comando pode ser enviado novamente
* `ERRO_DESCONHECIDO:xyz`: Erro de digitação no Frontend, como start_cal (minúsculo) ou STARTCAL sem underscore. Problema de encoding enviando caracteres extras, como START_CAL\n ou START_CAL\r\n — o \n quebra a comparação

### C. Dados de Monitoramento
Fora da calibração, o ESP32 envia automaticamente a cada 10 segundos (e também ao receber `GET_TURBIDEZ`) um JSON no formato:

```json
{"turbidez": 12.5, "nivel": "Boa para o peixe caranha (pouco turva)", "timestamp": 123456}
```

* `turbidez`: Valor em NTU. Retorna `0` quando a tensão está acima da curva (água limpa) e `3000` quando está abaixo da curva (muito turva).
* `nivel`: Classificação da água — "Boa" até 50 NTU, "Moderada" até 250 NTU e "Inadequada" acima de 250 NTU.
* `timestamp`: Tempo em milissegundos desde que o ESP32 foi ligado.

