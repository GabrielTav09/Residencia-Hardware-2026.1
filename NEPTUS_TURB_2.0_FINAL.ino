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

// --- Variável global para exibir a última voltagem medida ---
float ultimaVoltagemLida = 0.0;

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
// COMUNICAÇÃO BLE
// =============================================================================

void enviarMensagemBLE(String msg) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
    Serial.print("[BLE TX] ");
    Serial.println(msg);
  } else {
    Serial.print("[BLE TX ignorado - sem conexao] ");
    Serial.println(msg);
  }
}

// =============================================================================
// LEITURA REAL DE TENSÃO
// =============================================================================

float lerVoltagemPura() {
  float voltagem = 0;
  for (int i = 0; i < 800; i++) {
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3;
    delay(1);
  }
  voltagem /= 800.0;
  
  // Converte para escala do sensor (5V) e armazena na global
  ultimaVoltagemLida = voltagem * (5.0 / 3.3); 
  
  Serial.print("[SENSOR] Tensao medida: ");
  Serial.print(ultimaVoltagemLida, 4);
  Serial.println(" V");
  
  return ultimaVoltagemLida;
}

// =============================================================================
// CÁLCULO NTU
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
// CLASSIFICAÇÃO
// =============================================================================

String classificarTurbidez(float ntu) {
  if (ntu <= 50)       return "Boa para o peixe caranha (pouco turva)";
  else if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)";
  else                 return "Inadequada para o peixe caranha (muito turva)";
}

// =============================================================================
// JSON DE MONITORAMENTO
// =============================================================================

String criarJsonTurbidez() {
  float turbidez = lerTurbidez(); // Isso atualiza 'ultimaVoltagemLida' internamente
  String nivel   = classificarTurbidez(turbidez);

  // Aumentado para 256 bytes para comportar a nova variável
  StaticJsonDocument<256> json; 
  json["voltagem"]  = ultimaVoltagemLida; // Adicionado conforme solicitado
  json["turbidez"]  = turbidez;
  json["nivel"]     = nivel;
  json["timestamp"] = millis();

  String resposta;
  serializeJson(json, resposta);
  return resposta;
}

// =============================================================================
// PROCESSAMENTO UNIFICADO DE COMANDOS (Bluetooth e Serial)
// =============================================================================

void processarComando(String cmd) {
  cmd.trim(); // Limpa espaços ou quebras de linha
  if (cmd.length() == 0) return;

  Serial.print("\n[COMANDO RECEBIDO] ");
  Serial.println(cmd);

  if (cmd == "START_CAL") {
    ultimaInteracao = millis();
    if (estadoAtual == IDLE) {
      estadoAtual      = CAL_0;
      confirmarLeitura = false;
      enviarMensagemBLE("0_NTU");
      Serial.println("[STATUS] Calibracao Iniciada. Solicitando 0 NTU.");
    } else {
      estadoAtual      = IDLE;
      confirmarLeitura = false;
      enviarMensagemBLE("CALIB_CANCELADA");
      Serial.println("[STATUS] Calibracao Cancelada.");
    }
  }
  else if (cmd == "CONFIRM") {
    if (estadoAtual != IDLE && estadoAtual != PROCESSAR) {
      ultimaInteracao  = millis();
      confirmarLeitura = true;
      Serial.println("[STATUS] Leitura Confirmada pelo usuario. Processando amostra...");
    } else {
      enviarMensagemBLE("FORA_DE_CONT");
      Serial.println("[ERRO] Comando CONFIRM enviado fora do modo de calibracao.");
    }
  }
  else if (cmd == "GET_TURBIDEZ") {
    if (estadoAtual == IDLE) {
      String jsonData = criarJsonTurbidez();
      if (deviceConnected) {
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
      }
      Serial.print("[RETORNO GET_TURBIDEZ] ");
      Serial.println(jsonData);
    } else {
      enviarMensagemBLE("ERRO_CALIB");
      Serial.println("[ERRO] Sistema ocupado com calibracao. Nao e possivel ler turbidez agora.");
    }
  }
  else {
    enviarMensagemBLE("ERRO_DESCONHECIDO:" + cmd);
    Serial.println("[ERRO] Comando desconhecido -> " + cmd);
  }
}

