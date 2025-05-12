#include <esp_now.h>
#include <WiFi.h>

// Define the laser control pins
#define LASER1_PIN 2  // Pin for Laser 1
#define LASER2_PIN 4  // Pin for Laser 2
#define LASER3_PIN 15 // Pin for Laser 3

// Track the status of each laser
bool laser1_active = false;
bool laser2_active = false;
bool laser3_active = false;

// Track if each laser is in use
bool laser1_in_use = true;  // Manually set to true - laser 1 is in use
bool laser2_in_use = true;  // Manually set to true - laser 2 is in use
bool laser3_in_use = true;  // Manually set to true - laser 3 is in use

// Define the authorized sender MAC address (ESP32-CAM)
const uint8_t authorizedSenderMAC[] = {0xD4, 0x8C, 0x49, 0xB9, 0x4C, 0xDC}; // D4:8C:49:B9:4C:DC

// Updated structure to receive data with level and specific laser access permissions
typedef struct struct_message {
  char role[16];        // "student", "staff", or "supervisor"
  int level;            // 1, 2, or 3 for student/staff, 0 for supervisor
  bool can_access_laser1;
  bool can_access_laser2;
  bool can_access_laser3;
} struct_message;

// Structure for laser status request
typedef struct struct_laser_request {
  bool request_laser_status;
} struct_laser_request;

// Modified struct to send a single boolean representing combined laser status
typedef struct struct_response {
  bool laser_in_use;  // Single boolean that is true if ANY laser is in use
} struct_response;

// Create structured objects
struct_message myData;
struct_response myResponse;

// Store the sender's MAC to reply to
uint8_t senderMac[6];
bool reply_needed = false;

// Helper function to check if a MAC address matches the authorized sender
bool isAuthorizedSender(const uint8_t *mac) {
  for (int i = 0; i < 6; i++) {
    if (mac[i] != authorizedSenderMAC[i]) {
      return false;
    }
  }
  return true;
}

// Helper function to print a MAC address
void printMAC(const uint8_t *mac) {
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.println(macStr);
}

// Callback function when data is received
void OnDataReceive(const uint8_t *mac, const uint8_t *incomingData, int len) {
  Serial.print("Received message from MAC: ");
  printMAC(mac);
  
  // Verify this is coming from the authorized sender
  if (!isAuthorizedSender(mac)) {
    Serial.println("WARNING: Message received from unauthorized sender, ignoring");
    return;
  }
  
  // Store the sender's MAC for reply
  memcpy(&senderMac, mac, 6);
  
  // Check if this is a laser status request based on size
  if (len == sizeof(struct_laser_request)) {
    Serial.println("Laser status requested");
    reply_needed = true;
    return; // Handle this separately to send response
  }
  
  // This is a role+level+permissions message
  memcpy(&myData, incomingData, sizeof(myData));
  
  Serial.print("Role received: ");
  Serial.print(myData.role);
  Serial.print(" Level: ");
  Serial.println(myData.level);
  Serial.printf("Laser access permissions: L1=%d, L2=%d, L3=%d\n", 
                myData.can_access_laser1, myData.can_access_laser2, myData.can_access_laser3);
  
  // Check and control access to each laser based on permissions
  // Laser 1
  if (myData.can_access_laser1) {
    Serial.printf("%s Level %d granted access to Laser 1\n", myData.role, myData.level);
    pinMode(LASER1_PIN, OUTPUT);
    digitalWrite(LASER1_PIN, HIGH);
    laser1_active = true;
  } else {
    Serial.printf("%s Level %d denied access to Laser 1\n", myData.role, myData.level);
    if (laser1_active) {
      digitalWrite(LASER1_PIN, LOW);
      laser1_active = false;
    }
  }
  
  // Laser 2
  if (myData.can_access_laser2) {
    Serial.printf("%s Level %d granted access to Laser 2\n", myData.role, myData.level);
    pinMode(LASER2_PIN, OUTPUT);
    digitalWrite(LASER2_PIN, HIGH);
    laser2_active = true;
  } else {
    Serial.printf("%s Level %d denied access to Laser 2\n", myData.role, myData.level);
    if (laser2_active) {
      digitalWrite(LASER2_PIN, LOW);
      laser2_active = false;
    }
  }
  
  // Laser 3
  if (myData.can_access_laser3) {
    Serial.printf("%s Level %d granted access to Laser 3\n", myData.role, myData.level);
    pinMode(LASER3_PIN, OUTPUT);
    digitalWrite(LASER3_PIN, HIGH);
    laser3_active = true;
  } else {
    Serial.printf("%s Level %d denied access to Laser 3\n", myData.role, myData.level);
    if (laser3_active) {
      digitalWrite(LASER3_PIN, LOW);
      laser3_active = false;
    }
  }
}

// Callback function when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Delivery Success" : "Delivery Fail");
  reply_needed = false;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial monitor time to start
  
  // Initialize laser pins and set them to off initially
  pinMode(LASER1_PIN, OUTPUT);
  pinMode(LASER2_PIN, OUTPUT);
  pinMode(LASER3_PIN, OUTPUT);
  digitalWrite(LASER1_PIN, LOW);
  digitalWrite(LASER2_PIN, LOW);
  digitalWrite(LASER3_PIN, LOW);
  
  // Reset the state variables
  laser1_active = false;
  laser2_active = false;
  laser3_active = false;
  
  // Set the "in use" status for each laser
  laser1_in_use = true;
  laser2_in_use = true;
  laser3_in_use = true;
  
  Serial.println("ALL LASERS SET TO IN USE");
  Serial.print("Authorized sender MAC: ");
  printMAC(authorizedSenderMAC);
  
  // Set device as a Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Required: Start WiFi
  WiFi.begin();
  
  // Wait for WiFi to initialize
  delay(500);
  
  // Print MAC address
  Serial.print("ESP32 MAC Address: ");
  Serial.println(WiFi.macAddress());
  
  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_recv_cb(OnDataReceive);
  esp_now_register_send_cb(OnDataSent);
  
  Serial.println("ESP32 Ready to receive messages");
}

// Modified to send a single boolean for combined laser status
void loop() {
  // Send laser status if requested
  if (reply_needed) {
    // Determine if ANY laser is in use - if any one is in use, the combined status is "in use"
    bool any_laser_in_use = laser1_in_use || laser2_in_use || laser3_in_use;
    
    // Create the simplified response with just one boolean
    myResponse.laser_in_use = any_laser_in_use;
    
    // Register peer (the sender)
    esp_now_peer_info_t peerInfo;
    memset(&peerInfo, 0, sizeof(peerInfo));
    memcpy(peerInfo.peer_addr, senderMac, 6);
    peerInfo.channel = 0;  // Use current channel
    peerInfo.encrypt = false;
    
    // Make sure to delete peer if it exists before adding
    esp_now_del_peer(senderMac);
    
    // Add peer
    esp_now_add_peer(&peerInfo);
    
    // Send the response
    esp_err_t result = esp_now_send(senderMac, (uint8_t *)&myResponse, sizeof(myResponse));
    if (result != ESP_OK) {
      Serial.println("Error sending laser status");
    } else {
      Serial.println("Sent laser status response");
      Serial.printf("Laser status sent: %s (L1=%s, L2=%s, L3=%s)\n", 
                  any_laser_in_use ? "IN USE" : "NOT IN USE",
                  laser1_in_use ? "IN USE" : "NOT IN USE",
                  laser2_in_use ? "IN USE" : "NOT IN USE",
                  laser3_in_use ? "IN USE" : "NOT IN USE");
    }
    
    reply_needed = false;
  }
  
  delay(100);
}
