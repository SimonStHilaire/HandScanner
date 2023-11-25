#include "Arduino.h"
#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"
#include <Adafruit_NeoPixel.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <WebSocketsClient.h>

String ssid     = "";
String password = "";
String address = "";

String ap_ssid     = "HANDSCANNER";
String ap_password = "12345678";

WiFiServer server(80);
String header;

SoftwareSerial mySoftwareSerial(A2, A5); // RX, TX
DFRobotDFPlayerMini myDFPlayer;

int TouchPin = 27;
int LastTouchValue = HIGH;
#define RESET_PIN 13

int DENIED_SOUND = 1;
int ACCESS_SOUND = 2;
int SCAN_SOUND = 3;

bool AudioEnabled = true;

WebSocketsClient webSocket;

#define USE_WIFI true
#define CONNECT_WS true

#define LED_PIN    32

#define LED_PER_ROW 9
#define LED_ROW_COUNT 18
#define LED_COUNT LED_PER_ROW * LED_ROW_COUNT

#define SCAN_TIME  2.5
#define SCAN_DELAY SCAN_TIME * 1000 / LED_ROW_COUNT / 2


Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

uint32_t IdleColor = strip.Color(10, 0, 120);
uint32_t ScanColor = strip.Color(0, 0, 255);
uint32_t AccessColor = strip.Color(0, 255, 0);
uint32_t DeniedColor = strip.Color(255, 0, 0);
uint32_t OffColor = strip.Color(0, 0, 0);



enum State
{
  Initial,
  Configuring,
  Running
};

State CurrentState = Initial;

void printDetail(uint8_t type, int value);
void PlayScanSound();
void PlayDeniedSound();
void PlayGrantedSound();
void SetState(State newState);
bool ReadData(String& ssid, String& psk, String& address);
String GetGetParam(String prefix, String suffix);
void SetData(String ssid, String psk, String newAddress);
void HandleClient();
bool IsConnected();
void Connect();
void UpdateIdleVisual();
void hexdump(const void *mem, uint32_t len);
void AddXVisual();

void Update();

void setup()
{
  mySoftwareSerial.begin(9600);
  Serial.begin(115200);

  Serial.println();
  Serial.println(F("Initializing DFPlayer ... (May take 3~5 seconds)"));

  if (!EEPROM.begin(96))
  {
    Serial.println("Failed to initialise EEPROM");
  }

  if (!myDFPlayer.begin(mySoftwareSerial)) {  //Use softwareSerial to communicate with mp3.
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    AudioEnabled = false;
  }
  else
  {
    Serial.println(F("DFPlayer Mini online."));
    myDFPlayer.volume(25);  //Set volume value. From 0 to 30
  }

  pinMode(TouchPin, INPUT_PULLUP);
  pinMode(RESET_PIN, INPUT_PULLUP);

  strip.begin();

  SetIdleVisual();
  
  ReadData(ssid, password, address);
  if(digitalRead(RESET_PIN) == LOW)
  {
    SetState(Configuring);
  }
  else
  {
    SetState(Running);
  }
}

bool IsConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

void SetState(State newState)
{
  if(newState == CurrentState)
  {
    return;
  }
  
  Serial.print(("New state: "));
  Serial.println(newState);
 
  switch(newState)
  {
    case Configuring:
    {
      WiFi.mode(WIFI_AP);
      WiFi.softAP(ap_ssid.c_str(), ap_password.c_str());

      delayFor(100);

      //IPAddress Ip(10, 0, 0, 1);
      //IPAddress NMask(255, 255, 255, 0);
      //WiFi.softAPConfig(Ip, Ip, NMask);

      IPAddress IP = WiFi.softAPIP();
      
      Serial.print("AP IP address: ");
      Serial.println(IP);

      server.begin();
     
      break;
    }

    case Running:
      break;
  }

  CurrentState = newState;
}

