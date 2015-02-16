/*
  microBox.h - Library for Linux-Shell like interface for Arduino.
  Created by Sebastian Duell, 06.02.2015.
  More info under http://sebastian-duell.de
  Released under GPLv3.
*/

#ifndef _BASHCMD_H_
#define _BASHCMD_H_

#include <Arduino.h>

#define MAX_CMD_NUM 20

#define MAX_CMD_BUF_SIZE 40
#define MAX_PATH_LEN 10

#define PARTYPE_INT    0x01
#define PARTYPE_DOUBLE 0x02
#define PARTYPE_STRING 0x04
#define PARTYPE_RW     0x10
#define PARTYPE_RO     0x00

#define ESC_STATE_NONE 0
#define ESC_STATE_START 1
#define ESC_STATE_CODE 2

#define TELNET_IAC 255
#define TELNET_WILL 251
#define TELNET_WONT 252
#define TELNET_DO 253
#define TELNET_DONT 254

#define TELNET_OPTION_ECHO 1
#define TELNET_OPTION_SGA 3

#define TELNET_STATE_NORMAL 0
#define TELNET_STATE_IAC 1
#define TELNET_STATE_WILL 2
#define TELNET_STATE_WONT 3
#define TELNET_STATE_DO 4
#define TELNET_STATE_DONT 5
#define TELNET_STATE_CLOSE 6

typedef struct
{
    const char *cmdName;
    void (*cmdFunc)(char **param, uint8_t parCnt);
}CMD_ENTRY;

typedef struct
{
    const char *paramName;
    void *pParam;
    uint8_t parType;
    uint8_t len;
    void (*setFunc)(uint8_t id);
    void (*getFunc)(uint8_t id);
    uint8_t id;
}PARAM_ENTRY;

class microBox
{
public:
    microBox();
    ~microBox();
    void begin(PARAM_ENTRY *pParams, const char* hostName, bool localEcho=true, char *histBuf=NULL, int historySize=0);
    void cmdParser();
    bool isTimeout(unsigned long *lastTime, unsigned long intervall);
    bool AddCommand(const char *cmdName, void (*cmdFunc)(char **param, uint8_t parCnt));

private:
    static void ListDirCB(char **pParam, uint8_t parCnt);
    static void ListLongCB(char **pParam, uint8_t parCnt);
    static void ChangeDirCB(char **pParam, uint8_t parCnt);
    static void EchoCB(char **pParam, uint8_t parCnt);
    static void CatCB(char** pParam, uint8_t parCnt);
    static void watchCB(char** pParam, uint8_t parCnt);
    static void watchcsvCB(char** pParam, uint8_t parCnt);
    static void LoadParCB(char **pParam, uint8_t parCnt);
    static void SaveParCB(char **pParam, uint8_t parCnt);

    void ListDir(char **pParam, uint8_t parCnt, bool listLong=false);
    void ChangeDir(char **pParam, uint8_t parCnt);
    void Echo(char **pParam, uint8_t parCnt);
    void Cat(char** pParam, uint8_t parCnt);
    void watch(char** pParam, uint8_t parCnt);
    void watchcsv(char** pParam, uint8_t parCnt);

private:
    void ShowPrompt();
    uint8_t ParseCmdParams(char *pParam);
    void ErrorDir(const __FlashStringHelper *cmd);
    char *GetDir(char *pParam, bool useFile);
    char *GetFile(char *pParam);
    void PrintParam(uint8_t idx);
    int8_t GetParamIdx(char* pParam, bool partStr = false, int8_t startIdx = 0);
    int8_t GetCmdIdx(char* pCmd, int8_t startIdx = 0);
    uint8_t Cat_int(char* pParam);
    void ListDirHlp(bool dir, bool rw = true, int len=4096);
    uint8_t ParCmp(uint8_t idx1, uint8_t idx2, bool cmd=false);
    void HandleTab();
    void HistoryUp();
    void HistoryDown();
    void HistoryPrintHlpr();
    void AddToHistory(char *buf);
    void ExecCommand();
    void handleTelnet(uint8_t ch);
    void sendTelnetOpt(uint8_t option, uint8_t value);
    double parseFloat(char *pBuf);
    bool HandleEscSeq(unsigned char ch);
    void ReadWriteParamEE(bool write);

private:
    char currentDir[MAX_PATH_LEN];

    char cmdBuf[MAX_CMD_BUF_SIZE];
    char dirBuf[15];
    char *ParmPtr[10];
    uint8_t bufPos;
    bool watchMode;
    bool csvMode;
    uint8_t escSeq;
    unsigned long watchTimeout;
    const char* machName;
    int historyBufSize;
    char *historyBuf;
    int historyWrPos;
    int historyCursorPos;
    bool locEcho;
    uint8_t stateTelnet;

    static CMD_ENTRY Cmds[MAX_CMD_NUM];
    PARAM_ENTRY *Params;
    static const char dirList[][5] PROGMEM;
};

extern microBox microbox;

#endif
