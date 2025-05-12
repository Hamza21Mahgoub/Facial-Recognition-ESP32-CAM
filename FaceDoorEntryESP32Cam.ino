#include <ArduinoWebsockets.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "camera_index.h"
#include "Arduino.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
#include <esp_now.h>
#include <time.h>

#include <Preferences.h> 
#include <SD.h>          // Add SD card library
#include <SPI.h>         // Add SPI library
#include <FS.h>          // Add filesystem library
#include "driver/sdmmc_host.h" // Add SD MMC driver
#include "driver/sdspi_host.h" // Add SD SPI driver




 //west one
const char* ssid = "PINEMEDIA-008250"; 
const char* password = "ipebed49";


/*
// tilted works
const char* ssid = "Glide-8959"; 
const char* password = "tWczFs45n3";
*/

/*
const char* ssid = "iPhone (183)"; 
const char* password = "abbass12";
*/

#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7

// Select camera model
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"


// Define SD card pin for ESP32-CAM
// For ESP32-CAM AI-THINKER, the SD card CS pin is usually connected to GPIO4
#define SD_CS 13  // Try GPIO4 instead of GPIO13

// Define logging tag
#define TAG "face_recognition"
#define relay_pin 2 // pin for relay control

using namespace websockets;
WebsocketsServer socket_server;

bool isLaserOn = false;

camera_fb_t * fb = NULL;

long current_millis;
long last_detected_millis = 0;

unsigned long door_opened_millis = 0;
long interval = 5000;           // open lock for ... milliseconds
bool face_recognised = false;

// Flag to check if SD card is available
bool sd_card_available = false;

//time declerations


// Add these global variables near the top of your code
const char* ntpServer = "uk.pool.ntp.org";
const long gmtOffset_sec = 0;         // GMT+0 (UK time)
const int daylightOffset_sec = 3600;  // +1 hour during BST (British Summer Time)
bool time_synchronized = false;

// Function to initialize SD card - already in your code but ensure it's called
bool init_sd_card() {
  // Start by setting VSPI pins for SD card access
  SPI.begin(14, 2, 15, SD_CS); // SCLK, MISO, MOSI, SS
  
  // Add a short delay before initializing
  delay(500);
  
  if (!SD.begin(SD_CS, SPI)) {
    Serial.println("SD Card Mount Failed");
    // Try with default SPI configuration
    if (!SD.begin(SD_CS)) {
      Serial.println("SD Card Mount Failed with default SPI config too");
      return false;
    }
  }
  
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card attached");
    return false;
  }
  
  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  
  return true;
}

// Function to synchronize time with NTP server
void sync_time() {
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  // Wait for time to be synchronized (with timeout)
  time_t now = 0;
  struct tm timeinfo;
  int retry = 0;
  const int max_retries = 5;
  
  Serial.println("Synchronizing time via NTP...");
  while(timeinfo.tm_year < (2020 - 1900) && retry < max_retries) {
    delay(1000);
    time(&now);
    localtime_r(&now, &timeinfo);
    retry++;
    Serial.print(".");
  }
  Serial.println();
  
  if (timeinfo.tm_year >= (2020 - 1900)) {
    char time_buffer[30];
    strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
    Serial.printf("Time synchronized to UK time: %s\n", time_buffer);
    time_synchronized = true;
  } else {
    Serial.println("Failed to synchronize time with NTP server");
    time_synchronized = false;
  }
}

