#include <ESP8266mDNS.h>
#include <LEAmDNS.h>
#include <LEAmDNS_Priv.h>
#include <LEAmDNS_lwIPdefs.h>

#include <AsyncTelegram2.h>

#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <ESP8266mDNS.h>
#include <floatToString.h>

#include <FS.h>
#include <LittleFS.h>
#include <time.h>

// Replace with your network credentials
const char *ssid = "xxxxxxxx";
const char *password = "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";

#define DHTPIN 14  // Digital pin connected to the DHT sensor

#define BLUE_LED 12
#define RED_LED 13

//initialize telegram bot
#define BOTtoken "your telegram bot token here"

int64_t userid = 000000000;  //your chat id here. you can find this info at https://t.me/myidbot

#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"

#ifdef ESP8266
Session session;
X509List certificate(telegram_cert);
#endif

WiFiClientSecure client;
AsyncTelegram2 myBot(client);

int botRequestDelay(500);  //checks for new messages every half second
unsigned long lastTimeBotRan;

void sendDocument(TBMessage &msg,
                  AsyncTelegram2::DocumentType fileType,
                  const char *filename,
                  const char *caption = nullptr) {

  File file = LittleFS.open(filename, "r");
  if (file) {
    myBot.sendDocument(msg, file, file.size(), fileType, file.name(), caption);
    file.close();
  } else {
    Serial.println("Can't open the file. Upload \"data\" folder to filesystem");
  }
}
long timezone = -4;
byte daysavetime = 1;

FSInfo fs_info;

// Uncomment the type of sensor in use:
#define DHTTYPE DHT11  // DHT 11
//#define DHTTYPE    DHT22     // DHT 22 (AM2302)
//#define DHTTYPE    DHT21     // DHT 21 (AM2301)

DHT dht(DHTPIN, DHTTYPE);

// current temperature & humidity, updated in loop()
float t = 0.0;
float h = 0.0;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Generally, you should use "unsigned long" for variables that hold time
// The value will quickly become too large for an int to store
unsigned long previousMillis = 0;   // will store last time DHT was updated
unsigned long lastTempLogging = 0;  //stores last time temp was logged

float lastTempChange = 0;      //used to store the last change in temperature
float lastHumidityChange = 0;  //used to store the last change in humidity


// Updates DHT readings every 2 seconds
const long interval = 2000;
const long logInterval = 3600000;  //for logging temp and humidity every hour
//3600000 = 1 hour
//60000 = 1 minute

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .dht-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body onload="init()">
  <h2>ESP8266 DHT Server</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="dht-labels">Temperature</span> 
    <span id="temperature">%TEMPERATURE%</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>
    <i class="fas fa-tint" style="color:#00add6;"></i> 
    <span class="dht-labels">Humidity</span>
    <span id="humidity">%HUMIDITY%</span>
    <sup class="units">%</sup>
  </p>
  <a href='download'>Download</a>
</body>
<script>
function init() {
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("temperature").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/temperature", true);
  xhttp.send();
}, 10000 ) ;

setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("humidity").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/humidity", true);
  xhttp.send();
}, 10000 ) ;
}
</script>
</html>)rawliteral";

// Replaces placeholder with DHT values
String processor(const String &var) {
  if (var == "TEMPERATURE") {
    return String(t);
  } else if (var == "HUMIDITY") {
    return String(h);
  }
  return String();
}

void getTime(char *buffer, size_t bufferSize) {
  struct tm tmstruct;

  tmstruct.tm_year = 0;
  getLocalTime(&tmstruct, 5000);

  // Format the date and time into the provided buffer
  snprintf(buffer, bufferSize, "%d-%02d-%02d %02d:%02d:%02d",
           (tmstruct.tm_year) + 1900,
           (tmstruct.tm_mon) + 1,
           tmstruct.tm_mday,
           tmstruct.tm_hour,
           tmstruct.tm_min,
           tmstruct.tm_sec);
}



