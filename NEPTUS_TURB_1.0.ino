#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <Preferences.h> // Biblioteca para salvar dados na memória flash (permanente) do ESP32

// --- Definições de UUIDs para o protocolo Bluetooth NUS (Nordic UART Service) ---
#define SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- Configurações de Hardware ---
const int turbidezPin = 35; // Pino analógico onde o sensor de turbidez está conectado

// --- Variáveis Globais de Controle ---
BLECharacteristic *pTxCharacteristic; // Objeto para enviar dados para o celular
bool deviceConnected = false;         // Status da conexão Bluetooth
Preferences preferences;              // Objeto para gerenciar a memória interna (NVS)

// --- Variáveis da Fórmula de Turbidez (NTU = ax² + bx + c) ---
float coefA, coefB, coefC; 

// --- Máquina de Estados para Calibração ---
enum EstadoCalib { IDLE, CAL_0, CAL_100, CAL_200, CAL_300, CAL_400, CAL_500, PROCESSAR };
EstadoCalib estadoAtual = IDLE;   // Começa em modo de espera (operação normal)
bool confirmarLeitura = false;    // Gatilho ativado pelo comando Bluetooth "CONFIRM_STEP" para capturar a amostra
float leiturasV[6];               // Armazena a voltagem média capturada em cada ponto (0 a 500)
const float valoresNTU[] = {0, 100, 200, 300, 400, 500}; // Valores reais de referência para o cálculo

/**
 * Envia uma string simples via Bluetooth para o aplicativo.
 * Usado para avisar o usuário qual frasco colocar.
 */
void enviarMensagemBLE(String msg) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
}

/**
 * Calcula os novos coeficientes (a, b, c) da parábola usando Regressão Polinomial de 2º Grau.
 * Esta função ajusta a curva matemática para que ela se adapte perfeitamente aos pontos medidos.
 */
void calcularNovaCurva() {
  double n = 6; // Número total de pontos coletados
  double sumX = 0, sumX2 = 0, sumX3 = 0, sumX4 = 0, sumY = 0, sumXY = 0, sumX2Y = 0;

  // Realiza os somatórios matemáticos necessários para a Regressão Linear Múltipla
  for (int i = 0; i < n; i++) {
    double x = leiturasV[i];     // Voltagem capturada
    double y = valoresNTU[i];    // Valor de NTU correspondente
    sumX += x;
    sumX2 += (x * x);
    sumX3 += (x * x * x);
    sumX4 += (x * x * x * x);
    sumY += y;
    sumXY += (x * y);
    sumX2Y += (x * x * y);
  }

  // Cálculo do determinante para resolver o sistema de equações (Regra de Cramer)
  double det = n * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX3 - sumX2 * sumX2);

  if (det != 0) {
    // Calcula os coeficientes finais da fórmula
    coefC = (sumY * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumXY * sumX4 - sumX2Y * sumX3) + sumX2 * (sumXY * sumX3 - sumX2Y * sumX2)) / det;
    coefB = (n * (sumXY * sumX4 - sumX2Y * sumX3) - sumY * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX2Y - sumXY * sumX2)) / det;
    coefA = (n * (sumX2 * sumX2Y - sumXY * sumX3) - sumX * (sumX * sumX2Y - sumX2 * sumXY) + sumY * (sumX * sumX3 - sumX2 * sumX2)) / det;

    // Abre a memória flash e salva os coeficientes para que persistam após o reboot
    preferences.begin("turb_cal", false);
    preferences.putFloat("a", coefA);
    preferences.putFloat("b", coefB);
    preferences.putFloat("c", coefC);
    preferences.end();
    
    enviarMensagemBLE("CALIB_OK"); // Notifica o Front-end que o processo foi concluído
  }
}

/**
 * Realiza a leitura da voltagem no pino analógico.
 * Tira a média de 800 leituras para garantir que o valor seja estável e sem interferências.
 */
float lerVoltagemPura() {
  float voltagem = 0;
  for (int i = 0; i < 800; i++) {
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3;
    delay(1);
  }
  voltagem /= 800.0;
  // Converte a voltagem de leitura (máx 3.3V do ESP32) para a escala do sensor (máx 5.0V)
  return voltagem * (5.0 / 3.3);
}

/**
 * Converte a voltagem lida para o valor de turbidez (NTU) usando a fórmula calibrada.
 */
float lerTurbidez() {
  float voltagem = lerVoltagemPura();
  float ntu;

  // Limites de segurança para evitar valores irreais fora da curva
  if(voltagem < 1.2) ntu = 3000;
  else if(voltagem > 4.2) ntu = 0;
  else {
    // Aplicação da fórmula quadrática atualizada
    ntu = (coefA * voltagem * voltagem) + (coefB * voltagem) + coefC;
  }
  
  return (ntu < 0) ? 0 : ntu; // Não permite valores negativos de NTU
}