// Function to log access attempts to SD card
void log_access(const char* name, const char* role, int level, bool recognized, bool door_opened) {
  if (!sd_card_available) {
    Serial.println("Failed to log access: SD card not available");
    return;
  }
  
  // Create access logs directory if it doesn't exist
  if (!SD.exists("/access_logs")) {
    if(!SD.mkdir("/access_logs")) {
      Serial.println("Failed to create access_logs directory");
      return;
    }
  }
  
  // Get current date and time
  String dateStr, timeStr;
  
  if (time_synchronized) {
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char date_buffer[11];  // YYYY-MM-DD + null terminator
    char time_buffer[9];   // HH:MM:SS + null terminator
    
    strftime(date_buffer, sizeof(date_buffer), "%Y-%m-%d", &timeinfo);
    strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", &timeinfo);
    
    dateStr = String(date_buffer);
    timeStr = String(time_buffer);
  } else {
    // Fallback if time is not synchronized
    dateStr = "Unknown";
    timeStr = "Unknown";
  }
  
  // Prepare log entry
  String logEntry = dateStr + "," + 
                    timeStr + "," + 
                    String(name) + "," + 
                    String(role) + "," + 
                    String(level) + "," + 
                    String(recognized ? "Yes" : "No") + "," +
                    String(door_opened ? "Yes" : "No");
  
  // Open or create the log file
  String logFilePath = "/access_logs/access_log.csv";
  bool needHeader = !SD.exists(logFilePath);
  
  File logFile = SD.open(logFilePath, FILE_APPEND);
  
  if (logFile) {
    // Write header if new file
    if (needHeader) {
      logFile.println("Date,Time,Name,Role,Level,Access_Granted,Door_Opened");
    }
    
    // Write the log entry
    logFile.println(logEntry);
    logFile.close();
    
    Serial.println("Access logged: " + logEntry);
  } else {
    Serial.println("Failed to open log file for writing");
  }
}


//ESP_NOW Declarations
uint8_t receiverMacAddress[] = {0xA0, 0xDD, 0x6C, 0x9B, 0x08, 0x50};
//A0:DD:6C:9B:08:50  

// UPDATED: Modified structure to include role level
typedef struct struct_message {
  char role[16];        // "student", "staff", or "supervisor"
  int level;            // 1, 2, or 3 for student/staff, 0 for supervisor
  bool can_access_laser1;
  bool can_access_laser2;
  bool can_access_laser3;
} struct_message;

// Create a struct_message called myData
struct_message myData;

// Create a struct to request laser status
struct struct_laser_request {
  bool request_laser_status;
};

// Callback function called when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Last Packet Send Status: ");
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Delivery Success");
  } else {
    Serial.println("Delivery Fail");
  }
}

// Callback function when data is received
void OnDataReceive(const uint8_t *mac, const uint8_t *incomingData, int len) {
  struct_message receivedData;
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  Serial.print("Message received: ");
  Serial.print(receivedData.role);
  Serial.print(" level ");
  Serial.println(receivedData.level);
}

void setupESPNow() {
  // Init ESP-NOW
  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  
  // Register callbacks
  esp_now_register_send_cb(OnDataSent);
  esp_now_register_recv_cb(OnDataReceive);
  
  // Get the WiFi channel
  uint8_t currentChannel = WiFi.channel();
  Serial.print("ESP32-CAM is on WiFi channel: ");
  Serial.println(currentChannel);
  
  // Log the receiver address we're trying to communicate with
  Serial.print("Target ESP32-WROOM MAC: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           receiverMacAddress[0], receiverMacAddress[1], receiverMacAddress[2], 
           receiverMacAddress[3], receiverMacAddress[4], receiverMacAddress[5]);
  Serial.println(macStr);
  
  // Register peer with better configuration
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = currentChannel;  // Use your current WiFi channel
  peerInfo.encrypt = false;
  
  // Remove peer if it exists (for clean state)
  esp_now_del_peer(receiverMacAddress);
  
  // Add peer
  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    Serial.printf("Failed to add peer. Error: %d\n", result);
    return;
  }
  
  Serial.println("ESP-NOW initialized successfully");
  
  // Send a test message to check connection
  struct_message testMsg;
  strcpy(testMsg.role, "test");
  testMsg.level = 0;
  testMsg.can_access_laser1 = false;
  testMsg.can_access_laser2 = false;
  testMsg.can_access_laser3 = false;
  
  result = esp_now_send(receiverMacAddress, (uint8_t *)&testMsg, sizeof(testMsg));
  Serial.printf("Test message send result: %s\n", (result == ESP_OK) ? "Success" : "Failed");
}

// Add the missing struct_response definition
typedef struct struct_response {
  bool laser_in_use;
} struct_response;

// Global variable to store response status
volatile bool response_received = false;

