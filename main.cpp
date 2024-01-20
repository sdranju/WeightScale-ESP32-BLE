/* 
* ESP32 Weight Scale
* Shamsuddoha Ranju
* Github: https://github.com/sdranju
*/

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <HX711_ADC.h>


// Standard UUID's for BLE Weight Scale
#define SERVICE_UUID        "0000181D-0000-1000-8000-00805f9b34fb"
#define MEASURE_CHAR_UUID   "00002A9D-0000-1000-8000-00805f9b34fb"

// HX711 connection pins
#define SCK  23  //SCK OUTPUT
#define DT   22  //DT INPUT

// General variables
float preSetCalibValue = 1.0;  /* set your calibration value here */ 
bool deviceConnected = false;
bool oldDeviceConnected = false;


// initialize
HX711_ADC LoadCell(DT, SCK);

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristic = NULL;
BLEDescriptor *pDescr;
BLE2902 *pBLE2902;


// declare functions
float get_weigh();
void setup_bleserver();
void prepare_weight_for_ble(long weightInGram);


// BLE server callback routine
class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};


void setup() {
  Serial.begin(115200);
  delay(10);
  Serial.println("\n");

  //initialize loadcell
  LoadCell.begin();
  LoadCell.start(2000, true);  //stabilize and tare on start
  delay(200);

  Serial.println("\nInitializing LoadCell...");
  if (LoadCell.getTareTimeoutFlag() || LoadCell.getSignalTimeoutFlag()) {
    Serial.println("\nTimeout, check wiring for MCU <> HX711");
    //while (1);
  } else {
    Serial.println("\nSetting CalFactor...");
    LoadCell.setCalFactor(preSetCalibValue);  // set calibration value
  }

  // setup BLE server
  setup_bleserver();

  Serial.println("\n-- READY --");
}


void loop() {
  float weightData = get_weigh();  // get weight

  // notify the weight if a device is connected
  if (deviceConnected) {  
    prepare_weight_for_ble(abs(weightData));  // make sure that the value is always positive
  }

  // disconnecting
  if (!deviceConnected && oldDeviceConnected) {
    delay(500); // give the bluetooth stack the chance to get things ready
    pServer->startAdvertising(); // restart advertising
    Serial.println("Start advertising...\n");
    oldDeviceConnected = deviceConnected;
  }

  // connecting
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
  }
  
  delay(10);
}


// setup BLE
void setup_bleserver() {
  const char* dev_name = "WEIGHT-SCALE";  // set device name

  // Create the BLE Device
  BLEDevice::init(dev_name);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Weight-measurement Characteristic
  pCharacteristic = pService->createCharacteristic(
                      MEASURE_CHAR_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );                   

  // Create a BLE Descriptor
  pDescr = new BLEDescriptor((uint16_t)0x2901);
  pDescr->setValue(dev_name);  // setting same as device name
  pCharacteristic->addDescriptor(pDescr);
  
  pBLE2902 = new BLE2902();
  pBLE2902->setNotifications(true);
  
  // Add all Descriptors here
  pCharacteristic->addDescriptor(pBLE2902);
  pCharacteristic->setNotifyProperty(true);

  // Start the service
  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();
  Serial.println("Waiting for a BLE client to notify...\n");

}


// Sets our BLE Characteristic given a weight measurement (in kg)
// as per GATT standard
void prepare_weight_for_ble(long weightInGram) {
  uint16_t newVal = 0;           // field value: weight in BLE format
  unsigned char flags = 0;       // description of the weight
  unsigned char bytes[3] = {0};  // encoded data for transmission
  
  /*
   * Set the flags:
   * bit 0 => 0 means we're reporting in SI units (kg and meters)
   * bit 1 => 0 means there is no timestamp in our report
   * bit 2 => 0 means there is no User ID in our report
   * bit 3 => 0 means no BMI and Height data are present in our report
   * bits 4 to 7 are reserved, and set to zero
   */

  flags |= 0x0 << 0;
  flags |= 0x0 << 1;
  flags |= 0x0 << 2;
  flags |= 0x0 << 3;
  
  // Important: Convert the weight into BLE representation
  newVal = (uint16_t)((weightInGram / 5) + 0.5);

  /*
   * We set the value and notify the BLE Client every time we make a measurement, 
   * even if the value hasn't been changed.
   */

  bytes[0] = flags;

  // BLE GATT multi-byte values are encoded Least-Significant Byte first
  bytes[1] = (unsigned char) newVal;
  bytes[2] = (unsigned char) (newVal >> 8);

  pCharacteristic->setValue(bytes, sizeof(bytes));
  pCharacteristic->notify();
}


//get weight from loadcell
float get_weigh() {
  static boolean newDataReady = false;
  static float newWeigh = 0.0;

  // Check for new data
  if (LoadCell.update()) {
    newDataReady = true;
  }

  // Get weight from the sensor
  if (newDataReady) {
    newWeigh = LoadCell.getData();
    if (abs(newWeigh) < 20.0) {  // kill small fluctuation
      newWeigh = 0.0;
    }

    newDataReady = false;
  }

  return newWeigh;
}
