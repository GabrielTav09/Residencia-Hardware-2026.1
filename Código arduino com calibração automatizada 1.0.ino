#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <Preferences.h> // Para salvar os novos coeficientes na memória flash

// UUIDs do serviço NUS
#define SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- Pinos ---
const int turbidezPin = 35;
const int pinoBotaoCalib = 4; // Pino para o botão de calibração (ajuste se necessário)

BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;
Preferences preferences;

// --- Variáveis da Fórmula (Coeficientes Dinâmicos) ---
float coefA = 2247.5;  // Valores padrão originais 
float coefB = -11038.0;
float coefC = 13133.6;

// --- Lógica de Calibração ---
enum EstadoCalib { IDLE, CAL_0, CAL_100, CAL_200, CAL_300, CAL_400, CAL_500, PROCESSAR };
EstadoCalib estadoAtual = IDLE;
float leiturasV[6]; // Armazena voltagens para 0, 100, 200, 300, 400, 500 NTU
const float valoresNTU[] = {0, 100, 200, 300, 400, 500};

// Função para calcular a nova parábola (Mínimos Quadrados)
void calcularNovaCurva() {
  double n = 6;
  double sumX = 0, sumX2 = 0, sumX3 = 0, sumX4 = 0;
  double sumY = 0, sumXY = 0, sumX2Y = 0;

  for (int i = 0; i < n; i++) {
    double x = leiturasV[i];
    double y = valoresNTU[i];
    sumX += x;
    sumX2 += (x * x);
    sumX3 += (x * x * x);
    sumX4 += (x * x * x * x);
    sumY += y;
    sumXY += (x * y);
    sumX2Y += (x * x * y);
  }

  // Resolução via Regra de Cramer para sistema 3x3
  double det = n * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX3 - sumX2 * sumX2);

  if (det != 0) {
    coefC = (sumY * (sumX2 * sumX4 - sumX3 * sumX3) - sumX * (sumXY * sumX4 - sumX2Y * sumX3) + sumX2 * (sumXY * sumX3 - sumX2Y * sumX2)) / det;
    coefB = (n * (sumXY * sumX4 - sumX2Y * sumX3) - sumY * (sumX * sumX4 - sumX2 * sumX3) + sumX2 * (sumX * sumX2Y - sumXY * sumX2)) / det;
    coefA = (n * (sumX2 * sumX2Y - sumXY * sumX3) - sumX * (sumX * sumX2Y - sumX2Y * sumX2) + sumY * (sumX * sumX3 - sumX2 * sumX2)) / det;

    // Salva os novos valores permanentemente
    preferences.begin("turb_cal", false);
    preferences.putFloat("a", coefA);
    preferences.putFloat("b", coefB);
    preferences.putFloat("c", coefC);
    preferences.end();
    
    Serial.println("Calibração finalizada e salva!");
  }
}

// --- Classificação (Mantida original) ---
String classificarTurbidez(float ntu) {
  if (ntu <= 50) return "Boa para o peixe caranha (pouco turva)"; [cite: 2]
  else if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)"; [cite: 3]
  else return "Inadequada para o peixe caranha (muito turva)"; [cite: 4]
}

// --- Função de Leitura de Voltagem (Encapsulada a lógica de 800 amostras) ---
float lerVoltagemPura() {
  float voltagem = 0;
  for (int i = 0; i < 800; i++) { [cite: 6]
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3; [cite: 7]
    delay(1);
  }
  voltagem /= 800.0;
  return voltagem * (5.0 / 3.3); // Ajuste de escala [cite: 7]
}

// --- Função para ler turbidez (Atualizada com coeficientes variáveis) ---
float lerTurbidez() {
  float voltagem = lerVoltagemPura();
  Serial.print("Voltagem: ");
  Serial.println(voltagem);
  
  float ntu;
  if(voltagem < 1.2) ntu = 3000; [cite: 9]
  else if(voltagem > 4.2) ntu = 0; // Ajuste para o limite do sensor em 5V
  else {
    // Aplica a fórmula com os coeficientes atuais (calibrados ou padrão)
    ntu = (coefA * voltagem * voltagem) + (coefB * voltagem) + coefC; [cite: 10]
  }
  
  if (ntu < 0) ntu = 0;
  Serial.print("NTU: ");
  Serial.println(ntu);
  return ntu;
}

