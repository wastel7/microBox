#include <Wire.h>
#include <PID_v1.h>
#include <PID_AutoTune_v0.h>
#include <TimerOne.h>
#include <microBox.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

char historyBuf[100];
char hostname[] = "incubatDuino";

uint8_t buf[2];
double temp = 0;
double tempFilter = 0;
uint16_t filterCount = 25;
uint16_t adval;
uint16_t filterPos = 0;
unsigned long lastADRead = 0;
double maxDiv = 0;

uint16_t pidIntervall = 5000;
uint16_t adIntervall = 100;

double Kp=23.59674263, Ki=0.02554589, Kd=5449.07763671;
double Setpoint, Output;
double noiseband = 0.1;
uint16_t lookback = 60;

uint16_t atuneMode = 0;

PID incuPID(&temp, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);
PID_ATune aTune(&temp, &Output);

PARAM_ENTRY Params[]=
{
    {"ad_filtercnt", &filterCount, PARTYPE_INT | PARTYPE_RW, 0, NULL, NULL, 0},
    {"ad_intervall", &adIntervall, PARTYPE_INT | PARTYPE_RW, 0, NULL, NULL, 0},
    {"atune_lookback", &lookback, PARTYPE_INT | PARTYPE_RW, 0, NULL, NULL, 0},
    {"atune_noiseband", &noiseband, PARTYPE_DOUBLE | PARTYPE_RW, 0, NULL, NULL, 0},
    {"atune_status", &atuneMode, PARTYPE_INT | PARTYPE_RO, 0, NULL, NULL, 0},
    {"hostname", hostname, PARTYPE_STRING | PARTYPE_RW, sizeof(hostname), NULL, NULL, 0},
    {"max_div", &maxDiv, PARTYPE_DOUBLE | PARTYPE_RW, 0, NULL, NULL, 0},
    {"pid_intervall", &pidIntervall, PARTYPE_INT | PARTYPE_RW, 0, PidSetIntervall, NULL, 0},
    {"pid_kp", &Kp, PARTYPE_DOUBLE | PARTYPE_RW, 0, PidSetParams, NULL, 0},
    {"pid_ki", &Ki, PARTYPE_DOUBLE | PARTYPE_RW, 0, PidSetParams, NULL, 0},
    {"pid_kd", &Kd, PARTYPE_DOUBLE | PARTYPE_RW, 0, PidSetParams, NULL, 0},
    {"power", &Output, PARTYPE_DOUBLE | PARTYPE_RO, 0, NULL, NULL, 0},
    {"temp_act", &temp, PARTYPE_DOUBLE | PARTYPE_RO, 0, NULL, NULL, 0},
    {"temp_setpoint", &Setpoint, PARTYPE_DOUBLE | PARTYPE_RW, 0, NULL, NULL, 0},
    {NULL, NULL}
};

#define MAX_POWER_PORTS 1

typedef struct
{
    byte outPort;
    byte bPower;    // 0-100%
}TPowerPort;

TPowerPort gPowerPorts[MAX_POWER_PORTS];

byte percentCnt = 0;
byte percentCntDiv = 0;

void Timer1cb()
{
    byte i;

    percentCntDiv++;
    if(percentCntDiv == 5)
    {
        percentCntDiv = 0;
        percentCnt++;

        if(percentCnt == 100)
        {
            percentCnt = 0;

            for (i=0; i<MAX_POWER_PORTS; i++)
            {
                if(gPowerPorts[i].bPower == 255 || gPowerPorts[i].bPower == 0)
                {
                    digitalWrite(gPowerPorts[i].outPort, 0);                // turn output off
                }
                else
                {
                    digitalWrite(gPowerPorts[i].outPort, 1);                // turn output on
                }
            }
        }
        else
        {
            for (i=0; i<MAX_POWER_PORTS; i++)
            {
                if(percentCnt == gPowerPorts[i].bPower)
                {
                    digitalWrite(gPowerPorts[i].outPort, 0);                // turn output off
                }
            }
        }
    }
}

void DoATune(char **param, uint8_t parCnt)
{
    atuneMode = 1;
    Output=50;
    aTune.SetOutputStep(50);
    aTune.SetNoiseBand(noiseband);
    aTune.SetLookbackSec(lookback);

    Serial.println(F("Autotune started with setpoint=temp_act"));
}

void PidSetIntervall(uint8_t id)
{
    incuPID.SetSampleTime(pidIntervall);
}

void PidSetParams(uint8_t id)
{
    incuPID.SetTunings(Kp, Ki, Kd);
}

void freeRam(char **param, uint8_t parCnt) 
{
    extern int __heap_start, *__brkval;
    int v;
    Serial.println((int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval));
}

void reset(char **param, uint8_t parCnt)
{
    wdt_enable(WDTO_15MS);
    while(1)
    {
    }
}

void setup()
{
    uint8_t i;
    Setpoint = 40.0;

    atuneMode = 0;
    Serial.begin(115200);
    Wire.begin(); // join i2c bus (address optional for master)
    incuPID.SetMode(AUTOMATIC);
    incuPID.SetTunings(Kp, Ki, Kd);
    incuPID.SetOutputLimits(0, 100);
    incuPID.SetSampleTime(pidIntervall);
    aTune.SetControlType(1);

    pinMode(3, OUTPUT);
    for (i=0; i<MAX_POWER_PORTS; i++)
    {
        gPowerPorts[i].bPower = 0;
        gPowerPorts[i].outPort = 3;
    }

    Timer1.initialize(20000/5);         // initialize timer1, and set a 40 msecond period
    Timer1.attachInterrupt(Timer1cb);  // attaches callback() as a timer overflow interrupt

    microbox.begin(&Params[0], hostname, true, historyBuf, 100);
    microbox.AddCommand("atune", DoATune);
    microbox.AddCommand("free", freeRam);
    microbox.AddCommand("reset", reset);
}

void CalcMaxDiv()
{
    double d;

    d = Setpoint - temp;
    d = abs(d);
    if(d > maxDiv)
        maxDiv = d;
}

void ADRead()
{
    Wire.requestFrom(0x78, 2);    // request 6 bytes from slave device #2

    if(Wire.available() > 1)    // slave may send less than requested
    {
        buf[0] = Wire.read();    // receive a byte as character
        buf[1] = Wire.read();    // receive a byte as character

        adval = (buf[1]&0xF8) | buf[0]<<8;
        tempFilter += (((double)adval)/128) - 32.0;
        filterPos++;
        if(filterCount == filterPos)
        {
            temp = tempFilter/filterCount;
            tempFilter = 0;
            filterPos = 0;
            CalcMaxDiv();
        }
    }
    else
    {
        if(Wire.available())
            Wire.read();
    }
}

void loop()
{ 
    if(millis() - lastADRead >= adIntervall)
    {
        ADRead();
        lastADRead = millis();
    }

    if(atuneMode)
    {
        if(aTune.Runtime() != 0)
        {
            atuneMode = 0;

            Kp = aTune.GetKp();
            Ki = aTune.GetKi();
            Kd = aTune.GetKd();
            incuPID.SetTunings(Kp,Ki,Kd);
            incuPID.SetMode(AUTOMATIC);
            Serial.println(F("Autotune finished"));
        }
    }
    else
    {
        incuPID.Compute();
    }
    gPowerPorts[0].bPower = Output;

    microbox.cmdParser();
}
