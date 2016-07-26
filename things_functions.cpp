#include "spiffs_functions.h"
#include "things_functions.h"

TM1637Display display(CLK, DIO);


// ------- Curent variables ----------
long  range = 2; // este es el rango por el que se disparará la salida 2 y pasa a estado lógico 1
long  lastValue; // contiene el valor de la última medición que disparó a lógico 1, la salida 2
int   cycle = 0; // 1=alto 0=bajo
int   cycleChange = 0;
int   maxVoltage;
int   minVoltage = 1023;
long  contadorvisualizacion;
long  cycleCounter;
bool  continuar;
const unsigned long sampleTime = 1000000UL;                           // sample over 100ms, it is an exact number of cycles for both 50Hz and 60Hz mains
const unsigned long numSamples = 600UL;                               // choose the number of samples to divide sampleTime exactly, but low enough for the ADC to keep up
const unsigned long sampleInterval = sampleTime / numSamples;
long adc_zero;
unsigned long currentAcc;
unsigned int count;
unsigned long prevMicros;
long startMicros;
float dwh = 0;
// ---------------------------------------

void initThings(unsigned long ntp_timer){
  display.setBrightness(0x0f);
  pinMode(sensorIn, INPUT);
  pinMode(switchOut, OUTPUT);
  //digitalWrite(switchOut, HIGH);
  adc_zero = determineVQ(sensorIn); //Quiscent output voltage - the average voltage ACS712 shows with no load (0 A)
  startMicros = micros();
  currentAcc = 0;
  count = 0;
  display.setBrightness(0x0f);
  prevMicros = micros() - sampleInterval ;
}

void processThings(Thing (*ptrThings)[THINGS_LEN], Recipe (*ptrRecipes)[RECIPES_LEN], long nodeId, unsigned long ntp_timer){
  long sensorValue = 0; //analogRead(sensorIn);
  unsigned long curr_time = millis()/1000+ntp_timer;
  digitalWrite(switchOut, (*ptrThings)[0].value);
  (*ptrThings)[3].value = curr_time;
  (*ptrThings)[3].last_updated = curr_time;
  updateRecipes(ptrRecipes, nodeId, 3, curr_time, ntp_timer);
  if (micros() - startMicros > sampleTime) {// Displays to the serial port the results, after one second
    sensorValue = analogRead(sensorIn);    
    range = (2 + ((maxVoltage - minVoltage) / 5)); 
    maxVoltage = sensorValue; 
    minVoltage = sensorValue; 
    float rms = sqrt((float)currentAcc / (float)count) * (83.3333 / 1024.0); //75.7576
    float vrms = rms * 240;
    if (cycleCounter < 48 || cycleCounter > 52 || vrms < 300) vrms = 0;
    display.showNumberDec(round(vrms), false);
    (*ptrThings)[1].value = vrms;
    (*ptrThings)[1].last_updated = curr_time;
    dwh = dwh + (vrms / 3600);
    (*ptrThings)[2].value = dwh;    
    (*ptrThings)[2].last_updated = curr_time;
    //saveThingsToFile(ptrThings);
    cycleCounter = 0; 
    startMicros = micros();
    currentAcc = 0;
    count = 0;
    prevMicros = micros() - sampleInterval ;
  }
  if (micros() - prevMicros >= sampleInterval){  
    sensorValue = analogRead(sensorIn);
    
    long adc_raw = sensorValue - adc_zero;    
    currentAcc += (unsigned long)(adc_raw * adc_raw);
    ++count;
    prevMicros += sampleInterval;
  
  
    if (sensorValue >= ( lastValue + range) ) {
      lastValue = sensorValue;  
      cycle = 1;
      if (sensorValue > maxVoltage) {
        maxVoltage = sensorValue;
      }
    }
    if (sensorValue <= ( lastValue - range)) {
      lastValue = sensorValue; 
      cycle = 0;
      if (sensorValue < minVoltage) {
        minVoltage = sensorValue; 
      }
    }
    if (cycle == 1 && cycleChange == 0) {
      cycleChange = 1;
      cycleCounter++;
    }
    if (cycle == 0 && cycleChange == 1) {
      cycleChange = 0;
    }
  }    
}

// Auxiliary functions for measuring voltage and current zero point
int determineVQ(int PIN) {
  DBG_OUTPUT_PORT.print("Estemating voltage zero point: ");
  long VQ = 0;
  for (int i = 0; i < 5000; i++) {
    VQ += analogRead(PIN);
    delay(1);//depends on sampling (on filter capacitor), can be 1/80000 (80kHz) max.
  }
  VQ /= 5000;
  DBG_OUTPUT_PORT.print(map(VQ, 0, 1023, 0, 5000)); Serial.println(" mV");
  return int(VQ);
}


