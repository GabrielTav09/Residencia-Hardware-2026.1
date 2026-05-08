#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>

// UUIDs do serviço NUS
#define SERVICE_UUID        "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// --- Pino do sensor ---
const int turbidezPin = 35;

BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;

// --- Classificação da turbidez baseada no peixe caranha ---
String classificarTurbidez(float ntu) {
  if (ntu <= 50) {
    return "Boa para o peixe caranha (pouco turva)";
  } else if (ntu <= 250) {
    return "Moderada para o peixe caranha (turbidez mediana)";
  } else {
    return "Inadequada para o peixe caranha (muito turva)";
  }
}

// --- Função para ler turbidez ---
float lerTurbidez() {
  float voltagem = 0;
  // Leitura média para reduzir ruído
  for (int i = 0; i < 800; i++) {
    voltagem += ((float)analogRead(turbidezPin) / 4095.0) * 3.3; // ESP32 -> 3.3V
    delay(1);
  }
  voltagem /= 800.0;
  voltagem = voltagem * (5/3.3);
  
  Serial.print("Voltagem: ");
  Serial.println(voltagem);
  
  float ntu;
  // Aplicar fórmula do gráfico oficial
  if(voltagem < 1.2){
    ntu = 3000;
  }else if(voltagem > 2){
    ntu = 0;
  }else{
    ntu = 2247.5 * voltagem * voltagem - 11038 * voltagem + 13133.6;
  }
  
  Serial.print("NTU: ");
  Serial.println(ntu);
  
  return ntu;
}

// --- Função para criar JSON com dados da turbidez ---
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

// Callback do servidor BLE
class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("Cliente BLE conectado!");
  }
    
  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("Cliente BLE desconectado!");
    pServer->getAdvertising()->start();
  }
};

// Callback para escrita RX
class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) override {
    String rxValue = pCharacteristic->getValue();
    if (rxValue.length() > 0) {
      Serial.print("Recebido via RX: ");
      Serial.println(rxValue);
      
      // Responder com dados se solicitado
      if (rxValue == "GET_TURBIDEZ") {
        String jsonData = criarJsonTurbidez();
        pTxCharacteristic->setValue(jsonData.c_str());
        pTxCharacteristic->notify();
        //Serial.println("Dados enviados via comando: " + jsonData);
      }
    }
  }
};

void setup() {
  Serial.begin(115200);
  pinMode(turbidezPin, INPUT);
  
  Serial.println("Iniciando BLE Sensor de Turbidez (NUS)...");
    
  BLEDevice::init("ESP32-NEPTUS-TURB");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());
    
  BLEService *pService = pServer->createService(SERVICE_UUID);
    
  // Característica RX (recebe dados do cliente)
  BLECharacteristic *pRxCharacteristic = pService->createCharacteristic(
                                           CHARACTERISTIC_UUID_RX,
                                           BLECharacteristic::PROPERTY_WRITE
                                         );
  pRxCharacteristic->setCallbacks(new MyCallbacks());
    
  // Característica TX (envia dados para o cliente)
  pTxCharacteristic = pService->createCharacteristic(
                        CHARACTERISTIC_UUID_TX,
                        BLECharacteristic::PROPERTY_NOTIFY
                      );
    
  // Adicionar o descriptor BLE2902
  pTxCharacteristic->addDescriptor(new BLE2902());
    
  pTxCharacteristic->setValue("ESP32 Turbidez Sensor Ready");
  pService->start();
    
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
    
  Serial.println("ESP32 BLE Turbidez Sensor pronto para conexão!");
  Serial.println("Envie 'GET_TURBIDEZ' via BLE para solicitar leitura");
}

void loop() {
  if (deviceConnected) {
    // Enviar dados automaticamente a cada 5 segundos
    String jsonData = criarJsonTurbidez();
    pTxCharacteristic->setValue(jsonData.c_str());
    pTxCharacteristic->notify();
    
    Serial.println("Dados automáticos enviados: " + jsonData);
    delay(1000);
  } else {
    // Continuar lendo sensor mesmo sem conexão BLE
    float turbidez = lerTurbidez();
    Serial.println("Aguardando conexão BLE... Turbidez atual: " + String(turbidez) + " NTU");
    delay(1000);
  }
}