void delayFor(long milliseconds)
{
    long now = millis();

    while (millis() - now < milliseconds)
    {
        yield();
    }
}

void loop()
{
  switch(CurrentState)
  {
    case Configuring:
      HandleClient();
      break;

    case Running:
      Update();
      break;
  }
}

bool UseWiFi()
{
  return USE_WIFI && (ssid.length() > 0) && (password.length() > 0);
}

bool UseWebSockets()
{
  return CONNECT_WS && UseWiFi() && (address.length() > 0);
}

void Update()
{
  if(UseWiFi() && !IsConnected())
  {
    Connect();

    if(IsConnected())
    {
      if(UseWebSockets())
      {
        Serial.print("Connecting to websocket server on address: ");
        Serial.println(address);

        webSocket.begin(address, 4646, "/");

        webSocket.onEvent(webSocketEvent);
        //webSocket.setAuthorization("user", "Password");
        webSocket.setReconnectInterval(5000);
      }
    }
  }

  if(UseWiFi() && IsConnected() && UseWebSockets())
  {
    webSocket.loop();
  }

  int touchValue = digitalRead(TouchPin);

  if(touchValue == LOW && LastTouchValue == HIGH)
  {
    Serial.println("Scanning");
    myDFPlayer.play(SCAN_SOUND);

    DoScan();

    int finalTouchValue = digitalRead(TouchPin);

    if(finalTouchValue == LOW)
    {
      Serial.println("Access granted");

      if(AudioEnabled)
        myDFPlayer.play(ACCESS_SOUND);

      if(UseWebSockets() && webSocket.isConnected())
        webSocket.sendTXT("access");

      SetAccesVisual();
      delayFor(5000);
    }
    else
    {
      Serial.println("Access denied");

      if(AudioEnabled)
        myDFPlayer.play(DENIED_SOUND);

      if(UseWebSockets() && webSocket.isConnected())
        webSocket.sendTXT("denied");

      SetDeniedVisual();
      delayFor(500);
    }

    if(UseWebSockets() && webSocket.isConnected())
        webSocket.sendTXT("ready");

    Serial.println("Ready to retry");
    SetIdleVisual();
  } 

  LastTouchValue = touchValue;

    //if (AudioEnabled && myDFPlayer.available()) {
  //  printDetail(myDFPlayer.readType(), myDFPlayer.read()); //Print the detail message from DFPlayer to handle different errors and states.
  //}  
}

void SetIdleVisual()
{
  strip.setBrightness(50);
  strip.fill(IdleColor);
  strip.show();
}

void SetAccesVisual()
{
  strip.setBrightness(255);
  SetContourVisual(AccessColor);
}

void SetDeniedVisual()
{
  strip.setBrightness(128);
  SetContourVisual(DeniedColor);
}


void SetContourVisual(uint32_t color)
{
  strip.clear();

  SetRowColor(0, color);
  SetRowColor(1, color);
  SetRowColor(LED_ROW_COUNT - 2, color);
  SetRowColor(LED_ROW_COUNT - 1, color);

  SetColumnColor(0, color);
  SetColumnColor(1, color);
  SetColumnColor(LED_PER_ROW - 1, color);
  SetColumnColor(LED_PER_ROW - 2, color);

  strip.show();
}

void SetRowColor(int row, uint32_t color)
{
    for(int i = 0; i < LED_PER_ROW; ++i)
    {
      strip.setPixelColor(i + (row * LED_PER_ROW), color);
    } 
}

void SetColumnColor(int column, uint32_t color)
{
  for(int j = 0; j < LED_ROW_COUNT; ++j)
  {
    strip.setPixelColor(column + (j * LED_PER_ROW), color);
  } 
}

