#include <ArduinoWebsockets.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "camera_index.h"
#include "Arduino.h"
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
//#include "fb_gfx.h"
#include "esp32-hal-cpu.h"

const char* ssid = "";
const char* password = "";

#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7

// Select camera model
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"
using namespace websockets;


// Globals
static WebsocketsServer socket_server;
static WebsocketsClient client;
static SemaphoreHandle_t commsMutex;
static dl_matrix3du_t *image = NULL;
static camera_fb_t * fb = NULL;
static TaskHandle_t TaskRec;
static QueueHandle_t imageAvailable;
static QueueHandle_t imageUsed;
static char imageTokenVar = 0;


#define relay_pin 2 // pin 12 can also be used
static unsigned long activated_millis = 0;
static const unsigned long activateDuration_ms = 5000; // activate for ... milliseconds
static bool activated = false;
static unsigned long last_detected_millis = 0;

static void app_facenet_main();
static void app_httpserver_init();

static void TaskRecognise(void * parameter);

typedef struct
{
  uint8_t *image;
  box_array_t *net_boxes;
  dl_matrix3d_t *face_id;
} http_img_process_result;


// ----------------------------------------------
//
// ----------------------------------------------
static inline mtmn_config_t app_mtmn_config()
{
  mtmn_config_t mtmn_config = {0};
  mtmn_config.type = FAST;
  mtmn_config.min_face = 50; // Allows slightly smaller face
  mtmn_config.pyramid = 0.707;
  //mtmn_config.pyramid = 0.5;
  mtmn_config.pyramid_times = 4;
  mtmn_config.p_threshold.score = 0.6;
  mtmn_config.p_threshold.nms = 0.7;
  mtmn_config.p_threshold.candidate_number = 20;
  mtmn_config.r_threshold.score = 0.7;
  mtmn_config.r_threshold.nms = 0.7;
  mtmn_config.r_threshold.candidate_number = 10;
  mtmn_config.o_threshold.score = 0.7;
  mtmn_config.o_threshold.nms = 0.7;
  mtmn_config.o_threshold.candidate_number = 1;
  return mtmn_config;
}

mtmn_config_t mtmn_config = app_mtmn_config();
face_id_name_list st_face_list = {0};
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

// ----------------------------------------------
//
// ----------------------------------------------
typedef struct
{
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;

httpd_resp_value st_name;

// ----------------------------------------------
//
// ----------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  setCpuFrequencyMhz(240);
  uint32_t Freq = 0;
  Freq = getCpuFrequencyMhz();
  Serial.print("CPU Freq = ");
  Serial.print(Freq);
  Serial.println(" MHz");
  Freq = getXtalFrequencyMhz();
  Serial.print("XTAL Freq = ");
  Serial.print(Freq);
  Serial.println(" MHz");

  digitalWrite(relay_pin, LOW);
  pinMode(relay_pin, OUTPUT);

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

  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
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
  s->set_hmirror(s, 1);

  // Set up the queues
  imageAvailable = xQueueCreate(1, sizeof(char)); // Length 1 so only 1 item can be pending
  if (imageAvailable == NULL) {
    Serial.println("Error creating the imageAvailable queue");
  }
  imageUsed = xQueueCreate(1, sizeof(char)); // Length 1 so only 1 item can be pending
  if (imageUsed == NULL) {
    Serial.println("Error creating the imageUsed queue");
  }
  // Create mutex before starting tasks
  commsMutex = xSemaphoreCreateMutex();

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");


  app_httpserver_init();
  app_facenet_main();
  socket_server.listen(82);

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // Create a place for the image to be stored
  image = dl_matrix3du_alloc(1, 320, 240, 3);
  if ( image == NULL )
  {
    Serial.println("Failed to allocate image");
  }
  
  // Setup the recognition thread
  xTaskCreatePinnedToCore(TaskRecognise,"TaskRecognise",20000,NULL,tskIDLE_PRIORITY+1,&TaskRec,0); 
}

