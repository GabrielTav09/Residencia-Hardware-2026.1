#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h> // Compatível com sua versão 7.4.3


// UUIDs do serviço NUS
#define SERVICE_UUID           "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


const int turbidezPin = 35;
BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;


// --- Classificação da turbidez ---
String classificarTurbidez(float ntu) {
  if (ntu <= 50) return "Boa para o peixe caranha (pouco turva)";
  if (ntu <= 250) return "Moderada para o peixe caranha (turbidez mediana)";
  return "Inadequada para o peixe caranha (muito turva)";
}


// --- Função para ler turbidez com Regulagem de Tensão ---
float lerTurbidez() {
  float somaVoltagem = 0;
 
  // Mantendo suas 800 amostras originais para precisão de material
  for (int i = 0; i < 800; i++) {
    // Regulagem automática: O ESP32 lê de 0 a 4095.
    // Multiplicamos por 3.3 (voltagem real do chip) para estabilizar a leitura.
    float leitura = (float)analogRead(turbidezPin);
    somaVoltagem += (leitura / 4095.0) * 3.3;
    delay(1);
  }
 
  float voltagemMedia = somaVoltagem / 800.0;
 
  // Conversão para escala de 5V conforme seu original
  float voltagem = voltagemMedia * (5.0 / 3.3);
 
  Serial.print("Voltagem: ");
  Serial.println(voltagem);
 
  float ntu;
  if(voltagem < 1.2) {
    ntu = 3000;
  } else if(voltagem > 2.0) {
    ntu = 0;
  } else {
    // Sua fórmula original
    ntu = 2247.5 * voltagem * voltagem - 11038 * voltagem + 13133.6;
  }
 
  return ntu;
}


// --- Função para criar JSON (Atualizada para ArduinoJson 7) ---
String criarJsonTurbidez() {
  float turbidez = lerTurbidez();
  String nivel = classificarTurbidez(turbidez);
 
  // Na versão 7.x, usamos apenas JsonDocument (ela gerencia a memória sozinha)
  JsonDocument json;
 
  json["turbidez"] = turbidez;
  json["nivel"] = nivel;
  json["timestamp"] = millis();
 
  String resposta;
  serializeJson(json, resposta);
  return resposta;
}


// ... (Callbacks do Servidor BLE permanecem os mesmos) ...
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override { deviceConnected = true; }
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    BLEDevice::startAdvertising();
  }
};


class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    if (rxValue == "GET_TURBIDEZ") {
      String jsonData = criarJsonTurbidez();
      pTxCharacteristic->setValue(jsonData.c_str());
      pTxCharacteristic->notify();
    }
  }
};


void setup() {
  Serial.begin(115200);
  pinMode(turbidezPin, INPUT);
 
  BLEDevice::init("ESP32-NEPTUS-TURB");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
 
  BLEService *pService = pServer->createService(SERVICE_UUID);
 
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                         CHARACTERISTIC_UUID_RX,
                                         BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyCallbacks());
 
  pTxCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID_TX,
                      BLECharacteristic::PROPERTY_NOTIFY);
  pTxCharacteristic->addDescriptor(new BLE2902());
 
  pService->start();
  BLEDevice::startAdvertising();
  Serial.println("Pronto!");
}


void loop() {
  String jsonData = criarJsonTurbidez();
  if (deviceConnected) {
    pTxCharacteristic->setValue(jsonData.c_str());
    pTxCharacteristic->notify();
  }
  Serial.println(jsonData);
  delay(1000);
}