// Temporary callback for laser status
void LaserStatusCallback(const uint8_t *mac, const uint8_t *incomingData, int len) {
  // Check if the data is of the expected size for a laser status response
  if (len == sizeof(struct_response)) {
    struct_response receivedStatus;
    memcpy(&receivedStatus, incomingData, sizeof(receivedStatus));
    
    // Update the global laser status
    isLaserOn = receivedStatus.laser_in_use;
    Serial.printf("Laser status received: %s\n", isLaserOn ? "ON" : "OFF");
    
    // Mark that we received a response
    response_received = true;
  }
}

void checkLaserStatus() {
  struct_laser_request statusRequest;
  statusRequest.request_laser_status = true;
  
  // Reset response flag
  response_received = false;
  
  // Temporarily register our callback
  esp_now_register_recv_cb(LaserStatusCallback);
  
  // Make sure peer is properly registered before sending
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, receiverMacAddress, 6);
  peerInfo.channel = WiFi.channel();  // Use current channel
  peerInfo.encrypt = false;
  
  // Remove peer if it exists
  esp_now_del_peer(receiverMacAddress);
  
  // Add peer
  esp_err_t addResult = esp_now_add_peer(&peerInfo);
  if (addResult != ESP_OK) {
    Serial.printf("Failed to add peer. Error: %d\n", addResult);
  }
  
  // Try sending with retries
  bool sendSuccess = false;
  int retryCount = 0;
  const int MAX_RETRIES = 3;
  esp_err_t result;
  
  while (!sendSuccess && retryCount < MAX_RETRIES) {
    // Send the status request
    result = esp_now_send(receiverMacAddress, (uint8_t *)&statusRequest, sizeof(statusRequest));
    
    if (result == ESP_OK) {
      Serial.printf("Attempt %d: Sent laser status request\n", retryCount + 1);
      sendSuccess = true;
    } else {
      Serial.printf("Attempt %d: Failed to send laser status request. Error: %d\n", retryCount + 1, result);
      retryCount++;
      delay(100);  // Wait before retry
    }
  }
  
  if (!sendSuccess) {
    Serial.println("Failed to send laser status request after all retries");
    isLaserOn = false; // Default to false if communication fails
    esp_now_register_recv_cb(OnDataReceive);
    return;
  }
  
  // Wait for the response with timeout
  unsigned long startTime = millis();
  const unsigned long timeout = 3000; // 3 second timeout
  
  while (!response_received && (millis() - startTime < timeout)) {
    delay(10); // Small delay to prevent CPU hogging
  }
  
  // Restore the original callback
  esp_now_register_recv_cb(OnDataReceive);
  
  // If no response received within timeout, assume laser is off
  if (!response_received) {
    Serial.println("No laser status response received, assuming laser is OFF");
    isLaserOn = false;
  }
  
  Serial.printf("Final laser status: %s\n", isLaserOn ? "ON" : "OFF");
}

