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

// --- Controle de inatividade na calibração ---
unsigned long ultimaInteracao  = 0;
const unsigned long TIMEOUT_MS = 90000; // 1 minuto e 30 segundos

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
//
// PONTO CRÍTICO #1 — BLOQUEIO DE 800ms
// Esta função bloqueia o loop() por ~800ms (800 leituras × 1ms).
// Se um comando BLE (START_CAL, CONFIRM) chegar nessa janela,
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
// =============================================================================

String criarJsonTurbidez() {
  float voltagem = lerVoltagemPura();

  StaticJsonDocument<200> json;
  json["turbidez"]  = 0.0;
  json["tensao"]    = voltagem;
  json["nivel"]     = "MODO_TESTE";
  json["timestamp"] = millis();

  String resposta;
  serializeJson(json, resposta);
  return resposta;
}

// =============================================================================
// INATIVIDADE — Encerra calibração automaticamente após 1min30s sem interação
// Interações que resetam o timer: CONFIRM e START_CAL
// Retorna ao estado IDLE e notifica o Frontend com "INATIV"
// =============================================================================

void verificarInatividade() {
  if (estadoAtual == IDLE) return; // Só monitora durante calibração

  if (millis() - ultimaInteracao >= TIMEOUT_MS) {
    estadoAtual      = IDLE;
    confirmarLeitura = false;
    enviarMensagemBLE("INATIV");
    Serial.println("[INATIV] Calibracao encerrada por inatividade (1min30s).");
  }
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
    delay(500);
    pServer->getAdvertising()->start();
  }
};

// PONTO CRÍTICO #3 — CALLBACK RODA NA TASK BLE
// Nunca chame lerVoltagemPura() diretamente aqui.
// Use apenas flags e deixe o loop() processar.
//
// PONTO CRÍTICO #4 — STRINGS VAZIAS
// Alguns dispositivos Android enviam uma escrita vazia antes do comando real.

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();

    if (rxValue.length() == 0) return;

    Serial.print("[RX] ");
    Serial.println(rxValue);

    if (rxValue == "START_CAL") {
      ultimaInteracao = millis(); // Reseta o timer de inatividade
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
        ultimaInteracao  = millis(); // Reseta o timer de inatividade
        confirmarLeitura = true;
      } else {
        enviarMensagemBLE("FORA_DE_CONT");
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
        enviarMensagemBLE("ERRO_CALIB");
      }
    }
    else {
      enviarMensagemBLE("ERRO_DESCONHECIDO:" + rxValue);
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

  // --- VERIFICA INATIVIDADE (roda sempre, bloqueia só durante calibração) ---
  verificarInatividade();

  // --- MODO CALIBRAÇÃO ---
  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1;

        enviarMensagemBLE("LENDO");
        float tensao = lerVoltagemPura();

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
      enviarMensagemBLE("CALIB_OK");
      Serial.println("[CAL] Concluida (sem calculo - modo teste).");
      estadoAtual = IDLE;
    }
  }

  // --- MODO MONITORAMENTO ---
  else {
    if (deviceConnected) {
      unsigned long agora = millis();
      if (agora - ultimoEnvio >= INTERVALO_MS) {
        ultimoEnvio = agora;
        String jsonData = criarJsonTurbidez();
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
        Serial.print("[MONITOR] ");
        Serial.println(jsonData);
      }
    }
  }
}