void DoScan()
{
  if(UseWebSockets() && webSocket.isConnected())
    webSocket.sendTXT("scan");

  strip.setBrightness(255);

  int currentRow = 0;

  //UP
  for(int j = 0; j < LED_ROW_COUNT; ++j)
  {
    strip.clear();
    uint32_t color = OffColor;

    if(j == currentRow)
    {
      color = ScanColor;
    }
      
    SetRowColor(j, color);

    strip.show();

    delayFor(SCAN_DELAY);

    currentRow++;
  }

  //DOWN
  currentRow = LED_ROW_COUNT - 1;

  for(int j = LED_ROW_COUNT - 1; j >= 0; --j)
  {
    strip.clear();
    uint32_t color = OffColor;

    if(j == currentRow)
    {
      color = ScanColor;
    }
      
    for(int i = LED_PER_ROW - 1; i  >= 0; --i)
    {
      strip.setPixelColor(i + (j * LED_PER_ROW), color);
    } 

    strip.show();

    delayFor(SCAN_DELAY);

    currentRow--;
  }
}

void printDetail(uint8_t type, int value){
  switch (type) {
    case TimeOut:
      Serial.println(F("Time Out!"));
      break;
    case WrongStack:
      Serial.println(F("Stack Wrong!"));
      break;
    case DFPlayerCardInserted:
      Serial.println(F("Card Inserted!"));
      break;
    case DFPlayerCardRemoved:
      Serial.println(F("Card Removed!"));
      break;
    case DFPlayerCardOnline:
      Serial.println(F("Card Online!"));
      break;
    case DFPlayerPlayFinished:
      Serial.print(F("Number:"));
      Serial.print(value);
      Serial.println(F(" Play Finished!"));
      break;
    case DFPlayerError:
      Serial.print(F("DFPlayerError:"));
      switch (value) {
        case Busy:
          Serial.println(F("Card not found"));
          break;
        case Sleeping:
          Serial.println(F("Sleeping"));
          break;
        case SerialWrongStack:
          Serial.println(F("Get Wrong Stack"));
          break;
        case CheckSumNotMatch:
          Serial.println(F("Check Sum Not Match"));
          break;
        case FileIndexOut:
          Serial.println(F("File Index Out of Bound"));
          break;
        case FileMismatch:
          Serial.println(F("Cannot Find File"));
          break;
        case Advertise:
          Serial.println(F("In Advertise"));
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
}

bool ReadData(String& ssid, String& psk, String& address)
{
  ssid = EEPROM.readString(0);
  psk = EEPROM.readString(32);
  address = EEPROM.readString(64);

  if(ssid.length() > 0 && psk.length() > 0 && address.length() > 0)
  {
    Serial.print("SSID: ");
    Serial.println(ssid);
    
    Serial.print("PSK: ");
    Serial.println(psk);

    Serial.print("Server address: ");
    Serial.println(address);

    return true;
  }

  Serial.println("No credentials");
  
  return false;
}

void Connect()
{
  while(WiFi.status() != WL_CONNECTED && ssid.length() > 0 )
  {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);

    WiFi.disconnect(false);
    WiFi.begin(ssid.c_str(), password.c_str());
  
    //TODO: do in loop so we can display a message or use yield?
    for(int i = 0; i < 6; ++i)
    {
      Serial.print(".");
      // wait 1 second for re-trying
      delayFor(1000);

      if(WiFi.status() == WL_CONNECTED)
      {
        Serial.println("");
        Serial.print("Connected to ");
        Serial.println(ssid);

        break;
      }
    }
  }
}

void HandleClient()
{
  WiFiClient client = server.available();   // Listen for incoming clients

  if (client)
  {
    Serial.println("A client!");
    String currentLine = "";
    
    while (client.connected())
    {
      if (client.available())
      {             
        char c = client.read();
        
        header += c;
        if (c == '\n') 
        {    
          if (currentLine.length() == 0)
          {
            if(header.indexOf("GET /config.php") >= 0)
            {             
              String newAddress = GetGetParam("?address=", "&ssid");

              String newssid = GetGetParam("&ssid=", "&psk");
  
              String newpsk = GetGetParam("&psk=", "&action");

              Serial.print("Address: ");
              Serial.println(newAddress);
              Serial.print("SSID: ");
              Serial.println(newssid);
              Serial.print("PSK: ");
              Serial.println(newpsk);

              //if(newssid.length() > 0 && newpsk.length() > 0 && newAddress.length() > 0)
              {
                SetData(newssid, newpsk, newAddress);

                ESP.restart();
              }
            }

            client.println("HTTP/1.1 200 OK");
            client.println("Content-type:text/html");
            client.println("Connection: close");
            client.println();
                
            //Web page
            client.println("<!DOCTYPE html><html>");
            client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
            client.println("<link rel=\"icon\" href=\"data:,\">");
            // CSS to style the on/off buttons 
            // Feel free to change the background-color and font-size attributes to fit your preferences
            client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
            client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
            client.println("</style></head>");

            client.println("<body><h1>Configuration Lampe 001</h1>");

            client.println("<form method=\"GET\" action=\"config.php\" id=\"config\" />");

            client.println("Adresse du serveur: <input type=\"text\" name=\"address\" /> <br />");
            client.println("Nom du reseau: <input type=\"text\" name=\"ssid\" /> <br />");
            client.println("Mot de passe: <input type=\"text\" name=\"psk\" /> <br /> <br />");
            
            client.println("<input type=\"submit\" name=\"action\" value=\"Configurer\" />");
            
            client.println("</form>");
            
            client.println("</body></html>");
            
            client.println();

            break;
          } 
          else 
          {
            currentLine = "";
          }
        } 
        else if (c != '\r')
        {
          currentLine += c;
        }
      }
    }

    header = "";

    client.stop();
  }
}

void SetData(String new_ssid, String new_psk, String new_address)
{
    char ssid_c[32];
    
    sprintf(ssid_c, new_ssid.c_str());
    EEPROM.writeString(0, ssid_c);

    char psk_c[32];
    sprintf(psk_c, new_psk.c_str());
    EEPROM.writeString(32, psk_c);

    char address_c[32];
    sprintf(address_c, new_address.c_str());
    EEPROM.writeString(64, address_c);

    EEPROM.commit();
}

String GetGetParam(String prefix, String suffix)
{
  int startIndex = header.indexOf(prefix);

  if (startIndex >= 0)
  {
    int endIndex = header.indexOf(suffix);

    if(endIndex > startIndex)
    {
      return header.substring(startIndex+prefix.length(), endIndex);
    }
  }

  return "";
}

void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {

	switch(type) {
		case WStype_DISCONNECTED:
			Serial.printf("[WSc] Disconnected!\n");

			break;
		case WStype_CONNECTED:
			Serial.printf("[WSc] Connected to url: %s\n", payload);

			break;
		case WStype_TEXT:
			Serial.printf("[WSc] get text: %s\n", payload);

			// send message to server
			// webSocket.sendTXT("message here");
			break;
		case WStype_BIN:
			Serial.printf("[WSc] get binary length: %u\n", length);
			//hexdump(payload, length);

			// send data to server
			// webSocket.sendBIN(payload, length);
			break;
		case WStype_ERROR:			
		case WStype_FRAGMENT_TEXT_START:
		case WStype_FRAGMENT_BIN_START:
		case WStype_FRAGMENT:
		case WStype_FRAGMENT_FIN:
			break;
	}

}
/*
void hexdump(const void *mem, uint32_t len) {
  uint8_t cols = 16;
	const uint8_t* src = (const uint8_t*) mem;
	Serial.printf("\n[HEXDUMP] Address: 0x%08X len: 0x%X (%d)", (ptrdiff_t)src, len, len);
	for(uint32_t i = 0; i < len; i++) {
		if(i % cols == 0) {
			Serial.printf("\n[0x%08X] 0x%08X: ", (ptrdiff_t)src, i);
		}
		Serial.printf("%02X ", *src);
		src++;
	}
	Serial.printf("\n");
}
*/