// UPDATED: Modified to include role level and laser access permissions
void sendRoleViaESPNow(const char* role, int level) {
  // Copy role to the structure
  strlcpy(myData.role, role, sizeof(myData.role));
  myData.level = level;
  
  // Set laser access permissions based on role and level
  if (strcmp(role, "supervisor") == 0) {
    // Supervisor can access all lasers
    myData.can_access_laser1 = true;
    myData.can_access_laser2 = true;
    myData.can_access_laser3 = true;
  } 
  else if (strcmp(role, "student") == 0) {
    // Student level permissions
    switch (level) {
      case 1:
        myData.can_access_laser1 = false;
        myData.can_access_laser2 = false;
        myData.can_access_laser3 = false;
        break;
      case 2:
        myData.can_access_laser1 = true;
        myData.can_access_laser2 = false;
        myData.can_access_laser3 = false;
        break;
      case 3:
        myData.can_access_laser1 = true;
        myData.can_access_laser2 = true;
        myData.can_access_laser3 = false;
        break;
      default:
        myData.can_access_laser1 = false;
        myData.can_access_laser2 = false;
        myData.can_access_laser3 = false;
    }
  } 
  else if (strcmp(role, "staff") == 0) {
    // Staff level permissions
    switch (level) {
      case 1:
        myData.can_access_laser1 = true;
        myData.can_access_laser2 = false;
        myData.can_access_laser3 = false;
        break;
      case 2:
        myData.can_access_laser1 = true;
        myData.can_access_laser2 = true;
        myData.can_access_laser3 = false;
        break;
      case 3:
        myData.can_access_laser1 = true;
        myData.can_access_laser2 = true;
        myData.can_access_laser3 = true;
        break;
      default:
        myData.can_access_laser1 = false;
        myData.can_access_laser2 = false;
        myData.can_access_laser3 = false;
    }
  }
  
  // Get current time for retry mechanism
  unsigned long startTime = millis();
  bool sendSuccess = false;
  int retryCount = 0;
  const int MAX_RETRIES = 3;
  
  // Try to send with retries
  while (!sendSuccess && retryCount < MAX_RETRIES) {
    // Send message via ESP-NOW
    esp_err_t result = esp_now_send(receiverMacAddress, (uint8_t *) &myData, sizeof(myData));
    
    if (result == ESP_OK) {
      Serial.printf("Attempt %d: Sent role '%s' level %d to ESP32-WROOM\n", 
                    retryCount + 1, role, level);
      Serial.printf("Laser access: L1=%d, L2=%d, L3=%d\n", 
                    myData.can_access_laser1, myData.can_access_laser2, myData.can_access_laser3);
      sendSuccess = true;
    } else {
      Serial.printf("Attempt %d: Error sending role via ESP-NOW. Error code: %d\n", retryCount + 1, result);
      retryCount++;
      
      // Short delay before retry
      delay(100);
    }
  }
  
  if (!sendSuccess) {
    Serial.printf("Failed to send role '%s' level %d after %d attempts\n", role, level, MAX_RETRIES);
  }
}

void app_facenet_main();
void app_httpserver_init();

typedef struct
{
  uint8_t *image;
  box_array_t *net_boxes;
  dl_matrix3d_t *face_id;
} http_img_process_result;

// UPDATED: Modified to include role level
typedef struct
{
  char enroll_name[ENROLL_NAME_LEN];
  char hierarchy_level[16]; // "student", "staff", or "supervisor"
  int level;               // 1, 2, or 3 for student/staff, 0 for supervisor
} httpd_resp_value;

static inline mtmn_config_t app_mtmn_config()
{
  mtmn_config_t mtmn_config = {0};
  mtmn_config.type = FAST;
  mtmn_config.min_face = 80;
  mtmn_config.pyramid = 0.707;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.7;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.8;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.8;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
  return mtmn_config;
}
mtmn_config_t mtmn_config = app_mtmn_config();

face_id_name_list st_face_list;
static dl_matrix3du_t *aligned_face = NULL;

httpd_handle_t camera_httpd = NULL;

typedef enum
{
  START_STREAM,
  START_DETECT,
  SHOW_FACES,
  START_RECOGNITION,
  START_ENROLL,
  ENROLL_COMPLETE,
  DELETE_ALL,
} en_fsm_state;
en_fsm_state g_state;

httpd_resp_value st_name;

// UPDATED: Modified to extract name, role, and level from combined name
void parse_name_role_level(const char* combined_name, char* name_out, char* role_out, int* level_out) {
  char* role_delimiter = strchr(combined_name, '|');
  
  if (role_delimiter != NULL) {
    // Name has role information embedded
    int name_len = role_delimiter - combined_name;
    strncpy(name_out, combined_name, name_len);
    name_out[name_len] = '\0'; // Ensure null termination
    
    // Extract role and level
    char role_level[32];
    strcpy(role_level, role_delimiter + 1);
    
    // Check if there is a level specified
    char* level_delimiter = strchr(role_level, '-');
    
    if (level_delimiter != NULL) {
      // Extract role and level separately
      int role_len = level_delimiter - role_level;
      strncpy(role_out, role_level, role_len);
      role_out[role_len] = '\0';
      
      // Convert level string to int
      *level_out = atoi(level_delimiter + 1);
    } else {
      // No level specified, use default
      strcpy(role_out, role_level);
      
      // Default level based on role
      if (strcmp(role_out, "supervisor") == 0) {
        *level_out = 0; // Supervisors don't have levels
      } else {
        *level_out = 1; // Default level for students and staff
      }
    }
  } else {
    // No role info, just use the name as is
    strcpy(name_out, combined_name);
    strcpy(role_out, "student"); // Default role
    *level_out = 1; // Default level
  }
}