void listDir(const char *dirname) {
  Serial.printf("Listing directory: %s\n", dirname);

  Dir root = LittleFS.openDir(dirname);

  while (root.next()) {
    File file = root.openFile("r");
    Serial.print("  FILE: ");
    Serial.print(root.fileName());
    Serial.print("  SIZE: ");
    Serial.print(file.size());
    time_t cr = file.getCreationTime();
    time_t lw = file.getLastWrite();
    file.close();
    struct tm *tmstruct = localtime(&cr);
    Serial.printf("    CREATION: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
    tmstruct = localtime(&lw);
    Serial.printf("  LAST WRITE: %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct->tm_year) + 1900, (tmstruct->tm_mon) + 1, tmstruct->tm_mday, tmstruct->tm_hour, tmstruct->tm_min, tmstruct->tm_sec);
  }
}

void readFile(const char *path) {
  Serial.printf("Reading file: %s\n", path);

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(const char *path, const char *message) {
  Serial.printf("Writing file: %s\n", path);

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  delay(2000);  // Make sure the CREATE and LASTWRITE times are different
  file.close();
}

void appendFile(const char *path, const char *message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(const char *path1, const char *path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (LittleFS.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(const char *path) {
  Serial.printf("Deleting file: %s\n", path);
  if (LittleFS.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
  }
}


void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  dht.begin();
  lastTempChange = dht.readTemperature();
  lastHumidityChange = dht.readHumidity();
  pinMode(RED_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");

  Serial.println("Contacting Time Server");
  configTime(3600 * timezone, daysavetime * 3600, "time.nist.gov", "0.pool.ntp.org", "1.pool.ntp.org");
  client.setTrustAnchors(&certificate);  //add root certificate for api.telegram.org

#ifdef ESP8266
  // Sync time with NTP, to check properly Telegram certificate
  configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
  //Set certficate, session and some other base client properies
  client.setSession(&session);
  client.setTrustAnchors(&certificate);
  client.setBufferSizes(1024, 1024);
#elif defined(ESP32)
  // Sync time with NTP
  configTzTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
  client.setCACert(telegram_cert);
#endif

  // Set the Telegram bot properies
  myBot.setUpdateTime(1000);
  myBot.setTelegramToken(BOTtoken);

  // Check if all things are ok
  Serial.print("\nTest Telegram connection... ");
  myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

  myBot.setFormattingStyle(AsyncTelegram2::FormatStyle::HTML /* MARKDOWN */);


  struct tm tmstruct;
  delay(2000);

  tmstruct.tm_year = 0;
  getLocalTime(&tmstruct, 5000);
  Serial.printf("\nNow is : %d-%02d-%02d %02d:%02d:%02d\n", (tmstruct.tm_year) + 1900, (tmstruct.tm_mon) + 1, tmstruct.tm_mday, tmstruct.tm_hour, tmstruct.tm_min, tmstruct.tm_sec);

  // Start mDNS at esptemp.local address
  if (!MDNS.begin("esptemp")) {
    Serial.println("Error starting mDNS");
  }
  Serial.println("mDNS started");

  //LittleFS.format();  //format() deletes all the files  !!remove it to retain temp logs

  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }

  File dataFile = LittleFS.open("/data.csv", "r");
  if (!dataFile) {
    writeFile("/data.csv", "temp,humidity,time\n");
  } else {
    readFile("/data.csv");
  }
  dataFile.close();

  if (LittleFS.exists("/data.csv")) {
    Serial.println("file /data.csv exists!");
  } else {
    Serial.println("file /data.csv does not exist");
  }

  LittleFS.info(fs_info);
  Serial.println("total bytes");
  Serial.println(fs_info.totalBytes);
  Serial.println("used bytes");
  Serial.println(fs_info.usedBytes);
  //should be able to log temp and humidity for 79 days if logging is done every hour


  listDir("/");  //lists the files in the root directory


  //deleteFile("/data.csv");  //just temperorary so esp storage doesnt fill up

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println(".");
  }

  // Print ESP8266 Local IP Address
  Serial.println(WiFi.localIP());

  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(t).c_str());
  });
  server.on("/humidity", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/plain", String(h).c_str());
  });
  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (LittleFS.exists("/data.csv")) {
      Serial.println("**file exists**");
      request->send(LittleFS, "/data.csv", String(), true);
    } else {
      Serial.println("**file does NOT exist**");
      request->send(404, "text/plain", "File could not be found unfortunatly");
    }
  });

  // Start server
  server.begin();
}




