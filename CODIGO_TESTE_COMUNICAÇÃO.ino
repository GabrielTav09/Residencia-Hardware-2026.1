#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// --- UUIDs NUS ---
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- Hardware ---
const int turbidezPin = 35;

// --- BLE ---
BLECharacteristic *pTxCharacteristic;
bool deviceConnected  = false;
bool confirmarLeitura = false;

// --- Máquina de estados ---
enum EstadoCalib { IDLE, CAL_0, CAL_100, CAL_200, CAL_300, CAL_400, CAL_500, PROCESSAR };
EstadoCalib estadoAtual = IDLE;

const float valoresNTU[] = {0, 100, 200, 300, 400, 500};

// --- Controle de envio periódico (sem delay bloqueante) ---
unsigned long ultimoEnvio = 0;
const unsigned long INTERVALO_MS = 10000; // 10 segundos

// =============================================================================
// COMUNICAÇÃO
// =============================================================================

void enviarMensagemBLE(String msg) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
    Serial.print("[TX] ");
    Serial.println(msg);
  } else {
    Serial.print("[TX ignorado - sem conexao] ");
    Serial.println(msg);
  }
}

// =============================================================================
// LEITURA REAL DE TENSÃO
// Mantida conforme solicitado.
//
// PONTO CRÍTICO #1 — BLOQUEIO DE 800ms
// Esta função bloqueia o loop() por ~800ms (800 leituras × 1ms).
// Se um comando BLE (START_CAL, CONFIRM_STEP) chegar nesse janela,
// o callback onWrite() aguarda na fila do stack BLE e só executa
// depois que lerVoltagemPura() retornar. Em situações normais não
// causa desconexão, mas atrasa a resposta ao Frontend em até 800ms.
// SOLUÇÃO FUTURA: substituir delay(1) por leituras acumuladas via millis().
// =============================================================================

float lerVoltagemPura() {
  float voltagem = 0;
  for (int i = 0; i < 800; i++) {
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3;
    delay(1);
  }
  voltagem /= 800.0;
  return voltagem * (5.0 / 3.3); // Converte para escala do sensor (5V)
}

// =============================================================================
// JSON DE MONITORAMENTO
// Envia a tensão bruta e NTU como 0.0 (sem calibração neste teste).
// O Frontend pode validar se está recebendo o JSON corretamente.
// =============================================================================

String criarJsonTurbidez() {
  float voltagem = lerVoltagemPura();

  StaticJsonDocument<200> json;
  json["turbidez"] = 0.0;           // Sem cálculo — teste de protocolo apenas
  json["tensao"]   = voltagem;      // Valor real do sensor para validar hardware
  json["nivel"]    = "MODO_TESTE";
  json["timestamp"] = millis();

  String resposta;
  serializeJson(json, resposta);
  return resposta;
}

// =============================================================================
// CALLBACKS BLE
// =============================================================================

// PONTO CRÍTICO #2 — RECONEXÃO
// Sem o startAdvertising() manual no onDisconnect, o ESP32 some
// da lista BLE após a primeira desconexão e o app não o encontra mais.
// O delay(500) evita falha no startAdvertising() em alguns firmwares.

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] Conectado.");
  }
  void onDisconnect(BLEServer *pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Desconectado. Reiniciando advertising...");
    delay(500); // <- Necessário em alguns firmwares do ESP32
    pServer->getAdvertising()->start();
  }
};