// UPDATED: Helper function to create combined name with role and level
void create_combined_name(const char* name, const char* role, int level, char* combined_name) {
  // For supervisor, we don't need a level
  if (strcmp(role, "supervisor") == 0) {
    sprintf(combined_name, "%s|%s", name, role);
  } else {
    sprintf(combined_name, "%s|%s-%d", name, role, level);
  }
}


// UPDATED: Modified enrollment to include role level
static inline int do_enrollment(face_id_name_list *face_list, dl_matrix3d_t *new_id)
{
  Serial.println("START ENROLLING");
  Serial.printf("Enrolling %s with role %s level %d\n", 
               st_name.enroll_name, st_name.hierarchy_level, st_name.level);
  
  // Create combined name with role and level embedded
  char combined_name[ENROLL_NAME_LEN];
  create_combined_name(st_name.enroll_name, st_name.hierarchy_level, st_name.level, combined_name);
  
  // Use the original library's enrollment function
  int left_sample_face = enroll_face_id_to_flash_with_name(face_list, new_id, combined_name);
  
  Serial.printf("Face ID %s (Role %s Level %d) Enrollment: Sample %d\n",
           st_name.enroll_name,
           st_name.hierarchy_level,
           st_name.level,
           ENROLL_CONFIRM_TIMES - left_sample_face);
  return left_sample_face;
}

// Store hierarchy levels separately
#define MAX_FACES 7
char face_hierarchy_levels[MAX_FACES][ENROLL_NAME_LEN + 32]; // name:level:role-level
int face_level_count = 0;

// UPDATED: Modified to send hierarchy level and role level
static esp_err_t send_face_list(WebsocketsClient &client)
{
  client.send("delete_faces"); // tell browser to delete all faces
  face_id_node *head = st_face_list.head;
  char add_face[100];
  for (int i = 0; i < st_face_list.count; i++) // loop current faces
  {
    // Parse name, role, and level from the stored combined name
    char name[ENROLL_NAME_LEN];
    char role[16];
    int level;
    parse_name_role_level(head->id_name, name, role, &level);
    
    // Format: "listface:name:role:level"
    sprintf(add_face, "listface:%s:%s:%d", name, role, level);
    client.send(add_face); //send face to browser with hierarchy level and role level
    head = head->next;
  }
  return ESP_OK;
}

static esp_err_t delete_all_faces(WebsocketsClient &client)
{
  // Delete from memory
  delete_face_all_in_flash_with_name(&st_face_list);
  
  // Also clear our hierarchy level data
  face_level_count = 0;
  client.send("delete_faces");
  return ESP_OK;
}