// =============================================================================
// REGRESSÃO POLINOMIAL DE 2º GRAU
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

    preferences.begin("turb_cal", false);
    preferences.putFloat("a", coefA);
    preferences.putFloat("b", coefB);
    preferences.putFloat("c", coefC);
    preferences.end();

    Serial.println("\n=============================================");
    Serial.println("[CAL] SUCESSO! Novos coeficientes calculados:");
    Serial.print("  A = "); Serial.println(coefA, 6);
    Serial.print("  B = "); Serial.println(coefB, 6);
    Serial.print("  C = "); Serial.println(coefC, 6);
    Serial.println("=============================================\n");

    enviarMensagemBLE("CALIB_OK");
  } else {
    Serial.println("[CAL] ERRO: determinante zero. Verifique as amostras.");
    enviarMensagemBLE("CALIB_ERRO");
  }
}

// =============================================================================
// INATIVIDADE
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

class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] Dispositivo Conectado.");
  }
  void onDisconnect(BLEServer *pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] Desconectado. Reiniciando advertising...");
    delay(500);
    pServer->getAdvertising()->start();
  }
};

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    processarComando(rxValue); // Chama o processador unificado
  }
};

// =============================================================================
// SETUP
// =============================================================================

void setup() {
  Serial.begin(115200);
  pinMode(turbidezPin, INPUT);
  Serial.println("\n=== NEPTUS — SENSOR DE TURBIDEZ BLE ===");

  preferences.begin("turb_cal", true);
  coefA = preferences.getFloat("a", 2247.5);
  coefB = preferences.getFloat("b", -11038.0);
  coefC = preferences.getFloat("c", 13133.6);
  preferences.end();

  Serial.println("[MEMORIA] Coeficientes carregados na inicializacao:");
  Serial.print("  A = "); Serial.println(coefA, 6);
  Serial.print("  B = "); Serial.println(coefB, 6);
  Serial.print("  C = "); Serial.println(coefC, 6);
  Serial.println("=======================================\n");

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

  Serial.println("[BLE] Advertising iniciado. Aguardando conexoes ou comandos via Serial...");
}

// =============================================================================
// LOOP
// =============================================================================

void loop() {

  // --- VERIFICA COMANDOS VIA TERMINAL SERIAL ---
  if (Serial.available() > 0) {
    String comandoSerial = Serial.readStringUntil('\n');
    processarComando(comandoSerial);
  }

  // --- VERIFICA INATIVIDADE ---
  verificarInatividade();

  // --- MODO CALIBRAÇÃO ---
  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1;

        enviarMensagemBLE("LENDO");
        leiturasV[idx] = lerVoltagemPura(); // Armazena tensão real (já printada na função)

        Serial.print("[CAL] Amostra ");
        Serial.print(idx + 1);
        Serial.print("/6 gravada. (Tensão: ");
        Serial.print(leiturasV[idx], 4);
        Serial.println(" V)");

        estadoAtual      = (EstadoCalib)((int)estadoAtual + 1);
        confirmarLeitura = false;

        if (estadoAtual <= CAL_500) {
          int ntuProximo = (int)valoresNTU[(int)estadoAtual - 1];
          enviarMensagemBLE(String(ntuProximo) + "_NTU");
          Serial.print("[STATUS] Aguardando confirmacao para ");
          Serial.print(ntuProximo);
          Serial.println(" NTU.");
        }
      }
    }
    else if (estadoAtual == PROCESSAR) {
      enviarMensagemBLE("PROCESSANDO");
      Serial.println("[STATUS] Processando matriz de calibracao...");
      calcularNovaCurva(); // Calcula, imprime A, B e C, e salva na flash
      estadoAtual = IDLE;
    }
  }

  // --- MODO MONITORAMENTO (10 em 10 segundos) ---
  else {
    unsigned long agora = millis();
    if (agora - ultimoEnvio >= INTERVALO_MS) {
      ultimoEnvio = agora;
      
      // Gera o JSON com Voltagem, NTU e Nível
      String jsonData = criarJsonTurbidez();
      
      // Se tiver Bluetooth conectado, avisa o Front
      if (deviceConnected) {
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
      }
      
      // Sempre exibe no Serial Monitor para acompanhamento
      Serial.print("[MONITORAMENTO 10s] ");
      Serial.println(jsonData);
    }
  }
}
