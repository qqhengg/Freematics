/*************************************************************************
* Freematics Hub Client implementations for various communication devices
* Distributed under BSD license
* Visit https://freematics.com for more information
* (C)2012-2019 Developed by Stanley Huang <stanley@freematics.com.au>
*************************************************************************/

#include <Arduino.h>
#include "FreematicsBase.h"
#include "FreematicsNetwork.h"

#define XBEE_BAUDRATE 115200

unsigned int HTTPClient::genHeader(char* header, HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
{
    // generate HTTP header
    char *p = header;
    p += sprintf(p, "%s %s HTTP/1.1\r\nConnection: %s\r\n",
      method == HTTP_GET ? "GET" : "POST", path, keepAlive ? "keep-alive" : "close");
    if (method == HTTP_POST) {
      p += sprintf(p, "Content-length: %u\r\n", payloadSize);
    }
    p += sprintf(p, "\r\n");
    return (unsigned int)(p - header);
}

/*******************************************************************************
  Implementation for WiFi (on top of Arduino WiFi library)
*******************************************************************************/

bool ClientWIFI::setup(unsigned int timeout)
{
  for (uint32_t t = millis(); millis() - t < timeout;) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(50);
  }
  return false;
}

String ClientWIFI::getIP()
{
  return WiFi.localIP().toString();
}

bool ClientWIFI::begin(const char* ssid, const char* password)
{
  //listAPs();
  WiFi.begin(ssid, password);
  return true;
}

void ClientWIFI::end()
{
  WiFi.disconnect(true);
}

void ClientWIFI::listAPs()
{
  int n = WiFi.scanNetworks();
  if (n <= 0) {
      Serial.println("No WiFi AP found");
  } else {
      Serial.println("Nearby WiFi APs:");
      for (int i = 0; i < n; ++i) {
          // Print SSID and RSSI for each network found
          Serial.print(i + 1);
          Serial.print(": ");
          Serial.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial.println("dB)");
      }
  }
}

bool UDPClientWIFI::open(const char* host, uint16_t port)
{
  if (udp.beginPacket(host, port)) {
    udpIP = udp.remoteIP();
    udpPort = port;
    if (udp.endPacket()) {
      return true;
    }
  }
  return false;
}

bool UDPClientWIFI::send(const char* data, unsigned int len)
{
  if (udp.beginPacket(udpIP, udpPort)) {
    if (udp.write((uint8_t*)data, len) == len && udp.endPacket()) {
      return true;
    }
  }
  return false;
}

char* UDPClientWIFI::receive(int* pbytes, unsigned int timeout)
{
  uint32_t t = millis();
  do {
    int bytes = udp.parsePacket();
    if (bytes > 0) {
      bytes = udp.read(m_buffer, sizeof(m_buffer));
      if (pbytes) *pbytes = bytes;
      return m_buffer;
    }
  } while (millis() - t < timeout);
  return 0;
}

String UDPClientWIFI::queryIP(const char* host)
{
  return udpIP.toString();
}

void UDPClientWIFI::close()
{
  udp.stop();
}

bool HTTPClientWIFI::open(const char* host, uint16_t port)
{
  if (client.connect(host, port)) {
    m_state = HTTP_CONNECTED;
    return true;
  } else {
    m_state = HTTP_ERROR;
    return false;
  }
}

void HTTPClientWIFI::close()
{
  client.stop();
  m_state = HTTP_DISCONNECTED;
}