// ----------------------------------------------
//
// ----------------------------------------------
void clientSend(WebsocketsClient &client, const char * str)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  client.send(str);
  xSemaphoreGive(commsMutex);
}

// ----------------------------------------------
//
// ----------------------------------------------
void clientSendBinary(WebsocketsClient &client, const char * buf, int bufLen)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  client.sendBinary(buf, bufLen);
  xSemaphoreGive(commsMutex);
}

// ----------------------------------------------
//
// ----------------------------------------------
bool clientAvailable(WebsocketsClient &client)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  bool ret = client.available();
  xSemaphoreGive(commsMutex);
  return ret;
}

// ----------------------------------------------
//
// ----------------------------------------------
void clientPoll(WebsocketsClient &client)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  client.poll();
  xSemaphoreGive(commsMutex);
}

// ----------------------------------------------
//
// ----------------------------------------------
void SerialPrintln(const char * str)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  Serial.println(str);
  xSemaphoreGive(commsMutex);
}

// ----------------------------------------------
//
// ----------------------------------------------
void SerialPrint(const char * str)
{
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  Serial.print(str);
  xSemaphoreGive(commsMutex);
}

#define DEBUG_PRINT(a) 
#define DEBUG_PRINTLN(a)
 
// ----------------------------------------------
//
// ----------------------------------------------
static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

// ----------------------------------------------
//
// ----------------------------------------------
httpd_uri_t index_uri = {
  .uri       = "/",
  .method    = HTTP_GET,
  .handler   = index_handler,
  .user_ctx  = NULL
};

// ----------------------------------------------
//
// ----------------------------------------------
void app_httpserver_init () {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
    SerialPrintln("httpd_start");
  {
    httpd_register_uri_handler(camera_httpd, &index_uri);
  }
}

// ----------------------------------------------
//
// ----------------------------------------------
void app_facenet_main() {
  face_id_name_init(&st_face_list, FACE_ID_SAVE_NUMBER, ENROLL_CONFIRM_TIMES);
  aligned_face = dl_matrix3du_alloc(1, FACE_WIDTH, FACE_HEIGHT, 3);
  read_face_id_from_flash_with_name(&st_face_list);
}

// ----------------------------------------------
//
// ----------------------------------------------
static inline int do_enrollment(face_id_name_list *face_list, dl_matrix3d_t *new_id) {
  int left_sample_face = enroll_face_id_to_flash_with_name(face_list, new_id, st_name.enroll_name);
  return left_sample_face;
}

// ----------------------------------------------
//
// ----------------------------------------------
static esp_err_t send_face_list(WebsocketsClient &client) {
  // Need the whole set of send to complete as one action
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  client.send("delete_faces"); // tell browser to delete all faces
  face_id_node *head = st_face_list.head;
  char add_face[64];
  for (int i = 0; i < st_face_list.count; i++) // loop current faces
  {
    sprintf(add_face, "listface:%s", head->id_name);
    client.send(add_face); //send face to browser
    head = head->next;
  }
  xSemaphoreGive(commsMutex);
}

// ----------------------------------------------
//
// ----------------------------------------------
static void delete_all_faces(WebsocketsClient &client) {
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  delete_face_all_in_flash_with_name(&st_face_list);
  client.send("delete_faces");
  xSemaphoreGive(commsMutex);
}


// ----------------------------------------------
// Handle messages from the web server
// ----------------------------------------------
String message_str;

void handle_message(WebsocketsMessage msg) {
  message_str = msg.data();
}

// ----------------------------------------------
// Turn on the output
// ----------------------------------------------
void activate_output(WebsocketsClient &client) {
  if (digitalRead(relay_pin) == LOW) {
    xSemaphoreTake(commsMutex, portMAX_DELAY);
    digitalWrite(relay_pin, HIGH); //activate pin
    activated = true;
    Serial.println("acivated");
    client.send("door_open");
    activated_millis = millis(); // time relay closed and door opened
    xSemaphoreGive(commsMutex);
  }
}