// UPDATED: Modified to handle hierarchy level and role level
void handle_message(WebsocketsClient &client, WebsocketsMessage msg)
{
  if (msg.data() == "stream") {
    g_state = START_STREAM;
    client.send("STREAMING");
  }
  else if (msg.data() == "detect") {
    g_state = START_DETECT;
    client.send("DETECTING");
  }
  else if (msg.data().substring(0, 8) == "capture:") {
    g_state = START_ENROLL;
    
    // Parse the capture message: "capture:name:role:level"
    String capture_data = msg.data().substring(8);
    int first_separator = capture_data.indexOf(':');
    
    if (first_separator > 0) {
      String name = capture_data.substring(0, first_separator);
      String remaining = capture_data.substring(first_separator + 1);
      
      // Check for second separator (between role and level)
      int second_separator = remaining.indexOf(':');
      
      if (second_separator > 0) {
        String role = remaining.substring(0, second_separator);
        String level_str = remaining.substring(second_separator + 1);
        int level = level_str.toInt();
        
        name.toCharArray(st_name.enroll_name, sizeof(st_name.enroll_name));
        role.toCharArray(st_name.hierarchy_level, sizeof(st_name.hierarchy_level));
        st_name.level = level;
        
        Serial.printf("Capturing face for %s with role %s level %d\n", 
                     st_name.enroll_name, st_name.hierarchy_level, st_name.level);
      } else {
        // If no level was provided, set default based on role
        String role = remaining;
        role.toCharArray(st_name.enroll_name, sizeof(st_name.enroll_name));
        role.toCharArray(st_name.hierarchy_level, sizeof(st_name.hierarchy_level));
        
        if (role.equals("supervisor")) {
          st_name.level = 0;
        } else {
          st_name.level = 1; // Default level 1 for student or staff
        }
        
        Serial.printf("Capturing face for %s with role %s default level %d\n", 
                     st_name.enroll_name, st_name.hierarchy_level, st_name.level);
      }
    } else {
      // If neither role nor level was provided (should not happen with updated interface)
      capture_data.toCharArray(st_name.enroll_name, sizeof(st_name.enroll_name));
      strcpy(st_name.hierarchy_level, "student"); // Default to student
      st_name.level = 1; // Default to level 1
      
      Serial.printf("Capturing face for %s with default role student level 1\n", st_name.enroll_name);
    }
    
    client.send("CAPTURING");
  }
  else if (msg.data() == "recognise") {
    checkLaserStatus();
    if(!isLaserOn){
      g_state = START_RECOGNITION;
      client.send("RECOGNISING");
    }else{
      client.send("Access Denied - Laser is on");
      log_access("Attempted", "Unknown", 0, false, false);
     }
  }
  else if (msg.data().substring(0, 7) == "remove:") {
    char person[ENROLL_NAME_LEN * FACE_ID_SAVE_NUMBER];
    msg.data().substring(7).toCharArray(person, sizeof(person));
    
    // Need to find the combined name in the face list to delete it
    face_id_node *current = st_face_list.head;
    while (current != NULL) {
      char stored_name[ENROLL_NAME_LEN];
      char stored_role[16];
      int stored_level;
      parse_name_role_level(current->id_name, stored_name, stored_role, &stored_level);
      
      if (strcmp(stored_name, person) == 0) {
        // Delete from flash memory
        delete_face_id_in_flash_with_name(&st_face_list, current->id_name);
        break;
      }
      current = current->next;
    }
    
    send_face_list(client); // reset faces in the browser
  }
  else if (msg.data() == "delete_all") {
    delete_all_faces(client);
  }
}

void open_door(WebsocketsClient &client) {
  if (digitalRead(relay_pin) == HIGH) {
    digitalWrite(relay_pin, LOW); // Close relay to unlock door
    Serial.println("Door Unlocked");
    client.send("door_open");
    door_opened_millis = millis(); // time relay closed and door opened
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Initialize relay pin
  pinMode(relay_pin, OUTPUT);
  digitalWrite(relay_pin, HIGH);
  delay(2000);    
  digitalWrite(relay_pin, HIGH);

  // Initialize camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // Init with high specs to pre-allocate larger buffers
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  // SD card not used but we keep initialization function
  //sd_card_available = false;
  // We're not calling init_sd_card() here to disable SD functionality

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Add after WiFi connection
Serial.print("ESP32-CAM MAC Address: ");
Serial.println(WiFi.macAddress());
Serial.print("WiFi Channel: ");
Serial.println(WiFi.channel());

    // Add after the line "Serial.println("WiFi connected");"
  int currentChannel = WiFi.channel();
  Serial.print("Current WiFi channel: ");
  Serial.println(currentChannel);
    

    // Initialize SD card
  sd_card_available = init_sd_card();
  if (sd_card_available) {
    Serial.println("SD card initialized successfully");
  } else {
    Serial.println("SD card initialization failed");
  }
  
  // Synchronize time with NTP
  if (WiFi.status() == WL_CONNECTED) {
    sync_time();
  }

  setupESPNow();

  app_httpserver_init();
  app_facenet_main();
  socket_server.listen(82);

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)interface_html_gz, interface_html_gz_len);
}

httpd_uri_t index_uri = {
  .uri       = "/",
  .method    = HTTP_GET,
  .handler   = index_handler,
  .user_ctx  = NULL
};