void loop() {

  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // save the last time you updated the DHT values
    previousMillis = currentMillis;
    // Read temperature as Celsius (the default)
    float newT = dht.readTemperature();
    // Read temperature as Fahrenheit (isFahrenheit = true)
    // if temperature read failed, don't change t value


    if (isnan(newT)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      t = newT;
      //Serial.println(t);
    }
    // Read Humidity
    float newH = dht.readHumidity();
    // if humidity read failed, don't change h value
    if (isnan(newH)) {
      Serial.println("Failed to read from DHT sensor!");
    } else {
      h = newH;
      
      // this block runs if the temp and humidity is read succesfuly

      if (currentMillis - lastTempLogging >= logInterval) {
        lastTempLogging = currentMillis;
        char tempString[5];
        floatToString(t, tempString, sizeof(tempString), 1);

        char humString[5];
        floatToString(h, humString, sizeof(humString), 1);

        char timeBuffer[25];  //stores the time, the getTime function populates this array
        getTime(timeBuffer, sizeof(timeBuffer));

        char fileMessage[30];
        strcpy(fileMessage, "");

        strcat(fileMessage, tempString);
        strcat(fileMessage, ",");
        strcat(fileMessage, humString);
        strcat(fileMessage, ",");
        strcat(fileMessage, timeBuffer);
        strcat(fileMessage, "\n");
        Serial.print(fileMessage);

        appendFile("/data.csv", fileMessage);  //write temperature to file
        strcpy(fileMessage, "");               //empties the filemessage array so theres room for the next measurement
        LittleFS.info(fs_info);
        Serial.println("used bytes");
        Serial.println(fs_info.usedBytes);
      }

      if (t > 18) {
        digitalWrite(RED_LED, HIGH);
        digitalWrite(BLUE_LED, LOW);
        //if the temp is higher than 18C turn on red led and turn off blue led
      } else if (t < 18) {
        digitalWrite(BLUE_LED, HIGH);
        digitalWrite(RED_LED, LOW);
        //if the temp is lower than 18C turn on blue led and turn off blue led
      } else if (t == 18) {
        digitalWrite(BLUE_LED, HIGH);
        digitalWrite(RED_LED, HIGH);
        //if the temp is equal to 18c turn both leds on
      }
    }
  }

  // a variable to store telegram message data
  TBMessage msg;
  // if there is an incoming message...
  if (myBot.getNewMessage(msg)) {
    MessageType msgType = msg.messageType;

    // Received a text message
    if (msgType == MessageText) {
      String msgText = msg.text;
      Serial.print("Text message received: ");
      Serial.println(msgText);

      // Send docuements stored in filesystem passing the stream
      // (File is a class derived from Stream)
      if (msgText.indexOf("/csv") > -1) {  //the indexOf method of checking for bot commands lets you embed commands in a sentence
        Serial.println("\nSending csv file from filesystem");
        sendDocument(msg, AsyncTelegram2::DocumentType::CSV, "/data.csv");

      } else if (msgText == "/thanks") {
        myBot.sendMessage(msg, "you're welcome");
      } else if (msgText == "/temp") {
        myBot.sendMessage(msg, String(t));
      } else if (msgText == "/hum") {
        myBot.sendMessage(msg, String(h));
      } else if (msgText == "/now") {
        String message = "temperature: ";
        message.concat(t);
        message.concat(" humidity: ");
        message.concat(h);
        myBot.sendMessage(msg, message);
      } else if(msgText == "/start"){
          myBot.sendMessage(msg, "welcome to temp sensor. try the /now, /temp, and /hum commands");
      } else if (msgText.equalsIgnoreCase("/reset")) {
        myBot.sendMessage(msg, "Restarting ESP....");
        // Wait until bot synced with telegram to prevent cyclic reboot
        while (!myBot.noNewMessage()) {
          delay(50);
        }
        ESP.restart();
      }

      else {
        String replyMsg = "Welcome to the AsyncTelegram2 bot.\n\n";
        replyMsg += "/csv will send an example csv file from fylesystem\n";

        myBot.sendMessage(msg, replyMsg);
      }
    }
  }
}