String criarJsonTurbidez() {
  float turbidez = lerTurbidez();
  String nivel = classificarTurbidez(turbidez); [cite: 12]
  StaticJsonDocument<200> json;
  json["turbidez"] = turbidez;
  json["nivel"] = nivel;
  json["timestamp"] = millis();
  String resposta;
  serializeJson(json, resposta);
  return resposta; [cite: 13]
}

// Callbacks BLE (Mantidos originais)
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override { deviceConnected = true; Serial.println("Cliente conectado!"); } [cite: 14]
  void onDisconnect(BLEServer* pServer) override { deviceConnected = false; Serial.println("Cliente desconectado!"); pServer->getAdvertising()->start(); } [cite: 15]
};

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    if (rxValue == "GET_TURBIDEZ") {
      String jsonData = criarJsonTurbidez();
      pTxCharacteristic->setValue(jsonData.c_str()); [cite: 18]
      pTxCharacteristic->notify();
    }
  }
};

void setup() {
  Serial.begin(115200); [cite: 19]
  pinMode(turbidezPin, INPUT);
  pinMode(pinoBotaoCalib, INPUT_PULLUP);

  // Carrega calibração anterior, se existir
  preferences.begin("turb_cal", true);
  coefA = preferences.getFloat("a", 2247.5);
  coefB = preferences.getFloat("b", -11038.0);
  coefC = preferences.getFloat("c", 13133.6);
  preferences.end();

  BLEDevice::init("ESP32-NEPTUS-TURB"); [cite: 19]
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
  BLEService *pService = pServer->createService(SERVICE_UUID); [cite: 20]
  
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE); [cite: 21]
  pRxCharacteristic->setCallbacks(new MyCallbacks()); [cite: 22]
  
  pTxCharacteristic = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY); [cite: 23]
  pTxCharacteristic->addDescriptor(new BLE2902());
  
  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::startAdvertising(); [cite: 24]
  Serial.println("ESP32 Pronto. Pressione o botão para calibrar.");
}

void loop() {
  // --- Máquina de Estados da Calibração ---
  if (digitalRead(pinoBotaoCalib) == LOW && estadoAtual == IDLE) {
    delay(50); // Debounce
    estadoAtual = CAL_0;
    Serial.println("Iniciando Calibração: Coloque em água 0 NTU");
  }

  if (estadoAtual != IDLE) {
    if (estadoAtual >= CAL_0 && estadoAtual <= CAL_500) {
      delay(5000); // 5 segundos para estabilizar a amostra
      int idx = (int)estadoAtual - 1;
      leiturasV[idx] = lerVoltagemPura();
      Serial.printf("Lido %.2fV para %.0f NTU\n", leiturasV[idx], valoresNTU[idx]);
      
      estadoAtual = (EstadoCalib)((int)estadoAtual + 1);
      if (estadoAtual <= CAL_500) {
        Serial.printf("Próximo: Coloque em %.0f NTU\n", valoresNTU[(int)estadoAtual - 1]);
      }
    } else {
      calcularNovaCurva();
      estadoAtual = IDLE;
    }
  }

  // --- Lógica de Transmissão BLE Original ---
  if (deviceConnected && estadoAtual == IDLE) {
    String jsonData = criarJsonTurbidez();
    pTxCharacteristic->setValue(jsonData.c_str()); [cite: 26]
    pTxCharacteristic->notify();
    delay(5000); // Envio automático a cada 5s como no original
  } else if (estadoAtual == IDLE) {
    float turbidez = lerTurbidez(); [cite: 27]
    delay(1000);
  }
}
