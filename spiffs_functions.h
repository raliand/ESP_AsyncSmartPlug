/*-----------------------------------------------------------
NTP & time routines for ESP8266 
    for ESP8266 adapted Arduino IDE

by Stefan Thesen 05/2015 - free for anyone

code for ntp adopted from Michael Margolis
code for time conversion based on http://stackoverflow.com/
-----------------------------------------------------------*/

// note: all timing relates to 01.01.2000

#ifndef _DEF_SPIFS_FUNC_
#define _DEF_SPIFS_FUNC_

//Relations
#define EQUALS            0
#define NOT_EQUALS        1
#define BIGGER_THAN       2
#define SMALLER_THAN      3
#define NOT_BIGGER_THAN   4
#define NOT_SMALLER_THAN  5

//Thing types
#define LONG    0
#define SWITCH  1 //short
#define TEMP    2 //float
#define HUMID   3 //float
#define PRESS   4 //float
#define PERCENT 5 //float
#define GENERIC 6 //float
#define CLOCK   7 //unsigned long

//values
#define ON    1
#define OFF    0

#define DBG_OUTPUT_PORT Serial

#define RECIPE_JSON_SIZE (JSON_OBJECT_SIZE(11))
#define THING_JSON_SIZE (JSON_OBJECT_SIZE(7))
#define NODE_JSON_SIZE (JSON_OBJECT_SIZE(3))
#define RECIPES_LEN 20
#define THINGS_LEN 4
#define NODES_LEN 20

struct Recipe {
   int id;
   const char* name;
   long sourceNodeId;
   int sourceThingId;
   float sourceValue;
   int relation;
   int localThingId;
   float targetValue;
   float localValue;
   unsigned long last_updated;
};

struct Thing {
   int id;
   const char* name;
   int type;
   float value;
   bool override = false;
   unsigned long last_updated;
};

struct Node {
   long id;
   const char* name;
};

static Recipe arrRecipes[RECIPES_LEN];

static Thing arrThings[THINGS_LEN];

static Node arrNodes[NODES_LEN];

#include <FS.h>
#include <ArduinoJson.h>
extern "C"
{
  #include "user_interface.h"  
}


//char *loadFromFile(const char* cFileName);
bool loadFromFileNew(const char* cFileName, char* json, size_t maxSize);
void serializeNodes(const Node (*ptrNodes)[NODES_LEN], char* json, size_t maxSize);
void serializeThings(const Thing (*ptrThings)[THINGS_LEN], char* json, size_t maxSize);
void serializeRecipes(const Recipe (*ptrRecipes)[RECIPES_LEN], char* json, size_t maxSize);
bool deserializeThings(Thing (*ptrThings)[THINGS_LEN], char* json);
int deserializeNodes(Node (*ptrNodes)[NODES_LEN], char* json);
bool deserializeRecipes(Recipe (*ptrRecipes)[RECIPES_LEN], char* json);
volatile bool saveToFile(JsonArray& jArr, const char* cFileName);
volatile bool saveThingsToFile(const Thing (*ptrThings)[THINGS_LEN]);
bool saveNodesToFile(const Node (*ptrNodes)[NODES_LEN]);
bool saveRecipesToFile(const Recipe (*ptrRecipes)[RECIPES_LEN]);
void processRecipes(Thing (*ptrThings)[THINGS_LEN], Recipe (*ptrRecipes)[RECIPES_LEN]);
void updateRecipes(Recipe (*ptrRecipes)[RECIPES_LEN], long rNodeId, int rThingId, float rThingValue, unsigned long ntp_timer);
bool deleteNodes(void);
bool deleteThings(void);
bool deleteRecipes(void);
void deleteAll(void);

#endif