/**
 * Traduz o valor numérico em uma mensagem de texto sobre a qualidade da água.
 */
String classificarTurbidez(float ntu) {
  if (ntu <= 50) return "Boa para o peixe caranha (pouco turva)";
  else if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)";
  else return "Inadequada para o peixe caranha (muito turva)";
}

/**
 * Prepara o objeto JSON com os dados para envio via Bluetooth.
 */
String criarJsonTurbidez() {
  float turbidez = lerTurbidez();
  String nivel = classificarTurbidez(turbidez);
  
  StaticJsonDocument<200> json;
  json["turbidez"] = turbidez;
  json["nivel"] = nivel;
  json["timestamp"] = millis();
  
  String resposta;
  serializeJson(json, resposta);
  return resposta;
}

// --- Callbacks de conexão do Bluetooth ---
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override { deviceConnected = true; }
  void onDisconnect(BLEServer* pServer) override { 
    deviceConnected = false; 
    pServer->getAdvertising()->start(); // Permite que o ESP32 seja encontrado novamente
  }
};

// --- Callbacks de recebimento de mensagens do Celular ---
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    
    // Inicia o processo de calibração passo a passo
    if (rxValue == "START_CAL") {
      if (estadoAtual == IDLE) {
        estadoAtual = CAL_0; 
        confirmarLeitura = false;
        enviarMensagemBLE("COLOQUE_0_NTU"); // Solicita a primeira amostra ao usuário
      } else {
        estadoAtual = IDLE; // Cancela e retorna IMEDIATAMENTE ao estado de medição normal se acionado de novo
        enviarMensagemBLE("CALIB_CANCELADA");
      }
    } 
    // Confirma que a amostra foi trocada e autoriza a leitura do ponto atual
    else if (rxValue == "CONFIRM_STEP") {
      if (estadoAtual != IDLE && estadoAtual != PROCESSAR) {
        confirmarLeitura = true; // Avisa o loop principal para coletar o dado
      }
    }
    // Requisição manual de leitura (Bloqueada se estiver em modo de calibração)
    else if (rxValue == "GET_TURBIDEZ") {
      if (estadoAtual == IDLE) { // Só responde se o ESP não estiver no modo calibração
        String jsonData = criarJsonTurbidez();
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(turbidezPin, INPUT);

  // Carrega os últimos valores de calibração salvos ou usa os padrões se for a primeira vez
  preferences.begin("turb_cal", true);
  coefA = preferences.getFloat("a", 2247.5);
  coefB = preferences.getFloat("b", -11038.0);
  coefC = preferences.getFloat("c", 13133.6);
  preferences.end();

  // Inicialização do Servidor Bluetooth
  BLEDevice::init("ESP32-NEPTUS-TURB");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Porta de entrada de dados (Celular -> ESP32)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  // Porta de saída de dados (ESP32 -> Celular)
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
}

void loop() {
  // --- MODO DE CALIBRAÇÃO (Ativo quando estadoAtual NÃO for IDLE) ---
  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1; // Identifica qual posição do array preencher
        
        enviarMensagemBLE("LENDO...");
        leiturasV[idx] = lerVoltagemPura(); // Registra a voltagem média do frasco atual
        
        // Move para o próximo nível (0 -> 100 -> 200...)
        estadoAtual = (EstadoCalib)((int)estadoAtual + 1);
        confirmarLeitura = false; // Bloqueia e aguarda nova confirmação do usuário do App

        // Se ainda não chegou no fim, pede o próximo frasco
        if (estadoAtual <= CAL_500) {
          String proximo = "COLOQUE_" + String((int)valoresNTU[(int)estadoAtual - 1]) + "_NTU";
          enviarMensagemBLE(proximo);
        }
      }
    } 
    // Após coletar todos os 6 pontos, calcula a nova fórmula final
    else if (estadoAtual == PROCESSAR) {
      enviarMensagemBLE("PROCESSANDO...");
      calcularNovaCurva();
      estadoAtual = IDLE; // Retorna automaticamente ao modo de monitoramento normal
    }
  }
  // --- MODO DE MEDIÇÃO DE NTU NORMAL (Executado estritamente quando NÃO está calibrando) ---
  else {
    if (deviceConnected) {
      String jsonData = criarJsonTurbidez();
      pTxCharacteristic->setValue(jsonData.c_str());
      pTxCharacteristic->notify();
      delay(2000); // Envia os dados de leitura normais a cada 2 segundos
    }
  }
}
