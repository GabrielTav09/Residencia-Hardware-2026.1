#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include "esp_adc_cal.h" // Biblioteca para calibração/regulagem de tensão do ADC


// UUIDs do serviço NUS
#define SERVICE_UUID          "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_RX "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define CHARACTERISTIC_UUID_TX "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


// --- Pino do sensor ---
const int turbidezPin = 35;


BLECharacteristic *pTxCharacteristic;
bool deviceConnected = false;


// Configurações para a calibração automática de tensão
esp_adc_cal_characteristics_t *adc_chars;


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


// --- Função para ler turbidez com Regulagem de Tensão Automática ---
float lerTurbidez() {
  uint32_t leituraAcumulada = 0;
 
  // Leitura média de 800 amostras para reduzir ruído e captar variação de material
  for (int i = 0; i < 800; i++) {
    leituraAcumulada += analogRead(turbidezPin);
    delay(1);
  }
  uint32_t leituraMedia = leituraAcumulada / 800;


  // Regulagem Automática: Converte o valor bruto (0-4095) em milivolts reais
  // usando os dados de calibração de fábrica do ESP32 (VRef)
  uint32_t voltagemmV = esp_adc_cal_raw_to_voltage(leituraMedia, adc_chars);
 
  // Converte milivolts para Volts (0.0 a 3.3V)
  float voltagemEscalada = (float)voltagemmV / 1000.0;
 
  // Ajuste para a escala de 5V conforme seu código original
  float voltagem = voltagemEscalada * (5.0 / 3.3);
 
  Serial.print("Voltagem Regulada: ");
  Serial.println(voltagem);
 
  float ntu;
  // Aplicar fórmula do gráfico oficial
  if(voltagem < 1.2){
    ntu = 3000;
  }else if(voltagem > 2.0){ // Valor ajustado conforme sua lógica original
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
     
      if (rxValue == "GET_TURBIDEZ") {
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


  // --- Inicialização da Regulagem Automática do ADC ---
  adc_chars = (esp_adc_cal_characteristics_t *)calloc(1, sizeof(esp_adc_cal_characteristics_t));
  esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, adc_chars);
 
  Serial.println("Iniciando BLE Sensor de Turbidez (NUS)...");
   
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
  pTxCharacteristic->setValue("ESP32 Turbidez Sensor Ready");
  pService->start();
   
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06);
  pAdvertising->setMinPreferred(0x12);
  BLEDevice::startAdvertising();
   
  Serial.println("ESP32 BLE Turbidez Sensor pronto para conexão!");
}


void loop() {
  if (deviceConnected) {
    String jsonData = criarJsonTurbidez();
    pTxCharacteristic->setValue(jsonData.c_str());
    pTxCharacteristic->notify();
   
    Serial.println("Dados automáticos enviados: " + jsonData);
    delay(1000);
  } else {
    float turbidez = lerTurbidez();
    Serial.println("Aguardando conexão BLE... Turbidez atual: " + String(turbidez) + " NTU");
    delay(1000);
  }
}