// PONTO CRÍTICO #3 — CALLBACK RODA NA TASK BLE
// Nunca chame lerVoltagemPura() diretamente aqui.
// Use apenas flags (confirmarLeitura = true) e deixe o loop() processar.
// O código abaixo está correto — este aviso é para manutenção futura.
//
// PONTO CRÍTICO #4 — STRINGS VAZIAS
// Alguns dispositivos Android enviam uma escrita vazia antes do comando real.
// O if (rxValue.length() == 0) return; protege contra isso.

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() == 0) return; // Ignora escritas vazias

    Serial.print("[RX] ");
    Serial.println(rxValue);

    if (rxValue == "START_CAL") {
      if (estadoAtual == IDLE) {
        estadoAtual      = CAL_0;
        confirmarLeitura = false;
        enviarMensagemBLE("0_NTU");
      } else {
        estadoAtual      = IDLE;
        confirmarLeitura = false;
        enviarMensagemBLE("CALIB_CANCELADA");
      }
    }
    else if (rxValue == "CONFIRM") {
      if (estadoAtual != IDLE && estadoAtual != PROCESSAR) {
        confirmarLeitura = true;
        // Não responde aqui — o loop() processa e envia a próxima mensagem
      } else {
        // PONTO CRÍTICO #5 — CONFIRM fora de contexto
        // Sem este aviso, o Frontend pode ficar travado esperando uma resposta
        // que nunca vem, pois confirmarLeitura é ignorado quando IDLE.
        enviarMensagemBLE("ERRO_CONFIRM_FORA_DE_CONTEXTO");
      }
    }
    else if (rxValue == "GET_TURBIDEZ") {
      if (estadoAtual == IDLE) {
        String jsonData = criarJsonTurbidez();
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
        Serial.print("[TX] ");
        Serial.println(jsonData);
      } else {
        enviarMensagemBLE("ERRO_EM_CALIB");
      }
    }
    else {
      // PONTO CRÍTICO #6 — Eco de comandos desconhecidos
      // Ajuda a detectar erros de digitação ou encoding no Frontend.
      enviarMensagemBLE("ERRO_CMD_DESCONHECIDO:" + rxValue);
    }
  }
};

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(turbidezPin, INPUT);
  Serial.println("\n=== NEPTUS — TESTE DE COMUNICACAO BLE ===");

  BLEDevice::init("ESP32-NEPTUS-TURB");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_RX,
    BLECharacteristic::PROPERTY_WRITE
  );
  pRxCharacteristic->setCallbacks(new MyCallbacks());

  pTxCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID_TX,
    BLECharacteristic::PROPERTY_NOTIFY
  );
  pTxCharacteristic->addDescriptor(new BLE2902());

  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();

  Serial.println("[BLE] Advertising iniciado.");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {

  // --- MODO CALIBRAÇÃO ---COLOQUe
  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1;

        enviarMensagemBLE("LENDO");
        float tensao = lerVoltagemPura(); // Leitura real, apenas para log

        Serial.print("[CAL] Ponto ");
        Serial.print(idx);
        Serial.print(" | Tensao: ");
        Serial.println(tensao, 4);

        estadoAtual      = (EstadoCalib)((int)estadoAtual + 1);
        confirmarLeitura = false;

        if (estadoAtual <= CAL_500) {
          int ntuProximo = (int)valoresNTU[(int)estadoAtual - 1];
          enviarMensagemBLE(String(ntuProximo) + "_NTU");
        }
      }
    }
    else if (estadoAtual == PROCESSAR) {
      enviarMensagemBLE("PROCESSANDO");
      delay(300);
      enviarMensagemBLE("CALIB_OK"); // Sem cálculo real neste teste
      Serial.println("[CAL] Concluida (sem calculo - modo teste).");
      estadoAtual = IDLE;
    }
  }

  // --- MODO MONITORAMENTO (10 em 10 segundos, sem delay bloqueante) ---
  else {
    if (deviceConnected) {
      unsigned long agora = millis();
      if (agora - ultimoEnvio >= INTERVALO_MS) {
        ultimoEnvio = agora;
        // ATENÇÃO: lerVoltagemPura() bloqueia ~800ms — ver Ponto Crítico #1
        String jsonData = criarJsonTurbidez();
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
        Serial.print("[MONITOR] ");
        Serial.println(jsonData);
      }
    }
  }
}
