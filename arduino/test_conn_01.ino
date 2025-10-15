#include <NimBLEDevice.h>

// ------------------------------------------------------------------
// VALETON GP-5 PARAMETERS PROVIDED BY USER
// ------------------------------------------------------------------
// The target device's address and name
static NimBLEAddress Valeton_Address("C2:98:81:98:B5:48"); 
static const char* Valeton_Name = "GP-5 BLE";

// Target 128-bit Service and Characteristic UUIDs
static const char* Valeton_Service_UUID_Str = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
static const char* Valeton_Char_UUID_Str = "7772E5DB-3868-4112-A1A9-F2669D106BF3";

// UUID objects for discovery
NimBLEUUID Valeton_Service_UUID(Valeton_Service_UUID_Str);
NimBLEUUID Valeton_Char_UUID(Valeton_Char_UUID_Str);

// Pointers to the discovered objects
NimBLEClient* pClient = nullptr;
NimBLERemoteCharacteristic* pSysExChar = nullptr;

// Connection state flags
bool doConnect = false;
bool connected = false;

// ------------------------------------------------------------------
// SysEx (MIDI BLE) TEST DATA
// Example SysEx message: F0 [Manufacturer ID] [Data] F7
// NOTE: Actual GP-5 commands may differ and might need fragmentation
// due to the standard 20-byte BLE MTU limit.
// ------------------------------------------------------------------
uint8_t sysExTestMessage[] = {0xF0, 0x41, 0x00, 0x01, 0x02, 0x03, 0xF7}; 
const size_t sysExTestMessageLen = sizeof(sysExTestMessage);


/**
 * @brief Handles NOTIFY messages received from the Valeton GP-5.
 */
void notifyCallback(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    Serial.print("Notification/Indication received: ");
    Serial.print(pRemoteCharacteristic->getUUID().toString().c_str());
    Serial.print(" Data length: ");
    Serial.println(length);
    // Process the response/status data sent by the GP-5 here.
    Serial.print("Data: ");
    for (int i = 0; i < length; i++) {
        Serial.printf("%02X ", pData[i]);
    }
    Serial.println();
}

/**
 * @brief Discovers the required service and characteristic after connection.
 * @return true if the SysEx characteristic is found and configured.
 */
bool setupValetonCommunication() {
    Serial.println("Discovering services...");
    
    // 1. Search for the Service
    NimBLERemoteService* pRemoteService = pClient->getService(Valeton_Service_UUID);

    if (pRemoteService == nullptr) {
        Serial.print("ERROR: Valeton Service UUID not found: ");
        Serial.println(Valeton_Service_UUID_Str);
        return false;
    }

    Serial.println("-> Valeton Service found.");

    // 2. Search for the Characteristic
    pSysExChar = pRemoteService->getCharacteristic(Valeton_Char_UUID);

    if (pSysExChar == nullptr) {
        Serial.print("ERROR: SysEx Characteristic UUID not found: ");
        Serial.println(Valeton_Char_UUID_Str);
        return false;
    }

    Serial.println("-> SysEx Characteristic found.");

    // 3. Set up NOTIFY (if the characteristic supports it)
    if (pSysExChar->canNotify()) {
        if(pSysExChar->subscribe(true, notifyCallback)) {
            Serial.println("-> Notifications successfully subscribed.");
        } else {
            Serial.println("ERROR: Failed to subscribe to notifications.");
        }
    } else {
         Serial.println("-> Notification (NOTIFY) not supported by the characteristic.");
    }
    
    return true;
}

/**
 * @brief Handles BLE events (e.g., connect/disconnect).
 */
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) {
        Serial.println("Successfully connected to Valeton GP-5!");
        connected = true;
        // Suggested connection parameter update for stability
        pClient->updateConnParams(120, 120, 0, 60); 
    }

    void onDisconnect(NimBLEClient* pClient) {
        Serial.print("Disconnected from: ");
        Serial.println(pClient->getPeerAddress().toString().c_str());
        connected = false;
        doConnect = true; // Set flag to re-initiate scanning for reconnection
    }
};
static ClientCallbacks clientCB;

/**
 * @brief Handles scan results and initiates connection when the target is found.
 */
class AdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        
        // Check if the address matches OR (Name matches AND Address matches)
        if (advertisedDevice->getAddress().equals(Valeton_Address) || 
            (advertisedDevice->getName() == Valeton_Name && advertisedDevice->getAddress().equals(Valeton_Address)) ) 
        {
            Serial.print("GP-5 BLE Advertisement found! Address: ");
            Serial.println(advertisedDevice->getAddress().toString().c_str());

            advertisedDevice->getScan()->stop(); // Stop scanning
            pClient = NimBLEDevice::createClient(); 
            pClient->setClientCallbacks(&clientCB);
            pClient->setConnectionParams(120, 120, 0, 60); // Set connection parameters
            
            // Attempt to connect
            if (pClient->connect(advertisedDevice)) {
                Serial.println("Connection attempt started...");
                doConnect = true;
            } else {
                Serial.println("ERROR: Failed to initiate connection.");
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client initialization...");

    NimBLEDevice::init(""); // Initialize BLE stack

    // Set high transmit power (P9 is a common high level for ESP32)
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); 
    
    // Scanning setup
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks());
    pScan->setActiveScan(true); // Active scan to gather the device name
    pScan->setInterval(100);
    pScan->setWindow(99); 
    
    Serial.println("Starting scan to find GP-5 BLE device...");
    // Start scanning with 0 duration (indefinite, stopped by the callback)
    pScan->start(0, nullptr, false); 
}

void loop() {
    // If doConnect is true, either a successful connection occurred, or we need to reconnect.
    if (doConnect) {
        doConnect = false; // Run once per state change

        if (connected) {
            // This part runs AFTER A SUCCESSFUL CONNECTION
            if (setupValetonCommunication()) {
                
                // -----------------------------------------------------------
                //  MAIN COMMUNICATION: SENDING SysEx MESSAGE
                // -----------------------------------------------------------
                
                Serial.print("Sending test SysEx message (");
                Serial.print(sysExTestMessageLen);
                Serial.println(" bytes)...");

                // pSysExChar->writeValue(data, data_length, request_response(true/false))
                // USER REQUIREMENT: WRITE NO RESPONSE (last parameter is 'false')
                if (pSysExChar->writeValue(sysExTestMessage, sysExTestMessageLen, false)) {
                    Serial.println("==> SysEx message SUCCESSFULLY sent (WRITE NO RESPONSE).");
                } else {
                    Serial.println("ERROR: Failed to send SysEx message.");
                }
                
            } else {
                // Error in service/characteristic discovery. Disconnect.
                Serial.println("Failed communication setup, disconnecting...");
                pClient->disconnect();
            }
        } else {
            // Disconnected, restart scan to reconnect.
            Serial.println("Restarting scan to attempt reconnection.");
            NimBLEDevice::getScan()->start(0, nullptr, false);
        }
    }

    delay(5000); // Wait 5 seconds
    
    // Optional: Send more SysEx data here if periodic messaging is required
    if (connected && pSysExChar != nullptr) {
         // Additional communication logic...
    }
}
