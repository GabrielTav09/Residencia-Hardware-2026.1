#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <Preferences.h>

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

// --- Preferences (memória flash) ---
Preferences preferences;

// --- Coeficientes da fórmula NTU = ax² + bx + c ---
float coefA, coefB, coefC;

// --- Máquina de estados ---
enum EstadoCalib { IDLE, CAL_0, CAL_100, CAL_200, CAL_300, CAL_400, CAL_500, PROCESSAR };
EstadoCalib estadoAtual = IDLE;

float leiturasV[6];
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
// CÁLCULO NTU — Converte tensão para turbidez usando a fórmula calibrada
// =============================================================================

float lerTurbidez() {
  float voltagem = lerVoltagemPura();
  float ntu;

  if (voltagem < 1.2)      ntu = 3000; // Abaixo da curva — muito turvo
  else if (voltagem > 4.2) ntu = 0;    // Acima da curva — água limpa
  else {
    ntu = (coefA * voltagem * voltagem) + (coefB * voltagem) + coefC;
  }

  return (ntu < 0) ? 0 : ntu;
}

// =============================================================================
// CLASSIFICAÇÃO — Traduz NTU em qualidade da água
// =============================================================================

String classificarTurbidez(float ntu) {
  if (ntu <= 50)       return "Boa para o peixe caranha (pouco turva)";
  else if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)";
  else                 return "Inadequada para o peixe caranha (muito turva)";
}

// =============================================================================
// JSON DE MONITORAMENTO — Envia leitura real calibrada ao Frontend
// =============================================================================

String criarJsonTurbidez() {
  float turbidez = lerTurbidez();
  String nivel   = classificarTurbidez(turbidez);

  StaticJsonDocument<200> json;
  json["turbidez"]  = turbidez;
  json["nivel"]     = nivel;
  json["timestamp"] = millis();

  String resposta;
  serializeJson(json, resposta);
  return resposta;
}

// =============================================================================
// REGRESSÃO POLINOMIAL DE 2º GRAU — Calcula os coeficientes a, b, c
// Executada após coletar os 6 pontos de calibração (0 a 500 NTU)
// Salva os coeficientes na memória flash (Preferences) para persistir após reboot
// =============================================================================

void calcularNovaCurva() {
  double n = 6;
  double sumX = 0, sumX2 = 0, sumX3 = 0, sumX4 = 0, sumY = 0, sumXY = 0, sumX2Y = 0;

  for (int i = 0; i < n; i++) {
    double x = leiturasV[i];
    double y = valoresNTU[i];
    sumX   += x;
    sumX2  += (x * x);
    sumX3  += (x * x * x);
    sumX4  += (x * x * x * x);
    sumY   += y;
    sumXY  += (x * y);
    sumX2Y += (x * x * y);
  }

  double det = n   * (sumX2 * sumX4 - sumX3 * sumX3)
             - sumX * (sumX  * sumX4 - sumX2 * sumX3)
             + sumX2 * (sumX  * sumX3 - sumX2 * sumX2);

  if (det != 0) {
    coefC = (sumY  * (sumX2 * sumX4 - sumX3 * sumX3)
           - sumX  * (sumXY * sumX4 - sumX2Y * sumX3)
           + sumX2 * (sumXY * sumX3 - sumX2Y * sumX2)) / det;

    coefB = (n     * (sumXY  * sumX4 - sumX2Y * sumX3)
           - sumY  * (sumX   * sumX4 - sumX2  * sumX3)
           + sumX2 * (sumX   * sumX2Y - sumXY * sumX2)) / det;

    coefA = (n    * (sumX2  * sumX2Y - sumXY  * sumX3)
           - sumX * (sumX   * sumX2Y - sumX2  * sumXY)
           + sumY * (sumX   * sumX3  - sumX2  * sumX2)) / det;

    // Salva na memória flash para persistir após reboot
    preferences.begin("turb_cal", false);
    preferences.putFloat("a", coefA);
    preferences.putFloat("b", coefB);
    preferences.putFloat("c", coefC);
    preferences.end();

    Serial.println("[CAL] Novos coeficientes salvos:");
    Serial.print("  A = "); Serial.println(coefA, 6);
    Serial.print("  B = "); Serial.println(coefB, 6);
    Serial.print("  C = "); Serial.println(coefC, 6);

    enviarMensagemBLE("CALIB_OK");
  } else {
    // Determinante zero indica pontos colineares ou leituras inválidas
    Serial.println("[CAL] ERRO: determinante zero. Verifique as amostras.");
    enviarMensagemBLE("CALIB_ERRO");
  }
}

// =============================================================================
// INATIVIDADE — Encerra calibração automaticamente após 1min30s sem interação
// Interações que resetam o timer: CONFIRM e START_CAL
// Retorna ao estado IDLE e notifica o Frontend com "INATIV"
// =============================================================================

void verificarInatividade() {
  if (estadoAtual == IDLE) return;

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
      ultimaInteracao = millis();
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
        ultimaInteracao  = millis();
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
  Serial.println("\n=== NEPTUS — SENSOR DE TURBIDEZ BLE ===");

  // Carrega coeficientes salvos ou usa os padrões de fábrica
  preferences.begin("turb_cal", true);
  coefA = preferences.getFloat("a", 2247.5);
  coefB = preferences.getFloat("b", -11038.0);
  coefC = preferences.getFloat("c", 13133.6);
  preferences.end();

  Serial.println("[CAL] Coeficientes carregados:");
  Serial.print("  A = "); Serial.println(coefA, 6);
  Serial.print("  B = "); Serial.println(coefB, 6);
  Serial.print("  C = "); Serial.println(coefC, 6);

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

  // --- VERIFICA INATIVIDADE (roda sempre, age só durante calibração) ---
  verificarInatividade();

  // --- MODO CALIBRAÇÃO ---
  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1;

        enviarMensagemBLE("LENDO");
        leiturasV[idx] = lerVoltagemPura(); // Armazena tensão real para o cálculo

        Serial.print("[CAL] Ponto ");
        Serial.print(idx);
        Serial.print(" | Tensao: ");
        Serial.println(leiturasV[idx], 4);

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
      calcularNovaCurva(); // Calcula e salva os novos coeficientes
      estadoAtual = IDLE;
    }
  }

  // --- MODO MONITORAMENTO (10 em 10 segundos, sem delay bloqueante) ---
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