// ----------------------------------------------
//
// ----------------------------------------------
char recResponse1[64] = {0};  // Used to send message via the main loop
char recResponse2[64] = {0}; // Used to send message via the main loop
static http_img_process_result out_res = {0};

// ----------------------------------------------
//
// ----------------------------------------------
void doRecognition(WebsocketsClient &client) {
  out_res.image = image->item;
  out_res.net_boxes = NULL;
  out_res.face_id = NULL;
  
  xSemaphoreTake(commsMutex, portMAX_DELAY);
  out_res.net_boxes = face_detect(image, &mtmn_config);
  xSemaphoreGive(commsMutex);

  // If any faces are detected
  if (out_res.net_boxes)
  {
    // Align the faces
    xSemaphoreTake(commsMutex, portMAX_DELAY);
    auto aligned = align_face(out_res.net_boxes, image, aligned_face);
    xSemaphoreGive(commsMutex);
    if (aligned == ESP_OK)
    {
      xSemaphoreTake(commsMutex, portMAX_DELAY);
      out_res.face_id = get_face_id(aligned_face);
      xSemaphoreGive(commsMutex);
            
      last_detected_millis = millis();
      if (g_state == START_DETECT) {
        strcpy(recResponse1,"FACE DETECTED");
      }
  
      if (g_state == START_ENROLL)
      {
        xSemaphoreTake(commsMutex, portMAX_DELAY);
        int left_sample_face = do_enrollment(&st_face_list, out_res.face_id);
        xSemaphoreGive(commsMutex);
        sprintf(recResponse1, "SAMPLE NUMBER %d FOR %s", ENROLL_CONFIRM_TIMES - left_sample_face, st_name.enroll_name);
        if (left_sample_face == 0)
        {
          g_state = START_STREAM;
          sprintf(recResponse2, "FACE CAPTURED FOR %s", st_face_list.tail->id_name);
        }
      }
  
      if (g_state == START_RECOGNITION  && (st_face_list.count > 0))
      {
        xSemaphoreTake(commsMutex, portMAX_DELAY);
        face_id_node * face_id = recognize_face_with_name(&st_face_list, out_res.face_id);
        xSemaphoreGive(commsMutex);
        if (face_id)
        {
          activate_output(client);
          sprintf(recResponse1, "ACTIVATED %s", face_id->id_name);
        }
        else
        {
          strcpy(recResponse1,"FACE NOT RECOGNISED");
        }
      } // START_RECOGNITION and face list
      xSemaphoreTake(commsMutex, portMAX_DELAY);
      dl_matrix3d_free(out_res.face_id);
      xSemaphoreGive(commsMutex);
    } // align_face
    xSemaphoreTake(commsMutex, portMAX_DELAY);
    dl_lib_free(out_res.net_boxes->score);  // Free allocated memory
    dl_lib_free(out_res.net_boxes->box); 
    dl_lib_free(out_res.net_boxes->landmark);
    dl_lib_free(out_res.net_boxes);
    xSemaphoreGive(commsMutex);
  } 
  else // No net boxes
  {
    if (g_state != START_DETECT) {
      strcpy(recResponse1,"NO FACE DETECTED");
    }
  } // no net boxes
  
  if (g_state == START_DETECT && millis() - last_detected_millis > 500) { // Detecting but no face detected
    strcpy(recResponse1,"DETECTING");
  }

  // Checking for string overruns
  if (recResponse1[63]!=0){
    SerialPrintln(" <err1>");
  }
  if (recResponse2[63]!=0){
    SerialPrintln(" <err2>");
  }
}

// ----------------------------------------------
//
// ----------------------------------------------
static void TaskRecognise(void * parameter){
  while(1) 
  {
    // Check if there is an item in the queue
    xQueuePeek( imageAvailable, ( void * )&imageTokenVar, ( TickType_t ) portMAX_DELAY );

    // Write to queue
    xQueueSend ( imageUsed, (void*)&imageTokenVar, ( TickType_t ) portMAX_DELAY );

    vTaskDelay((TickType_t) 1);
    doRecognition(client);
    vTaskDelay((TickType_t) 1);

    // Read from the queue - Makes it possible for a new item to be posted
    xQueueReceive( imageAvailable,( void * )&imageTokenVar, ( TickType_t ) portMAX_DELAY );
    vTaskDelay((TickType_t) 1);
  }
}

