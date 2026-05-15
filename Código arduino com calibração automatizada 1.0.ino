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
// Define as etapas do processo: Espera, 0 NTU, 100 NTU... até Processar o cálculo final
enum EstadoCalib { IDLE, CAL_0, CAL_100, CAL_200, CAL_300, CAL_400, CAL_500, PROCESSAR };
EstadoCalib estadoAtual = IDLE;   // Começa em modo de espera (operação normal)
bool confirmarLeitura = false;    // Gatilho ativado pelo comando Bluetooth para capturar a amostra
float leiturasV[6];               // Armazena a voltagem média capturada em cada ponto (0 a 500)
const float valoresNTU[] = {0, 100, 200, 300, 400, 500}; // Valores reais de referência

/**
 * Envia uma string simples via Bluetooth para o aplicativo.
 * Usado para mensagens de status (ex: "COLOQUE_100_NTU").
 */
void enviarMensagemBLE(String msg) {
  if (deviceConnected) {
    pTxCharacteristic->setValue(msg.c_str());
    pTxCharacteristic->notify();
  }
}

/**
 * Calcula os novos coeficientes (a, b, c) da parábola usando Regressão Polinomial de 2º Grau.
 * Utiliza o método dos Mínimos Quadrados e a Regra de Cramer para resolver o sistema linear.
 */
void calcularNovaCurva() {
  double n = 6; // Número de pontos de calibração
  double sumX = 0, sumX2 = 0, sumX3 = 0, sumX4 = 0, sumY = 0, sumXY = 0, sumX2Y = 0;

  // Acumula os somatórios necessários para montar a matriz do sistema
  for (int i = 0; i < n; i++) {
    double x = leiturasV[i];     // Voltagem medida
    double y = valoresNTU[i];    // NTU de referência
    sumX += x;
    sumX2 += (x * x);
    sumX3 += (x * x * x);
    sumX4 += (x * x * x * x);
    sumY += y;
    sumXY += (x * y);
    sumX2Y += (x * x * y);
  }

  // Cálculo do determinante da matriz principal (3x3)
  double det = n * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX3 - sumX2 * sumX2);

  if (det != 0) {
    // Resolve os valores de a, b e c substituindo as colunas pelos resultados (Cramer)
    coefC = (sumY * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumXY * sumX4 - sumX2Y * sumX3) + sumX2 * (sumXY * sumX3 - sumX2Y * sumX2)) / det;
    coefB = (n * (sumXY * sumX4 - sumX2Y * sumX3) - sumY * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX2Y - sumXY * sumX2)) / det;
    coefA = (n * (sumX2 * sumX2Y - sumXY * sumX3) - sumX * (sumX * sumX2Y - sumX2Y * sumX2) + sumY * (sumX * sumX3 - sumX2 * sumX2)) / det;

    // Grava os novos coeficientes na memória permanente para não perder ao desligar
    preferences.begin("turb_cal", false);
    preferences.putFloat("a", coefA);
    preferences.putFloat("b", coefB);
    preferences.putFloat("c", coefC);
    preferences.end();
    
    enviarMensagemBLE("CALIB_OK"); // Avisa o app que o cálculo terminou
  }
}

/**
 * Lê a voltagem do sensor de forma estável.
 * Realiza 800 amostras e tira a média para eliminar ruído elétrico.
 */
float lerVoltagemPura() {
  float voltagem = 0;
  for (int i = 0; i < 800; i++) {
    // Converte leitura analógica (0-4095) para voltagem (0-3.3V)
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3;
    delay(1);
  }
  voltagem /= 800.0;
  // Converte a escala de 3.3V do ESP32 para a escala de 5.0V do sensor de turbidez
  return voltagem * (5.0 / 3.3);
}

/**
 * Aplica a fórmula matemática calibrada para converter Voltagem em NTU.
 */
float lerTurbidez() {
  float voltagem = lerVoltagemPura();
  float ntu;

  // Filtros de segurança baseados nos limites físicos do sensor
  if(voltagem < 1.2) ntu = 3000;
  else if(voltagem > 4.2) ntu = 0;
  else {
    // Aplicação da equação parabólica encontrada na calibração
    ntu = (coefA * voltagem * voltagem) + (coefB * voltagem) + coefC;
  }
  
  return (ntu < 0) ? 0 : ntu; // Garante que não retorne valores negativos
}

