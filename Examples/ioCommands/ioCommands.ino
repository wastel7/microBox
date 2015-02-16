#include <microBox.h>

char historyBuf[100];
char hostname[] = "ioBash";

PARAM_ENTRY Params[]=
{
  {"hostname", hostname, PARTYPE_STRING | PARTYPE_RW, sizeof(hostname), NULL, NULL, 0}, 
  {NULL, NULL}
};

void getMillis(char **param, uint8_t parCnt)
{
  Serial.println(millis());
}

void freeRam(char **param, uint8_t parCnt) 
{
  extern int __heap_start, *__brkval;
  int v;
  Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
}

void writePin(char **param, uint8_t parCnt)
{
    uint8_t pin, pinval;
    if(parCnt == 2)
    {
        pin = atoi(param[0]);
        pinval = atoi(param[1]);
        digitalWrite(pin, pinval);
    }
    else
        Serial.println(F("Usage: writepin pinNum pinvalue"));
}

void readPin(char **param, uint8_t parCnt)
{
    uint8_t pin;
    if(parCnt == 1)
    {
        pin = atoi(param[0]);
        Serial.println(digitalRead(pin));
    }
    else
        Serial.println(F("Usage: readpin pinNum"));
}

void setPinDirection(char **param, uint8_t parCnt)
{
    uint8_t pin, pindir;
    if(parCnt == 2)
    {
        pin = atoi(param[0]);
        if(strcmp(param[1], "out") == 0)
            pindir = OUTPUT;
        else
            pindir = INPUT;

        pinMode(pin, pindir);
    }
    else
        Serial.println(F("Usage: setpindir pinNum in|out"));
}

void readAnalogPin(char **param, uint8_t parCnt)
{
    uint8_t pin;
    if(parCnt == 1)
    {
        pin = atoi(param[0]);
        Serial.println(analogRead(pin));
    }
    else
        Serial.println(F("Usage: readanalog pinNum"));
}

void writeAnalogPin(char **param, uint8_t parCnt)
{
    uint8_t pin, pinval;
    if(parCnt == 2)
    {
        pin = atoi(param[0]);
        pinval = atoi(param[1]);
        analogWrite(pin, pinval);
    }
    else
        Serial.println(F("Usage: writeanalog pinNum pinvalue"));
}

void setup()
{
  Serial.begin(115200);
  
  microbox.begin(&Params[0], hostname, true, historyBuf, 100);
  microbox.AddCommand("free", freeRam);
  microbox.AddCommand("millis", getMillis);
  microbox.AddCommand("readanalog", readAnalogPin);
  microbox.AddCommand("readpin", readPin);
  microbox.AddCommand("setpindir", setPinDirection);
  microbox.AddCommand("writeanalog", writeAnalogPin);
  microbox.AddCommand("writepin", writePin);
}

void loop()
{ 
  microbox.cmdParser();
}