// ----------------------------------------------
//
// ----------------------------------------------
static void serviceWebPage(){
  vTaskDelay((TickType_t) 100);
  if (message_str != "")
  {
    if (message_str == "stream") {
      g_state = START_STREAM;
      clientSend(client,"STREAMING");
    }
    if (message_str == "detect") {
      g_state = START_DETECT;
      clientSend(client,"DETECTING");
    }
    if (message_str.substring(0, 8) == "capture:") {
      g_state = START_ENROLL;
      char person[FACE_ID_SAVE_NUMBER * ENROLL_NAME_LEN] = {0,};
      message_str.substring(8).toCharArray(person, sizeof(person));
      memcpy(st_name.enroll_name, person, strlen(person) + 1);
      clientSend(client,"CAPTURING");
    }
    if (message_str == "recognise") {
      g_state = START_RECOGNITION;
      clientSend(client,"RECOGNISING");
    }
    if (message_str.substring(0, 7) == "remove:") {
      char person[ENROLL_NAME_LEN * FACE_ID_SAVE_NUMBER];
      message_str.substring(7).toCharArray(person, sizeof(person));
      delete_face_id_in_flash_with_name(&st_face_list, person);
      send_face_list(client); // reset faces in the browser
    }
    if (message_str == "delete_all") {
      delete_all_faces(client);
    }

    message_str = "";
  } 

  if ( recResponse1[0] != 0 )
  {
      clientSend(client,recResponse1);
      recResponse1[0] = 0;
  }
  if ( recResponse2[0] != 0 )
  {
      clientSend(client,recResponse2);
      recResponse2[0] = 0;
      send_face_list(client);
  }
}

// ----------------------------------------------
//
// ----------------------------------------------
void loop() {
  camera_fb_t * tmpfb = NULL;

  client = socket_server.accept();
  client.onMessage(handle_message);
  serviceWebPage();
  
  send_face_list(client);
  clientSend(client,"STREAMING");

  // While there is a web client connected
  while (clientAvailable(client)) 
  {
    // Service the web page
    clientPoll(client);
    serviceWebPage();

    // Check if the output need to be turned off
    if ( activated && ( (millis() - activated_millis) > activateDuration_ms )) {
      digitalWrite(relay_pin, LOW); //open relay
      activated = false;
      Serial.println("De-activate");
    }
  
    // Get a frame from the camera
    xSemaphoreTake(commsMutex, portMAX_DELAY);
    fb = esp_camera_fb_get();
    xSemaphoreGive(commsMutex);
        
    if (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION)
    {
      // Check if there is any room in the queue
      if ( xQueuePeek( imageAvailable, ( void * )&imageTokenVar, ( TickType_t ) 0 )!= pdPASS)
      {
        // Convert it to an image
        xSemaphoreTake(commsMutex, portMAX_DELAY);
        fmt2rgb888(fb->buf, fb->len, fb->format, image->item);
        xSemaphoreGive(commsMutex);
      
        // Signal that the image is available
        if( xQueueSend( imageAvailable,( void * )&imageTokenVar, ( TickType_t ) portMAX_DELAY ) == pdPASS )
        {
          // Wait for the image to have been used
          vTaskDelay((TickType_t) 1);
          xQueueReceive( imageUsed, (void*)&imageTokenVar, ( TickType_t ) portMAX_DELAY );
          vTaskDelay((TickType_t) 1);
        } // Frame has been used
      } // Recognition thread ready for new frame
    } // if running face detection

    // Send the frame to the web page
    clientSendBinary(client, (char *)fb->buf, fb->len);

    // Release the frame
    xSemaphoreTake(commsMutex, portMAX_DELAY);
    esp_camera_fb_return(fb);
    fb = NULL;
    xSemaphoreGive(commsMutex);
    
  } // while client available
}