void app_httpserver_init ()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
    Serial.println("httpd_start");
  {
    httpd_register_uri_handler(camera_httpd, &index_uri);
  }
}

void app_facenet_main()
{
  // Initialize face recognition
  face_id_name_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
  aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  
  // Load faces from flash memory as usual
  read_face_id_from_flash_with_name(&st_face_list);
  
  // Populate face hierarchy levels for interface
  face_level_count = 0;
  face_id_node *head = st_face_list.head;
  while (head != NULL && face_level_count < MAX_FACES) {
    char name[ENROLL_NAME_LEN];
    char role[16];
    int level;
    parse_name_role_level(head->id_name, name, role, &level);
    
    sprintf(face_hierarchy_levels[face_level_count], "%s:%s:%d", name, role, level);
    face_level_count++;
    
    head = head->next;
  }
  
  Serial.printf("Loaded %d faces into recognition system\n", st_face_list.count);
}

void loop() {
  auto client = socket_server.accept();
  client.onMessage(handle_message);
  dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, 320, 240, 3);
  http_img_process_result out_res = {0};
  out_res.image = image_matrix->item;

  send_face_list(client);
  client.send("STREAMING");

  while (client.available()) {
    client.poll();

    if (millis() - interval > door_opened_millis) { // current time - face recognised time > 5 secs
      digitalWrite(relay_pin, HIGH); //open relay
    }

    fb = esp_camera_fb_get();

    if (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION)
    {
      out_res.net_boxes = NULL;
      out_res.face_id = NULL;

      fmt2rgb888(fb->buf, fb->len, fb->format, out_res.image);

      out_res.net_boxes = face_detect(image_matrix, &mtmn_config);

      if (out_res.net_boxes)
      {
        if (align_face(out_res.net_boxes, image_matrix, aligned_face) == ESP_OK)
        {

          out_res.face_id = get_face_id(aligned_face);
          last_detected_millis = millis();
          if (g_state == START_DETECT) {
            client.send("FACE DETECTED");
          }

          if (g_state == START_ENROLL)
          {
            int left_sample_face = do_enrollment(&st_face_list, out_res.face_id);
            char enrolling_message[64];
            sprintf(enrolling_message, "SAMPLE NUMBER %d FOR %s", ENROLL_CONFIRM_TIMES - left_sample_face, st_name.enroll_name);
            client.send(enrolling_message);
            if (left_sample_face == 0)
            {
              Serial.printf("Enrolled Face ID: %s (Role %s Level %d)\n", st_name.enroll_name, st_name.hierarchy_level, st_name.level);
              g_state = START_STREAM;
              char captured_message[64];
              sprintf(captured_message, "FACE CAPTURED FOR %s", st_name.enroll_name);
              client.send(captured_message);
              send_face_list(client);
            }
          }

          if (g_state == START_RECOGNITION && (st_face_list.count > 0))
          {
            face_id_node *f = recognize_face_with_name(&st_face_list, out_res.face_id);
            if (f)
            {
              // Extract name, role and level from the combined name
              char name[ENROLL_NAME_LEN];
              char role[16];
              int level;
              parse_name_role_level(f->id_name, name, role, &level);
              
              char recognised_message[120];
              sprintf(recognised_message, "DOOR OPEN FOR %s (%s Level %d)", name, role, level);
              open_door(client);
              client.send(recognised_message);

              // Send appropriate role and level information via ESP-NOW
              sendRoleViaESPNow(role, level);
              Serial.printf("%s level %d detected - notification sent to ESP32-WROOM\n", role, level);
              log_access(name, role, level, true, true);
            }
            else
            {
              client.send("FACE NOT RECOGNISED");
              log_access("Unknown", "Unknown", 0, false, false);
            }
          }
          dl_matrix3d_free(out_res.face_id);
        }
      }
      else
      {
        if (g_state != START_DETECT) {
          client.send("NO FACE DETECTED");
        }
      }

      if (g_state == START_DETECT && millis() - last_detected_millis > 500) { // Detecting but no face detected
        client.send("DETECTING");
      }
    }

    client.sendBinary((const char *)fb->buf, fb->len);

    esp_camera_fb_return(fb);
    fb = NULL;
  }
}
