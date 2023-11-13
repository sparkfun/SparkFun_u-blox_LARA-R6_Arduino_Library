/*
  Arduino library for the u-blox LARA-R6 LTE-M / NB-IoT modules with secure cloud, as used on the SparkFun MicroMod Asset Tracker
  By: Paul Clark
  October 19th 2020

  Based extensively on the:
  Arduino Library for the SparkFun LTE CAT M1/NB-IoT Shield - LARA-R4
  Written by Jim Lindblom @ SparkFun Electronics, September 5, 2018

  This Arduino library provides mechanisms to initialize and use
  the LARA-R6 module over either a SoftwareSerial or hardware serial port.

  Please see LICENSE.md for the license information

*/

#include <SparkFun_u-blox_LARA-R6_Arduino_Library.h>

LARA_R6::LARA_R6(int powerPin, int resetPin, uint8_t maxInitTries)
{
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  _softSerial = nullptr;
#endif
  _hardSerial = nullptr;
  _baud = 0;
  _resetPin = resetPin;
  _powerPin = powerPin;
  _invertPowerPin = false;
  _maxInitTries = maxInitTries;
  _socketListenCallback = nullptr;
  _socketReadCallback = nullptr;
  _socketReadCallbackPlus = nullptr;
  _socketCloseCallback = nullptr;
  _gpsRequestCallback = nullptr;
  _simStateReportCallback = nullptr;
  _psdActionRequestCallback = nullptr;
  _pingRequestCallback = nullptr;
  _httpCommandRequestCallback = nullptr;
  _mqttCommandRequestCallback = nullptr;
  _registrationCallback = nullptr;
  _epsRegistrationCallback = nullptr;
  _debugAtPort = nullptr;
  _debugPort = nullptr;
  _printDebug = false;
  _lastRemoteIP = {0, 0, 0, 0};
  _lastLocalIP = {0, 0, 0, 0};
  for (int i = 0; i < LARA_R6_NUM_SOCKETS; i++)
    _lastSocketProtocol[i] = 0; // Set to zero initially. Will be set to TCP/UDP by socketOpen etc.
  _autoTimeZoneForBegin = true;
  _bufferedPollReentrant = false;
  _pollReentrant = false;
  _laraResponseBacklogLength = 0;
  _laraRXBuffer = nullptr;
  _pruneBuffer = nullptr;
  _laraResponseBacklog = nullptr;
}

LARA_R6::~LARA_R6(void) {
  if (nullptr != _laraRXBuffer) {
    delete[] _laraRXBuffer;
    _laraRXBuffer = nullptr;
  }
  if (nullptr != _pruneBuffer) {
    delete[] _pruneBuffer;
    _pruneBuffer = nullptr;
  }
  if (nullptr != _laraResponseBacklog) {
    delete[] _laraResponseBacklog;
    _laraResponseBacklog = nullptr;
  }
}

#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
bool LARA_R6::begin(SoftwareSerial &softSerial, unsigned long baud)
{
  if (nullptr == _laraRXBuffer)
  {
    _laraRXBuffer = new char[_RXBuffSize];
    if (nullptr == _laraRXBuffer)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _laraRXBuffer!"));
      return false;
    }
  }
  memset(_laraRXBuffer, 0, _RXBuffSize);

  if (nullptr == _pruneBuffer)
  {
    _pruneBuffer = new char[_RXBuffSize];
    if (nullptr == _pruneBuffer)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _pruneBuffer!"));
      return false;
    }
  }
  memset(_pruneBuffer, 0, _RXBuffSize);

  if (nullptr == _laraResponseBacklog)
  {
    _laraResponseBacklog = new char[_RXBuffSize];
    if (nullptr == _laraResponseBacklog)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _laraResponseBacklog!"));
      return false;
    }
  }
  memset(_laraResponseBacklog, 0, _RXBuffSize);

  LARA_R6_error_t err;

  _softSerial = &softSerial;

  err = init(baud);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    return true;
  }
  return false;
}
#endif

bool LARA_R6::begin(HardwareSerial &hardSerial, unsigned long baud)
{
  if (nullptr == _laraRXBuffer)
  {
    _laraRXBuffer = new char[_RXBuffSize];
    if (nullptr == _laraRXBuffer)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _laraRXBuffer!"));
      return false;
    }
  }
  memset(_laraRXBuffer, 0, _RXBuffSize);

  if (nullptr == _pruneBuffer)
  {
    _pruneBuffer = new char[_RXBuffSize];
    if (nullptr == _pruneBuffer)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _pruneBuffer!"));
      return false;
    }
  }
  memset(_pruneBuffer, 0, _RXBuffSize);

  if (nullptr == _laraResponseBacklog)
  {
    _laraResponseBacklog = new char[_RXBuffSize];
    if (nullptr == _laraResponseBacklog)
    {
      if (_printDebug == true)
        _debugPort->println(F("begin: not enough memory for _laraResponseBacklog!"));
      return false;
    }
  }
  memset(_laraResponseBacklog, 0, _RXBuffSize);

  LARA_R6_error_t err;

  _hardSerial = &hardSerial;

  err = init(baud);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    return true;
  }
  return false;
}

//Calling this function with nothing sets the debug port to Serial
//You can also call it with other streams like Serial1, SerialUSB, etc.
void LARA_R6::enableDebugging(Print &debugPort)
{
  _debugPort = &debugPort;
  _printDebug = true;
}

//Calling this function with nothing sets the debug port to Serial
//You can also call it with other streams like Serial1, SerialUSB, etc.
void LARA_R6::enableAtDebugging(Print &debugPort)
{
  _debugAtPort = &debugPort;
  _printAtDebug = true;
}

// This function was originally written by Matthew Menze for the LTE Shield (LARA-R4) library
// See: https://github.com/sparkfun/SparkFun_LTE_Shield_Arduino_Library/pull/8
// It does the same job as ::poll but also processed any 'old' data stored in the backlog first
// It also has a built-in timeout - which ::poll does not
bool LARA_R6::bufferedPoll(void)
{
  if (_bufferedPollReentrant == true) // Check for reentry (i.e. bufferedPoll has been called from inside a callback)
    return false;

  _bufferedPollReentrant = true;

  int avail = 0;
  char c = 0;
  bool handled = false;
  unsigned long timeIn = millis();
  char *event;
  int backlogLen = _laraResponseBacklogLength;

  memset(_laraRXBuffer, 0, _RXBuffSize); // Clear _laraRXBuffer

  // Does the backlog contain any data? If it does, copy it into _laraRXBuffer and then clear the backlog
  if (_laraResponseBacklogLength > 0)
  {
    //The backlog also logs reads from other tasks like transmitting.
    if (_printDebug == true)
    {
      _debugPort->print(F("bufferedPoll: backlog found! backlogLen is "));
      _debugPort->println(_laraResponseBacklogLength);
    }
    memcpy(_laraRXBuffer + avail, _laraResponseBacklog, _laraResponseBacklogLength);
    avail += _laraResponseBacklogLength;
    memset(_laraResponseBacklog, 0, _RXBuffSize); // Clear the backlog making sure it is NULL-terminated
    _laraResponseBacklogLength = 0;
  }

  if ((hwAvailable() > 0) || (backlogLen > 0)) // If either new data is available, or backlog had data.
  {
    //Check for incoming serial data. Copy it into the backlog

    // Important note:
    // On ESP32, Serial.available only provides an update every ~120 bytes during the reception of long messages:
    // https://gitter.im/espressif/arduino-esp32?at=5e25d6370a1cf54144909c85
    // Be aware that if a long message is being received, the code below will timeout after _rxWindowMillis = 2 millis.
    // At 115200 baud, hwAvailable takes ~120 * 10 / 115200 = 10.4 millis before it indicates that data is being received.

    while (((millis() - timeIn) < _rxWindowMillis) && (avail < _RXBuffSize))
    {
      if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is NULL
      {
        c = readChar();
        // bufferedPoll is only interested in the URCs.
        // The URCs are all readable.
        // strtok does not like NULL characters.
        // So we need to make sure no NULL characters are added to _laraRXBuffer
        if (c == '\0')
          c = '0'; // Convert any NULLs to ASCII Zeros
        _laraRXBuffer[avail++] = c;
        timeIn = millis();
      } else {
        yield();
      }
    }

    // _laraRXBuffer now contains the backlog (if any) and the new serial data (if any)

    // A health warning about strtok:
    // strtok will convert any delimiters it finds ("\r\n" in our case) into NULL characters.
    // Also, be very careful that you do not use strtok within an strtok while loop.
    // The next call of strtok(NULL, ...) in the outer loop will use the pointer saved from the inner loop!
    // In our case, strtok is also used in pruneBacklog, which is called by waitForRespone or sendCommandWithResponse,
    // which is called by the parse functions called by processURCEvent...
    // The solution is to use strtok_r - the reentrant version of strtok

    char *preservedEvent;
    event = strtok_r(_laraRXBuffer, "\r\n", &preservedEvent); // Look for an 'event' (_laraRXBuffer contains something ending in \r\n)

    if (event != nullptr)
      if (_printDebug == true)
        _debugPort->println(F("bufferedPoll: event(s) found! ===>"));

    while (event != nullptr) // Keep going until all events have been processed
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("bufferedPoll: start of event: "));
        _debugPort->println(event);
      }

      //Process the event
      bool latestHandled = processURCEvent((const char *)event);
      if (latestHandled) {
        if ((true == _printAtDebug) && (nullptr != event)) {
          _debugAtPort->print(event);
        }
        handled = true; // handled will be true if latestHandled has ever been true
      }
      if ((_laraResponseBacklogLength > 0) && ((avail + _laraResponseBacklogLength) < _RXBuffSize)) // Has any new data been added to the backlog?
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("bufferedPoll: new backlog added!"));
        }
        memcpy(_laraRXBuffer + avail, _laraResponseBacklog, _laraResponseBacklogLength);
        avail += _laraResponseBacklogLength;
        memset(_laraResponseBacklog, 0, _RXBuffSize); //Clear out the backlog buffer again.
        _laraResponseBacklogLength = 0;
      }

      //Walk through any remaining events
      event = strtok_r(nullptr, "\r\n", &preservedEvent);

      if (_printDebug == true)
        _debugPort->println(F("bufferedPoll: end of event")); //Just to denote end of processing event.

      if (event == nullptr)
        if (_printDebug == true)
          _debugPort->println(F("bufferedPoll: <=== end of event(s)!"));
    }
  }

  _bufferedPollReentrant = false;

  return handled;
} // /bufferedPoll

// Parse incoming URC's - the associated parse functions pass the data to the user via the callbacks (if defined)
bool LARA_R6::processURCEvent(const char *event)
{
  { // URC: +UUSORD (Read Socket Data)
    int socket, length;
    char *searchPtr = strstr(event, LARA_R6_READ_SOCKET_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_READ_SOCKET_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // Skip spaces
      int ret = sscanf(searchPtr, "%d,%d", &socket, &length);
      if (ret == 2)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: read socket data"));
        // From the LARA_R6 AT Commands Manual:
        // "For the UDP socket type the URC +UUSORD: <socket>,<length> notifies that a UDP packet has been received,
        //  either when buffer is empty or after a UDP packet has been read and one or more packets are stored in the
        //  buffer."
        // So we need to check if this is a TCP socket or a UDP socket:
        //  If UDP, we call parseSocketReadIndicationUDP.
        //  Otherwise, we call parseSocketReadIndication.
        if (_lastSocketProtocol[socket] == LARA_R6_UDP)
        {
          if (_printDebug == true)
            _debugPort->println(F("processReadEvent: received +UUSORD but socket is UDP. Calling parseSocketReadIndicationUDP"));
          parseSocketReadIndicationUDP(socket, length);
        }
        else
          parseSocketReadIndication(socket, length);
        return true;
      }
    }
  }
  { // URC: +UUSORF (Receive From command (UDP only))
    int socket, length;
    char *searchPtr = strstr(event, LARA_R6_READ_UDP_SOCKET_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_READ_UDP_SOCKET_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int ret = sscanf(searchPtr, "%d,%d", &socket, &length);
      if (ret == 2)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: UDP receive"));
        parseSocketReadIndicationUDP(socket, length);
        return true;
      }
    }
  }
  { // URC: +UUSOLI (Set Listening Socket)
    int socket = 0;
    int listenSocket = 0;
    unsigned int port = 0;
    unsigned int listenPort = 0;
    IPAddress remoteIP = {0,0,0,0};
    IPAddress localIP = {0,0,0,0};
    int remoteIPstore[4]  = {0,0,0,0};
    int localIPstore[4] = {0,0,0,0};

    char *searchPtr = strstr(event, LARA_R6_LISTEN_SOCKET_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_LISTEN_SOCKET_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int ret = sscanf(searchPtr,
                      "%d,\"%d.%d.%d.%d\",%u,%d,\"%d.%d.%d.%d\",%u",
                      &socket,
                      &remoteIPstore[0], &remoteIPstore[1], &remoteIPstore[2], &remoteIPstore[3],
                      &port, &listenSocket,
                      &localIPstore[0], &localIPstore[1], &localIPstore[2], &localIPstore[3],
                      &listenPort);
      for (int i = 0; i <= 3; i++)
      {
        if (ret >= 5)
          remoteIP[i] = (uint8_t)remoteIPstore[i];
        if (ret >= 11)
          localIP[i] = (uint8_t)localIPstore[i];
      }
      if (ret >= 5)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: socket listen"));
        parseSocketListenIndication(listenSocket, localIP, listenPort, socket, remoteIP, port);
        return true;
      }
    }
  }
  { // URC: +UUSOCL (Close Socket)
    int socket;
    char *searchPtr = strstr(event, LARA_R6_CLOSE_SOCKET_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_CLOSE_SOCKET_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int ret = sscanf(searchPtr, "%d", &socket);
      if (ret == 1)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: socket close"));
        if ((socket >= 0) && (socket <= 6))
        {
          if (_socketCloseCallback != nullptr)
          {
            _socketCloseCallback(socket);
          }
        }
        return true;
      }
    }
  }
  { // URC: +UULOC (Localization information - CellLocate and hybrid positioning)
    ClockData clck;
    PositionData gps;
    SpeedData spd;
    unsigned long uncertainty;
    int scanNum;
    int latH, lonH, alt;
    unsigned int speedU, cogU;
    char latL[10], lonL[10];
    int dateStore[5];

    // Maybe we should also scan for +UUGIND and extract the activated gnss system?

    // This assumes the ULOC response type is "0" or "1" - as selected by gpsRequest detailed
    char *searchPtr = strstr(event, LARA_R6_GNSS_REQUEST_LOCATION_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_GNSS_REQUEST_LOCATION_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr,
                        "%d/%d/%d,%d:%d:%d.%d,%d.%[^,],%d.%[^,],%d,%lu,%u,%u,%*s",
                        &dateStore[0], &dateStore[1], &clck.date.year,
                        &dateStore[2], &dateStore[3], &dateStore[4], &clck.time.ms,
                        &latH, latL, &lonH, lonL, &alt, &uncertainty,
                        &speedU, &cogU);
      clck.date.day = dateStore[0];
      clck.date.month = dateStore[1];
      clck.time.hour = dateStore[2];
      clck.time.minute = dateStore[3];
      clck.time.second = dateStore[4];

      if (scanNum >= 13)
      {
        // Found a Location string!
        if (_printDebug == true)
        {
          _debugPort->println(F("processReadEvent: location"));
        }

        if (latH >= 0)
          gps.lat = (float)latH + ((float)atol(latL) / pow(10, strlen(latL)));
        else
          gps.lat = (float)latH - ((float)atol(latL) / pow(10, strlen(latL)));
        if (lonH >= 0)
          gps.lon = (float)lonH + ((float)atol(lonL) / pow(10, strlen(lonL)));
        else
          gps.lon = (float)lonH - ((float)atol(lonL) / pow(10, strlen(lonL)));
        gps.alt = (float)alt;
        if (scanNum >= 15) // If detailed response, get speed data
        {
          spd.speed = (float)speedU;
          spd.cog = (float)cogU;
        }

        // if (_printDebug == true)
        // {
        //   _debugPort->print(F("processReadEvent: location:  lat: "));
        //   _debugPort->print(gps.lat, 7);
        //   _debugPort->print(F(" lon: "));
        //   _debugPort->print(gps.lon, 7);
        //   _debugPort->print(F(" alt: "));
        //   _debugPort->print(gps.alt, 2);
        //   _debugPort->print(F(" speed: "));
        //   _debugPort->print(spd.speed, 2);
        //   _debugPort->print(F(" cog: "));
        //   _debugPort->println(spd.cog, 2);
        // }

        if (_gpsRequestCallback != nullptr)
        {
          _gpsRequestCallback(clck, gps, spd, uncertainty);
        }

        return true;
      }
    }
  }
  { // URC: +UUSIMSTAT (SIM Status)
    LARA_R6_sim_states_t state;
    int scanNum;
    int stateStore;

    char *searchPtr = strstr(event, LARA_R6_SIM_STATE_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_SIM_STATE_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d", &stateStore);

      if (scanNum == 1)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: SIM status"));

        state = (LARA_R6_sim_states_t)stateStore;

        if (_simStateReportCallback != nullptr)
        {
          _simStateReportCallback(state);
        }

        return true;
      }
    }
  }
  { // URC: +UUHTTPCR (HTTP Command Result)
    int profile, command, result;
    int scanNum;

    char *searchPtr = strstr(event, LARA_R6_HTTP_COMMAND_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_HTTP_COMMAND_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,%d,%d", &profile, &command, &result);

      if (scanNum == 3)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: HTTP command result"));

        if ((profile >= 0) && (profile < LARA_R6_NUM_HTTP_PROFILES))
        {
          if (_httpCommandRequestCallback != nullptr)
          {
            _httpCommandRequestCallback(profile, command, result);
          }
        }

        return true;
      }
    }
  }
  { // URC: +UUMQTTC (MQTT Command Result)
    int command, result;
    int scanNum;
    int qos = -1;
    String topic;

    char *searchPtr = strstr(event, LARA_R6_MQTT_COMMAND_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_MQTT_COMMAND_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ')
      {
        searchPtr++; // skip spaces
      }

      scanNum = sscanf(searchPtr, "%d,%d", &command, &result);
      if ((scanNum == 2) && (command == LARA_R6_MQTT_COMMAND_SUBSCRIBE))
      {
        char topicC[100] = "";
        scanNum = sscanf(searchPtr, "%*d,%*d,%d,\"%[^\"]\"", &qos, topicC);
        topic = topicC;
      }
      if ((scanNum == 2) || (scanNum == 4))
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("processReadEvent: MQTT command result"));
        }

        if (_mqttCommandRequestCallback != nullptr)
        {
          _mqttCommandRequestCallback(command, result);
        }

        return true;
      }
    }
  }
  { // URC: +UUFTPCR (FTP Command Result)
    int ftpCmd;
    int ftpResult;
    int scanNum;
    char *searchPtr = strstr(event, LARA_R6_FTP_COMMAND_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_FTP_COMMAND_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ')
      {
        searchPtr++; // skip spaces
      }

      scanNum = sscanf(searchPtr, "%d,%d", &ftpCmd, &ftpResult);
      if (scanNum == 2 && _ftpCommandRequestCallback != nullptr)
      {
        _ftpCommandRequestCallback(ftpCmd, ftpResult);
        return true;
      }
    }
  }
  { // URC: +UUPING (Ping Result)
    int retry = 0;
    int p_size = 0;
    int ttl = 0;
    String remote_host = "";
    IPAddress remoteIP = {0, 0, 0, 0};
    long rtt = 0;
    int scanNum;

    // Try to extract the UUPING retries and payload size
    char *searchPtr = strstr(event, LARA_R6_PING_COMMAND_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_PING_COMMAND_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,%d,", &retry, &p_size);

      if (scanNum == 2)
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("processReadEvent: ping"));
        }

        searchPtr = strchr(++searchPtr, '\"'); // Search to the first quote

        // Extract the remote host name, stop at the next quote
        while ((*(++searchPtr) != '\"') && (*searchPtr != '\0'))
        {
          remote_host.concat(*(searchPtr));
        }

        if (*searchPtr != '\0') // Make sure we found a quote
        {
          int remoteIPstore[4];
          scanNum = sscanf(searchPtr, "\",\"%d.%d.%d.%d\",%d,%ld",
                            &remoteIPstore[0], &remoteIPstore[1], &remoteIPstore[2], &remoteIPstore[3], &ttl, &rtt);
          for (int i = 0; i <= 3; i++)
          {
            remoteIP[i] = (uint8_t)remoteIPstore[i];
          }

          if (scanNum == 6) // Make sure we extracted enough data
          {
            if (_pingRequestCallback != nullptr)
            {
              _pingRequestCallback(retry, p_size, remote_host, remoteIP, ttl, rtt);
            }
          }
        }
        return true;
      }
    }
  }
  { // URC: +CREG
    int status = 0;
    unsigned int lac = 0, ci = 0, Act = 0;
    char *searchPtr = strstr(event, LARA_R6_REGISTRATION_STATUS_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_REGISTRATION_STATUS_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int scanNum = sscanf(searchPtr, "%d,\"%4x\",\"%4x\",%d", &status, &lac, &ci, &Act);
      if (scanNum == 4)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: CREG"));

        if (_registrationCallback != nullptr)
        {
          _registrationCallback((LARA_R6_registration_status_t)status, lac, ci, Act);
        }

        return true;
      }
    }
  }
  { // URC: +CEREG
    int status = 0;
    unsigned int tac = 0, ci = 0, Act = 0;
    char *searchPtr = strstr(event, LARA_R6_EPSREGISTRATION_STATUS_URC);
    if (searchPtr != nullptr)
    {
      searchPtr += strlen(LARA_R6_EPSREGISTRATION_STATUS_URC); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int scanNum = sscanf(searchPtr, "%d,\"%4x\",\"%4x\",%d", &status, &tac, &ci, &Act);
      if (scanNum == 4)
      {
        if (_printDebug == true)
          _debugPort->println(F("processReadEvent: CEREG"));

        if (_epsRegistrationCallback != nullptr)
        {
          _epsRegistrationCallback((LARA_R6_registration_status_t)status, tac, ci, Act);
        }

        return true;
      }
    }
  }
  // NOTE: When adding new URC messages, remember to update pruneBacklog too!

  return false;
}

// This is the original poll function.
// It is 'blocking' - it does not return when serial data is available until it receives a `\n`.
// ::bufferedPoll is the new improved version. It processes any data in the backlog and includes a timeout.
bool LARA_R6::poll(void)
{
  if (_pollReentrant == true) // Check for reentry (i.e. poll has been called from inside a callback)
    return false;

  _pollReentrant = true;

  int avail = 0;
  char c = 0;
  bool handled = false;

  memset(_laraRXBuffer, 0, _RXBuffSize); // Clear _laraRXBuffer

  if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is NULL
  {
    while (c != '\n') // Copy characters into _laraRXBuffer. Stop at the first new line
    {
      if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is NULL
      {
        c = readChar();
        _laraRXBuffer[avail++] = c;
      } else {
        yield();
      }
    }

    // Now search for all supported URC's
    handled = processURCEvent(_laraRXBuffer);
    if (handled && (true == _printAtDebug)) {
      _debugAtPort->write(_laraRXBuffer, avail);
    }
    if ((handled == false) && (strlen(_laraRXBuffer) > 2))
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("poll: "));
        _debugPort->println(_laraRXBuffer);
      }
    }
    else
    {
    }
  }

  _pollReentrant = false;

  return handled;
}

void LARA_R6::setSocketListenCallback(void (*socketListenCallback)(int, IPAddress, unsigned int, int, IPAddress, unsigned int))
{
  _socketListenCallback = socketListenCallback;
}

void LARA_R6::setSocketReadCallback(void (*socketReadCallback)(int, String))
{
  _socketReadCallback = socketReadCallback;
}

void LARA_R6::setSocketReadCallbackPlus(void (*socketReadCallbackPlus)(int, const char *, int, IPAddress, int)) // socket, data, length, remoteAddress, remotePort
{
  _socketReadCallbackPlus = socketReadCallbackPlus;
}

void LARA_R6::setSocketCloseCallback(void (*socketCloseCallback)(int))
{
  _socketCloseCallback = socketCloseCallback;
}

void LARA_R6::setGpsReadCallback(void (*gpsRequestCallback)(ClockData time,
                                                            PositionData gps, SpeedData spd, unsigned long uncertainty))
{
  _gpsRequestCallback = gpsRequestCallback;
}

void LARA_R6::setSIMstateReportCallback(void (*simStateReportCallback)(LARA_R6_sim_states_t state))
{
  _simStateReportCallback = simStateReportCallback;
}

void LARA_R6::setPSDActionCallback(void (*psdActionRequestCallback)(int result, IPAddress ip))
{
  _psdActionRequestCallback = psdActionRequestCallback;
}

void LARA_R6::setPingCallback(void (*pingRequestCallback)(int retry, int p_size, String remote_hostname, IPAddress ip, int ttl, long rtt))
{
  _pingRequestCallback = pingRequestCallback;
}

void LARA_R6::setHTTPCommandCallback(void (*httpCommandRequestCallback)(int profile, int command, int result))
{
  _httpCommandRequestCallback = httpCommandRequestCallback;
}

void LARA_R6::setMQTTCommandCallback(void (*mqttCommandRequestCallback)(int command, int result))
{
  _mqttCommandRequestCallback = mqttCommandRequestCallback;
}

void LARA_R6::setFTPCommandCallback(void (*ftpCommandRequestCallback)(int command, int result))
{
    _ftpCommandRequestCallback = ftpCommandRequestCallback;
}

LARA_R6_error_t LARA_R6::setRegistrationCallback(void (*registrationCallback)(LARA_R6_registration_status_t status, unsigned int lac, unsigned int ci, int Act))
{
  _registrationCallback = registrationCallback;

  char *command = lara_r6_calloc_char(strlen(LARA_R6_REGISTRATION_STATUS) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_REGISTRATION_STATUS, 2/*enable URC with location*/);
  LARA_R6_error_t err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setEpsRegistrationCallback(void (*registrationCallback)(LARA_R6_registration_status_t status, unsigned int tac, unsigned int ci, int Act))
{
  _epsRegistrationCallback = registrationCallback;

  char *command = lara_r6_calloc_char(strlen(LARA_R6_EPSREGISTRATION_STATUS) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_EPSREGISTRATION_STATUS, 2/*enable URC with location*/);
  LARA_R6_error_t err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

size_t LARA_R6::write(uint8_t c)
{
  return hwWrite(c);
}

size_t LARA_R6::write(const char *str)
{
  return hwPrint(str);
}

size_t LARA_R6::write(const char *buffer, size_t size)
{
  return hwWriteData(buffer, size);
}

LARA_R6_error_t LARA_R6::at(void)
{
  LARA_R6_error_t err;

  err = sendCommandWithResponse(nullptr, LARA_R6_RESPONSE_OK, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  return err;
}

LARA_R6_error_t LARA_R6::enableEcho(bool enable)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_ECHO) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s%d", LARA_R6_COMMAND_ECHO, enable ? 1 : 0);
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

String LARA_R6::getManufacturerID(void)
{
  char *response;
  char idResponse[16] = {0x00}; // E.g. u-blox
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_MANU_ID,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%15s\r\n", idResponse) != 1)
    {
      memset(idResponse, 0, 16);
    }
  }
  free(response);
  return String(idResponse);
}

String LARA_R6::getModelID(void)
{
  char *response;
  char idResponse[32] = {0x00}; // E.g. LARA-R610M8Q
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_MODEL_ID,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%31s\r\n", idResponse) != 1)
    {
      memset(idResponse, 0, 16);
    }
  }
  free(response);
  return String(idResponse);
}

String LARA_R6::getFirmwareVersion(void)
{
  char *response;
  char idResponse[16] = {0x00}; // E.g. 11.40
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_FW_VER_ID,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%15s\r\n", idResponse) != 1)
    {
      memset(idResponse, 0, 16);
    }
  }
  free(response);
  return String(idResponse);
}

String LARA_R6::getSerialNo(void)
{
  char *response;
  char idResponse[32] = {0x00}; // E.g. 357520070120767
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_SERIAL_NO,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%31s\r\n", idResponse) != 1)
    {
      memset(idResponse, 0, 16);
    }
  }
  free(response);
  return String(idResponse);
}

String LARA_R6::getIMEI(void)
{
  char *response;
  char imeiResponse[32] = {0x00}; // E.g. 004999010640000
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_IMEI,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%31s\r\n", imeiResponse) != 1)
    {
      memset(imeiResponse, 0, 16);
    }
  }
  free(response);
  return String(imeiResponse);
}

String LARA_R6::getIMSI(void)
{
  char *response;
  char imsiResponse[32] = {0x00}; // E.g. 222107701772423
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_IMSI,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (sscanf(response, "\r\n%31s\r\n", imsiResponse) != 1)
    {
      memset(imsiResponse, 0, 16);
    }
  }
  free(response);
  return String(imsiResponse);
}

String LARA_R6::getCCID(void)
{
  char *response;
  char ccidResponse[32] = {0x00}; // E.g. +CCID: 8939107900010087330
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_CCID,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "\r\n+CCID:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("\r\n+CCID:"); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      if (sscanf(searchPtr, "%31s", ccidResponse) != 1)
      {
        ccidResponse[0] = 0;
      }
    }
  }
  free(response);
  return String(ccidResponse);
}

String LARA_R6::getSubscriberNo(void)
{
  char *response;
  char idResponse[128] = {0x00}; // E.g. +CNUM: "ABCD . AAA","123456789012",129
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_CNUM,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_10_SEC_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "\r\n+CNUM:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("\r\n+CNUM:"); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      if (sscanf(searchPtr, "%127s", idResponse) != 1)
      {
        idResponse[0] = 0;
      }
    }
  }
  free(response);
  return String(idResponse);
}

String LARA_R6::getCapabilities(void)
{
  char *response;
  char idResponse[128] = {0x00}; // E.g. +GCAP: +FCLASS, +CGSM
  LARA_R6_error_t err;

  response = lara_r6_calloc_char(minimumResponseAllocation);

  err = sendCommandWithResponse(LARA_R6_COMMAND_REQ_CAP,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "\r\n+GCAP:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("\r\n+GCAP:"); // Move searchPtr to first character - probably a space
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      if (sscanf(searchPtr, "%127s", idResponse) != 1)
      {
        idResponse[0] = 0;
      }
    }
  }
  free(response);
  return String(idResponse);
}

LARA_R6_error_t LARA_R6::reset(void)
{
  LARA_R6_error_t err;

  err = functionality(SILENT_RESET_WITH_SIM);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    // Reset will set the baud rate back to 115200
    //beginSerial(9600);
    err = LARA_R6_ERROR_INVALID;
    while (err != LARA_R6_ERROR_SUCCESS)
    {
      beginSerial(LARA_R6_DEFAULT_BAUD_RATE);
      setBaud(_baud);
      beginSerial(_baud);
      err = at();
    }
    return init(_baud);
  }
  return err;
}

String LARA_R6::clock(void)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  char *clockBegin;
  char *clockEnd;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_CLOCK) + 2);
  if (command == nullptr)
    return "";
  sprintf(command, "%s?", LARA_R6_COMMAND_CLOCK);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return "";
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return "";
  }

  // Response format: \r\n+CCLK: "YY/MM/DD,HH:MM:SS-TZ"\r\n\r\nOK\r\n
  clockBegin = strchr(response, '\"'); // Find first quote
  if (clockBegin == nullptr)
  {
    free(command);
    free(response);
    return "";
  }
  clockBegin += 1;                     // Increment pointer to begin at first number
  clockEnd = strchr(clockBegin, '\"'); // Find last quote
  if (clockEnd == nullptr)
  {
    free(command);
    free(response);
    return "";
  }
  *(clockEnd) = '\0'; // Set last quote to null char -- end string

  String clock = String(clockBegin); // Extract the clock as a String _before_ freeing response

  free(command);
  free(response);

  return (clock);
}

LARA_R6_error_t LARA_R6::clock(uint8_t *y, uint8_t *mo, uint8_t *d,
                               uint8_t *h, uint8_t *min, uint8_t *s, int8_t *tz)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  char tzPlusMinus;
  int scanNum = 0;

  int iy, imo, id, ih, imin, is, itz;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_CLOCK) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_COMMAND_CLOCK);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  // Response format (if TZ is negative): \r\n+CCLK: "YY/MM/DD,HH:MM:SS-TZ"\r\n\r\nOK\r\n
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+CCLK:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+CCLK:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "\"%d/%d/%d,%d:%d:%d%c%d\"\r\n",
               &iy, &imo, &id, &ih, &imin, &is, &tzPlusMinus, &itz);
    }
    if (scanNum == 8)
    {
      *y = iy;
      *mo = imo;
      *d = id;
      *h = ih;
      *min = imin;
      *s = is;
      if (tzPlusMinus == '-')
        *tz = 0 - itz;
      else
        *tz = itz;
    }
    else
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::setClock(uint8_t y, uint8_t mo, uint8_t d,
                                  uint8_t h, uint8_t min, uint8_t s, int8_t tz)
{
  //Convert y,mo,d,h,min,s,tz into a String
  //Some platforms don't support sprintf correctly (for %02d or %+02d) so we need to build the String manually
  //Format is "yy/MM/dd,hh:mm:ss+TZ"
  //TZ can be +/- and is in increments of 15 minutes (not hours)

  String theTime = "";

  theTime.concat(y / 10);
  theTime.concat(y % 10);
  theTime.concat('/');
  theTime.concat(mo / 10);
  theTime.concat(mo % 10);
  theTime.concat('/');
  theTime.concat(d / 10);
  theTime.concat(d % 10);
  theTime.concat(',');
  theTime.concat(h / 10);
  theTime.concat(h % 10);
  theTime.concat(':');
  theTime.concat(min / 10);
  theTime.concat(min % 10);
  theTime.concat(':');
  theTime.concat(s / 10);
  theTime.concat(s % 10);
  if (tz < 0)
  {
    theTime.concat('-');
    tz = 0 - tz;
  }
  else
    theTime.concat('+');
  theTime.concat(tz / 10);
  theTime.concat(tz % 10);

  return (setClock(theTime));
}

LARA_R6_error_t LARA_R6::setClock(String theTime)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_CLOCK) + theTime.length() + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_COMMAND_CLOCK, theTime.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

void LARA_R6::autoTimeZoneForBegin(bool tz)
{
  _autoTimeZoneForBegin = tz;
}

LARA_R6_error_t LARA_R6::autoTimeZone(bool enable)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_AUTO_TZ) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_COMMAND_AUTO_TZ, enable ? 1 : 0);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

int8_t LARA_R6::rssi(void)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int rssi;

  command = lara_r6_calloc_char(strlen(LARA_R6_SIGNAL_QUALITY) + 1);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s", LARA_R6_SIGNAL_QUALITY);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, 10000,
                                minimumResponseAllocation, AT_COMMAND);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return -1;
  }

  int scanned = 0;
  char *searchPtr = strstr(response, "+CSQ:");
  if (searchPtr != nullptr)
  {
    searchPtr += strlen("+CSQ:"); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
    scanned = sscanf(searchPtr, "%d,%*d", &rssi);
  }
  if (scanned != 1)
  {
    rssi = -1;
  }

  free(command);
  free(response);
  return rssi;
}

LARA_R6_error_t LARA_R6::getExtSignalQuality(signal_quality& signal_quality)
{
  char *command;
  char *response;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_EXT_SIGNAL_QUALITY) + 1);
  if (command == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s", LARA_R6_EXT_SIGNAL_QUALITY);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command,
                                LARA_R6_RESPONSE_OK_OR_ERROR, response, 10000,
                                minimumResponseAllocation, AT_COMMAND);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return LARA_R6_ERROR_ERROR;
  }

  int scanned = 0;
  const char * responseStr = "+CESQ:";
  char *searchPtr = strstr(response, responseStr);
  if (searchPtr != nullptr)
  {
    searchPtr += strlen(responseStr); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
    scanned = sscanf(searchPtr, "%u,%u,%u,%u,%u,%u", &signal_quality.rxlev, &signal_quality.ber,
                       &signal_quality.rscp, &signal_quality.enc0, &signal_quality.rsrq, &signal_quality.rsrp);
  }

  err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  if (scanned == 6)
  {
    err = LARA_R6_ERROR_SUCCESS;
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_registration_status_t LARA_R6::registration(bool eps)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int status;
  const char* tag = eps ? LARA_R6_EPSREGISTRATION_STATUS : LARA_R6_REGISTRATION_STATUS;
  command = lara_r6_calloc_char(strlen(tag) + 3);
  if (command == nullptr)
    return LARA_R6_REGISTRATION_INVALID;
  sprintf(command, "%s?", tag);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_REGISTRATION_INVALID;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT,
                                minimumResponseAllocation, AT_COMMAND);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return LARA_R6_REGISTRATION_INVALID;
  }

  int scanned = 0;
  const char *startTag = eps ? LARA_R6_EPSREGISTRATION_STATUS_URC : LARA_R6_REGISTRATION_STATUS_URC;
  char *searchPtr = strstr(response, startTag);
  if (searchPtr != nullptr)
  {
    searchPtr += eps ? strlen(LARA_R6_EPSREGISTRATION_STATUS_URC) : strlen(LARA_R6_REGISTRATION_STATUS_URC); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
	  scanned = sscanf(searchPtr, "%*d,%d", &status);
  }
  if (scanned != 1)
    status = LARA_R6_REGISTRATION_INVALID;

  free(command);
  free(response);
  return (LARA_R6_registration_status_t)status;
}

bool LARA_R6::setNetworkProfile(mobile_network_operator_t mno, bool autoReset, bool urcNotification)
{
  mobile_network_operator_t currentMno;

  // Check currently set MNO profile
  if (getMNOprofile(&currentMno) != LARA_R6_ERROR_SUCCESS)
  {
    return false;
  }

  if (currentMno == mno)
  {
    return true;
  }

  // Disable transmit and receive so we can change operator
  if (functionality(MINIMUM_FUNCTIONALITY) != LARA_R6_ERROR_SUCCESS)
  {
    return false;
  }

  if (setMNOprofile(mno, autoReset, urcNotification) != LARA_R6_ERROR_SUCCESS)
  {
    return false;
  }

  if (reset() != LARA_R6_ERROR_SUCCESS)
  {
    return false;
  }

  return true;
}

mobile_network_operator_t LARA_R6::getNetworkProfile(void)
{
  mobile_network_operator_t mno;
  LARA_R6_error_t err;

  err = getMNOprofile(&mno);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    return MNO_INVALID;
  }
  return mno;
}

LARA_R6_error_t LARA_R6::setAPN(String apn, uint8_t cid, LARA_R6_pdp_type pdpType)
{
  LARA_R6_error_t err;
  char *command;
  char pdpStr[8];

  memset(pdpStr, 0, 8);

  if (cid >= 8)
    return LARA_R6_ERROR_UNEXPECTED_PARAM;

  command = lara_r6_calloc_char(strlen(LARA_R6_MESSAGE_PDP_DEF) + strlen(apn.c_str()) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  switch (pdpType)
  {
  case PDP_TYPE_INVALID:
    free(command);
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
    break;
  case PDP_TYPE_IP:
    memcpy(pdpStr, "IP", 2);
    break;
  case PDP_TYPE_NONIP:
    memcpy(pdpStr, "NONIP", 5);
    break;
  case PDP_TYPE_IPV4V6:
    memcpy(pdpStr, "IPV4V6", 6);
    break;
  case PDP_TYPE_IPV6:
    memcpy(pdpStr, "IPV6", 4);
    break;
  default:
    free(command);
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
    break;
  }
  if (apn == nullptr)
  {
    if (_printDebug == true)
      _debugPort->println(F("setAPN: nullptr"));
    sprintf(command, "%s=%d,\"%s\",\"\"", LARA_R6_MESSAGE_PDP_DEF,
            cid, pdpStr);
  }
  else
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("setAPN: "));
      _debugPort->println(apn);
    }
    sprintf(command, "%s=%d,\"%s\",\"%s\"", LARA_R6_MESSAGE_PDP_DEF,
            cid, pdpStr, apn.c_str());
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);

  return err;
}

// Return the Access Point Name and IP address for the chosen context identifier
LARA_R6_error_t LARA_R6::getAPN(int cid, String *apn, IPAddress *ip, LARA_R6_pdp_type* pdpType)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  if (cid > LARA_R6_NUM_PDP_CONTEXT_IDENTIFIERS)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_MESSAGE_PDP_DEF) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_MESSAGE_PDP_DEF);

  response = lara_r6_calloc_char(1024);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT, 1024);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    // Example:
    // +CGDCONT: 0,"IP","payandgo.o2.co.uk","0.0.0.0",0,0,0,0,0,0,0,0,0,0
    // +CGDCONT: 1,"IP","payandgo.o2.co.uk.mnc010.mcc234.gprs","10.160.182.234",0,0,0,2,0,0,0,0,0,0
	int rcid = -1;
    char *searchPtr = response;

    bool keepGoing = true;
    while (keepGoing == true)
    {
      int scanned = 0;
      // Find the first/next occurrence of +CGDCONT:
      searchPtr = strstr(searchPtr, "+CGDCONT:");
      if (searchPtr != nullptr)
      {
        char strPdpType[10];
        char strApn[128];
        int ipOct[4];

        searchPtr += strlen("+CGDCONT:"); // Point to the cid
        while (*searchPtr == ' ') searchPtr++; // skip spaces
        scanned = sscanf(searchPtr, "%d,\"%[^\"]\",\"%[^\"]\",\"%d.%d.%d.%d",
        &rcid, strPdpType, strApn,
				&ipOct[0], &ipOct[1], &ipOct[2], &ipOct[3]);
        if ((scanned == 7) && (rcid == cid)) {
          if (apn) *apn = strApn;
          for (int o = 0; ip && (o < 4); o++)
          {
            (*ip)[o] = (uint8_t)ipOct[o];
          }
          if (pdpType) {
            *pdpType = (0 == strcmp(strPdpType, "IPV4V6"))  ? PDP_TYPE_IPV4V6 :
                       (0 == strcmp(strPdpType, "IPV6"))    ? PDP_TYPE_IPV6 :
                       (0 == strcmp(strPdpType, "IP"))	    ? PDP_TYPE_IP :
                                                              PDP_TYPE_INVALID;
          }
          keepGoing = false;
        }
      }
      else // We don't have a match so let's clear the APN and IP address
      {
        if (apn) *apn = "";
        if (pdpType) *pdpType = PDP_TYPE_INVALID;
        if (ip) *ip = {0, 0, 0, 0};
        keepGoing = false;
      }
    }
  }
  else
  {
    err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::getSimStatus(String* code)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_SIMPIN) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_COMMAND_SIMPIN);
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    int scanned = 0;
    char c[16];
    char *searchPtr = strstr(response, "+CPIN:");
    if (searchPtr != nullptr) {
      searchPtr += strlen("+CPIN:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanned = sscanf(searchPtr, "%15s\r\n", c);
    }
    if (scanned == 1)
    {
      if(code)
        *code = c;
    }
    else
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::setSimPin(String pin)
{
  LARA_R6_error_t err;
  char *command;
  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_SIMPIN) + 4 + pin.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_COMMAND_SIMPIN, pin.c_str());
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setSIMstateReportingMode(int mode)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_SIM_STATE) + 4);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_SIM_STATE, mode);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::getSIMstateReportingMode(int *mode)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  int m;

  command = lara_r6_calloc_char(strlen(LARA_R6_SIM_STATE) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_SIM_STATE);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    int scanned = 0;
    char *searchPtr = strstr(response, "+USIMSTAT:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USIMSTAT:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanned = sscanf(searchPtr, "%d\r\n", &m);
    }
    if (scanned == 1)
    {
      *mode = m;
    }
    else
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);
  return err;
}

const char *PPP_L2P[5] = {
    "",
    "PPP",
    "M-HEX",
    "M-RAW_IP",
    "M-OPT-PPP",
};

LARA_R6_error_t LARA_R6::enterPPP(uint8_t cid, char dialing_type_char,
                                  unsigned long dialNumber, LARA_R6::LARA_R6_l2p_t l2p)
{
  LARA_R6_error_t err;
  char *command;

  if ((dialing_type_char != 0) && (dialing_type_char != 'T') &&
      (dialing_type_char != 'P'))
  {
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
  }

  command = lara_r6_calloc_char(strlen(LARA_R6_MESSAGE_ENTER_PPP) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (dialing_type_char != 0)
  {
    sprintf(command, "%s%c*%lu**%s*%u#", LARA_R6_MESSAGE_ENTER_PPP, dialing_type_char,
            dialNumber, PPP_L2P[l2p], (unsigned int)cid);
  }
  else
  {
    sprintf(command, "%s*%lu**%s*%u#", LARA_R6_MESSAGE_ENTER_PPP,
            dialNumber, PPP_L2P[l2p], (unsigned int)cid);
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_CONNECT, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

uint8_t LARA_R6::getOperators(struct operator_stats *opRet, int maxOps)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  uint8_t opsSeen = 0;

  command = lara_r6_calloc_char(strlen(LARA_R6_OPERATOR_SELECTION) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=?", LARA_R6_OPERATOR_SELECTION);

  int responseSize = (maxOps + 1) * 48;
  response = lara_r6_calloc_char(responseSize);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // AT+COPS maximum response time is 3 minutes (180000 ms)
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_3_MIN_TIMEOUT, responseSize);

  // Sample responses:
  // +COPS: (3,"Verizon Wireless","VzW","311480",8),,(0,1,2,3,4),(0,1,2)
  // +COPS: (1,"313 100","313 100","313100",8),(2,"AT&T","AT&T","310410",8),(3,"311 480","311 480","311480",8),,(0,1,2,3,4),(0,1,2)

  if (_printDebug == true)
  {
    _debugPort->print(F("getOperators: Response: {"));
    _debugPort->print(response);
    _debugPort->println(F("}"));
  }

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *opBegin;
    char *opEnd;
    int op = 0;
    int stat;
    char longOp[26];
    char shortOp[11];
    int act;
    unsigned long numOp;

    opBegin = response;

    for (; op < maxOps; op++)
    {
      opBegin = strchr(opBegin, '(');
      if (opBegin == nullptr)
        break;
      opEnd = strchr(opBegin, ')');
      if (opEnd == nullptr)
        break;

      int sscanRead = sscanf(opBegin, "(%d,\"%[^\"]\",\"%[^\"]\",\"%lu\",%d)%*s",
                             &stat, longOp, shortOp, &numOp, &act);
      if (sscanRead == 5)
      {
        opRet[op].stat = stat;
        opRet[op].longOp = (String)(longOp);
        opRet[op].shortOp = (String)(shortOp);
        opRet[op].numOp = numOp;
        opRet[op].act = act;
        opsSeen += 1;
      }
      // TODO: Search for other possible patterns here
      else
      {
        break; // Break out if pattern doesn't match.
      }
      opBegin = opEnd + 1; // Move opBegin to beginning of next value
    }
  }

  free(command);
  free(response);

  return opsSeen;
}

LARA_R6_error_t LARA_R6::registerOperator(struct operator_stats oper)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_OPERATOR_SELECTION) + 24);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=1,2,\"%lu\"", LARA_R6_OPERATOR_SELECTION, oper.numOp);

  // AT+COPS maximum response time is 3 minutes (180000 ms)
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_3_MIN_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::automaticOperatorSelection()
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_OPERATOR_SELECTION) + 6);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=0,0", LARA_R6_OPERATOR_SELECTION);

  // AT+COPS maximum response time is 3 minutes (180000 ms)
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_3_MIN_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::getOperator(String *oper)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  char *searchPtr;
  char mode;

  command = lara_r6_calloc_char(strlen(LARA_R6_OPERATOR_SELECTION) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_OPERATOR_SELECTION);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // AT+COPS maximum response time is 3 minutes (180000 ms)
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_3_MIN_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    searchPtr = strstr(response, "+COPS:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+COPS:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      mode = *searchPtr;              // Read first char -- should be mode
      if (mode == '2')                // Check for de-register
      {
        err = LARA_R6_ERROR_DEREGISTERED;
      }
      // Otherwise if it's default, manual, set-only, or automatic
      else if ((mode == '0') || (mode == '1') || (mode == '3') || (mode == '4'))
      {
        *oper = "";
        searchPtr = strchr(searchPtr, '\"'); // Move to first quote
        if (searchPtr == nullptr)
        {
          err = LARA_R6_ERROR_DEREGISTERED;
        }
        else
        {
          while ((*(++searchPtr) != '\"') && (*searchPtr != '\0'))
          {
            oper->concat(*(searchPtr));
          }
        }
        if (_printDebug == true)
        {
          _debugPort->print(F("getOperator: "));
          _debugPort->println(*oper);
        }
      }
    }
  }

  free(response);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::deregisterOperator(void)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_OPERATOR_SELECTION) + 4);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=2", LARA_R6_OPERATOR_SELECTION);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_3_MIN_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setSMSMessageFormat(LARA_R6_message_format_t textMode)
{
  char *command;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_MESSAGE_FORMAT) + 4);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_MESSAGE_FORMAT,
          (textMode == LARA_R6_MESSAGE_FORMAT_TEXT) ? 1 : 0);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::sendSMS(String number, String message)
{
  char *command;
  char *messageCStr;
  char *numberCStr;
  LARA_R6_error_t err;

  numberCStr = lara_r6_calloc_char(number.length() + 2);
  if (numberCStr == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  number.toCharArray(numberCStr, number.length() + 1);

  command = lara_r6_calloc_char(strlen(LARA_R6_SEND_TEXT) + strlen(numberCStr) + 8);
  if (command != nullptr)
  {
    sprintf(command, "%s=\"%s\"", LARA_R6_SEND_TEXT, numberCStr);

    err = sendCommandWithResponse(command, ">", nullptr,
                                  LARA_R6_3_MIN_TIMEOUT);
    free(command);
    free(numberCStr);
    if (err != LARA_R6_ERROR_SUCCESS)
      return err;

    messageCStr = lara_r6_calloc_char(message.length() + 1);
    if (messageCStr == nullptr)
    {
      hwWrite(ASCII_CTRL_Z);
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    }
    message.toCharArray(messageCStr, message.length() + 1);
    messageCStr[message.length()] = ASCII_CTRL_Z;

    err = sendCommandWithResponse(messageCStr, LARA_R6_RESPONSE_OK_OR_ERROR,
                                  nullptr, LARA_R6_3_MIN_TIMEOUT, minimumResponseAllocation, NOT_AT_COMMAND);

    free(messageCStr);
  }
  else
  {
    free(numberCStr);
    err = LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  return err;
}

LARA_R6_error_t LARA_R6::getPreferredMessageStorage(int *used, int *total, String memory)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  int u;
  int t;

  command = lara_r6_calloc_char(strlen(LARA_R6_PREF_MESSAGE_STORE) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_PREF_MESSAGE_STORE, memory.c_str());

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_3_MIN_TIMEOUT);

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return err;
  }

  int scanned = 0;
  char *searchPtr = strstr(response, "+CPMS:");
  if (searchPtr != nullptr)
  {
    searchPtr += strlen("+CPMS:"); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
    scanned = sscanf(searchPtr, "%d,%d", &u, &t);
  }
  if (scanned == 2)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getPreferredMessageStorage: memory1 (read and delete): "));
      _debugPort->print(memory);
      _debugPort->print(F(" used: "));
      _debugPort->print(u);
      _debugPort->print(F(" total: "));
      _debugPort->println(t);
    }
    *used = u;
    *total = t;
  }
  else
  {
    err = LARA_R6_ERROR_INVALID;
  }

  free(response);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::readSMSmessage(int location, String *unread, String *from, String *dateTime, String *message)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  command = lara_r6_calloc_char(strlen(LARA_R6_READ_TEXT_MESSAGE) + 5);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_READ_TEXT_MESSAGE, location);

  response = lara_r6_calloc_char(1024);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_10_SEC_TIMEOUT, 1024);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = response;

    // Find the first occurrence of +CMGR:
    searchPtr = strstr(searchPtr, "+CMGR:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+CMGR:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      int pointer = 0;
      while ((*(++searchPtr) != '\"') && (*searchPtr != '\0') && (pointer < 12))
      {
        unread->concat(*(searchPtr));
        pointer++;
      }
      if ((*searchPtr == '\0') || (pointer == 12))
      {
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }
      // Search to the next quote
      searchPtr = strchr(++searchPtr, '\"');
      pointer = 0;
      while ((*(++searchPtr) != '\"') && (*searchPtr != '\0') && (pointer < 24))
      {
        from->concat(*(searchPtr));
        pointer++;
      }
      if ((*searchPtr == '\0') || (pointer == 24))
      {
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }
      // Skip two commas
      searchPtr = strchr(++searchPtr, ',');
      searchPtr = strchr(++searchPtr, ',');
      // Search to the next quote
      searchPtr = strchr(++searchPtr, '\"');
      pointer = 0;
      while ((*(++searchPtr) != '\"') && (*searchPtr != '\0') && (pointer < 24))
      {
        dateTime->concat(*(searchPtr));
        pointer++;
      }
      if ((*searchPtr == '\0') || (pointer == 24))
      {
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }
      // Search to the next new line
      searchPtr = strchr(++searchPtr, '\n');
      pointer = 0;
      while ((*(++searchPtr) != '\r') && (*searchPtr != '\n') && (*searchPtr != '\0') && (pointer < 512))
      {
        message->concat(*(searchPtr));
        pointer++;
      }
      if ((*searchPtr == '\0') || (pointer == 512))
      {
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }
    }
    else
    {
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
  }
  else
  {
    err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::deleteSMSmessage(int location, int deleteFlag)
{
  char *command;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_DELETE_MESSAGE) + 12);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (deleteFlag == 0)
    sprintf(command, "%s=%d", LARA_R6_DELETE_MESSAGE, location);
  else
    sprintf(command, "%s=%d,%d", LARA_R6_DELETE_MESSAGE, location, deleteFlag);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, LARA_R6_55_SECS_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setBaud(unsigned long baud)
{
  LARA_R6_error_t err;
  char *command;
  int b = 0;

  // Error check -- ensure supported baud
  for (; b < NUM_SUPPORTED_BAUD; b++)
  {
    if (LARA_R6_SUPPORTED_BAUD[b] == baud)
    {
      break;
    }
  }
  if (b >= NUM_SUPPORTED_BAUD)
  {
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
  }

  // Construct command
  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_BAUD) + 7 + 12);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%lu", LARA_R6_COMMAND_BAUD, baud);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_SET_BAUD_TIMEOUT);

  free(command);

  return err;
}

LARA_R6_error_t LARA_R6::setFlowControl(LARA_R6_flow_control_t value)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_FLOW_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s%d", LARA_R6_FLOW_CONTROL, value);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);

  return err;
}

LARA_R6_error_t LARA_R6::setGpioMode(LARA_R6_gpio_t gpio,
                                     LARA_R6_gpio_mode_t mode, int value)
{
  LARA_R6_error_t err;
  char *command;

  // Example command: AT+UGPIOC=16,2
  // Example command: AT+UGPIOC=23,0,1
  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_GPIO) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (mode == GPIO_OUTPUT)
    sprintf(command, "%s=%d,%d,%d", LARA_R6_COMMAND_GPIO, gpio, mode, value);
  else
    sprintf(command, "%s=%d,%d", LARA_R6_COMMAND_GPIO, gpio, mode);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_10_SEC_TIMEOUT);

  free(command);

  return err;
}

LARA_R6::LARA_R6_gpio_mode_t LARA_R6::getGpioMode(LARA_R6_gpio_t gpio)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  char gpioChar[4];
  char *gpioStart;
  int gpioMode;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_GPIO) + 2);
  if (command == nullptr)
    return GPIO_MODE_INVALID;
  sprintf(command, "%s?", LARA_R6_COMMAND_GPIO);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return GPIO_MODE_INVALID;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return GPIO_MODE_INVALID;
  }

  sprintf(gpioChar, "%d", gpio);          // Convert GPIO to char array
  gpioStart = strstr(response, gpioChar); // Find first occurence of GPIO in response

  free(command);
  free(response);

  if (gpioStart == nullptr)
    return GPIO_MODE_INVALID; // If not found return invalid
  sscanf(gpioStart, "%*d,%d\r\n", &gpioMode);

  return (LARA_R6_gpio_mode_t)gpioMode;
}

int LARA_R6::socketOpen(LARA_R6_socket_protocol_t protocol, unsigned int localPort)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  int sockId = -1;
  char *responseStart;

  command = lara_r6_calloc_char(strlen(LARA_R6_CREATE_SOCKET) + 10);
  if (command == nullptr)
    return -1;
  if (localPort == 0)
    sprintf(command, "%s=%d", LARA_R6_CREATE_SOCKET, (int)protocol);
  else
    sprintf(command, "%s=%d,%d", LARA_R6_CREATE_SOCKET, (int)protocol, localPort);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    if (_printDebug == true)
      _debugPort->println(F("socketOpen: Fail: nullptr response"));
    free(command);
    return -1;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("socketOpen: Fail: Error: "));
      _debugPort->print(err);
      _debugPort->print(F("  Response: {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
    free(command);
    free(response);
    return -1;
  }

  responseStart = strstr(response, "+USOCR:");
  if (responseStart == nullptr)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("socketOpen: Failure: {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
    free(command);
    free(response);
    return -1;
  }

  responseStart += strlen("+USOCR:"); //  Move searchPtr to first char
  while (*responseStart == ' ') responseStart++; // skip spaces
  sscanf(responseStart, "%d", &sockId);
  _lastSocketProtocol[sockId] = (int)protocol;

  free(command);
  free(response);

  return sockId;
}

LARA_R6_error_t LARA_R6::socketClose(int socket, unsigned long timeout)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  command = lara_r6_calloc_char(strlen(LARA_R6_CLOSE_SOCKET) + 10);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }
  // if timeout is short, close asynchronously and don't wait for socket closure (we will get the URC later)
  // this will make sure the AT command parser is not confused during init()
  const char* format = (LARA_R6_STANDARD_RESPONSE_TIMEOUT == timeout) ? "%s=%d,1" : "%s=%d";
  sprintf(command, format, LARA_R6_CLOSE_SOCKET, socket);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response, timeout);

  if ((err != LARA_R6_ERROR_SUCCESS) && (_printDebug == true))
  {
    _debugPort->print(F("socketClose: Error: "));
    _debugPort->println(socketGetLastError());
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::socketConnect(int socket, const char *address,
                                       unsigned int port)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_CONNECT_SOCKET) + strlen(address) + 11);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,\"%s\",%d", LARA_R6_CONNECT_SOCKET, socket, address, port);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, LARA_R6_IP_CONNECT_TIMEOUT);

  free(command);

  return err;
}

LARA_R6_error_t LARA_R6::socketConnect(int socket, IPAddress address,
                                       unsigned int port)
{
  char *charAddress = lara_r6_calloc_char(16);
  if (charAddress == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  memset(charAddress, 0, 16);
  sprintf(charAddress, "%d.%d.%d.%d", address[0], address[1], address[2], address[3]);

  return (socketConnect(socket, (const char *)charAddress, port));
}

LARA_R6_error_t LARA_R6::socketWrite(int socket, const char *str, int len)
{
  char *command;
  char *response;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_WRITE_SOCKET) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }
  int dataLen = len == -1 ? strlen(str) : len;
  sprintf(command, "%s=%d,%d", LARA_R6_WRITE_SOCKET, socket, dataLen);

  err = sendCommandWithResponse(command, "@", response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT * 5);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    unsigned long writeDelay = millis();
    while (millis() < (writeDelay + 50))
      delay(1); //u-blox specification says to wait 50ms after receiving "@" to write data.

    if (len == -1)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketWrite: writing: "));
        _debugPort->println(str);
      }
      hwPrint(str);
    }
    else
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketWrite: writing "));
        _debugPort->print(len);
        _debugPort->println(F(" bytes"));
      }
      hwWriteData(str, len);
    }

    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_SOCKET_WRITE_TIMEOUT);
  }

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("socketWrite: Error: "));
      _debugPort->print(err);
      _debugPort->print(F(" => {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::socketWrite(int socket, String str)
{
  return socketWrite(socket, str.c_str(), str.length());
}

LARA_R6_error_t LARA_R6::socketWriteUDP(int socket, const char *address, int port, const char *str, int len)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int dataLen = len == -1 ? strlen(str) : len;

  command = lara_r6_calloc_char(64);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s=%d,\"%s\",%d,%d", LARA_R6_WRITE_UDP_SOCKET,
          socket, address, port, dataLen);
  err = sendCommandWithResponse(command, "@", response, LARA_R6_STANDARD_RESPONSE_TIMEOUT * 5);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (len == -1) //If binary data we need to send a length.
    {
      hwPrint(str);
    }
    else
    {
      hwWriteData(str, len);
    }
    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_SOCKET_WRITE_TIMEOUT);
  }
  else
  {
    if (_printDebug == true)
      _debugPort->print(F("socketWriteUDP: Error: "));
    if (_printDebug == true)
      _debugPort->println(socketGetLastError());
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::socketWriteUDP(int socket, IPAddress address, int port, const char *str, int len)
{
  char *charAddress = lara_r6_calloc_char(16);
  if (charAddress == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  memset(charAddress, 0, 16);
  sprintf(charAddress, "%d.%d.%d.%d", address[0], address[1], address[2], address[3]);

  return (socketWriteUDP(socket, (const char *)charAddress, port, str, len));
}

LARA_R6_error_t LARA_R6::socketWriteUDP(int socket, String address, int port, String str)
{
  return socketWriteUDP(socket, address.c_str(), port, str.c_str(), str.length());
}

LARA_R6_error_t LARA_R6::socketRead(int socket, int length, char *readDest, int *bytesRead)
{
  char *command;
  char *response;
  char *strBegin;
  int readIndexTotal = 0;
  int readIndexThisRead = 0;
  LARA_R6_error_t err;
  int scanNum = 0;
  int readLength = 0;
  int socketStore = 0;
  int bytesLeftToRead = length;
  int bytesToRead;

  // Set *bytesRead to zero
  if (bytesRead != nullptr)
    *bytesRead = 0;

  // Check if length is zero
  if (length == 0)
  {
    if (_printDebug == true)
      _debugPort->print(F("socketRead: length is 0! Call socketReadAvailable?"));
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
  }

  // Allocate memory for the command
  command = lara_r6_calloc_char(strlen(LARA_R6_READ_SOCKET) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  // Allocate memory for the response
  // We only need enough to read _laraR6maxSocketRead bytes - not the whole thing
  int responseLength = _laraR6maxSocketRead + strlen(LARA_R6_READ_SOCKET) + minimumResponseAllocation;
  response = lara_r6_calloc_char(responseLength);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // If there are more than _laraR6maxSocketRead (1024) bytes to be read,
  // we need to do multiple reads to get all the data

  while (bytesLeftToRead > 0)
  {
    if (bytesLeftToRead > _laraR6maxSocketRead) // Limit a single read to _laraR6maxSocketRead
      bytesToRead = _laraR6maxSocketRead;
    else
      bytesToRead = bytesLeftToRead;

    sprintf(command, "%s=%d,%d", LARA_R6_READ_SOCKET, socket, bytesToRead);

    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT, responseLength);

    if (err != LARA_R6_ERROR_SUCCESS)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketRead: sendCommandWithResponse err "));
        _debugPort->println(err);
      }
      free(command);
      free(response);
      return err;
    }

    // Extract the data
    char *searchPtr = strstr(response, "+USORD:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USORD:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,%d",
                        &socketStore, &readLength);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketRead: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    // Check that readLength == bytesToRead
    if (readLength != bytesToRead)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketRead: length mismatch! bytesToRead="));
        _debugPort->print(bytesToRead);
        _debugPort->print(F(" readLength="));
        _debugPort->println(readLength);
      }
    }

    // Check that readLength > 0
    if (readLength == 0)
    {
      if (_printDebug == true)
      {
        _debugPort->println(F("socketRead: zero length!"));
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_ZERO_READ_LENGTH;
    }

    // Find the first double-quote:
    strBegin = strchr(searchPtr, '\"');

    if (strBegin == nullptr)
    {
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    // Now copy the data into readDest
    readIndexThisRead = 1; // Start after the quote
    while (readIndexThisRead < (readLength + 1))
    {
      readDest[readIndexTotal] = strBegin[readIndexThisRead];
      readIndexTotal++;
      readIndexThisRead++;
    }

    if (_printDebug == true)
      _debugPort->println(F("socketRead: success"));

    // Update *bytesRead
    if (bytesRead != nullptr)
      *bytesRead = readIndexTotal;

    // How many bytes are left to read?
    // This would have been bytesLeftToRead -= bytesToRead
    // Except the LARA can potentially return less data than requested...
    // So we need to subtract readLength instead.
    bytesLeftToRead -= readLength;
    if (_printDebug == true)
    {
      if (bytesLeftToRead > 0)
      {
        _debugPort->print(F("socketRead: multiple read. bytesLeftToRead: "));
        _debugPort->println(bytesLeftToRead);
      }
    }
  } // /while (bytesLeftToRead > 0)

  free(command);
  free(response);

  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::socketReadAvailable(int socket, int *length)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int readLength = 0;
  int socketStore = 0;

  command = lara_r6_calloc_char(strlen(LARA_R6_READ_SOCKET) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,0", LARA_R6_READ_SOCKET, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USORD:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USORD:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,%d",
                        &socketStore, &readLength);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketReadAvailable: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *length = readLength;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::socketReadUDP(int socket, int length, char *readDest,
                                      IPAddress *remoteIPAddress, int *remotePort, int *bytesRead)
{
  char *command;
  char *response;
  char *strBegin;
  int readIndexTotal = 0;
  int readIndexThisRead = 0;
  LARA_R6_error_t err;
  int scanNum = 0;
  int remoteIPstore[4] = { 0, 0, 0, 0 };
  int portStore = 0;
  int readLength = 0;
  int socketStore = 0;
  int bytesLeftToRead = length;
  int bytesToRead;

  // Set *bytesRead to zero
  if (bytesRead != nullptr)
    *bytesRead = 0;

  // Check if length is zero
  if (length == 0)
  {
    if (_printDebug == true)
      _debugPort->print(F("socketReadUDP: length is 0! Call socketReadAvailableUDP?"));
    return LARA_R6_ERROR_UNEXPECTED_PARAM;
  }

  // Allocate memory for the command
  command = lara_r6_calloc_char(strlen(LARA_R6_READ_UDP_SOCKET) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  // Allocate memory for the response
  // We only need enough to read _laraR6maxSocketRead bytes - not the whole thing
  int responseLength = _laraR6maxSocketRead + strlen(LARA_R6_READ_UDP_SOCKET) + minimumResponseAllocation;
  response = lara_r6_calloc_char(responseLength);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // If there are more than _laraR6maxSocketRead (1024) bytes to be read,
  // we need to do multiple reads to get all the data

  while (bytesLeftToRead > 0)
  {
    if (bytesLeftToRead > _laraR6maxSocketRead) // Limit a single read to _laraR6maxSocketRead
      bytesToRead = _laraR6maxSocketRead;
    else
      bytesToRead = bytesLeftToRead;

    sprintf(command, "%s=%d,%d", LARA_R6_READ_UDP_SOCKET, socket, bytesToRead);

    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT, responseLength);

    if (err != LARA_R6_ERROR_SUCCESS)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketReadUDP: sendCommandWithResponse err "));
        _debugPort->println(err);
      }
      free(command);
      free(response);
      return err;
    }

    // Extract the data
    char *searchPtr = strstr(response, "+USORF:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USORF:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,\"%d.%d.%d.%d\",%d,%d",
                        &socketStore, &remoteIPstore[0], &remoteIPstore[1], &remoteIPstore[2], &remoteIPstore[3],
                        &portStore, &readLength);
    }
    if (scanNum != 7)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketReadUDP: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    // Check that readLength == bytesToRead
    if (readLength != bytesToRead)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketReadUDP: length mismatch! bytesToRead="));
        _debugPort->print(bytesToRead);
        _debugPort->print(F(" readLength="));
        _debugPort->println(readLength);
      }
    }

    // Check that readLength > 0
    if (readLength == 0)
    {
      if (_printDebug == true)
      {
        _debugPort->println(F("socketRead: zero length!"));
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_ZERO_READ_LENGTH;
    }

    // Find the third double-quote
    strBegin = strchr(searchPtr, '\"');
    strBegin = strchr(strBegin + 1, '\"');
    strBegin = strchr(strBegin + 1, '\"');

    if (strBegin == nullptr)
    {
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    // Now copy the data into readDest
    readIndexThisRead = 1; // Start after the quote
    while (readIndexThisRead < (readLength + 1))
    {
      readDest[readIndexTotal] = strBegin[readIndexThisRead];
      readIndexTotal++;
      readIndexThisRead++;
    }

    // If remoteIPaddress is not nullptr, copy the remote IP address
    if (remoteIPAddress != nullptr)
    {
      IPAddress tempAddress;
      for (int i = 0; i <= 3; i++)
      {
        tempAddress[i] = (uint8_t)remoteIPstore[i];
      }
      *remoteIPAddress = tempAddress;
    }

    // If remotePort is not nullptr, copy the remote port
    if (remotePort != nullptr)
    {
      *remotePort = portStore;
    }

    if (_printDebug == true)
      _debugPort->println(F("socketReadUDP: success"));

    // Update *bytesRead
    if (bytesRead != nullptr)
      *bytesRead = readIndexTotal;

    // How many bytes are left to read?
    // This would have been bytesLeftToRead -= bytesToRead
    // Except the LARA can potentially return less data than requested...
    // So we need to subtract readLength instead.
    bytesLeftToRead -= readLength;
    if (_printDebug == true)
    {
      if (bytesLeftToRead > 0)
      {
        _debugPort->print(F("socketReadUDP: multiple read. bytesLeftToRead: "));
        _debugPort->println(bytesLeftToRead);
      }
    }
  } // /while (bytesLeftToRead > 0)

  free(command);
  free(response);

  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::socketReadAvailableUDP(int socket, int *length)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int readLength = 0;
  int socketStore = 0;

  command = lara_r6_calloc_char(strlen(LARA_R6_READ_UDP_SOCKET) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,0", LARA_R6_READ_UDP_SOCKET, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USORF:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USORF:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,%d",
                        &socketStore, &readLength);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("socketReadAvailableUDP: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *length = readLength;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::socketListen(int socket, unsigned int port)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_LISTEN_SOCKET) + 9);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d", LARA_R6_LISTEN_SOCKET, socket, port);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::socketDirectLinkMode(int socket)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_DIRECT_LINK) + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_SOCKET_DIRECT_LINK, socket);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_CONNECT, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::socketDirectLinkTimeTrigger(int socket, unsigned long timerTrigger)
{
  // valid range is 0 (trigger disabled), 100-120000
  if (!((timerTrigger == 0) || ((timerTrigger >= 100) && (timerTrigger <= 120000))))
    return LARA_R6_ERROR_ERROR;

  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_UD_CONFIGURATION) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=5,%d,%ld", LARA_R6_UD_CONFIGURATION, socket, timerTrigger);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::socketDirectLinkDataLengthTrigger(int socket, int dataLengthTrigger)
{
  // valid range is 0, 3-1472 for UDP
  if (!((dataLengthTrigger == 0) || ((dataLengthTrigger >= 3) && (dataLengthTrigger <= 1472))))
    return LARA_R6_ERROR_ERROR;

  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_UD_CONFIGURATION) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=6,%d,%d", LARA_R6_UD_CONFIGURATION, socket, dataLengthTrigger);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::socketDirectLinkCharacterTrigger(int socket, int characterTrigger)
{
  // The allowed range is -1, 0-255, the factory-programmed value is -1; -1 means trigger disabled.
  if (!((characterTrigger >= -1) && (characterTrigger <= 255)))
    return LARA_R6_ERROR_ERROR;

  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_UD_CONFIGURATION) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=7,%d,%d", LARA_R6_UD_CONFIGURATION, socket, characterTrigger);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::socketDirectLinkCongestionTimer(int socket, unsigned long congestionTimer)
{
  // valid range is 0, 1000-72000
  if (!((congestionTimer == 0) || ((congestionTimer >= 1000) && (congestionTimer <= 72000))))
    return LARA_R6_ERROR_ERROR;

  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_UD_CONFIGURATION) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=8,%d,%ld", LARA_R6_UD_CONFIGURATION, socket, congestionTimer);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::querySocketType(int socket, LARA_R6_socket_protocol_t *protocol)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,0", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,0,%d",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketType: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *protocol = (LARA_R6_socket_protocol_t)paramVal;
    _lastSocketProtocol[socketStore] = paramVal;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketLastError(int socket, int *error)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,1", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,1,%d",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketLastError: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *error = paramVal;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketTotalBytesSent(int socket, uint32_t *total)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  long unsigned int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,2", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,2,%lu",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketTotalBytesSent: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *total = (uint32_t)paramVal;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketTotalBytesReceived(int socket, uint32_t *total)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  long unsigned int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,3", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,3,%lu",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketTotalBytesReceived: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *total = (uint32_t)paramVal;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketRemoteIPAddress(int socket, IPAddress *address, int *port)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  int paramVals[5];

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,4", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,4,\"%d.%d.%d.%d\",%d",
                        &socketStore,
                        &paramVals[0], &paramVals[1], &paramVals[2], &paramVals[3],
                        &paramVals[4]);
    }
    if (scanNum != 6)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketRemoteIPAddress: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    IPAddress tempAddress = { (uint8_t)paramVals[0], (uint8_t)paramVals[1],
                              (uint8_t)paramVals[2], (uint8_t)paramVals[3] };
    *address = tempAddress;
    *port = paramVals[4];
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketStatusTCP(int socket, LARA_R6_tcp_socket_status_t *status)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,10", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,10,%d",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketStatusTCP: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *status = (LARA_R6_tcp_socket_status_t)paramVal;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::querySocketOutUnackData(int socket, uint32_t *total)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int socketStore = 0;
  long unsigned int paramVal;

  command = lara_r6_calloc_char(strlen(LARA_R6_SOCKET_CONTROL) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,11", LARA_R6_SOCKET_CONTROL, socket);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOCTL:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOCTL:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanNum = sscanf(searchPtr, "%d,11,%lu",
                        &socketStore, &paramVal);
    }
    if (scanNum != 2)
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("querySocketOutUnackData: error: scanNum is "));
        _debugPort->println(scanNum);
      }
      free(command);
      free(response);
      return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }

    *total = (uint32_t)paramVal;
  }

  free(command);
  free(response);

  return err;
}

//Issues command to get last socket error, then prints to serial. Also updates rx/backlog buffers.
int LARA_R6::socketGetLastError()
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  int errorCode = -1;

  command = lara_r6_calloc_char(64);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s", LARA_R6_GET_ERROR);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    char *searchPtr = strstr(response, "+USOER:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+USOER:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      sscanf(searchPtr, "%d", &errorCode);
    }
  }

  free(command);
  free(response);

  return errorCode;
}

IPAddress LARA_R6::lastRemoteIP(void)
{
  return _lastRemoteIP;
}

LARA_R6_error_t LARA_R6::resetHTTPprofile(int profile)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_HTTP_PROFILE, profile);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPserverIPaddress(int profile, IPAddress address)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 64);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%d.%d.%d.%d\"", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_SERVER_IP,
          address[0], address[1], address[2], address[3]);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPserverName(int profile, String server)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 12 + server.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_SERVER_NAME,
          server.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPusername(int profile, String username)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 12 + username.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_USERNAME,
          username.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPpassword(int profile, String password)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 12 + password.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_PASSWORD,
          password.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPauthentication(int profile, bool authenticate)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,%d", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_AUTHENTICATION,
          authenticate);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPserverPort(int profile, int port)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,%d", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_SERVER_PORT,
          port);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPcustomHeader(int profile, String header)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 12 + header.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_ADD_CUSTOM_HEADERS,
          header.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::setHTTPsecure(int profile, bool secure, int secprofile)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROFILE) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (secprofile == -1)
      sprintf(command, "%s=%d,%d,%d", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_SECURE,
          secure);
  else sprintf(command, "%s=%d,%d,%d,%d", LARA_R6_HTTP_PROFILE, profile, LARA_R6_HTTP_OP_CODE_SECURE,
        secure, secprofile);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::ping(String remote_host, int retry, int p_size,
                              unsigned long timeout, int ttl)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_PING_COMMAND) + 48 +
                                remote_host.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\",%d,%d,%ld,%d", LARA_R6_PING_COMMAND,
          remote_host.c_str(), retry, p_size, timeout, ttl);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::sendHTTPGET(int profile, String path, String responseFilename)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_COMMAND) + 24 +
                                path.length() + responseFilename.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\",\"%s\"", LARA_R6_HTTP_COMMAND, profile, LARA_R6_HTTP_COMMAND_GET,
          path.c_str(), responseFilename.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::sendHTTPPOSTdata(int profile, String path, String responseFilename,
                                          String data, LARA_R6_http_content_types_t httpContentType)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_COMMAND) + 24 +
                                path.length() + responseFilename.length() + data.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\",\"%s\",\"%s\",%d", LARA_R6_HTTP_COMMAND, profile, LARA_R6_HTTP_COMMAND_POST_DATA,
          path.c_str(), responseFilename.c_str(), data.c_str(), httpContentType);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::sendHTTPPOSTfile(int profile, String path, String responseFilename,
                                          String requestFile, LARA_R6_http_content_types_t httpContentType)
{
  LARA_R6_error_t err;
  char *command;

  if (profile >= LARA_R6_NUM_HTTP_PROFILES)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_COMMAND) + 24 +
                                path.length() + responseFilename.length() + requestFile.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\",\"%s\",\"%s\",%d", LARA_R6_HTTP_COMMAND, profile, LARA_R6_HTTP_COMMAND_POST_FILE,
          path.c_str(), responseFilename.c_str(), requestFile.c_str(), httpContentType);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::getHTTPprotocolError(int profile, int *error_class, int *error_code)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  int rprofile, eclass, ecode;

  command = lara_r6_calloc_char(strlen(LARA_R6_HTTP_PROTOCOL_ERROR) + 4);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_HTTP_PROTOCOL_ERROR, profile);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    int scanned = 0;
    char *searchPtr = strstr(response, "+UHTTPER:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+UHTTPER:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanned = sscanf(searchPtr, "%d,%d,%d\r\n",
                        &rprofile, &eclass, &ecode);
    }
    if (scanned == 3)
    {
      *error_class = eclass;
      *error_code = ecode;
    }
    else
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::nvMQTT(LARA_R6_mqtt_nv_parameter_t parameter)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_NVM) + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d", LARA_R6_MQTT_NVM, parameter);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::setMQTTclientId(const String& clientId)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_PROFILE) + clientId.length() + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d,\"%s\"", LARA_R6_MQTT_PROFILE, LARA_R6_MQTT_PROFILE_CLIENT_ID, clientId.c_str());
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::setMQTTserver(const String& serverName, int port)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_PROFILE) + serverName.length() + 16);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d,\"%s\",%d", LARA_R6_MQTT_PROFILE, LARA_R6_MQTT_PROFILE_SERVERNAME, serverName.c_str(), port);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::setMQTTcredentials(const String& userName, const String& pwd)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_PROFILE) + userName.length() + pwd.length() + 16);
    if (command == nullptr) {
        return LARA_R6_ERROR_OUT_OF_MEMORY;
    }
    sprintf(command, "%s=%d,\"%s\",\"%s\"", LARA_R6_MQTT_PROFILE, LARA_R6_MQTT_PROFILE_USERNAMEPWD, userName.c_str(), pwd.c_str());
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::setMQTTsecure(bool secure, int secprofile)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_PROFILE) + 16);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    if (secprofile == -1) sprintf(command, "%s=%d,%d", LARA_R6_MQTT_PROFILE, LARA_R6_MQTT_PROFILE_SECURE, secure);
    else sprintf(command, "%s=%d,%d,%d", LARA_R6_MQTT_PROFILE, LARA_R6_MQTT_PROFILE_SECURE, secure, secprofile);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::connectMQTT(void)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_LOGIN);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::disconnectMQTT(void)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_LOGOUT);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::subscribeMQTTtopic(int max_Qos, const String& topic)
{
  LARA_R6_error_t err;
  char *command;
  command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 16 + topic.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_SUBSCRIBE, max_Qos, topic.c_str());
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::unsubscribeMQTTtopic(const String& topic)
{
  LARA_R6_error_t err;
  char *command;
  command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 16 + topic.length());
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d,\"%s\"", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_UNSUBSCRIBE, topic.c_str());
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::readMQTT(int* pQos, String* pTopic, uint8_t *readDest, int readLength, int *bytesRead)
{
  char *command;
  char *response;
  LARA_R6_error_t err;
  int scanNum = 0;
  int total_length, topic_length, data_length;

  // Set *bytesRead to zero
  if (bytesRead != nullptr)
    *bytesRead = 0;

  // Allocate memory for the command
  command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 10);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  // Allocate memory for the response
  int responseLength = readLength + minimumResponseAllocation;
  response = lara_r6_calloc_char(responseLength);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // Note to self: if the file contents contain "OK\r\n" sendCommandWithResponse will return true too early...
  // To try and avoid this, look for \"\r\n\r\nOK\r\n there is a extra \r\n beetween " and the the standard \r\nOK\r\n
  const char mqttReadTerm[] = "\"\r\n\r\nOK\r\n";
  sprintf(command, "%s=%d,%d", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_READ, 1);
  err = sendCommandWithResponse(command, mqttReadTerm, response,
                                (5 * LARA_R6_STANDARD_RESPONSE_TIMEOUT), responseLength);

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("readMQTT: sendCommandWithResponse err "));
      _debugPort->println(err);
    }
    free(command);
    free(response);
    return err;
  }

  // Extract the data
  char *searchPtr = strstr(response, "+UMQTTC:");
  int cmd = 0;
  if (searchPtr != nullptr)
  {
    searchPtr += strlen("+UMQTTC:"); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
    scanNum = sscanf(searchPtr, "%d,%d,%d,%d,\"%*[^\"]\",%d,\"", &cmd, pQos, &total_length, &topic_length, &data_length);
  }
  if ((scanNum != 5) || (cmd != LARA_R6_MQTT_COMMAND_READ))
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("readMQTT: error: scanNum is "));
      _debugPort->println(scanNum);
    }
    free(command);
    free(response);
    return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  err = LARA_R6_ERROR_SUCCESS;
  searchPtr = strstr(searchPtr, "\"");
  if (searchPtr!= nullptr) {
    if (pTopic) {
      searchPtr[topic_length + 1] = '\0'; // zero terminate
      *pTopic = searchPtr + 1;
      searchPtr[topic_length + 1] = '\"'; // restore
    }
    searchPtr = strstr(searchPtr + topic_length + 2, "\"");
    if (readDest && (searchPtr != nullptr) && (response + responseLength >= searchPtr + data_length + 1) && (searchPtr[data_length + 1] == '"')) {
      if (data_length > readLength) {
        data_length = readLength;
        if (_printDebug == true) {
          _debugPort->print(F("readMQTT: error: trucate message"));
        }
        err = LARA_R6_ERROR_OUT_OF_MEMORY;
      }
      memcpy(readDest, searchPtr+1, data_length);
      *bytesRead = data_length;
    } else {
      if (_printDebug == true) {
        _debugPort->print(F("readMQTT: error: message end "));
      }
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
  }
  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::mqttPublishTextMsg(const String& topic, const char * const msg, uint8_t qos, bool retain)
{
  if (topic.length() < 1 || msg == nullptr)
  {
    return LARA_R6_ERROR_INVALID;
  }

  LARA_R6_error_t err;

  char sanitized_msg[MAX_MQTT_DIRECT_MSG_LEN + 1];
  memset(sanitized_msg, 0, sizeof(sanitized_msg));

  // Check the message length and truncate if necessary.
  size_t msg_len = strnlen(msg, MAX_MQTT_DIRECT_MSG_LEN);
  if (msg_len > MAX_MQTT_DIRECT_MSG_LEN)
  {
    msg_len = MAX_MQTT_DIRECT_MSG_LEN;
  }

  strncpy(sanitized_msg, msg, msg_len);
  char * msg_ptr = sanitized_msg;
  while (*msg_ptr != 0)
  {
    if (*msg_ptr == '"')
    {
      *msg_ptr = ' ';
    }

    msg_ptr++;
  }

  char *command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 20 + topic.length() + msg_len);
  if (command == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s=%d,%u,%u,0,\"%s\",\"%s\"", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_PUBLISH, qos, (retain ? 1:0), topic.c_str(), sanitized_msg);

  sendCommand(command, true);
  err = waitForResponse(LARA_R6_RESPONSE_MORE, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    sendCommand(msg, false);
    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  }

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::mqttPublishBinaryMsg(const String& topic, const char * const msg, size_t msg_len, uint8_t qos, bool retain)
{
  /*
   * The modem prints the '>' as the signal to send the binary message content.
   * at+umqttc=9,0,0,"topic",4
   *
   * >"xY"
   * OK
   *
   * +UUMQTTC: 9,1
   */
  if (topic.length() < 1|| msg == nullptr || msg_len > MAX_MQTT_DIRECT_MSG_LEN)
  {
    return LARA_R6_ERROR_INVALID;
  }

  LARA_R6_error_t err;
  char *command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 20 + topic.length());
  if (command == nullptr)
  {
     return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s=%d,%u,%u,\"%s\",%u", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_PUBLISHBINARY, qos, (retain ? 1:0), topic.c_str(), msg_len);

  sendCommand(command, true);
  err = waitForResponse(LARA_R6_RESPONSE_MORE, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    sendCommand(msg, false);
    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  }

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::mqttPublishFromFile(const String& topic, const String& filename, uint8_t qos, bool retain)
{
  if (topic.length() < 1|| filename.length() < 1)
  {
    return LARA_R6_ERROR_INVALID;
  }

  LARA_R6_error_t err;
  char *command = lara_r6_calloc_char(strlen(LARA_R6_MQTT_COMMAND) + 20 + topic.length() + filename.length());
  if (command == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s=%d,%u,%u,\"%s\",\"%s\"", LARA_R6_MQTT_COMMAND, LARA_R6_MQTT_COMMAND_PUBLISHFILE, qos, (retain ? 1:0), topic.c_str(), filename.c_str());

  sendCommand(command, true);
  err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::getMQTTprotocolError(int *error_code, int *error_code2)
{
  LARA_R6_error_t err;
  char *response;

  int code, code2;

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(LARA_R6_MQTT_PROTOCOL_ERROR, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    int scanned = 0;
    char *searchPtr = strstr(response, "+UMQTTER:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+UMQTTER:"); //  Move searchPtr to first char
      while (*searchPtr == ' ') searchPtr++; // skip spaces
      scanned = sscanf(searchPtr, "%d,%d\r\n",
                        &code, &code2);
    }
    if (scanned == 2)
    {
      *error_code = code;
      *error_code2 = code2;
    }
    else
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::setFTPserver(const String& serverName)
{
  constexpr size_t cmd_len = 145;
  char command[cmd_len]; // long enough for AT+UFTP=1,<128 bytes>

  snprintf(command, cmd_len - 1, "%s=%d,\"%s\"", LARA_R6_FTP_PROFILE, LARA_R6_FTP_PROFILE_SERVERNAME, serverName.c_str());
  return sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                 LARA_R6_STANDARD_RESPONSE_TIMEOUT);
}

LARA_R6_error_t LARA_R6::setFTPtimeouts(const unsigned int timeout, const unsigned int cmd_linger, const unsigned int data_linger)
{
  constexpr size_t cmd_len = 64;
  char command[cmd_len]; // long enough for AT+UFTP=1,<128 bytes>

  snprintf(command, cmd_len - 1, "%s=%d,%u,%u,%u", LARA_R6_FTP_PROFILE, LARA_R6_FTP_PROFILE_TIMEOUT, timeout, cmd_linger, data_linger);
  return sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                 LARA_R6_STANDARD_RESPONSE_TIMEOUT);
}

LARA_R6_error_t LARA_R6::setFTPcredentials(const String& userName, const String& pwd)
{
  LARA_R6_error_t err;
  constexpr size_t cmd_len = 48;
  char command[cmd_len]; // long enough for AT+UFTP=n,<30 bytes>

  snprintf(command, cmd_len - 1, "%s=%d,\"%s\"", LARA_R6_FTP_PROFILE, LARA_R6_FTP_PROFILE_USERNAME, userName.c_str());
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    return err;
  }

  snprintf(command, cmd_len - 1, "%s=%d,\"%s\"", LARA_R6_FTP_PROFILE, LARA_R6_FTP_PROFILE_PWD, pwd.c_str());
  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  return err;
}

LARA_R6_error_t LARA_R6::connectFTP(void)
{
  constexpr size_t cmd_len = 16;
  char command[cmd_len]; // long enough for AT+UFTPC=n

  snprintf(command, cmd_len - 1, "%s=%d", LARA_R6_FTP_COMMAND, LARA_R6_FTP_COMMAND_LOGIN);
  return sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                 LARA_R6_STANDARD_RESPONSE_TIMEOUT);
}

LARA_R6_error_t LARA_R6::disconnectFTP(void)
{
  constexpr size_t cmd_len = 16;
  char command[cmd_len]; // long enough for AT+UFTPC=n

  snprintf(command, cmd_len - 1, "%s=%d", LARA_R6_FTP_COMMAND, LARA_R6_FTP_COMMAND_LOGOUT);
  return sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                 LARA_R6_STANDARD_RESPONSE_TIMEOUT);
}

LARA_R6_error_t LARA_R6::ftpGetFile(const String& filename)
{
  char * command = lara_r6_calloc_char(strlen(LARA_R6_FTP_COMMAND) + (2 * filename.length()) + 16);
  if (command == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  sprintf(command, "%s=%d,\"%s\",\"%s\"", LARA_R6_FTP_COMMAND, LARA_R6_FTP_COMMAND_GET_FILE, filename.c_str(), filename.c_str());
  //memset(response, 0, sizeof(response));
  //sendCommandWithResponse(command, LARA_R6_RESPONSE_CONNECT, response, 8000 /* ms */, response_len);
  LARA_R6_error_t err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::getFTPprotocolError(int *error_code, int *error_code2)
{
  LARA_R6_error_t err;
  char *response;

  int code, code2;

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(LARA_R6_FTP_PROTOCOL_ERROR, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    int scanned = 0;
    char *searchPtr = strstr(response, "+UFTPER:");
    if (searchPtr != nullptr)
    {
      searchPtr += strlen("+UFTPER:"); //  Move searchPtr to first char
      while (*searchPtr == ' ')
      {
        searchPtr++; // skip spaces
      }
      scanned = sscanf(searchPtr, "%d,%d\r\n", &code, &code2);
    }

    if (scanned == 2)
    {
      *error_code = code;
      *error_code2 = code2;
    }
    else
    {
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
  }

  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::resetSecurityProfile(int secprofile)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_SEC_PROFILE) + 6);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  sprintf(command, "%s=%d", LARA_R6_SEC_PROFILE, secprofile);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::configSecurityProfile(int secprofile, LARA_R6_sec_profile_parameter_t parameter, int value)
{
    LARA_R6_error_t err;
    char *command;

    command = lara_r6_calloc_char(strlen(LARA_R6_SEC_PROFILE) + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d,%d,%d", LARA_R6_SEC_PROFILE, secprofile,parameter,value);
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::configSecurityProfileString(int secprofile, LARA_R6_sec_profile_parameter_t parameter, String value)
{
    LARA_R6_error_t err;
    char *command;
    command = lara_r6_calloc_char(strlen(LARA_R6_SEC_PROFILE) + value.length() + 10);
    if (command == nullptr)
      return LARA_R6_ERROR_OUT_OF_MEMORY;
    sprintf(command, "%s=%d,%d,\"%s\"", LARA_R6_SEC_PROFILE, secprofile,parameter,value.c_str());
    err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                  LARA_R6_STANDARD_RESPONSE_TIMEOUT);
    free(command);
    return err;
}

LARA_R6_error_t LARA_R6::setSecurityManager(LARA_R6_sec_manager_opcode_t opcode, LARA_R6_sec_manager_parameter_t parameter, String name, String data)
{
  char *command;
  char *response;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_SEC_MANAGER) + name.length() + 20);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }
  int dataLen = data.length();
  sprintf(command, "%s=%d,%d,\"%s\",%d", LARA_R6_SEC_MANAGER, opcode, parameter, name.c_str(), dataLen);

  err = sendCommandWithResponse(command, ">", response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("dataDownload: writing "));
      _debugPort->print(dataLen);
      _debugPort->println(F(" bytes"));
    }
    hwWriteData(data.c_str(), dataLen);
    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT*3);
  }


  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("dataDownload: Error: "));
      _debugPort->print(err);
      _debugPort->print(F(" => {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::activatePDPcontext(bool status, int cid)
{
  LARA_R6_error_t err;
  char *command;

  if (cid >= LARA_R6_NUM_PDP_CONTEXT_IDENTIFIERS)
    return LARA_R6_ERROR_ERROR;

  command = lara_r6_calloc_char(strlen(LARA_R6_MESSAGE_PDP_CONTEXT_ACTIVATE) + 32);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (cid == -1)
    sprintf(command, "%s=%d", LARA_R6_MESSAGE_PDP_CONTEXT_ACTIVATE, status);
  else
    sprintf(command, "%s=%d,%d", LARA_R6_MESSAGE_PDP_CONTEXT_ACTIVATE, status, cid);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}

bool LARA_R6::isGPSon(void)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  bool on = false;

  command = lara_r6_calloc_char(strlen(LARA_R6_GNSS_POWER) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_GNSS_POWER);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response,
                                LARA_R6_10_SEC_TIMEOUT);

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    // Example response: "+UGPS: 0" for off "+UGPS: 1,0,1" for on
    // Search for a ':' followed by a '1' or ' 1'
    char *pch1 = strchr(response, ':');
    if (pch1 != nullptr)
    {
      char *pch2 = strchr(response, '1');
      if ((pch2 != nullptr) && ((pch2 == pch1 + 1) || (pch2 == pch1 + 2)))
        on = true;
    }
  }

  free(command);
  free(response);

  return on;
}

LARA_R6_error_t LARA_R6::gpsPower(bool enable, gnss_system_t gnss_sys, gnss_aiding_mode_t gnss_aiding)
{
  LARA_R6_error_t err;
  char *command;
  bool gpsState;

  // Don't turn GPS on/off if it's already on/off
  gpsState = isGPSon();
  if ((enable && gpsState) || (!enable && !gpsState))
  {
    return LARA_R6_ERROR_SUCCESS;
  }

  // GPS power management
  command = lara_r6_calloc_char(strlen(LARA_R6_GNSS_POWER) + 32); // gnss_sys could be up to three digits
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (enable)
  {
    sprintf(command, "%s=1,%d,%d", LARA_R6_GNSS_POWER, gnss_aiding, gnss_sys);
  }
  else
  {
    sprintf(command, "%s=0", LARA_R6_GNSS_POWER);
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, 10000);

  free(command);
  return err;
}

/*
LARA_R6_error_t LARA_R6::gpsEnableClock(bool enable)
{
    // AT+UGZDA=<0,1>
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsGetClock(struct ClockData *clock)
{
    // AT+UGZDA?
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsEnableFix(bool enable)
{
    // AT+UGGGA=<0,1>
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsGetFix(struct PositionData *pos)
{
    // AT+UGGGA?
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsEnablePos(bool enable)
{
    // AT+UGGLL=<0,1>
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsGetPos(struct PositionData *pos)
{
    // AT+UGGLL?
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsEnableSat(bool enable)
{
    // AT+UGGSV=<0,1>
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsGetSat(uint8_t *sats)
{
    // AT+UGGSV?
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}
*/

LARA_R6_error_t LARA_R6::gpsEnableRmc(bool enable)
{
  // AT+UGRMC=<0,1>
  LARA_R6_error_t err;
  char *command;

  // ** Don't call gpsPower here. It causes problems for +UTIME and the PPS signal **
  // ** Call isGPSon and gpsPower externally if required **
  // if (!isGPSon())
  // {
  //     err = gpsPower(true);
  //     if (err != LARA_R6_ERROR_SUCCESS)
  //     {
  //         return err;
  //     }
  // }

  command = lara_r6_calloc_char(strlen(LARA_R6_GNSS_GPRMC) + 3);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_GNSS_GPRMC, enable ? 1 : 0);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, LARA_R6_10_SEC_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::gpsGetRmc(struct PositionData *pos, struct SpeedData *spd,
                                   struct ClockData *clk, bool *valid)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  char *rmcBegin;

  command = lara_r6_calloc_char(strlen(LARA_R6_GNSS_GPRMC) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_GNSS_GPRMC);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_10_SEC_TIMEOUT);
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    // Fast-forward response string to $GPRMC starter
    rmcBegin = strstr(response, "$GPRMC");
    if (rmcBegin == nullptr)
    {
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
    else
    {
      *valid = parseGPRMCString(rmcBegin, pos, clk, spd);
    }
  }

  free(command);
  free(response);
  return err;
}

/*
LARA_R6_error_t LARA_R6::gpsEnableSpeed(bool enable)
{
    // AT+UGVTG=<0,1>
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}

LARA_R6_error_t LARA_R6::gpsGetSpeed(struct SpeedData *speed)
{
    // AT+UGVTG?
    LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;
    return err;
}
*/

LARA_R6_error_t LARA_R6::gpsRequest(unsigned int timeout, uint32_t accuracy,
                                    bool detailed, unsigned int sensor)
{
  // AT+ULOC=2,<useCellLocate>,<detailed>,<timeout>,<accuracy>
  LARA_R6_error_t err;
  char *command;

  // This function will only work if the GPS module is initially turned off.
  if (isGPSon())
  {
    gpsPower(false);
  }

  if (timeout > 999)
    timeout = 999;
  if (accuracy > 999999)
    accuracy = 999999;

  command = lara_r6_calloc_char(strlen(LARA_R6_GNSS_REQUEST_LOCATION) + 24);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
#if defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_ESP8266)
  sprintf(command, "%s=2,%d,%d,%d,%d", LARA_R6_GNSS_REQUEST_LOCATION,
          sensor, detailed ? 1 : 0, timeout, accuracy);
#else
  sprintf(command, "%s=2,%d,%d,%d,%ld", LARA_R6_GNSS_REQUEST_LOCATION,
          sensor, detailed ? 1 : 0, timeout, accuracy);
#endif

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, LARA_R6_10_SEC_TIMEOUT);

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::gpsAidingServerConf(const char *primaryServer, const char *secondaryServer, const char *authToken,
                                             unsigned int days, unsigned int period, unsigned int resolution,
                                             unsigned int gnssTypes, unsigned int mode, unsigned int dataType)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_AIDING_SERVER_CONFIGURATION) + 256);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  sprintf(command, "%s=\"%s\",\"%s\",\"%s\",%d,%d,%d,%d,%d,%d", LARA_R6_AIDING_SERVER_CONFIGURATION,
          primaryServer, secondaryServer, authToken,
          days, period, resolution, gnssTypes, mode, dataType);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);
  return err;
}


// OK for text files. But will fail with binary files (containing \0) on some platforms.
LARA_R6_error_t LARA_R6::appendFileContents(String filename, const char *str, int len)
{
  char *command;
  char *response;
  LARA_R6_error_t err;

  command = lara_r6_calloc_char(strlen(LARA_R6_FILE_SYSTEM_DOWNLOAD_FILE) + filename.length() + 10);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }
  int dataLen = len == -1 ? strlen(str) : len;
  sprintf(command, "%s=\"%s\",%d", LARA_R6_FILE_SYSTEM_DOWNLOAD_FILE, filename.c_str(), dataLen);

  err = sendCommandWithResponse(command, ">", response,
                                LARA_R6_STANDARD_RESPONSE_TIMEOUT*2);

  unsigned long writeDelay = millis();
  while (millis() < (writeDelay + 50))
    delay(1); //uBlox specification says to wait 50ms after receiving "@" to write data.

  if (err == LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("fileDownload: writing "));
      _debugPort->print(dataLen);
      _debugPort->println(F(" bytes"));
    }
    hwWriteData(str, dataLen);

    err = waitForResponse(LARA_R6_RESPONSE_OK, LARA_R6_RESPONSE_ERROR, LARA_R6_STANDARD_RESPONSE_TIMEOUT*5);
  }
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("fileDownload: Error: "));
      _debugPort->print(err);
      _debugPort->print(F(" => {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::appendFileContents(String filename, String str)
{
    return appendFileContents(filename, str.c_str(), str.length());
}


// OK for text files. But will fail with binary files (containing \0) on some platforms.
LARA_R6_error_t LARA_R6::getFileContents(String filename, String *contents)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  // Start by getting the file size so we know in advance how much data to expect
  int fileSize = 0;
  err = getFileSize(filename, &fileSize);
  if (err != LARA_R6_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: getFileSize returned err "));
      _debugPort->println(err);
    }
    return err;
  }

  command = lara_r6_calloc_char(strlen(LARA_R6_FILE_SYSTEM_READ_FILE) + filename.length() + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_FILE_SYSTEM_READ_FILE, filename.c_str());

  response = lara_r6_calloc_char(fileSize + minimumResponseAllocation);
  if (response == nullptr)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: response alloc failed: "));
      _debugPort->println(fileSize + minimumResponseAllocation);
    }
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // A large file will completely fill the backlog buffer - but it will be pruned afterwards
  // Note to self: if the file contents contain "OK\r\n" sendCommandWithResponse will return true too early...
  // To try and avoid this, look for \"\r\nOK\r\n
  const char fileReadTerm[] = "\r\nOK\r\n"; //LARA-R6 returns "\"\r\n\r\nOK\r\n" while LARA-R6 return "\"\r\nOK\r\n";
  err = sendCommandWithResponse(command, fileReadTerm,
                                response, (5 * LARA_R6_STANDARD_RESPONSE_TIMEOUT),
                                (fileSize + minimumResponseAllocation));

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: sendCommandWithResponse returned err "));
      _debugPort->println(err);
    }
    free(command);
    free(response);
    return err;
  }

  // Response format: \r\n+URDFILE: "filename",36,"these bytes are the data of the file"\r\n\r\nOK\r\n
  int scanned = 0;
  int readFileSize = 0;
  char *searchPtr = strstr(response, "+URDFILE:");
  if (searchPtr != nullptr)
  {
    searchPtr = strchr(searchPtr, '\"'); // Find the first quote
    searchPtr = strchr(++searchPtr, '\"'); // Find the second quote

    scanned = sscanf(searchPtr, "\",%d,", &readFileSize); // Get the file size (again)
    if (scanned == 1)
    {
      searchPtr = strchr(++searchPtr, '\"'); // Find the third quote

      if (searchPtr == nullptr)
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("getFileContents: third quote not found!"));
        }
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }

      int bytesRead = 0;

      while (bytesRead < readFileSize)
      {
        searchPtr++; // Increment searchPtr then copy file char into contents
      // Important Note: some implementations of concat, like the one on ESP32, are binary-compatible.
      // But some, like SAMD, are not. They use strlen or strcpy internally - which don't like \0's.
      // The only true binary-compatible solution is to use getFileContents(String filename, char *contents)...
        contents->concat(*(searchPtr)); // Append file char to contents
        bytesRead++;
      }
      if (_printDebug == true)
      {
        _debugPort->print(F("getFileContents: total bytes read: "));
        _debugPort->println(bytesRead);
      }
      err = LARA_R6_ERROR_SUCCESS;
    }
    else
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("getFileContents: sscanf failed! scanned is "));
        _debugPort->println(scanned);
      }
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
  }
  else
  {
    if (_printDebug == true)
      _debugPort->println(F("getFileContents: strstr failed!"));
    err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);
  return err;
}

// OK for binary files. Make sure contents can hold the entire file. Get the size first with getFileSize.
LARA_R6_error_t LARA_R6::getFileContents(String filename, char *contents)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  // Start by getting the file size so we know in advance how much data to expect
  int fileSize = 0;
  err = getFileSize(filename, &fileSize);
  if (err != LARA_R6_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: getFileSize returned err "));
      _debugPort->println(err);
    }
    return err;
  }

  command = lara_r6_calloc_char(strlen(LARA_R6_FILE_SYSTEM_READ_FILE) + filename.length() + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_FILE_SYSTEM_READ_FILE, filename.c_str());

  response = lara_r6_calloc_char(fileSize + minimumResponseAllocation);
  if (response == nullptr)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: response alloc failed: "));
      _debugPort->println(fileSize + minimumResponseAllocation);
    }
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  // A large file will completely fill the backlog buffer - but it will be pruned afterwards
  // Note to self: if the file contents contain "OK\r\n" sendCommandWithResponse will return true too early...
  // To try and avoid this, look for \"\r\nOK\r\n
  const char fileReadTerm[] = "\"\r\nOK\r\n";
  err = sendCommandWithResponse(command, fileReadTerm,
                                response, (5 * LARA_R6_STANDARD_RESPONSE_TIMEOUT),
                                (fileSize + minimumResponseAllocation));

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileContents: sendCommandWithResponse returned err "));
      _debugPort->println(err);
    }
    free(command);
    free(response);
    return err;
  }

  // Response format: \r\n+URDFILE: "filename",36,"these bytes are the data of the file"\r\n\r\nOK\r\n
  int scanned = 0;
  int readFileSize = 0;
  char *searchPtr = strstr(response, "+URDFILE:");
  if (searchPtr != nullptr)
  {
    searchPtr = strchr(searchPtr, '\"'); // Find the first quote
    searchPtr = strchr(++searchPtr, '\"'); // Find the second quote

    scanned = sscanf(searchPtr, "\",%d,", &readFileSize); // Get the file size (again)
    if (scanned == 1)
    {
      searchPtr = strchr(++searchPtr, '\"'); // Find the third quote

      if (searchPtr == nullptr)
      {
        if (_printDebug == true)
        {
          _debugPort->println(F("getFileContents: third quote not found!"));
        }
        free(command);
        free(response);
        return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
      }

      int bytesRead = 0;

      while (bytesRead < readFileSize)
      {
        searchPtr++; // Increment searchPtr then copy file char into contents
        contents[bytesRead] = *searchPtr; // Append file char to contents
        bytesRead++;
      }
      if (_printDebug == true)
      {
        _debugPort->print(F("getFileContents: total bytes read: "));
        _debugPort->println(bytesRead);
      }
      err = LARA_R6_ERROR_SUCCESS;
    }
    else
    {
      if (_printDebug == true)
      {
        _debugPort->print(F("getFileContents: sscanf failed! scanned is "));
        _debugPort->println(scanned);
      }
      err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
    }
  }
  else
  {
    if (_printDebug == true)
      _debugPort->println(F("getFileContents: strstr failed!"));
    err = LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::getFileBlock(const String& filename, char* buffer, size_t offset, size_t requested_length, size_t& bytes_read)
{
  bytes_read = 0;
  if (filename.length() < 1 || buffer == nullptr || requested_length < 1)
  {
      return LARA_R6_ERROR_UNEXPECTED_PARAM;
  }

  // trying to get a byte at a time does not seem to be reliable so this method must use
  // a real UART.
  if (_hardSerial == nullptr)
  {
    if (_printDebug == true)
    {
      _debugPort->println(F("getFileBlock: only works with a hardware UART"));
    }
    return LARA_R6_ERROR_INVALID;
  }

  size_t cmd_len = filename.length() + 32;
  char* cmd = lara_r6_calloc_char(cmd_len);
  sprintf(cmd, "at+urdblock=\"%s\",%zu,%zu\r\n", filename.c_str(), offset, requested_length);
  sendCommand(cmd, false);

  int ich;
  char ch;
  int quote_count = 0;
  size_t comma_idx = 0;

  while (quote_count < 3)
  {
    ich = _hardSerial->read();
    if (ich < 0)
    {
      continue;
    }
    ch = (char)(ich & 0xFF);
    cmd[bytes_read++] = ch;
    if (ch == '"')
    {
      quote_count++;
    }
    else if (ch == ',' && comma_idx == 0)
    {
      comma_idx = bytes_read;
    }
  }

  cmd[bytes_read] = 0;
  cmd[bytes_read - 2] = 0;

  // Example response:
  // +URDBLOCK: "wombat.bin",64000,"<data starts here>... "<cr><lf>
  size_t data_length = strtoul(&cmd[comma_idx], nullptr, 10);
  free(cmd);

  bytes_read = 0;
  size_t bytes_remaining = data_length;
  while (bytes_read < data_length)
  {
    // This method seems more reliable than reading a byte at a time.
    size_t rc = _hardSerial->readBytes(&buffer[bytes_read], bytes_remaining);
    bytes_read += rc;
    bytes_remaining -= rc;
  }

  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::getFileSize(String filename, int *size)
{
  LARA_R6_error_t err;
  char *command;
  char *response;

  command = lara_r6_calloc_char(strlen(LARA_R6_FILE_SYSTEM_LIST_FILES) + filename.length() + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=2,\"%s\"", LARA_R6_FILE_SYSTEM_LIST_FILES, filename.c_str());

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileSize: Fail: Error: "));
      _debugPort->print(err);
      _debugPort->print(F("  Response: {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
    free(command);
    free(response);
    return err;
  }

  char *responseStart = strstr(response, "+ULSTFILE:");
  if (responseStart == nullptr)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getFileSize: Failure: {"));
      _debugPort->print(response);
      _debugPort->println(F("}"));
    }
    free(command);
    free(response);
    return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  int fileSize;
  responseStart += strlen("+ULSTFILE:"); //  Move searchPtr to first char
  while (*responseStart == ' ') responseStart++; // skip spaces
  sscanf(responseStart, "%d", &fileSize);
  *size = fileSize;

  free(command);
  free(response);
  return err;
}

LARA_R6_error_t LARA_R6::deleteFile(String filename)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_FILE_SYSTEM_DELETE_FILE) + filename.length() + 8);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=\"%s\"", LARA_R6_FILE_SYSTEM_DELETE_FILE, filename.c_str());

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  if (err != LARA_R6_ERROR_SUCCESS)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("deleteFile: Fail: Error: "));
      _debugPort->println(err);
    }
  }

  free(command);
  return err;
}

LARA_R6_error_t LARA_R6::modulePowerOff(void)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_POWER_OFF) + 6);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  sprintf(command, "%s", LARA_R6_COMMAND_POWER_OFF);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR, nullptr,
                                LARA_R6_POWER_OFF_TIMEOUT);

  free(command);
  return err;
}

void LARA_R6::modulePowerOn(void)
{
  if (_powerPin >= 0)
  {
    powerOn();
  }
  else
  {
    if (_printDebug == true)
      _debugPort->println(F("modulePowerOn: not supported. _powerPin not defined."));
  }
}

/////////////
// Private //
/////////////

LARA_R6_error_t LARA_R6::init(unsigned long baud,
                              LARA_R6::LARA_R6_init_type_t initType)
{
  int retries = _maxInitTries;
  LARA_R6_error_t err = LARA_R6_ERROR_SUCCESS;

  beginSerial(baud);

  do
  {
    if (_printDebug == true)
      _debugPort->println(F("init: Begin module init."));

    if (initType == LARA_R6_INIT_AUTOBAUD)
    {
      if (_printDebug == true)
        _debugPort->println(F("init: Attempting autobaud connection to module."));

      err = autobaud(baud);

      if (err != LARA_R6_ERROR_SUCCESS) {
        initType = LARA_R6_INIT_RESET;
      }
    }
    else if (initType == LARA_R6_INIT_RESET)
    {
      if (_printDebug == true)
        _debugPort->println(F("init: Power cycling module."));

      powerOff();
      delay(LARA_R6_POWER_OFF_PULSE_PERIOD);
      powerOn();
      beginSerial(baud);
      delay(2000);

      err = at();
      if (err != LARA_R6_ERROR_SUCCESS)
      {
         initType = LARA_R6_INIT_AUTOBAUD;
      }
    }
    if (err == LARA_R6_ERROR_SUCCESS)
    {
      err = enableEcho(false); // = disableEcho
      if (err != LARA_R6_ERROR_SUCCESS)
      {
        if (_printDebug == true)
          _debugPort->println(F("init: Module failed echo test."));
        initType =  LARA_R6_INIT_AUTOBAUD;
      }
    }
  }
  while ((retries --) && (err != LARA_R6_ERROR_SUCCESS));

  // we tried but seems failed
  if (err != LARA_R6_ERROR_SUCCESS) {
    if (_printDebug == true)
      _debugPort->println(F("init: Module failed to init. Exiting."));
    return (LARA_R6_ERROR_NO_RESPONSE);
  }

  if (_printDebug == true)
    _debugPort->println(F("init: Module responded successfully."));

  _baud = baud;
  setGpioMode(GPIO1, NETWORK_STATUS);
  //setGpioMode(GPIO2, GNSS_SUPPLY_ENABLE);
  setGpioMode(GPIO6, TIME_PULSE_OUTPUT);
  setSMSMessageFormat(LARA_R6_MESSAGE_FORMAT_TEXT);
  autoTimeZone(_autoTimeZoneForBegin);
  for (int i = 0; i < LARA_R6_NUM_SOCKETS; i++)
  {
    socketClose(i, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  }

  return LARA_R6_ERROR_SUCCESS;
}

void LARA_R6::invertPowerPin(bool invert)
{
  _invertPowerPin = invert;
}

// Do a graceful power off. Hold the PWR_ON pin low for LARA_R6_POWER_OFF_PULSE_PERIOD
// Note: +CPWROFF () is preferred to this.
void LARA_R6::powerOff(void)
{
  if (_powerPin >= 0)
  {
    if (_invertPowerPin) // Set the pin state before making it an output
      digitalWrite(_powerPin, HIGH);
    else
      digitalWrite(_powerPin, LOW);
    pinMode(_powerPin, OUTPUT);
    if (_invertPowerPin) // Set the pin state
      digitalWrite(_powerPin, HIGH);
    else
      digitalWrite(_powerPin, LOW);
    delay(LARA_R6_POWER_OFF_PULSE_PERIOD);
    pinMode(_powerPin, INPUT); // Return to high-impedance, rely on (e.g.) LARA module internal pull-up
    if (_printDebug == true)
      _debugPort->println(F("powerOff: complete"));
  }
}

void LARA_R6::powerOn(void)
{
  if (_powerPin >= 0)
  {
    if (_invertPowerPin) // Set the pin state before making it an output
      digitalWrite(_powerPin, HIGH);
    else
      digitalWrite(_powerPin, LOW);
    pinMode(_powerPin, OUTPUT);
    if (_invertPowerPin) // Set the pin state
      digitalWrite(_powerPin, HIGH);
    else
      digitalWrite(_powerPin, LOW);
    delay(LARA_R6_POWER_ON_PULSE_PERIOD);
    pinMode(_powerPin, INPUT); // Return to high-impedance, rely on (e.g.) LARA module internal pull-up
    //delay(2000);               // Do this in init. Wait before sending AT commands to module. 100 is too short.
    if (_printDebug == true)
      _debugPort->println(F("powerOn: complete"));
  }
}

//This does an abrupt emergency hardware shutdown of the LARA-R6 series modules.
//It only works if you have access to both the RESET_N and PWR_ON pins.
//You cannot use this function on the SparkFun Asset Tracker and RESET_N is tied to the MicroMod processor !RESET!...
void LARA_R6::hwReset(void)
{
  if ((_resetPin >= 0) && (_powerPin >= 0))
  {
    digitalWrite(_resetPin, HIGH); // Start by making sure the RESET_N pin is high
    pinMode(_resetPin, OUTPUT);
    digitalWrite(_resetPin, HIGH);

    if (_invertPowerPin) // Now pull PWR_ON low - invert as necessary (on the Asset Tracker)
    {
      digitalWrite(_powerPin, HIGH); // Inverted - Asset Tracker
      pinMode(_powerPin, OUTPUT);
      digitalWrite(_powerPin, HIGH);
    }
    else
    {
      digitalWrite(_powerPin, LOW); // Not inverted
      pinMode(_powerPin, OUTPUT);
      digitalWrite(_powerPin, LOW);
    }

    delay(LARA_R6_RESET_PULSE_PERIOD); // Wait 23 seconds... (Yes, really!)

    digitalWrite(_resetPin, LOW); // Now pull RESET_N low

    delay(100); // Wait a little... (The data sheet doesn't say how long for)

    if (_invertPowerPin) // Now pull PWR_ON high - invert as necessary (on the Asset Tracker)
    {
      digitalWrite(_powerPin, LOW); // Inverted - Asset Tracker
    }
    else
    {
      digitalWrite(_powerPin, HIGH); // Not inverted
    }

    delay(1500); // Wait 1.5 seconds

    digitalWrite(_resetPin, HIGH); // Now pull RESET_N high again

    pinMode(_resetPin, INPUT); // Return to high-impedance, rely on LARA module internal pull-up
    pinMode(_powerPin, INPUT); // Return to high-impedance, rely on LARA module internal pull-up
  }
}

LARA_R6_error_t LARA_R6::functionality(LARA_R6_functionality_t function)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_FUNC) + 16);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s=%d", LARA_R6_COMMAND_FUNC, function);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_3_MIN_TIMEOUT);

  free(command);

  return err;
}

LARA_R6_error_t LARA_R6::setMNOprofile(mobile_network_operator_t mno, bool autoReset, bool urcNotification)
{
  LARA_R6_error_t err;
  char *command;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_MNO) + 9);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  if (mno == MNO_SIM_ICCID) // Only add autoReset and urcNotification if mno is MNO_SIM_ICCID
    sprintf(command, "%s=%d,%d,%d", LARA_R6_COMMAND_MNO, (uint8_t)mno, (uint8_t)autoReset, (uint8_t)urcNotification);
  else
    sprintf(command, "%s=%d", LARA_R6_COMMAND_MNO, (uint8_t)mno);

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                nullptr, LARA_R6_STANDARD_RESPONSE_TIMEOUT);

  free(command);

  return err;
}

LARA_R6_error_t LARA_R6::getMNOprofile(mobile_network_operator_t *mno)
{
  LARA_R6_error_t err;
  char *command;
  char *response;
  mobile_network_operator_t o;
  int d;
  int r;
  int u;
  int oStore;

  command = lara_r6_calloc_char(strlen(LARA_R6_COMMAND_MNO) + 2);
  if (command == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  sprintf(command, "%s?", LARA_R6_COMMAND_MNO);

  response = lara_r6_calloc_char(minimumResponseAllocation);
  if (response == nullptr)
  {
    free(command);
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  err = sendCommandWithResponse(command, LARA_R6_RESPONSE_OK_OR_ERROR,
                                response, LARA_R6_STANDARD_RESPONSE_TIMEOUT);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(command);
    free(response);
    return err;
  }

  int scanned = 0;
  char *searchPtr = strstr(response, "+UMNOPROF:");
  if (searchPtr != nullptr)
  {
    searchPtr += strlen("+UMNOPROF:"); //  Move searchPtr to first char
    while (*searchPtr == ' ') searchPtr++; // skip spaces
    scanned = sscanf(searchPtr, "%d,%d,%d,%d", &oStore, &d, &r, &u);
  }
  o = (mobile_network_operator_t)oStore;

  if (scanned >= 1)
  {
    if (_printDebug == true)
    {
      _debugPort->print(F("getMNOprofile: MNO is: "));
      _debugPort->println(o);
    }
    *mno = o;
  }
  else
  {
    err = LARA_R6_ERROR_INVALID;
  }

  free(command);
  free(response);

  return err;
}

LARA_R6_error_t LARA_R6::waitForResponse(const char *expectedResponse, const char *expectedError, uint16_t timeout)
{
  unsigned long timeIn;
  bool found = false;
  bool error = false;
  int responseIndex = 0, errorIndex = 0;
  // bool printedSomething = false;

  timeIn = millis();

  int responseLen = (int)strlen(expectedResponse);
  int errorLen = (int)strlen(expectedError);

  while ((!found) && ((timeIn + timeout) > millis()))
  {
    if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is nullptr
    {
      char c = readChar();
      // if (_printDebug == true)
      // {
      //   if (printedSomething == false)
      //     _debugPort->print(F("waitForResponse: "));
      //   _debugPort->write(c);
      //   printedSomething = true;
      // }
      if ((responseIndex < responseLen) && (c == expectedResponse[responseIndex]))
      {
        if (++responseIndex == responseLen)
        {
          found = true;
        }
      }
      else
      {
        responseIndex = ((responseIndex < responseLen) && (c == expectedResponse[0])) ? 1 : 0;
      }
      if ((errorIndex < errorLen) && (c == expectedError[errorIndex]))
      {
        if (++errorIndex == errorLen)
        {
          error = true;
          found = true;
        }
      }
      else
      {
        errorIndex = ((errorIndex < errorLen) && (c == expectedError[0])) ? 1 : 0;
      }
      //_laraResponseBacklog is a global array that holds the backlog of any events
      //that came in while waiting for response. To be processed later within bufferedPoll().
      //Note: the expectedResponse or expectedError will also be added to the backlog.
      //The backlog is only used by bufferedPoll to process the URCs - which are all readable.
      //bufferedPoll uses strtok - which does not like nullptr characters.
      //So let's make sure no NULLs end up in the backlog!
      if (_laraResponseBacklogLength < _RXBuffSize) // Don't overflow the buffer
      {
        if (c == '\0')
          _laraResponseBacklog[_laraResponseBacklogLength++] = '0'; // Change NULLs to ASCII Zeros
        else
          _laraResponseBacklog[_laraResponseBacklogLength++] = c;
      }
    } else {
      yield();
    }
  }

  // if (_printDebug == true)
  //   if (printedSomething)
  //     _debugPort->println();

  pruneBacklog(); // Prune any incoming non-actionable URC's and responses/errors from the backlog

  if (found == true)
  {
    if (true == _printAtDebug) {
      _debugAtPort->print((error == true) ? expectedError : expectedResponse);
    }

    return (error == true) ? LARA_R6_ERROR_ERROR : LARA_R6_ERROR_SUCCESS;
  }

  return LARA_R6_ERROR_NO_RESPONSE;
}

LARA_R6_error_t LARA_R6::sendCommandWithResponse(
    const char *command, const char *expectedResponse, char *responseDest,
    unsigned long commandTimeout, int destSize, bool at)
{
  bool found = false;
  bool error = false;
  int responseIndex = 0;
  int errorIndex = 0;
  int destIndex = 0;
  unsigned int charsRead = 0;
  int responseLen = 0;
  int errorLen = 0;
  const char* expectedError= nullptr;
  bool printResponse = false; // Change to true to print the full response
  bool printedSomething = false;

  if (_printDebug == true)
  {
    _debugPort->print(F("sendCommandWithResponse: Command: "));
    _debugPort->println(String(command));
  }

  sendCommand(command, at); //Sending command needs to dump data to backlog buffer as well.
  unsigned long timeIn = millis();
  if (LARA_R6_RESPONSE_OK_OR_ERROR == expectedResponse) {
    expectedResponse = LARA_R6_RESPONSE_OK;
    expectedError = LARA_R6_RESPONSE_ERROR;
    responseLen = sizeof(LARA_R6_RESPONSE_OK)-1;
    errorLen = sizeof(LARA_R6_RESPONSE_ERROR)-1;
  } else {
    responseLen = (int)strlen(expectedResponse);
  }

  while ((!found) && ((timeIn + commandTimeout) > millis()))
  {
    if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is nullptr
    {
      char c = readChar();
      if ((printResponse = true) && (_printDebug == true))
      {
        if (printedSomething == false)
        {
          _debugPort->print(F("sendCommandWithResponse: Response: "));
          printedSomething = true;
        }
        _debugPort->write(c);
      }
      if (responseDest != nullptr)
      {
        if (destIndex < destSize) // Only add this char to response if there is room for it
          responseDest[destIndex] = c;
        destIndex++;
        if (destIndex == destSize)
        {
          if (_printDebug == true)
          {
            if ((printResponse = true) && (printedSomething))
              _debugPort->println();
            _debugPort->print(F("sendCommandWithResponse: Panic! responseDest is full!"));
            if ((printResponse = true) && (printedSomething))
              _debugPort->print(F("sendCommandWithResponse: Ignored response: "));
          }
        }
      }
      charsRead++;
      if ((errorIndex < errorLen) && (c == expectedError[errorIndex]))
      {
        if (++errorIndex == errorLen)
        {
          error = true;
          found = true;
        }
      }
      else
      {
        errorIndex = ((errorIndex < errorLen) && (c == expectedError[0])) ? 1 : 0;
      }
      if ((responseIndex < responseLen) && (c == expectedResponse[responseIndex]))
      {
        if (++responseIndex == responseLen)
        {
          found = true;
        }
      }
      else
      {
        responseIndex = ((responseIndex < responseLen) && (c == expectedResponse[0])) ? 1 : 0;
      }
      //_laraResponseBacklog is a global array that holds the backlog of any events
      //that came in while waiting for response. To be processed later within bufferedPoll().
      //Note: the expectedResponse or expectedError will also be added to the backlog
      //The backlog is only used by bufferedPoll to process the URCs - which are all readable.
      //bufferedPoll uses strtok - which does not like NULL characters.
      //So let's make sure no NULLs end up in the backlog!
      if (_laraResponseBacklogLength < _RXBuffSize) // Don't overflow the buffer
      {
        if (c == '\0')
          _laraResponseBacklog[_laraResponseBacklogLength++] = '0'; // Change NULLs to ASCII Zeros
        else
          _laraResponseBacklog[_laraResponseBacklogLength++] = c;
      }
    } else {
      yield();
    }
  }

  if (_printDebug == true)
    if ((printResponse = true) && (printedSomething))
      _debugPort->println();

  pruneBacklog(); // Prune any incoming non-actionable URC's and responses/errors from the backlog

  if (found)
  {
    if ((true == _printAtDebug) && ((nullptr != responseDest) || (nullptr != expectedResponse))) {
      _debugAtPort->print((nullptr != responseDest) ? responseDest : expectedResponse);
    }
    return error ? LARA_R6_ERROR_ERROR : LARA_R6_ERROR_SUCCESS;
  }
  else if (charsRead == 0)
  {
    return LARA_R6_ERROR_NO_RESPONSE;
  }
  else
  {
    if ((true == _printAtDebug) && (nullptr != responseDest)) {
      _debugAtPort->print(responseDest);
    }
    return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }
}

// Send a custom command with an expected (potentially partial) response, store entire response
LARA_R6_error_t LARA_R6::sendCustomCommandWithResponse(const char *command, const char *expectedResponse,
                                                       char *responseDest, unsigned long commandTimeout, bool at)
{
  // Assume the user has allocated enough storage for any response. Set destSize to 32766.
  return sendCommandWithResponse(command, expectedResponse, responseDest, commandTimeout, 32766, at);
}

void LARA_R6::sendCommand(const char *command, bool at)
{
  //Check for incoming serial data. Copy it into the backlog

  // Important note:
  // On ESP32, Serial.available only provides an update every ~120 bytes during the reception of long messages:
  // https://gitter.im/espressif/arduino-esp32?at=5e25d6370a1cf54144909c85
  // Be aware that if a long message is being received, the code below will timeout after _rxWindowMillis = 2 millis.
  // At 115200 baud, hwAvailable takes ~120 * 10 / 115200 = 10.4 millis before it indicates that data is being received.

  unsigned long timeIn = millis();
  if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is NULL
  {
    while (((millis() - timeIn) < _rxWindowMillis) && (_laraResponseBacklogLength < _RXBuffSize)) //May need to escape on newline?
    {
      if (hwAvailable() > 0) //hwAvailable can return -1 if the serial port is NULL
      {
        //_laraResponseBacklog is a global array that holds the backlog of any events
        //that came in while waiting for response. To be processed later within bufferedPoll().
        //Note: the expectedResponse or expectedError will also be added to the backlog
        //The backlog is only used by bufferedPoll to process the URCs - which are all readable.
        //bufferedPoll uses strtok - which does not like NULL characters.
        //So let's make sure no NULLs end up in the backlog!
        char c = readChar();
        if (c == '\0') // Make sure no NULL characters end up in the backlog! Change them to ASCII Zeros
          c = '0';
        _laraResponseBacklog[_laraResponseBacklogLength++] = c;
        timeIn = millis();
      } else {
        yield();
      }
    }
  }

  //Now send the command
  if (at)
  {
    hwPrint(LARA_R6_COMMAND_AT);
    hwPrint(command);
    hwPrint("\r\n");
  }
  else
  {
    hwPrint(command);
  }
}

LARA_R6_error_t LARA_R6::parseSocketReadIndication(int socket, int length)
{
  LARA_R6_error_t err;
  char *readDest;

  if ((socket < 0) || (length < 0))
  {
    return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  // Return now if both callbacks pointers are nullptr - otherwise the data will be read and lost!
  if ((_socketReadCallback == nullptr) && (_socketReadCallbackPlus == nullptr))
    return LARA_R6_ERROR_INVALID;

  readDest = lara_r6_calloc_char(length + 1);
  if (readDest == nullptr)
    return LARA_R6_ERROR_OUT_OF_MEMORY;

  int bytesRead;
  err = socketRead(socket, length, readDest, &bytesRead);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(readDest);
    return err;
  }

  if (_socketReadCallback != nullptr)
  {
    String dataAsString = ""; // Create an empty string
    // Copy the data from readDest into the String in a binary-compatible way
    // Important Note: some implementations of concat, like the one on ESP32, are binary-compatible.
    // But some, like SAMD, are not. They use strlen or strcpy internally - which don't like \0's.
    // The only true binary-compatible solution is to use socketReadCallbackPlus...
    for (int i = 0; i < bytesRead; i++)
      dataAsString.concat(readDest[i]);
    _socketReadCallback(socket, dataAsString);
  }

  if (_socketReadCallbackPlus != nullptr)
  {
    IPAddress dummyAddress = { 0, 0, 0, 0 };
    int dummyPort = 0;
    _socketReadCallbackPlus(socket, (const char *)readDest, bytesRead, dummyAddress, dummyPort);
  }

  free(readDest);
  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::parseSocketReadIndicationUDP(int socket, int length)
{
  LARA_R6_error_t err;
  char *readDest;
  IPAddress remoteAddress = { 0, 0, 0, 0 };
  int remotePort = 0;

  if ((socket < 0) || (length < 0))
  {
    return LARA_R6_ERROR_UNEXPECTED_RESPONSE;
  }

  // Return now if both callbacks pointers are nullptr - otherwise the data will be read and lost!
  if ((_socketReadCallback == nullptr) && (_socketReadCallbackPlus == nullptr))
    return LARA_R6_ERROR_INVALID;

  readDest = lara_r6_calloc_char(length + 1);
  if (readDest == nullptr)
  {
    return LARA_R6_ERROR_OUT_OF_MEMORY;
  }

  int bytesRead;
  err = socketReadUDP(socket, length, readDest, &remoteAddress, &remotePort, &bytesRead);
  if (err != LARA_R6_ERROR_SUCCESS)
  {
    free(readDest);
    return err;
  }

  if (_socketReadCallback != nullptr)
  {
    String dataAsString = ""; // Create an empty string
    // Important Note: some implementations of concat, like the one on ESP32, are binary-compatible.
    // But some, like SAMD, are not. They use strlen or strcpy internally - which don't like \0's.
    // The only true binary-compatible solution is to use socketReadCallbackPlus...
    for (int i = 0; i < bytesRead; i++) // Copy the data from readDest into the String in a binary-compatible way
      dataAsString.concat(readDest[i]);
    _socketReadCallback(socket, dataAsString);
  }

  if (_socketReadCallbackPlus != nullptr)
  {
    _socketReadCallbackPlus(socket, (const char *)readDest, bytesRead, remoteAddress, remotePort);
  }

  free(readDest);
  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::parseSocketListenIndication(int listeningSocket, IPAddress localIP, unsigned int listeningPort, int socket, IPAddress remoteIP, unsigned int port)
{
  _lastLocalIP = localIP;
  _lastRemoteIP = remoteIP;

  if (_socketListenCallback != nullptr)
  {
    _socketListenCallback(listeningSocket, localIP, listeningPort, socket, remoteIP, port);
  }

  return LARA_R6_ERROR_SUCCESS;
}

LARA_R6_error_t LARA_R6::parseSocketCloseIndication(String *closeIndication)
{
  int search;
  int socket;

  search = closeIndication->indexOf(LARA_R6_CLOSE_SOCKET_URC);
  search += strlen(LARA_R6_CLOSE_SOCKET_URC);
  while (closeIndication->charAt(search) == ' ') search ++; // skip spaces

  // Socket will be first integer, should be single-digit number between 0-6:
  socket = closeIndication->substring(search, search + 1).toInt();

  if (_socketCloseCallback != nullptr)
  {
    _socketCloseCallback(socket);
  }

  return LARA_R6_ERROR_SUCCESS;
}

size_t LARA_R6::hwPrint(const char *s)
{
  if ((true == _printAtDebug) && (nullptr != s)) {
    _debugAtPort->print(s);
  }
  if (_hardSerial != nullptr)
  {
    return _hardSerial->print(s);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    return _softSerial->print(s);
  }
#endif

  return (size_t)0;
}

size_t LARA_R6::hwWriteData(const char *buff, int len)
{
  if ((true == _printAtDebug) && (nullptr != buff) && (0 < len) ) {
    _debugAtPort->write(buff,len);
  }
  if (_hardSerial != nullptr)
  {
    return _hardSerial->write((const uint8_t *)buff, len);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    return _softSerial->write((const uint8_t *)buff, len);
  }
#endif
  return (size_t)0;
}

size_t LARA_R6::hwWrite(const char c)
{
  if (true == _printAtDebug) {
    _debugAtPort->write(c);
  }
  if (_hardSerial != nullptr)
  {
    return _hardSerial->write(c);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    return _softSerial->write(c);
  }
#endif

  return (size_t)0;
}

int LARA_R6::readAvailable(char *inString)
{
  int len = 0;

  if (_hardSerial != nullptr)
  {
    while (_hardSerial->available())
    {
      char c = (char)_hardSerial->read();
      if (inString != nullptr)
      {
        inString[len++] = c;
      }
    }
    if (inString != nullptr)
    {
      inString[len] = 0;
    }
    //if (_printDebug == true)
    //  _debugPort->println(inString);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    while (_softSerial->available())
    {
      char c = (char)_softSerial->read();
      if (inString != nullptr)
      {
        inString[len++] = c;
      }
    }
    if (inString != nullptr)
    {
      inString[len] = 0;
    }
  }
#endif

  return len;
}

char LARA_R6::readChar(void)
{
  char ret = 0;

  if (_hardSerial != nullptr)
  {
    ret = (char)_hardSerial->read();
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    ret = (char)_softSerial->read();
  }
#endif

  return ret;
}

int LARA_R6::hwAvailable(void)
{
  if (_hardSerial != nullptr)
  {
    return _hardSerial->available();
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    return _softSerial->available();
  }
#endif

  return -1;
}

void LARA_R6::beginSerial(unsigned long baud)
{
  delay(100);
  if (_hardSerial != nullptr)
  {
    _hardSerial->end();
    _hardSerial->begin(baud);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    _softSerial->end();
    _softSerial->begin(baud);
  }
#endif
  delay(100);
}

void LARA_R6::setTimeout(unsigned long timeout)
{
  if (_hardSerial != nullptr)
  {
    _hardSerial->setTimeout(timeout);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    _softSerial->setTimeout(timeout);
  }
#endif
}

bool LARA_R6::find(char *target)
{
  bool found = false;
  if (_hardSerial != nullptr)
  {
    found = _hardSerial->find(target);
  }
#ifdef LARA_R6_SOFTWARE_SERIAL_ENABLED
  else if (_softSerial != nullptr)
  {
    found = _softSerial->find(target);
  }
#endif
  return found;
}

LARA_R6_error_t LARA_R6::autobaud(unsigned long desiredBaud)
{
  LARA_R6_error_t err = LARA_R6_ERROR_INVALID;
  int b = 0;

  while ((err != LARA_R6_ERROR_SUCCESS) && (b < NUM_SUPPORTED_BAUD))
  {
    beginSerial(LARA_R6_SUPPORTED_BAUD[b++]);
    setBaud(desiredBaud);
    beginSerial(desiredBaud);
    err = at();
  }
  if (err == LARA_R6_ERROR_SUCCESS)
  {
    beginSerial(desiredBaud);
  }
  return err;
}

char *LARA_R6::lara_r6_calloc_char(size_t num)
{
  return (char *)calloc(num, sizeof(char));
}

//This prunes the backlog of non-actionable events. If new actionable events are added, you must modify the if statement.
void LARA_R6::pruneBacklog()
{
  char *event;

  // if (_printDebug == true)
  // {
  //   if (_laraResponseBacklogLength > 0) //Handy for debugging new parsing.
  //   {
  //     _debugPort->println(F("pruneBacklog: before pruning, backlog was:"));
  //     _debugPort->println(_laraResponseBacklog);
  //     _debugPort->println(F("pruneBacklog: end of backlog"));
  //   }
  //   else
  //   {
  //     _debugPort->println(F("pruneBacklog: backlog was empty"));
  //   }
  // }

  memset(_pruneBuffer, 0, _RXBuffSize); // Clear the _pruneBuffer

  _laraResponseBacklogLength = 0; // Zero the backlog length

  char *preservedEvent;
  event = strtok_r(_laraResponseBacklog, "\r\n", &preservedEvent); // Look for an 'event' - something ending in \r\n

  while (event != nullptr) //If event is actionable, add it to pruneBuffer.
  {
    // These are the events we want to keep so they can be processed by poll / bufferedPoll
    if ((strstr(event, LARA_R6_READ_SOCKET_URC) != nullptr)
        || (strstr(event, LARA_R6_READ_UDP_SOCKET_URC) != nullptr)
        || (strstr(event, LARA_R6_LISTEN_SOCKET_URC) != nullptr)
        || (strstr(event, LARA_R6_CLOSE_SOCKET_URC) != nullptr)
        || (strstr(event, LARA_R6_GNSS_REQUEST_LOCATION_URC) != nullptr)
        || (strstr(event, LARA_R6_SIM_STATE_URC) != nullptr)
        || (strstr(event, LARA_R6_HTTP_COMMAND_URC) != nullptr)
        || (strstr(event, LARA_R6_MQTT_COMMAND_URC) != nullptr)
        || (strstr(event, LARA_R6_PING_COMMAND_URC) != nullptr)
        || (strstr(event, LARA_R6_REGISTRATION_STATUS_URC) != nullptr)
        || (strstr(event, LARA_R6_EPSREGISTRATION_STATUS_URC) != nullptr)
        || (strstr(event, LARA_R6_FTP_COMMAND_URC) != nullptr))
    {
      strcat(_pruneBuffer, event); // The URCs are all readable text so using strcat is OK
      strcat(_pruneBuffer, "\r\n"); // strtok blows away delimiter, but we want that for later.
      _laraResponseBacklogLength += strlen(event) + 2; // Add the length of this event to _laraResponseBacklogLength
    }

    event = strtok_r(nullptr, "\r\n", &preservedEvent); // Walk though any remaining events
  }

  memset(_laraResponseBacklog, 0, _RXBuffSize); //Clear out backlog buffer.
  memcpy(_laraResponseBacklog, _pruneBuffer, _laraResponseBacklogLength); //Copy the pruned buffer back into the backlog

  // if (_printDebug == true)
  // {
  //   if (_laraResponseBacklogLength > 0) //Handy for debugging new parsing.
  //   {
  //     _debugPort->println(F("pruneBacklog: after pruning, backlog is now:"));
  //     _debugPort->println(_laraResponseBacklog);
  //     _debugPort->println(F("pruneBacklog: end of backlog"));
  //   }
  //   else
  //   {
  //     _debugPort->println(F("pruneBacklog: backlog is now empty"));
  //   }
  // }
}

// GPS Helper Functions:

// Read a source string until a delimiter is hit, store the result in destination
char *LARA_R6::readDataUntil(char *destination, unsigned int destSize,
                             char *source, char delimiter)
{

  char *strEnd;
  size_t len;

  strEnd = strchr(source, delimiter);

  if (strEnd != nullptr)
  {
    len = strEnd - source;
    memset(destination, 0, destSize);
    memcpy(destination, source, len);
  }

  return strEnd;
}

bool LARA_R6::parseGPRMCString(char *rmcString, PositionData *pos,
                               ClockData *clk, SpeedData *spd)
{
  char *ptr, *search;
  unsigned long tTemp;
  char tempData[TEMP_NMEA_DATA_SIZE];

  // if (_printDebug == true)
  // {
  //   _debugPort->println(F("parseGPRMCString: rmcString: "));
  //   _debugPort->println(rmcString);
  // }

  // Fast-forward test to first value:
  ptr = strchr(rmcString, ',');
  ptr++; // Move ptr past first comma

  // If the next character is another comma, there's no time data
  // Find time:
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  // Next comma should be present and not the next position
  if ((search != nullptr) && (search != ptr))
  {
    pos->utc = atof(tempData);                             // Extract hhmmss.ss as float
    tTemp = pos->utc;                                      // Convert to unsigned long (discard the digits beyond the decimal point)
    clk->time.ms = ((unsigned int)(pos->utc * 100)) % 100; // Extract the milliseconds
    clk->time.hour = tTemp / 10000;
    tTemp -= ((unsigned long)clk->time.hour * 10000);
    clk->time.minute = tTemp / 100;
    tTemp -= ((unsigned long)clk->time.minute * 100);
    clk->time.second = tTemp;
  }
  else
  {
    pos->utc = 0.0;
    clk->time.hour = 0;
    clk->time.minute = 0;
    clk->time.second = 0;
  }
  ptr = search + 1; // Move pointer to next value

  // Find status character:
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  // Should be a single character: V = Data invalid, A = Data valid
  if ((search != nullptr) && (search == ptr + 1))
  {
    pos->status = *ptr; // Assign char at ptr to status
  }
  else
  {
    pos->status = 'X'; // Made up very bad status
  }
  ptr = search + 1;

  // Find latitude:
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    pos->lat = atof(tempData);              // Extract ddmm.mmmmm as float
    unsigned long lat_deg = pos->lat / 100; // Extract the degrees
    pos->lat -= (float)lat_deg * 100.0;     // Subtract the degrees leaving only the minutes
    pos->lat /= 60.0;                       // Convert minutes into degrees
    pos->lat += (float)lat_deg;             // Finally add the degrees back on again
  }
  else
  {
    pos->lat = 0.0;
  }
  ptr = search + 1;

  // Find latitude hemishpere
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search == ptr + 1))
  {
    if (*ptr == 'S')    // Is the latitude South
      pos->lat *= -1.0; // Make lat negative
  }
  ptr = search + 1;

  // Find longitude:
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    pos->lon = atof(tempData);              // Extract dddmm.mmmmm as float
    unsigned long lon_deg = pos->lon / 100; // Extract the degrees
    pos->lon -= (float)lon_deg * 100.0;     // Subtract the degrees leaving only the minutes
    pos->lon /= 60.0;                       // Convert minutes into degrees
    pos->lon += (float)lon_deg;             // Finally add the degrees back on again
  }
  else
  {
    pos->lon = 0.0;
  }
  ptr = search + 1;

  // Find longitude hemishpere
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search == ptr + 1))
  {
    if (*ptr == 'W')    // Is the longitude West
      pos->lon *= -1.0; // Make lon negative
  }
  ptr = search + 1;

  // Find speed
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    spd->speed = atof(tempData); // Extract speed over ground in knots
    spd->speed *= 0.514444;      // Convert to m/s
  }
  else
  {
    spd->speed = 0.0;
  }
  ptr = search + 1;

  // Find course over ground
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    spd->cog = atof(tempData);
  }
  else
  {
    spd->cog = 0.0;
  }
  ptr = search + 1;

  // Find date
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    tTemp = atol(tempData);
    clk->date.day = tTemp / 10000;
    tTemp -= ((unsigned long)clk->date.day * 10000);
    clk->date.month = tTemp / 100;
    tTemp -= ((unsigned long)clk->date.month * 100);
    clk->date.year = tTemp;
  }
  else
  {
    clk->date.day = 0;
    clk->date.month = 0;
    clk->date.year = 0;
  }
  ptr = search + 1;

  // Find magnetic variation in degrees:
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search != ptr))
  {
    spd->magVar = atof(tempData);
  }
  else
  {
    spd->magVar = 0.0;
  }
  ptr = search + 1;

  // Find magnetic variation direction
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, ',');
  if ((search != nullptr) && (search == ptr + 1))
  {
    if (*ptr == 'W')       // Is the magnetic variation West
      spd->magVar *= -1.0; // Make magnetic variation negative
  }
  ptr = search + 1;

  // Find position system mode
  // Possible values for posMode: N = No fix, E = Estimated/Dead reckoning fix, A = Autonomous GNSS fix,
  //                              D = Differential GNSS fix, F = RTK float, R = RTK fixed
  search = readDataUntil(tempData, TEMP_NMEA_DATA_SIZE, ptr, '*');
  if ((search != nullptr) && (search = ptr + 1))
  {
    pos->mode = *ptr;
  }
  else
  {
    pos->mode = 'X';
  }
  ptr = search + 1;

  if (pos->status == 'A')
  {
    return true;
  }
  return false;
}