int HTTPClientWIFI::send(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
{
  unsigned int headerSize = genHeader(m_buffer, method, path, keepAlive, payload, payloadSize);
  if (client.write(m_buffer, headerSize) != headerSize) {
    m_state = HTTP_DISCONNECTED;
    return 0;
  }
  if (payloadSize) {
    if (client.write(payload, payloadSize) != payloadSize) {
      m_state = HTTP_ERROR;
      return -1;
    }
  }
  m_state = HTTP_SENT;
  return headerSize + payloadSize;
}

char* HTTPClientWIFI::receive(int* pbytes, unsigned int timeout)
{
  int bytes = 0;
  bool eos = false;
  for (uint32_t t = millis(); millis() - t < timeout; ) {
    if (!client.available()) {
      delay(50);
      continue;
    }
    m_buffer[bytes++] = client.read();
    m_buffer[bytes] = 0;
    if (strstr(m_buffer, "\r\n\r\n")) {
      eos = true;
      break;
    }
  }
  if (!eos) {
    m_state = HTTP_ERROR;
    return 0;
  }
  if (pbytes) *pbytes = bytes;
  m_state = HTTP_CONNECTED;
  return m_buffer;
}

/*******************************************************************************
  Implementation for SIM800 (SIM800 AT command-set)
*******************************************************************************/

bool ClientSIM800::begin(CFreematics* device)
{
  m_device = device;
  if (m_stage == 0) {
    device->xbBegin(XBEE_BAUDRATE);
    m_stage = 1;
  }
  for (byte n = 0; n < 10; n++) {
    // try turning on module
    device->xbTogglePower();
    delay(2000);
    // discard any stale data
    device->xbPurge();
    for (byte m = 0; m < 3; m++) {
      if (sendCommand("AT\r")) {
        m_stage = 2;
        return true;
      }
    }
  }
  return false;
}

void ClientSIM800::end()
{
  sendCommand("AT+CPOWD=1\r");
  m_stage = 1;
}

bool ClientSIM800::setup(const char* apn, bool gps, unsigned int timeout)
{
  uint32_t t = millis();
  bool success = false;
  sendCommand("ATE0\r");
  do {
    success = sendCommand("AT+CREG?\r", 3000, "+CREG: 0,1") != 0;
  } while (!success && millis() - t < timeout);
  if (!success) return false;
  do {
    success = sendCommand("AT+CGATT?\r", 3000, "+CGATT: 1");
  } while (!success && millis() - t < timeout);
  sprintf(m_buffer, "AT+CSTT=\"%s\"\r", apn);
  if (!sendCommand(m_buffer)) {
    return false;
  }
  sendCommand("AT+CIICR\r");
  return success;
}

String ClientSIM800::getIP()
{
  for (uint32_t t = millis(); millis() - t < 60000; ) {
    if (sendCommand("AT+CIFSR\r", 3000, ".")) {
      char *p;
      for (p = m_buffer; *p && !isdigit(*p); p++);
      char *q = strchr(p, '\r');
      if (q) *q = 0;
      return p;
    }
  }
  return "";
}

int ClientSIM800::getSignal()
{
    if (sendCommand("AT+CSQ\r", 500)) {
        char *p = strchr(m_buffer, ':');
        if (p) {
          p += 2;
          int db = atoi(p) * 10;
          p = strchr(p, '.');
          if (p) db += *(p + 1) - '0';
          return db;
        }
    }
    return -1;
}
String ClientSIM800::getOperatorName()
{
    // display operator name
    if (sendCommand("AT+COPS?\r") == 1) {
        char *p = strstr(m_buffer, ",\"");
        if (p) {
            p += 2;
            char *s = strchr(p, '\"');
            if (s) *s = 0;
            return p;
        }
    }
    return "";
}

bool ClientSIM800::checkSIM()
{
    return (sendCommand("AT+CPIN?\r") && strstr(m_buffer, "READY"));
}

String ClientSIM800::queryIP(const char* host)
{
  sprintf(m_buffer, "AT+CDNSGIP=\"%s\"\r", host);
  if (sendCommand(m_buffer, 10000)) {
    char *p = strstr(m_buffer, host);
    if (p) {
      p = strstr(p, ",\"");
      if (p) {
        char *ip = p + 2;
        p = strchr(ip, '\"');
        if (p) *p = 0;
        return ip;
      }
    }
  }
  return "";
}

bool ClientSIM800::sendCommand(const char* cmd, unsigned int timeout, const char* expected)
{
  if (cmd) {
    m_device->xbWrite(cmd);
  }
  m_buffer[0] = 0;
  byte ret = m_device->xbReceive(m_buffer, sizeof(m_buffer), timeout, &expected, 1);
  if (ret) {
    return true;
  } else {
    return false;
  }
}

bool ClientSIM800::getLocation(NET_LOCATION* loc)
{
  if (sendCommand("AT+CIPGSMLOC=1,1\r", 3000)) do {
    char *p;
    if (!(p = strchr(m_buffer, ':'))) break;
    if (!(p = strchr(p, ','))) break;
    loc->lng = atof(++p);
    if (!(p = strchr(p, ','))) break;
    loc->lat = atof(++p);
    if (!(p = strchr(p, ','))) break;
    loc->year = atoi(++p) - 2000;
    if (!(p = strchr(p, '/'))) break;
    loc->month = atoi(++p);
    if (!(p = strchr(p, '/'))) break;
    loc->day = atoi(++p);
    if (!(p = strchr(p, ','))) break;
    loc->hour = atoi(++p);
    if (!(p = strchr(p, ':'))) break;
    loc->minute = atoi(++p);
    if (!(p = strchr(p, ':'))) break;
    loc->second = atoi(++p);
    return true;
  } while(0);
  return false;
}

bool UDPClientSIM800::open(const char* host, uint16_t port)
{
  //sendCommand("AT+CLPORT=\"UDP\",8000\r");
  sendCommand("AT+CIPSRIP=1\r");
  //sendCommand("AT+CIPUDPMODE=1\r");
  sprintf(m_buffer, "AT+CIPSTART=\"UDP\",\"%s\",\"%u\"\r", host, port);
  return sendCommand(m_buffer, 3000);
}

void UDPClientSIM800::close()
{
  sendCommand("AT+CIPCLOSE\r");
}

bool UDPClientSIM800::send(const char* data, unsigned int len)
{
  sprintf(m_buffer, "AT+CIPSEND=%u\r", len);
  if (sendCommand(m_buffer, 200, ">")) {
    m_device->xbWrite(data, len);
    m_device->xbWrite("\r", 1);
    if (sendCommand(0, 5000, "\r\nSEND OK")) {
      return true;
    }
  }
  return false;
}

char* UDPClientSIM800::receive(int* pbytes, unsigned int timeout)
{
	char *data = checkIncoming(pbytes);
	if (data) return data;
  if (sendCommand("AT+CIPUDPMODE?\r", timeout, "RECV FROM:")) {
		return checkIncoming(pbytes);
  }
  return 0;
}

char* UDPClientSIM800::checkIncoming(int* pbytes)
{
	char *p = strstr(m_buffer, "RECV FROM:");
	if (p) {
    *p = '-'; // mark this datagram as checked
    p = strchr(p, '\n');
    if (p) {
      if (pbytes) *pbytes = strlen(p);
      return p + 1;
    }
  }
  return 0;
}

bool HTTPClientSIM800::open(const char* host, uint16_t port)
{
  if (!sendCommand("AT+HTTPINIT\r")) return false;
  m_host = host;
  m_port = port;
  m_state = HTTP_CONNECTED;
  return true;
}

int HTTPClientSIM800::send(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
{
  sendCommand("AT+HTTPPARA = \"CID\",1\r");
  sprintf(m_buffer, "AT+HTTPPARA=\"URL\",\"%s:%u%s\"\r", m_host.c_str(), m_port, path);
  if (!sendCommand(m_buffer)) {  
  } else if (method == HTTP_GET) {
    if (sendCommand("AT+HTTPACTION=0\r", HTTP_CONN_TIMEOUT)) {
      m_state = HTTP_SENT;
      return strlen(path);  
    }
  } else {
    sprintf(m_buffer, "AT+HTTPDATA=%u,10000\r", payloadSize);
    if (sendCommand(m_buffer)) {
      if (sendCommand("AT+HTTPACTION=1\r", HTTP_CONN_TIMEOUT))
        m_state = HTTP_SENT;
        return payloadSize;
    }
  }
  m_state = HTTP_ERROR;
  Serial.println(m_buffer);
  return -1;
}

void HTTPClientSIM800::close()
{
  sendCommand("AT+HTTPTERM\r");
  m_state = HTTP_DISCONNECTED;
}

char* HTTPClientSIM800::receive(int* pbytes, unsigned int timeout)
{
  char *p = strstr(m_buffer, "+HTTPACTION:");
  if (!p) {
    if (!sendCommand(0, timeout, "+HTTPACTION")) return 0;
  }
  if (sendCommand("AT+HTTPREAD\r", 1000)) {
    Serial.println(m_buffer);
    p = strstr(m_buffer, "+HTTPREAD: ");
    if (p) {
      p += 11;
      int bytes = atoi(p);
      p = strchr(p, '\n');
      if (p++) {
        p[bytes] = 0;
        if (pbytes) *pbytes = bytes;
        return p;
      }
    }
  }
  m_state = HTTP_ERROR;
  return 0;  
}

/*******************************************************************************
  Implementation for SIM5360
*******************************************************************************/

bool ClientSIM5360::begin(CFreematics* device)
{
  m_device = device;
  if (m_stage == 0) {
    device->xbBegin(XBEE_BAUDRATE);
    m_stage = 1;
  }
  for (byte n = 0; n < 10; n++) {
    // try turning on module
    device->xbTogglePower();
    delay(3000);
    // discard any stale data
    device->xbPurge();
    for (byte m = 0; m < 5; m++) {
      if (sendCommand("AT\r") && sendCommand("ATE0\r") && sendCommand("ATI\r")) {
        m_stage = 2;
        // retrieve module info
        char *p = strstr(m_buffer, "Model:");
        if (p) p = strchr(p, '_');
        if (p++) {
          int i = 0;
          while (i < sizeof(m_model) - 1 && p[i] && p[i] != '\r' && p[i] != '\n') {
            m_model[i] = p[i];
            i++;
          }
          m_model[i] = 0;
        }
        p = strstr(m_buffer, "IMEI:");
        if (p) strncpy(IMEI, p + 6, sizeof(IMEI) - 1);
        return true;
      }
    }
  }
  return false;
}

void ClientSIM5360::end()
{
  sendCommand("AT+CRESET\r");
  sendCommand("AT+GPS=0\r");
  delay(1000);
  sendCommand("AT+CPOF\r");
  m_stage = 1;
  if (m_gps) {
    delete m_gps;
    m_gps = 0;
  }
}

bool ClientSIM5360::setup(const char* apn, bool gps, unsigned int timeout)
{
  uint32_t t = millis();
  bool success = false;
  //sendCommand("AT+CNMP=13\r"); // GSM only
  //sendCommand("AT+CNMP=14\r"); // WCDMA only
  do {
    do {
      success = sendCommand("AT+CPSI?\r", 1000, "Online");
      if (strstr(m_buffer, "Off")) {
        success = false;
        break;
      }
      if (success) {
        if (!strstr(m_buffer, "NO SERVICE"))
          break;
        success = false;
      }
    } while (millis() - t < timeout);
    if (!success) break;

    success = false;
    do {
      if (sendCommand("AT+CREG?\r", 1000, "+CREG: 0,")) {
        char *p = strstr(m_buffer, "+CREG: 0,");
        success = (p && (*(p + 9) == '1' || *(p + 9) == '5'));
      }
    } while (!success && millis() - t < timeout);
    if (!success) break;

    success = false;
    do {
      if (sendCommand("AT+CGREG?\r",1000, "+CGREG: 0,")) {
        char *p = strstr(m_buffer, "+CGREG: 0,");
        success = (p && (*(p + 10) == '1' || *(p + 10) == '5'));
      }
    } while (!success && millis() - t < timeout);
    if (!success) break;

    if (apn && *apn) {
      sprintf(m_buffer, "AT+CGSOCKCONT=1,\"IP\",\"%s\"\r", apn);
      sendCommand(m_buffer);
    }
    if (!success) break;

    //sendCommand("AT+CSOCKAUTH=1,1,\"APN_PASSWORD\",\"APN_USERNAME\"\r");

    sendCommand("AT+CSOCKSETPN=1\r");
    sendCommand("AT+CIPMODE=0\r");
    sendCommand("AT+NETOPEN\r");
  } while(0);
  if (!success) Serial.println(m_buffer);
  // enable internal GPS if required
  if (gps) {
    sendCommand("AT+CVAUXV=61\r");
    sendCommand("AT+CVAUXS=1\r");
    if (sendCommand("AT+CGPS=1\r") && sendCommand("AT+CGPSINFO=1\r")) {
      if (!m_gps) {
        m_gps = new GPS_DATA;
        memset(m_gps, 0, sizeof(GPS_DATA));
      }
    }
  }
  return success;
}

String ClientSIM5360::getIP()
{
  uint32_t t = millis();
  do {
    if (sendCommand("AT+IPADDR\r", 3000, "\r\nOK\r\n")) {
      char *p = strstr(m_buffer, "+IPADDR:");
      if (p) {
        char *ip = p + 9;
        if (*ip != '0') {
					char *q = strchr(ip, '\r');
					if (q) *q = 0;
          return ip;
        }
      }
    }
    delay(500);
  } while (millis() - t < 15000);
  return "";
}

int ClientSIM5360::getSignal()
{
  if (sendCommand("AT+CSQ\r", 500)) {
      char *p = strchr(m_buffer, ':');
      if (p) {
        p += 2;
        int db = atoi(p) * 10;
        p = strchr(p, '.');
        if (p) db += *(p + 1) - '0';
        return db;
      }
  }
  return -1;
}

String ClientSIM5360::getOperatorName()
{
  if (sendCommand("AT+COPS?\r")) {
      char *p = strstr(m_buffer, ",\"");
      if (p) {
          p += 2;
          char *s = strchr(p, '\"');
          if (s) *s = 0;
          return p;
      }
  }
  return "";
}

bool ClientSIM5360::checkSIM()
{
  bool success;
  for (byte n = 0; n < 10 && !(success = sendCommand("AT+CPIN?\r", 500, ": READY")); n++);
  return success;  
}

String ClientSIM5360::queryIP(const char* host)
{
  sprintf(m_buffer, "AT+CDNSGIP=\"%s\"\r", host);
  if (sendCommand(m_buffer, 10000)) {
    char *p = strstr(m_buffer, host);
    if (p) {
      p = strstr(p, ",\"");
      if (p) {
        char *ip = p + 2;
        p = strchr(ip, '\"');
        if (p) *p = 0;
        return ip;
      }
    }
  }
  return "";
}

bool ClientSIM5360::sendCommand(const char* cmd, unsigned int timeout, const char* expected)
{
  if (cmd) {
    m_device->xbWrite(cmd);
  }
  m_buffer[0] = 0;
  byte ret = m_device->xbReceive(m_buffer, sizeof(m_buffer), timeout, &expected, 1);
  if (ret) {
    return true;
  } else {
    return false;
  }
}

float ClientSIM5360::parseDegree(const char* s)
{
  char *p;
  unsigned long left = atol(s);
  unsigned long tenk_minutes = (left % 100UL) * 100000UL;
  if ((p = strchr(s, '.')))
  {
    unsigned long mult = 10000;
    while (isdigit(*++p))
    {
      tenk_minutes += mult * (*p - '0');
      mult /= 10;
    }
  }
  return (left / 100) + (float)tenk_minutes / 6 / 1000000;
}

void ClientSIM5360::checkGPS()
{
  // check and parse GPS data
  char *p;
  if (m_gps && (p = strstr(m_buffer, "+CGPSINFO:"))) do {
    if (!(p = strchr(p, ':'))) break;
    if (*(++p) == ',') break;
    m_gps->lat = parseDegree(p);
    if (!(p = strchr(p, ','))) break;
    if (*(++p) == 'S') m_gps->lat = -m_gps->lat;
    if (!(p = strchr(p, ','))) break;
    m_gps->lng = parseDegree(++p);
    if (!(p = strchr(p, ','))) break;
    if (*(++p) == 'W') m_gps->lng = -m_gps->lng;
    if (!(p = strchr(p, ','))) break;
    m_gps->date = atoi(++p);
    if (!(p = strchr(p, ','))) break;
    m_gps->time = atof(++p) * 100;
    if (!(p = strchr(p, ','))) break;
    m_gps->alt = atof(++p);
    if (!(p = strchr(p, ','))) break;
    m_gps->speed = atof(++p);
    if (!(p = strchr(p, ','))) break;
    m_gps->heading = atoi(++p);
    m_gps->ts = millis();
  } while (0);
}

bool UDPClientSIM5360::open(const char* host, uint16_t port)
{
  if (host) {
    udpIP = queryIP(host);
    if (!udpIP.length()) {
      udpIP = host;
    }
    udpPort = port;
  }
  sprintf(m_buffer, "AT+CIPOPEN=0,\"UDP\",\"%s\",%u,8000\r", udpIP.c_str(), udpPort);
  if (!sendCommand(m_buffer, 3000)) {
    close();
    Serial.println(m_buffer);
    return false;
  }
  return true;
}

void UDPClientSIM5360::close()
{
  sendCommand("AT+CIPCLOSE=0\r");
}

bool UDPClientSIM5360::send(const char* data, unsigned int len)
{
  sprintf(m_buffer, "AT+CIPSEND=0,%u,\"%s\",%u\r", len, udpIP.c_str(), udpPort);
  if (sendCommand(m_buffer, 100, ">")) {
    m_device->xbWrite(data, len);
    return sendCommand(0, 1000);
  }
  return false;
}

char* UDPClientSIM5360::receive(int* pbytes, unsigned int timeout)
{
	char *data = checkIncoming(pbytes);
	if (data) return data;
  if (sendCommand(0, timeout, "+IPD")) {
		return checkIncoming(pbytes);
  }
  return 0;
}

char* UDPClientSIM5360::checkIncoming(int* pbytes)
{
  checkGPS();
  char *p = strstr(m_buffer, "+IPD");
	if (p) {
    *p = '-'; // mark this datagram as checked
    int len = atoi(p + 4);
    if (pbytes) *pbytes = len;
    p = strchr(p, '\n');
    if (p) {
      if (strlen(++p) > len) *(p + len) = 0;
      return p;
    }
  }
	return 0;
}

bool HTTPClientSIM5360::open(const char* host, uint16_t port)
{
  sendCommand("AT+CHTTPSSTART\r", 1000);
  sprintf(m_buffer, "AT+CHTTPSOPSE=\"%s\",%u,1\r", host, port);
  if (sendCommand(m_buffer, HTTP_CONN_TIMEOUT)) {
    m_state = HTTP_CONNECTED;
    return true;
  } else {
    m_state = HTTP_ERROR;
    return false;
  }
}

void HTTPClientSIM5360::close()
{
  sendCommand("AT+CHTTPSCLSE\r");
  m_state = HTTP_DISCONNECTED;
}

int HTTPClientSIM5360::send(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
{
  unsigned int headerSize = genHeader(m_buffer, method, path, keepAlive, payload, payloadSize);
  // issue HTTP send command
  sprintf(m_buffer, "AT+CHTTPSSEND=%u\r", headerSize + payloadSize);
  if (!sendCommand(m_buffer, 100, ">")) {
    m_state = HTTP_DISCONNECTED;
    return 0;
  }
  // send HTTP header
  genHeader(m_buffer, method, path, keepAlive, payload, payloadSize);
  m_device->xbWrite(m_buffer);
  // send POST payload if any
  if (payload) m_device->xbWrite(payload);
  m_buffer[0] = 0;
  if (sendCommand("AT+CHTTPSSEND\r")) {
    m_state = HTTP_SENT;
    return headerSize + payloadSize;
  } else {
    Serial.println(m_buffer);
    m_state = HTTP_ERROR;
    return -1;
  }
}

char* HTTPClientSIM5360::receive(int* pbytes, unsigned int timeout)
{
  const char* expected = "+CHTTPS: RECV EVENT";
  checkGPS();
  if (!strstr(m_buffer, expected)) {
    bool hasEvent = sendCommand(0, timeout, expected);
    checkGPS();
    if (!hasEvent) return 0;
  }

  // start receiving
  int received = 0;
  char* payload = 0;
  /*
    +CHTTPSRECV:XX\r\n
    [XX bytes from server]\r\n
    +CHTTPSRECV: 0\r\n
  */
  sprintf(m_buffer, "AT+CHTTPSRECV=%u\r", sizeof(m_buffer) - 36);
  bool success = sendCommand(m_buffer, HTTP_CONN_TIMEOUT, "\r\n+CHTTPSRECV: 0");
  checkGPS();
  if (success) {
    char *p = strstr(m_buffer, "+CHTTPSRECV:");
    if (p) {
      p = strchr(p, ',');
      if (p) {
        received = atoi(p + 1);
        char *q = strchr(p, '\n');
        payload = q ? (q + 1) : p;
        if (m_buffer + sizeof(m_buffer) - payload > received) {
          payload[received] = 0;
        }
      }
    }
  }
  if (received == 0) {
    m_state = HTTP_ERROR;
    return 0;
  } else {
    m_state = HTTP_CONNECTED;
    if (pbytes) *pbytes = received;
    return payload;
  }
}

bool ClientSIM7600::setup(const char* apn, bool gps, unsigned int timeout)
{
  uint32_t t = millis();
  bool success = false;
  //sendCommand("AT+CNMP=13\r"); // GSM only
  //sendCommand("AT+CNMP=14\r"); // WCDMA only
  //sendCommand("AT+CNMP=38\r"); // LTE only
  do {
    do {
      success = sendCommand("AT+CPSI?\r", 1000, "Online");
      if (strstr(m_buffer, "Off")) {
        success = false;
        break;
      }
      if (success) {
        if (!strstr(m_buffer, "NO SERVICE"))
          break;
        success = false;
      }
    } while (millis() - t < timeout);
    if (!success) break;

    success = false;
    do {
      if (sendCommand("AT+CREG?\r", 1000, "+CREG: 0,")) {
        char *p = strstr(m_buffer, "+CREG: 0,");
        success = (p && (*(p + 9) == '1' || *(p + 9) == '5'));
      }
    } while (!success && millis() - t < timeout);
    if (!success) break;

    success = false;
    do {
      if (sendCommand("AT+CGREG?\r",1000, "+CGREG: 0,")) {
        char *p = strstr(m_buffer, "+CGREG: 0,");
        success = (p && (*(p + 10) == '1' || *(p + 10) == '5'));
      }
    } while (!success && millis() - t < timeout);
    if (!success) break;

    if (apn && *apn) {
      sprintf(m_buffer, "AT+CGSOCKCONT=1,\"IP\",\"%s\"\r", apn);
      sendCommand(m_buffer);
    }
    if (!success) break;

    //sendCommand("AT+CSOCKAUTH=1,1,\"APN_PASSWORD\",\"APN_USERNAME\"\r");

    sendCommand("AT+CSOCKSETPN=1\r");
    sendCommand("AT+CIPMODE=0\r");
    sendCommand("AT+NETOPEN\r");
  } while(0);
  // enable internal GPS if required
  if (gps) {
    if (sendCommand("AT+CGPS=1\r") && sendCommand("AT+CGPSINFO=1\r")) {
      if (!m_gps) {
        m_gps = new GPS_DATA;
        memset(m_gps, 0, sizeof(GPS_DATA));
      }
    }
    sendCommand("AT+CVAUXV=3050\r");
    sendCommand("AT+CVAUXS=1\r");
  }
  return success;
}

bool UDPClientSIM7600::open(const char* host, uint16_t port)
{
  if (host) {
    udpIP = queryIP(host);
    if (!udpIP.length()) {
      udpIP = host;
    }
    udpPort = port;
  }
  sprintf(m_buffer, "AT+CIPOPEN=0,\"UDP\",\"%s\",%u,8000\r", udpIP.c_str(), udpPort);
  if (!sendCommand(m_buffer, 3000)) {
    close();
    Serial.println(m_buffer);
    return false;
  }
  return true;
}

void UDPClientSIM7600::close()
{
  sendCommand("AT+CIPCLOSE=0\r");
}

bool UDPClientSIM7600::send(const char* data, unsigned int len)
{
  sprintf(m_buffer, "AT+CIPSEND=0,%u,\"%s\",%u\r", len, udpIP.c_str(), udpPort);
  if (sendCommand(m_buffer, 100, ">")) {
    m_device->xbWrite(data, len);
    return sendCommand(0, 1000);
  }
  return false;
}

char* UDPClientSIM7600::receive(int* pbytes, unsigned int timeout)
{
	char *data = checkIncoming(pbytes);
	if (data) return data;
  if (sendCommand(0, timeout, "+IPD")) {
		return checkIncoming(pbytes);
  }
  return 0;
}

char* UDPClientSIM7600::checkIncoming(int* pbytes)
{
  checkGPS();
  char *p = strstr(m_buffer, "+IPD");
	if (p) {
    *p = '-'; // mark this datagram as checked
    int len = atoi(p + 4);
    if (pbytes) *pbytes = len;
    p = strchr(p, '\n');
    if (p) {
      if (strlen(++p) > len) *(p + len) = 0;
      return p;
    }
  }
	return 0;
}

bool HTTPClientSIM7600::open(const char* host, uint16_t port)
{
  sprintf(m_buffer, "AT+CHTTPACT=\"%s\",%u\r", host, port);
  if (sendCommand(m_buffer, HTTP_CONN_TIMEOUT, "+CHTTPACT: REQUEST")) {
    m_state = HTTP_CONNECTED;
    return true;
  } else {
    m_state = HTTP_ERROR;
    return false;
  }
}

void HTTPClientSIM7600::close()
{
  m_state = HTTP_DISCONNECTED;
}

int HTTPClientSIM7600::send(HTTP_METHOD method, const char* path, bool keepAlive, const char* payload, int payloadSize)
{
  // send HTTP header
  int headerSize = genHeader(m_buffer, method, path, keepAlive, payload, payloadSize);
  m_device->xbWrite(m_buffer);
  // send POST payload if any
  if (payload) m_device->xbWrite(payload);
  m_buffer[0] = 0;
  if (sendCommand("\x1A")) {
    m_state = HTTP_SENT;
    return headerSize + payloadSize;
  } else {
    Serial.println(m_buffer);
    m_state = HTTP_ERROR;
    return -1;
  }
}

char* HTTPClientSIM7600::receive(int* pbytes, unsigned int timeout)
{
  int received = 0;
  char* payload = 0;
  bool success = sendCommand(0, HTTP_CONN_TIMEOUT, "\r\n+CHTTPACT: 0");
  checkGPS();
  if (success) {
    char *p = strstr(m_buffer, "\r\n+CHTTPACT: DATA,");
    if (p) {
      p += 18;
      received = atoi(p);
      char *q = strchr(p, '\n');
      payload = q ? (q + 1) : p;
      if (m_buffer + sizeof(m_buffer) - payload > received) {
        payload[received] = 0;
      }
    }
  }
  if (received == 0) {
    m_state = HTTP_ERROR;
    return 0;
  } else {
    m_state = HTTP_DISCONNECTED;
    if (pbytes) *pbytes = received;
    return payload;
  }
}