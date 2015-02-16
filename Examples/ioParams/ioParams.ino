#include <microBox.h>

#define DIGI_PARAMS 14
#define ANALOG_PARAMS 6

char historyBuf[100];
char hostname[] = "ioParams";

uint16_t digiPins[DIGI_PARAMS];
uint16_t analogPins[ANALOG_PARAMS];

PARAM_ENTRY Params[]=
{
    {"digi_0", &digiPins[0], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 0},
    {"digi_1", &digiPins[1], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 1},
    {"digi_2", &digiPins[2], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 2},
    {"digi_3", &digiPins[3], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 3},
    {"digi_4", &digiPins[4], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 4},
    {"digi_5", &digiPins[5], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 5},
    {"digi_6", &digiPins[6], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 6},
    {"digi_7", &digiPins[7], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 7},
    {"digi_8", &digiPins[8], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 8},
    {"digi_9", &digiPins[9], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 9},
    {"digi_10", &digiPins[10], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 10},
    {"digi_11", &digiPins[11], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 11},
    {"digi_12", &digiPins[12], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 12},
    {"digi_13", &digiPins[13], PARTYPE_INT | PARTYPE_RW, 0, SetDigiPin, GetDigiPin, 13},
    {"ana_0", &analogPins[0], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 0},
    {"ana_1", &analogPins[1], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 1},
    {"ana_2", &analogPins[2], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 2},
    {"ana_3", &analogPins[3], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 3},
    {"ana_4", &analogPins[4], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 4},
    {"ana_5", &analogPins[5], PARTYPE_INT | PARTYPE_RO, 0, NULL, GetAnalogPin, 5},
    {"hostname", hostname, PARTYPE_STRING | PARTYPE_RW, sizeof(hostname), NULL, NULL, 0},
    {NULL, NULL}
};

void SetDigiPin(uint8_t id)
{
    pinMode(id, OUTPUT);
    digitalWrite(id, digiPins[id]);
}

void GetDigiPin(uint8_t id)
{
    pinMode(id, INPUT);
    digiPins[id] = digitalRead(id);
}

void GetAnalogPin(uint8_t id)
{
    analogPins[id] = analogRead(id);
}

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

void setup()
{
    Serial.begin(115200);

    microbox.begin(&Params[0], hostname, true, historyBuf, 100);
    microbox.AddCommand("free", freeRam);
    microbox.AddCommand("millis", getMillis);
}

void loop()
{ 
    microbox.cmdParser();
}