/**
 * Classifica a qualidade da água conforme as necessidades do peixe Caranha.
 */
String classificarTurbidez(float ntu) {
  if (ntu <= 50) return "Boa para o peixe caranha (pouco turva)";
  else if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)";
  else return "Inadequada para o peixe caranha (muito turva)";
}

/**
 * Gera um pacote JSON com todos os dados atuais para enviar ao Front-end.
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

// --- Callbacks do Servidor BLE ---

class MyServerCallbacks: public BLEServerCallbacks {
  // Chamado quando o celular conecta
  void onConnect(BLEServer* pServer) override { 
    deviceConnected = true; 
    Serial.println("Conectado");
  }
  // Chamado quando o celular desconecta
  void onDisconnect(BLEServer* pServer) override { 
    deviceConnected = false; 
    pServer->getAdvertising()->start(); // Reinicia o anúncio para permitir nova conexão
    Serial.println("Desconectado");
  }
};

/**
 * Gerencia os comandos recebidos do aplicativo.
 */
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    
    // Comando para iniciar ou cancelar a calibração
    if (rxValue == "START_CAL") {
      if (estadoAtual == IDLE) {
        estadoAtual = CAL_0; // Entra na máquina de estados
        confirmarLeitura = false;
        enviarMensagemBLE("COLOQUE_0_NTU");
      } else {
        estadoAtual = IDLE; // Se já estava calibrando, o usuário cancelou
        enviarMensagemBLE("CALIB_CANCELADA");
      }
    } 
    // Comando enviado pelo botão de confirmação do App após o usuário trocar o frasco
    else if (rxValue == "CONFIRM_STEP") {
      if (estadoAtual != IDLE && estadoAtual != PROCESSAR) {
        confirmarLeitura = true; // Autoriza o loop a fazer a leitura
      }
    }
    // Solicitação manual de dados de turbidez
    else if (rxValue == "GET_TURBIDEZ") {
      if (estadoAtual == IDLE) {
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

  // Inicializa a memória flash e tenta recuperar os coeficientes salvos anteriormente
  preferences.begin("turb_cal", true); // Abre em modo leitura
  coefA = preferences.getFloat("a", 2247.5); // Se não existir, usa o valor padrão original
  coefB = preferences.getFloat("b", -11038.0);
  coefC = preferences.getFloat("c", 13133.6);
  preferences.end();

  // Configuração básica do Bluetooth Low Energy
  BLEDevice::init("ESP32-NEPTUS-TURB");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  // Característica para RECEBER comandos (RX)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
  
  // Característica para ENVIAR dados (TX) com Notificação
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising();
  Serial.println("Pronto para uso");
}

void loop() {
  // --- Lógica da Máquina de Estados da Calibração ---
  if (estadoAtual != IDLE) {
    // Se estamos em uma das fases de coleta (0 a 500 NTU)
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      if (confirmarLeitura) {
        int idx = (int)estadoAtual - 1; // Mapeia o estado para o índice do array (0 a 5)
        
        enviarMensagemBLE("LENDO...");
        leiturasV[idx] = lerVoltagemPura(); // Captura a voltagem média da amostra atual
        
        // Passa para o próximo estado (ex: de CAL_0 para CAL_100)
        estadoAtual = (EstadoCalib)((int)estadoAtual + 1);
        confirmarLeitura = false; // Trava e espera o próximo clique do usuário no app

        // Se ainda houver amostras, solicita a próxima ao usuário
        if (estadoAtual <= CAL_500) {
          String proximo = "COLOQUE_" + String((int)valoresNTU[(int)estadoAtual - 1]) + "_NTU";
          enviarMensagemBLE(proximo);
        }
      }
    } 
    // Se todos os pontos foram coletados, gera a nova fórmula
    else if (estadoAtual == PROCESSAR) {
      enviarMensagemBLE("PROCESSANDO...");
      calcularNovaCurva();
      estadoAtual = IDLE; // Retorna ao modo de operação normal
    }
  }

  // --- Operação Normal de Envio Automático ---
  if (deviceConnected && estadoAtual == IDLE) {
    String jsonData = criarJsonTurbidez();
    pTxCharacteristic->setValue(jsonData.c_str());
    pTxCharacteristic->notify();
    delay(2000); // Aguarda 2 segundos antes de enviar a próxima atualização
  }
}
