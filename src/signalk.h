#ifndef SIGNALK_H
#define SIGNALK_H

#ifdef __cplusplus
extern "C" {
#endif

void     sendMeta();

void print_info()
{
  Serial.println("===================== Print Info =======================");

  Serial.print("ssid: ");
  Serial.println(ssid);
  Serial.print("password: ");
  Serial.println(password);
  Serial.print("dev_name: ");
  Serial.println(device_name);

  Serial.print("server: ws://");
  Serial.print(skserver);
  Serial.print(":");
  Serial.print(skport);
  Serial.print("/");
  Serial.println(skpath);
  Serial.print("token: ");
  Serial.println(token);
  //
  Serial.println("========================================================");
}

// Wifi i WSSocket connection

void onWsEventsCallback(WebsocketsEvent event, String data)
{
  if (event == WebsocketsEvent::ConnectionOpened)
  {
    socketState = 1; // Just so it does not try to reconnect
    ledOn = 100;
    ledOff = 100;
    if (DEBUG)
    {
      Serial.println("Wss Connnection Opened");
    }
  }
  else if (event == WebsocketsEvent::ConnectionClosed)
  {
    ledOn = 100;
    ledOff = 500;
    if (strlen(skserver) == 0)
    {
      mdnsDone = false;
    }
    socketState = -2;
    if (DEBUG)
    {
      Serial.println("Wss Connnection Closed");
    }
    vTaskDelay(1000);
  }
  else if (event == WebsocketsEvent::GotPing)
  {
    if (DEBUG)
    {
      Serial.println("Wss Got a Ping!");
    }
  }
  else if (event == WebsocketsEvent::GotPong)
  {
    if (DEBUG)
    {
      Serial.println("Wss Got a Pong!");
    }
  }
  else
  {
    if (DEBUG)
    {
      Serial.println("Received unindentified WebSockets event");
    }
  }
}

void onWsMessageCallback(WebsocketsMessage message)
{
  if (DEBUG)
  {
    Serial.print("Got Message: ");
    Serial.println(message.data());
  }

  if (socketState >= 0)
  {

    ledOn = 1000;
    ledOff = 0;
    /*
    if (DEBUG) { Serial.println("Sending login"); }
    En principi això ja no es necessari
    sendLogin();
    */

    socketState = 2;

    if (DEBUG)
    {
      Serial.println("Sending meta :");
      Serial.println(metaUpdate);
    }
    sendMeta();
  }
  else if (socketState == 1)
  {

    // Ideally check if "statusCode":200 is in message

    socketState = 2;
    setLed();
    if (DEBUG)
    {
      Serial.println("Sending meta :");
      Serial.println(metaUpdate);
    }
    sendMeta();
  }
}

bool start_wifi()
{

  ledOn = 100;
  ledOff = 900;

  // We start by connecting to a WiFi network

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int retries = 0;

  while (WiFi.status() != WL_CONNECTED)
  {
    retries++;
    if (retries > 10)
    {
      return false;
    }
    vTaskDelay(200);
  }

  if (DEBUG)
  {
    Serial.println("");
  }
  if (DEBUG)
  {
    Serial.print("WiFi Connected to ");
  }
  if (DEBUG)
  {
    Serial.println(ssid);
  }
  if (DEBUG)
  {
    Serial.print("IP address: ");
  }
  if (DEBUG)
  {
    Serial.println(WiFi.localIP());
  }

  ledOn = 100;
  ledOff = 800;

  return true;
}

String requestAuth(char *skserver, int skport, char *skpath)
{

  HTTPClient http;

  sprintf(bigBuffer, "http://%s:%d%s", skserver, skport, "/signalk/v1/access/requests");

  String url(bigBuffer);
  Serial.print("URL Request ");
  Serial.println(url);
  http.begin(url);

  String body = "{ \"clientId\": \"" + String(device_name) + "\", \"description\": \"Rudder Angle Sensor\",\"permissions\": \"readwrite\"}";

  http.addHeader("Content-Type", "application/json");
  int responseCode = http.POST(body);

  Serial.print("Made POST with responseCode ");
  Serial.println(responseCode);

  vTaskDelay(500);
  if (responseCode == -1)
  {
    return "";
  }

  String payload = http.getString();
  http.end();

  Serial.print("Answer : ");
  Serial.println(payload);

  JsonDocument doc;
  deserializeJson(doc, payload.c_str());

  if (responseCode == 400)
  {
  }
  else
  {
  }
  // Serial.print("Object "); Serial.println(doc);
  String href = String((const char *)doc["href"]);
  Serial.print("HRef ");
  Serial.println(href);

  return href;
}

bool checkAuth(char *skserver, int skport, String path)
{
  HTTPClient http;

  sprintf(bigBuffer, "http://%s:%d%s", skserver, skport, (char *)path.c_str());
  String url(bigBuffer);
  Serial.println("URL Request  for checking token: ");
  Serial.println(url);

  http.begin(url);
  int responseCode = http.GET();

  String payload = http.getString();
  http.end();
  Serial.print("Answer : ");
  Serial.println(payload);
  JsonDocument doc;
  deserializeJson(doc, payload.c_str());

  if (String((const char *)doc["accessRequest"]["permission"]) == "APPROVED")
  {
    sprintf(token, "Bearer %s", (const char *)doc["accessRequest"]["token"]);

    Serial.println("Token:");
    Serial.println(token);
    client.addHeader("Authorization", token);
    EEPROM.put(192, token);
    EEPROM.commit();
    return true;
  }
  else
  {
    return false;
  }
}

void validateToken()
{
}
bool connectWs(char *skserver, int skport, char *skpath)
{
  if (strlen(token) > 0)
  {
    client.onMessage(onWsMessageCallback);
    client.onEvent(onWsEventsCallback);
    client.addHeader("Authorization", token);
    bool connected = client.connect(skserver, skport, skpath);
    if (connected)
    {
      ledOn = 100;
      ledOff = 200;
      return true;
    }
    else
    {
      token[0] = 0;
    }
  }
  String href = requestAuth(skserver, skport, skpath);
  if (href == "")
  {
    return false;
  }

  while (!checkAuth(skserver, skport, href))
  {
    ledOn = 100;
    ledOff = 400;
    vTaskDelay(5000);
  }

  if (strlen(token) > 0)
  {
    client.onMessage(onWsMessageCallback);
    client.onEvent(onWsEventsCallback);
    client.addHeader("Authorization", token);
    bool connected = client.connect(skserver, skport, skpath);
    if (connected)
    {
      ledOn = 100;
      ledOff = 200;
    }
    return connected;
  }
  else
  {
    socketState = -5; // No way!!!
    Serial.println("********** NO WAY TO GET IDENTIFIED ***********");
    return false;
  }
}

void browseService(const char *service, const char *proto)
{
  Serial.printf("Browsing for service _%s._%s.local. ... ", service, proto);
  int n = MDNS.queryService(service, proto);
  if (n == 0)
  {
    Serial.println("no services found");
  }
  else
  {
    Serial.print(n);
    Serial.println(" service(s) found");
    for (int i = 0; i < n; ++i)
    {
      if (MDNS.hostname(i) == "signalk")
      {
        sprintf(bigBuffer, "%d.%d.%d.%d", MDNS.IP(i)[0], MDNS.IP(i)[01], MDNS.IP(i)[02], MDNS.IP(i)[3]);

        if (strcmp(skserver, bigBuffer) != 0)
        {

          // Reset the connection. Mujst gtet a new Token

          strcpy(skserver, bigBuffer);
          skport = MDNS.port(i);

          token[0] = 0;
          EEPROM.put(192, token);
          EEPROM.put(68, skserver);
          EEPROM.put(88, skport);
          EEPROM.commit();
        }
      }
      // Print details for each service found
      Serial.print("  ");
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(MDNS.hostname(i));
      Serial.print(" (");
      Serial.print(MDNS.IP(i));
      Serial.print(":");
      Serial.print(MDNS.port(i));
      Serial.println(")");
    }
  }
  Serial.println();
}

void startMdns()
{
  Serial.println("Starting MDNS");
  vTaskDelay(10);
  if (!MDNS.begin(device_name))
  { // Set the hostname to "esp32.local"
    Serial.println("Error setting up MDNS responder!");
    while (1)
    {
      vTaskDelay(1000);
    }
  }
  Serial.println("MDNS Started");

  browseService("_http", "_tcp");
  vTaskDelay(10000);
  mdnsDone = true;
}

/* socketState es l'indicador de l'estat mde la connexió de xarxa a SignalK

  Els valors poden ser :

    -5 : Quan no fem res de la xarxaperquè ha fallat la conexió, etc.
    -4 : Valor Inicial. NO estem connectats a la WiFI ni res i emprincipi podem no tenir SSID. Acció depèn de : strlen(ssid >0) -A Pasa a -3 else delay 100
    -3 : Ja tenim una SSID i per tant intentem fer una connexió. Si connected passa a -2, si no espera 1ms


    */

void networkTask(void *parameter)
{
  int points = 0;
  int oldState = -10;
  for (;;)
  {
    if (DEBUG)
    {
      if (oldState != socketState)
      {
        Serial.print("Network Task state changed from ");
        Serial.print(oldState);
        Serial.print(" to ");
        Serial.println(socketState);
        oldState = socketState;
      }
    }

    switch (socketState)
    {

    case -5: // Network Disabled

      vTaskDelay(1000);
      break;

    case -4: // Check ssid

      if (WiFi.status() == WL_CONNECTED)
      { // Should not happen!!!
        Serial.println("Perqué em vull connectar a la xarxa si ja estic connectat?????");
        socketState = -2;
      }

      if (strlen(ssid) > 0)
      {
        socketState = -3;
      }
      else
      {
        vTaskDelay(1000);
      }
      break;

    case -3:

      if (strlen(ssid) == 0)
      {
        socketState = -4;
        vTaskDelay(1000);
      }
      if (strlen(ssid) != 0)
      {
        if (start_wifi())
        {
          if (WiFi.status() == WL_CONNECTED)
          {
            socketState = -2;
          }
          else
          {
          }
          vTaskDelay(100); // Esperem a que canviin la ssid o el password
        }
      }
      else
      {
        vTaskDelay(1000);
      }

      break;

    case -2: // Make connection to server
      if (!mdnsDone)
      {
        startMdns();
      }
      if (connectWs(skserver, skport, skpath))
      {
        Serial.println("Connected to server????");
        print_info();
        socketState = 0;
      }
      break;
    }
    client.poll();
    vTaskDelay(1);
  }
}

void sendMeta()
{ // Not needed, rudderAngle already defined in standard

  return;
  client.send(metaUpdate);
}

void sendData(float angle)
{

  if (socketState != 2 && DEBUG)
  {
    Serial.println("sendData called when not connected to server");
    return;
  }

  String s = update1 + me + update2 + String(angle) + update4;
  client.send(s);
  if (DEBUG)
  {
    Serial.print("Sent: ");
  }
  if (DEBUG)
  {
    Serial.println(s);
  }
}


#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif