#include <Arduino.h>

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <WiFiUDP.h>
#include <Hash.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <Hash.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <TM1637Display.h>
//#include "ESPAsyncUDP.h"
#include "time_ntp.h"
#include "spiffs_functions.h"
#include "things_functions.h"

#define CHIP_ID system_get_chip_id()
#define DBG_OUTPUT_PORT Serial

extern "C"
{
  #include "user_interface.h"  
}

char nodeName[128] = "SmartPlugEsp"; 
unsigned int localPort = 2390;    // local port to listen for UDP packets
static bool hasTime = false;

char packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming and outgoing packets

//AsyncUDP udp;
WiFiUDP Udp;  // A UDP instance to let us send and receive packets over UDP

unsigned long multicast_timer=millis()+5000;
// ntp timestamp
unsigned long ntp_timer=0;
int lastNodeIdx;
char sThings[THING_JSON_SIZE*THINGS_LEN];
char sNodes[NODE_JSON_SIZE*NODES_LEN];
char sRecipes[RECIPE_JSON_SIZE*RECIPES_LEN];

AsyncWebServer server(80);

AsyncWebSocket ws("/ws");

void multicast_status() 
{
  //DBG_OUTPUT_PORT.println("Multicast Status");
  String message = String(CHIP_ID) + ":" + String(nodeName);
  IPAddress ip = WiFi.localIP();
  ip[3] = 255;
  for( int idx = 0 ; idx < THINGS_LEN ; idx++ ){
    const Thing tmpThing = arrThings[idx];
    message += "&"; 
    message += tmpThing.id;
    message += ":" ;
    switch((int)tmpThing.type){
      case CLOCK:{
          message += epoch_to_string(millis()/1000+ntp_timer);
      }
      break;
      default:
        message += String(tmpThing.value);
      break;
    }
    if (tmpThing.id == 2 || tmpThing.id == 3){
      char buffer[JSON_OBJECT_SIZE(9)];
      StaticJsonBuffer<JSON_OBJECT_SIZE(9)> jsonBuffer;
      JsonObject &msg = jsonBuffer.createObject();
      msg["command"] = "thing_update";
      msg["nodeId"] = String(CHIP_ID);
      msg["nodeName"] = nodeName;
      msg["thingId"] = tmpThing.id;
      msg["thingName"] = tmpThing.name;
      msg["lastUpdate"] = millis()/1000+ntp_timer;
      msg["value"] = tmpThing.value;
      msg.printTo(buffer, sizeof(buffer));
      msg.printTo(DBG_OUTPUT_PORT);
      DBG_OUTPUT_PORT.print("\n");
      ws.textAll(buffer);
    }
  }
  Udp.beginPacket(ip, localPort); 
  Udp.print(message);
  Udp.endPacket();
//  udp.print(message);
//  DBG_OUTPUT_PORT.println(system_get_free_heap_size());
}

// WEB HANDLER IMPLEMENTATION
class SPIFFSEditor: public AsyncWebHandler {
  private:
    String _username;
    String _password;
    bool _uploadAuthenticated;
  public:
    SPIFFSEditor(String username=String(), String password=String()):_username(username),_password(password),_uploadAuthenticated(false){}
    bool canHandle(AsyncWebServerRequest *request){
      if(request->method() == HTTP_GET && request->url() == "/edit" && (SPIFFS.exists("/edit.htm") || SPIFFS.exists("/edit.htm.gz")))
        return true;
      else if(request->method() == HTTP_GET && request->url() == "/list")
        return true;
      else if(request->method() == HTTP_GET && (request->url().endsWith("/") || SPIFFS.exists(request->url()) || (!request->hasParam("download") && SPIFFS.exists(request->url()+".gz"))))
        return true;
      else if(request->method() == HTTP_POST && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_DELETE && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_PUT && request->url() == "/edit")
        return true;
      return false;
    }

    void handleRequest(AsyncWebServerRequest *request){
      if(_username.length() && (request->method() != HTTP_GET || request->url() == "/edit" || request->url() == "/list") && !request->authenticate(_username.c_str(),_password.c_str()))
        return request->requestAuthentication();

      if(request->method() == HTTP_GET && request->url() == "/edit"){
        request->send(SPIFFS, "/edit.htm");
      } else if(request->method() == HTTP_GET && request->url() == "/list"){
        if(request->hasParam("dir")){
          String path = request->getParam("dir")->value();
          Dir dir = SPIFFS.openDir(path);
          path = String();
          String output = "[";
          while(dir.next()){
            File entry = dir.openFile("r");
            if (output != "[") output += ',';
            bool isDir = false;
            output += "{\"type\":\"";
            output += (isDir)?"dir":"file";
            output += "\",\"name\":\"";
            output += String(entry.name()).substring(1);
            output += "\"}";
            entry.close();
          }
          output += "]";
          request->send(200, "text/json", output);
          output = String();
        }
        else
          request->send(400);
      } else if(request->method() == HTTP_GET){
        String path = request->url();
        if(path.endsWith("/"))
          path += "index.htm";
        request->send(SPIFFS, path, String(), request->hasParam("download"));
      } else if(request->method() == HTTP_DELETE){
        if(request->hasParam("path", true)){
          ESP.wdtDisable(); SPIFFS.remove(request->getParam("path", true)->value()); ESP.wdtEnable(10);
          request->send(200, "", "DELETE: "+request->getParam("path", true)->value());
        } else
          request->send(404);
      } else if(request->method() == HTTP_POST){
        if(request->hasParam("data", true, true) && SPIFFS.exists(request->getParam("data", true, true)->value()))
          request->send(200, "", "UPLOADED: "+request->getParam("data", true, true)->value());
        else
          request->send(500);
      } else if(request->method() == HTTP_PUT){
        if(request->hasParam("path", true)){
          String filename = request->getParam("path", true)->value();
          if(SPIFFS.exists(filename)){
            request->send(200);
          } else {
            File f = SPIFFS.open(filename, "w");
            if(f){
              f.write(0x00);
              f.close();
              request->send(200, "", "CREATE: "+filename);
            } else {
              request->send(500);
            }
          }
        } else
          request->send(400);
      }
    }

    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if(!index){
        if(!_username.length() || request->authenticate(_username.c_str(),_password.c_str()))
          _uploadAuthenticated = true;
        request->_tempFile = SPIFFS.open(filename, "w");
      }
      if(_uploadAuthenticated && request->_tempFile && len){
        ESP.wdtDisable(); request->_tempFile.write(data,len); ESP.wdtEnable(10);
      }
      if(_uploadAuthenticated && final)
        if(request->_tempFile) request->_tempFile.close();
    }
};


// SKETCH BEGIN
void onEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
  if(type == WS_EVT_CONNECT){
    os_printf("ws[%s][%u] connect\n", server->url(), client->id());
//    client->printf("Hello Client %u :)", client->id());
//    client->ping();
  } else if(type == WS_EVT_DISCONNECT){
    os_printf("ws[%s][%u] disconnect: %u\n", server->url(), client->id());
  } else if(type == WS_EVT_ERROR){
    os_printf("ws[%s][%u] error(%u): %s\n", server->url(), client->id(), *((uint16_t*)arg), (char*)data);
  } else if(type == WS_EVT_PONG){
    os_printf("ws[%s][%u] pong[%u]: %s\n", server->url(), client->id(), len, (len)?(char*)data:"");
  } else if(type == WS_EVT_DATA){
    AwsFrameInfo * info = (AwsFrameInfo*)arg;
    String msg = "";
    if(info->final && info->index == 0 && info->len == len){
      //the whole message is in a single frame and we got all of it's data
      os_printf("ws[%s][%u] %s-message[%llu]: ", server->url(), client->id(), (info->opcode == WS_TEXT)?"text":"binary", info->len);

      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      os_printf("%s\n",msg.c_str());

      if(info->opcode == WS_TEXT){
        StaticJsonBuffer<500> jsonCommandBuffer;
            JsonObject &response = jsonCommandBuffer.parseObject(msg.c_str());
            if (response.success()) {
              //StaticJsonBuffer<2500> jsonBuffer; 
              if (response["command"].as<String>().compareTo("request_nodes") == 0){
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
                char jNodes[NODE_JSON_SIZE*NODES_LEN];
                serializeNodes(&arrNodes, jNodes, NODE_JSON_SIZE*NODES_LEN);
                String sJson = "{\"command\":\"response_nodes\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"nodes\":"+jNodes+"}";
                client->text(sJson);
                DBG_OUTPUT_PORT.println(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("request_things") == 0){
                char jThings[THING_JSON_SIZE*THINGS_LEN];
                serializeThings(&arrThings, jThings, THING_JSON_SIZE*THINGS_LEN);
                String sJson = "{\"command\":\"response_things\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"things\":"+jThings+"}";
                client->text(sJson);
                DBG_OUTPUT_PORT.println(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("request_recipes") == 0){
                char jRecipes[RECIPE_JSON_SIZE*RECIPES_LEN];
                serializeRecipes(&arrRecipes, jRecipes, RECIPE_JSON_SIZE*RECIPES_LEN);
                String sJson = "{\"command\":\"response_recipes\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"recipes\":"+jRecipes+"}";
                client->text(sJson);
                DBG_OUTPUT_PORT.println(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("save_thing") == 0){
                StaticJsonBuffer<200> jsonBuffer;
                JsonObject &thing = jsonBuffer.parseObject(response["thing"].as<String>());
                int index = thing["id"].as<int>() - 1;
                //aThings[index].name = thing["name"].as<String>().c_str();
                arrThings[index].type = thing["type"].as<int>();
                arrThings[index].value = thing["value"].as<float>();
                arrThings[index].override = thing["override"].as<bool>();
                arrThings[index].last_updated = millis()/1000+ntp_timer;
                bool saved = saveThingsToFile(&arrThings);
                DBG_OUTPUT_PORT.println("Updated thing value.");
                String sJson = "{\"command\":\"response_save_thing\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"success\":"+(saved ? "true" : "false")+"}";
                client->text(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("save_recipe") == 0){
                StaticJsonBuffer<200> jsonBuffer;
                JsonObject &recipe = jsonBuffer.parseObject(response["recipe"].as<String>());
                int index = recipe["id"].as<int>() - 1;
                arrRecipes[index].name = recipe["name"].as<String>().c_str();
                arrRecipes[index].sourceNodeId = recipe["sourceNodeId"].as<long>();
                arrRecipes[index].sourceThingId = recipe["sourceThingId"].as<int>();
                arrRecipes[index].sourceValue = recipe["sourceValue"].as<float>();
                arrRecipes[index].relation = recipe["relation"].as<int>();
                arrRecipes[index].localThingId = recipe["localThingId"].as<int>();
                arrRecipes[index].targetValue = recipe["targetValue"].as<float>();
                arrRecipes[index].localValue = recipe["localValue"].as<float>();
                arrRecipes[index].last_updated = millis()/1000+ntp_timer;
                bool saved = saveRecipesToFile(&arrRecipes);
                DBG_OUTPUT_PORT.println("Updated recipe value.");
                String sJson = "{\"command\":\"response_save_recipe\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"success\":"+(saved ? "true" : "false")+"}";
                client->text(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else {
                StaticJsonBuffer<200> jsonBuffer;
                JsonObject &msg = jsonBuffer.createObject();
                char buffer[200];
                msg["command"] = "response_default";
                msg["nodeId"] = CHIP_ID;
                msg["nodeName"] = nodeName;
                msg.printTo(buffer, sizeof(buffer));
                client->text(buffer);
              }
            }            
            Udp.begin(localPort);
      } else
        client->binary("I got your binary message");
    } else {
      //message is comprised of multiple frames or the frame is split into multiple packets
      if(info->index == 0){
        if(info->num == 0)
          os_printf("ws[%s][%u] %s-message start\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
        os_printf("ws[%s][%u] frame[%u] start[%llu]\n", server->url(), client->id(), info->num, info->len);
      }

      os_printf("ws[%s][%u] frame[%u] %s[%llu - %llu]: ", server->url(), client->id(), info->num, (info->message_opcode == WS_TEXT)?"text":"binary", info->index, info->index + len);
  
      if(info->opcode == WS_TEXT){
        for(size_t i=0; i < info->len; i++) {
          msg += (char) data[i];
        }
      } else {
        char buff[3];
        for(size_t i=0; i < info->len; i++) {
          sprintf(buff, "%02x ", (uint8_t) data[i]);
          msg += buff ;
        }
      }
      os_printf("%s\n",msg.c_str());

      if((info->index + len) == info->len){
        os_printf("ws[%s][%u] frame[%u] end[%llu]\n", server->url(), client->id(), info->num, info->len);
        if(info->final){
          os_printf("ws[%s][%u] %s-message end\n", server->url(), client->id(), (info->message_opcode == WS_TEXT)?"text":"binary");
          if(info->message_opcode == WS_TEXT){
            //client->text("I got your text message");
            StaticJsonBuffer<500> jsonCommandBuffer;
            JsonObject &response = jsonCommandBuffer.parseObject(msg.c_str());
            if (response.success()) {
              //StaticJsonBuffer<2500> jsonBuffer; 
              if (response["command"].as<String>().compareTo("request_nodes") == 0){
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
                char* jNodes;
                serializeNodes(&arrNodes, jNodes, 1500);
                String sJson = "{\"command\":\"response_nodes\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"nodes\":"+jNodes+"}";
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("request_things") == 0){
                char jThings[THING_JSON_SIZE*THINGS_LEN];
                serializeThings(&arrThings, jThings, THING_JSON_SIZE*THINGS_LEN);
                String sJson = "{\"command\":\"response_things\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"things\":"+jThings+"}";
                //webSocket.sendTXT(num, sJson);
                client->text(sJson);
                DBG_OUTPUT_PORT.println(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("request_recipes") == 0){
              } else if (response["command"].as<String>().compareTo("request_watts") == 0){
              } else if (response["command"].as<String>().compareTo("save_thing") == 0){
                StaticJsonBuffer<200> jsonBuffer;
                JsonObject &thing = jsonBuffer.parseObject(response["thing"].as<String>());
                int index = thing["id"].as<int>() - 1;
                //aThings[index].name = thing["name"].as<String>().c_str();
                arrThings[index].type = thing["type"].as<int>();
                arrThings[index].value = thing["value"].as<float>();
                arrThings[index].override = thing["override"].as<bool>();
                arrThings[index].last_updated = millis()/1000+ntp_timer;
                bool saved = saveThingsToFile(&arrThings);//, thing["id"].as<int>(), thing["name"].as<String>().c_str(), thing["type"].as<int>(), thing["value"].as<String>().c_str(), thing["override"].as<bool>(), ntp_timer);
                DBG_OUTPUT_PORT.println("Updated thing value.");
                String sJson = "{\"command\":\"response_save_thing\",\"nodeId\":"+String(CHIP_ID)+",\"nodeName\":\""+nodeName+"\",\"success\":"+(saved ? "true" : "false")+"}";
                //webSocket.sendTXT(num, sJson);
                client->text(sJson);
                DBG_OUTPUT_PORT.println(system_get_free_heap_size());
              } else if (response["command"].as<String>().compareTo("save_recipe") == 0){
              } else {
                StaticJsonBuffer<200> jsonBuffer;
                JsonObject &msg = jsonBuffer.createObject();
                char buffer[200];
                msg["command"] = "response_default";
                msg["nodeId"] = CHIP_ID;
                msg["nodeName"] = nodeName;
                msg.printTo(buffer, sizeof(buffer));
                //webSocket.sendTXT(num, buffer);
                client->text(buffer);
              }
            }            
            Udp.begin(localPort);
          } else
            client->binary("I got your binary message");
        }
      }
    }
  }
}


const char* ssid = "raliand";
const char* password = "rakvi321";
const char* http_username = "admin";
const char* http_password = "admin";


extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

//Use the internal hardware buffer
static void _u0_putc(char c){
  while(((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}

void initSerial(){
  DBG_OUTPUT_PORT.begin(115200);
  ets_install_putc1((void *) &_u0_putc);
  system_set_os_print(1);
}

void setup(){
  initSerial();
  SPIFFS.begin();
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    DBG_OUTPUT_PORT.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }  
  
  DBG_OUTPUT_PORT.println("connected...yeey :)");
  
  ArduinoOTA.begin();
  //DBG_OUTPUT_PORT.printf("format start\n"); SPIFFS.format();  DBG_OUTPUT_PORT.printf("format end\n");

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  
  server.serveStatic("/fs", SPIFFS, "/");

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.onNotFound([](AsyncWebServerRequest *request){
    os_printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      os_printf("GET");
    else if(request->method() == HTTP_POST)
      os_printf("POST");
    else if(request->method() == HTTP_DELETE)
      os_printf("DELETE");
    else if(request->method() == HTTP_PUT)
      os_printf("PUT");
    else if(request->method() == HTTP_PATCH)
      os_printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      os_printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      os_printf("OPTIONS");
    else
      os_printf("UNKNOWN");
    os_printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      os_printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      os_printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      os_printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        os_printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        os_printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        os_printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      os_printf("UploadStart: %s\n", filename.c_str());
    os_printf("%s", (const char*)data);
    if(final)
      os_printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      os_printf("BodyStart: %u\n", total);
    os_printf("%s", (const char*)data);
    if(index + len == total)
      os_printf("BodyEnd: %u\n", total);
  });
  server.begin();
  
  MDNS.begin(nodeName);
  DBG_OUTPUT_PORT.print("Open http://");
  DBG_OUTPUT_PORT.print(nodeName);
  DBG_OUTPUT_PORT.println(".local/edit to see the file browser");
  
  ntp_timer = getNTPTimestamp();
  ntp_timer -= millis()/1000;  // keep distance to millis() counter
//    deleteAll();
//    delay(100);
  if(!loadFromFileNew("/nodes.json",sNodes,NODE_JSON_SIZE*NODES_LEN)){
    arrNodes[0].id = CHIP_ID;
    arrNodes[0].name = nodeName;
    saveNodesToFile(&arrNodes);
    lastNodeIdx = 1;
    delay(100);
  }
  DBG_OUTPUT_PORT.print("sNodes ");
  DBG_OUTPUT_PORT.println(sNodes);
  lastNodeIdx = deserializeNodes(&arrNodes,sNodes);
  delay(100);

  if(!loadFromFileNew("/things.json",sThings,THING_JSON_SIZE*THINGS_LEN)){
    arrThings[0].id = 1;
    arrThings[0].name = "Plug";
    arrThings[0].type = SWITCH;
    arrThings[0].value = OFF;
    arrThings[0].override = false;
    arrThings[0].last_updated = millis()/1000+ntp_timer;
    arrThings[1].id = 2;
    arrThings[1].name = "Wats";
    arrThings[1].type = GENERIC;
    arrThings[1].value = 0;
    arrThings[1].override = false;
    arrThings[1].last_updated = millis()/1000+ntp_timer;
    arrThings[2].id = 3;
    arrThings[2].name = "Daily Wats/Hour";
    arrThings[2].type = GENERIC;
    arrThings[2].value = 1;
    arrThings[2].override = false;
    arrThings[2].last_updated = millis()/1000+ntp_timer;
    arrThings[3].id = 4;
    arrThings[3].name = "Clock";
    arrThings[3].type = CLOCK;
    arrThings[3].value = millis()/1000+ntp_timer;
    arrThings[3].override = false;
    arrThings[3].last_updated = millis()/1000+ntp_timer;
    saveThingsToFile(&arrThings);
  }
  DBG_OUTPUT_PORT.print("sThings ");
  DBG_OUTPUT_PORT.println(sThings);
  deserializeThings(&arrThings,sThings);
  delay(100);
  
//    sRecipes = loadFromFile("/recipes.json");
//    DBG_OUTPUT_PORT.print("sRecipes ");
//    DBG_OUTPUT_PORT.println(sRecipes);
//    delay(100);
  
  initThings(ntp_timer);
  
  Udp.begin(localPort);

//  if(udp.listenMulticast(IPAddress(239,1,2,3), 1234)) {
//        DBG_OUTPUT_PORT.print("UDP Listening on IP: ");
//        DBG_OUTPUT_PORT.println(WiFi.localIP());
//        udp.onPacket([](AsyncUDPPacket packet) {
//            DBG_OUTPUT_PORT.print("UDP Packet Type: ");
//            DBG_OUTPUT_PORT.print(packet.isBroadcast()?"Broadcast":packet.isMulticast()?"Multicast":"Unicast");
//            DBG_OUTPUT_PORT.print(", From: ");
//            DBG_OUTPUT_PORT.print(packet.remoteIP());
//            DBG_OUTPUT_PORT.print(":");
//            DBG_OUTPUT_PORT.print(packet.remotePort());
//            DBG_OUTPUT_PORT.print(", To: ");
//            DBG_OUTPUT_PORT.print(packet.localIP());
//            DBG_OUTPUT_PORT.print(":");
//            DBG_OUTPUT_PORT.print(packet.localPort());
//            DBG_OUTPUT_PORT.print(", Length: ");
//            DBG_OUTPUT_PORT.print(packet.length());
//            DBG_OUTPUT_PORT.print(", Data: ");
//            DBG_OUTPUT_PORT.write(packet.data(), packet.length());
//            DBG_OUTPUT_PORT.println();
//            //reply to the client
//            packet.printf("Got %u bytes of data", packet.length());
//            char* command = strtok((char*)packet.data(), "&");
//            if(command != 0){
//                char* separator = strchr(command, ':');
//                if (separator != 0){
//                    // Actually split the string in 2: replace ':' with 0
//                    *separator = 0;
//                    String rNodeId = String(command);
//                    ++separator;
//                    String rNodeName = String(separator);
//                    arrNodes[lastNodeIdx].id = rNodeId.toInt();
//                    arrNodes[lastNodeIdx].name = rNodeName.c_str();
//                    //saveNodesToFile(&arrNodes);
//                    lastNodeIdx++;
//                    DBG_OUTPUT_PORT.println("");
//                    command = strtok(0, "&");
//                    while (command != 0){
//                        char* separator1 = strchr(command, ':');
//                        if (separator != 0){
//                              // Actually split the string in 2: replace ':' with 0
//                              *separator1 = 0;
//                              int rThingId = atoi(command);
//                              ++separator1;
//                              String rThingValue = String(separator1);
//                              updateRecipes(&arrRecipes, atol(rNodeId.c_str()),rThingId,atof(rThingValue.c_str()), ntp_timer);
//                              saveRecipesToFile(&arrRecipes);
//                              // Do something with servoId and position  vb
//                        }
//                      // Find the next command in input string
//                      command = strtok(0, "&");
//                    }
//                }
//            }
//        });
//        //Send multicast
//        //udp.print("Hello!");
//    }
  DBG_OUTPUT_PORT.println("HTTP server started");
}

void loop(){
  ArduinoOTA.handle();
  int noBytes = Udp.parsePacket();
  if ( noBytes ) {
    DBG_OUTPUT_PORT.print(epoch_to_string(millis()/1000+ntp_timer));
    DBG_OUTPUT_PORT.print(":Packet of ");
    DBG_OUTPUT_PORT.print(noBytes);
    DBG_OUTPUT_PORT.print(" received from ");
    DBG_OUTPUT_PORT.print(Udp.remoteIP());
    DBG_OUTPUT_PORT.print(":");
    DBG_OUTPUT_PORT.println(String(Udp.remotePort()));
    // We've received a packet, read the data from it
    Udp.read(packetBuffer,noBytes); // read the packet into the buffer
    char* command = strtok(packetBuffer, "&");
    if(command != 0){
      char* separator = strchr(command, ':');
      if (separator != 0)
      {
        // Actually split the string in 2: replace ':' with 0
        *separator = 0;
        String rNodeId = String(command);
        ++separator;
        String rNodeName = String(separator);
        arrNodes[lastNodeIdx].id = rNodeId.toInt();
        arrNodes[lastNodeIdx].name = rNodeName.c_str();
        //saveNodesToFile(&arrNodes);
        lastNodeIdx++;
        DBG_OUTPUT_PORT.println("");
        command = strtok(0, "&");
        while (command != 0){
          char* separator1 = strchr(command, ':');
          if (separator != 0)
          {
              // Actually split the string in 2: replace ':' with 0
              *separator1 = 0;
              int rThingId = atoi(command);
              ++separator1;
              String rThingValue = String(separator1);
              updateRecipes(&arrRecipes, atol(rNodeId.c_str()),rThingId,atof(rThingValue.c_str()), ntp_timer);
              saveRecipesToFile(&arrRecipes);
              // Do something with servoId and position  vb
          }
          // Find the next command in input string
          command = strtok(0, "&");
        }
      }
    }
  } // end if packet received
  processRecipes(&arrThings,&arrRecipes);
  processThings(&arrThings,&arrRecipes,CHIP_ID, ntp_timer);
  if(multicast_timer < millis()){
    multicast_status();
    multicast_timer = millis()+5000;      
  }
}
