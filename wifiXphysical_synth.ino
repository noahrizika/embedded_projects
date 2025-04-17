#include <WiFi.h>
#include <regex>
#include <string>

#define BUTTON_INPUT 32
#define NOISE_OUTPUT 33
#define LEDC_RESOLUTION 8

SemaphoreHandle_t play_freq_mutex;

const char *ssid = "";
const char *password = "";

NetworkServer server(80);

// HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK) and a content-type so the client knows what's coming, then a blank line
const char *html = R""""(
HTTP/1.1 200 OK
Content-type:text/html

<!DOCTYPE html>
<html>
<head>
  <title>Remote Synthesizer</title>
  <style>
    body { text-align: center; font-family: Arial, sans-serif; }
    button { background-color: slateblue; color: white; border: none; width: 50px; height: 30px; }
    button:hover { background-color: darkslateblue; }
    button:active { background-color: mediumslateblue; }
  </style>  
</head>
<body>
  <h1>Remote Synthesizer</h1>
  <p>
    <a href="/600"><button>600</button></a>
    <a href="/500"><button>500</button></a>
    <a href="/400"><button>400</button></a>
    <a href="/300"><button>300</button></a>
    <a href="/200"><button>200</button></a>
    <a href="/100"><button>100</button></a>
    <a href="/0"><button>0</button></a>
  </p>
</body>
</html>

)"""";

void play_freq(int freq) {
  ledcWriteTone(NOISE_OUTPUT, freq);
}

void handle_web_connection_task(void *param) {
  std::regex regex(R"(GET /(\d{3}) )");
  std::string currentLine;
  int remote_freq;

  while (1) {
    NetworkClient client = server.accept();  // listen for incoming clients
    if (client) {
      Serial.println("New Client.");  // print a message out the serial port
      currentLine = "";               // hold incoming data from the client

      while (client.connected()) {
        if (client.available()) {   // if there's bytes to read from the client
          char c = client.read();
          Serial.write(c);

          if (c == '\n') {
            // if the current line is blank, then you just got two newline characters in a row
            // this signifies the end of the client HTTP request, so send a response
            if (currentLine.length() == 0) {
              client.println(html);
              break;
            }
            else {
              std::smatch match;
              if (std::regex_search(currentLine, match, regex) && xSemaphoreTake(play_freq_mutex, portMAX_DELAY)) {
                remote_freq = std::stoi(match[1]);
                play_freq(remote_freq);
                Serial.printf("Web tone: %d Hz\n", remote_freq);
                xSemaphoreGive(play_freq_mutex);
              }
              currentLine = "";
            }
          }
          else if (c != '\r') {
            currentLine += c;
          }
        }
      }

      // close the connection:
      client.stop();
      Serial.println("Client Disconnected.");
    }
    vTaskDelay(20 / portTICK_PERIOD_MS); // yield to processor
  }
}

void handle_buttons_io_task(void *param) {
  uint16_t val;
  int local_freq;

  while (1) {
    val = analogRead(BUTTON_INPUT);

    if (val > 3500) {        // top btn
      local_freq = 550;
    }
    else if (val > 2500) {   // middle btn
      local_freq = 400;
    }
    else if (val > 700) {    // bottom btn
      local_freq = 175;
    }
    else {
      local_freq = 0;
    }

    if (local_freq != 0 && xSemaphoreTake(play_freq_mutex, portMAX_DELAY)) {
      play_freq(local_freq);
      delay(1000);
      play_freq(0);
      xSemaphoreGive(play_freq_mutex);
    }
    
    vTaskDelay(20 / portTICK_PERIOD_MS); // yield to processor
  }
}

void setup() {
  Serial.begin(9600);
  Serial.println("Serial is online");

  play_freq_mutex = xSemaphoreCreateMutex();
  if (play_freq_mutex == NULL) {
    Serial.println("Failed to create mutex");
    while (1); // halt if mutex creation fails
  }

  pinMode(BUTTON_INPUT, INPUT);

  // attach pin to PWM (automatic channel assignment)
  ledcAttach(NOISE_OUTPUT, 1000, LEDC_RESOLUTION); // !! do NOT  pinMode() for NOISE_OUTPUT. do ledcAttach instead. => don't use pinMode with PWM pins

  delay(10);

  // connect to WiFi
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  server.begin();

  xTaskCreatePinnedToCore(handle_web_connection_task, "WebTask", 8192, NULL, 1, NULL, 0);
  xTaskCreatePinnedToCore(handle_buttons_io_task, "ButtonsIOTask", 2048, NULL, 1, NULL, 1);
}

void loop() {}

