/*
  microBox.cpp - Library for Linux-Shell like interface for Arduino.
  Created by Sebastian Duell, 06.02.2015.
  More info under http://sebastian-duell.de
  Released under GPLv3.
*/

#include <microBox.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>

microBox microbox;
const prog_char fileDate[] PROGMEM = __DATE__;

CMD_ENTRY microBox::Cmds[] =
{
    {"cat", microBox::CatCB},
    {"cd", microBox::ChangeDirCB},
    {"echo", microBox::EchoCB},
    {"loadpar", microBox::LoadParCB},
    {"ll", microBox::ListLongCB},
    {"ls", microBox::ListDirCB},
    {"savepar", microBox::SaveParCB},
    {"watch", microBox::watchCB},
    {"watchcsv", microBox::watchcsvCB},
    {NULL, NULL}
};

const char microBox::dirList[][5] PROGMEM =
{
    "bin", "dev", "etc", "proc", "sbin", "var", "lib", "sys", "tmp", "usr", ""
};

microBox::microBox()
{
    bufPos = 0;
    watchMode = false;
    csvMode = false;
    locEcho = false;
    watchTimeout = 0;
    escSeq = 0;
    historyWrPos = 0;
    historyBufSize = 0;
    historyCursorPos = -1;
    stateTelnet = TELNET_STATE_NORMAL;
}

microBox::~microBox()
{
}

void microBox::begin(PARAM_ENTRY *pParams, const char* hostName, bool localEcho, char *histBuf, int historySize)
{
    historyBuf = histBuf;
    if(historyBuf != NULL && historySize != 0)
    {
        historyBufSize = historySize;
        historyBuf[0] = 0;
        historyBuf[1] = 0;
    }

    locEcho = localEcho;
    Params = pParams;
    machName = hostName;
    ParmPtr[0] = NULL;
    strcpy(currentDir, "/");
    ShowPrompt();
}

bool microBox::AddCommand(const char *cmdName, void (*cmdFunc)(char **param, uint8_t parCnt))
{
    uint8_t idx = 0;

    while((Cmds[idx].cmdFunc != NULL) && (idx < (MAX_CMD_NUM-1)))
    {
        idx++;
    }
    if(idx < (MAX_CMD_NUM-1))
    {
        Cmds[idx].cmdName = cmdName;
        Cmds[idx].cmdFunc = cmdFunc;
        idx++;
        Cmds[idx].cmdFunc = NULL;
        Cmds[idx].cmdName = NULL;
        return true;
    }
    return false;
}

bool microBox::isTimeout(unsigned long *lastTime, unsigned long intervall)
{
    unsigned long m;

    m = millis();
    if(((m - *lastTime) >= intervall) || (*lastTime > m))
    {
        *lastTime = m;
        return true;
    }
    return false;
}

void microBox::ShowPrompt()
{
    Serial.print(F("root@"));
    Serial.print(machName);
    Serial.print(F(":"));
    Serial.print(currentDir);
    Serial.print(F(">"));
}

uint8_t microBox::ParseCmdParams(char *pParam)
{
    uint8_t idx = 0;

    ParmPtr[idx] = pParam;
    if(pParam != NULL)
    {
        idx++;
        while((pParam = strchr(pParam, ' ')) != NULL)
        {
            pParam[0] = 0;
            pParam++;
            ParmPtr[idx++] = pParam;
        }
    }
    return idx;
}

void microBox::ExecCommand()
{
    bool found = false;
    Serial.println();
    if(bufPos > 0)
    {
        uint8_t i=0;
        uint8_t dstlen;
        uint8_t srclen;
        char *pParam;

        cmdBuf[bufPos] = 0;
        pParam = strchr(cmdBuf, ' ');
        if(pParam != NULL)
        {
            pParam++;
            srclen = pParam - cmdBuf - 1;
        }
        else
            srclen = bufPos;

        AddToHistory(cmdBuf);
        historyCursorPos = -1;

        while(Cmds[i].cmdName != NULL && found == false)
        {
            dstlen = strlen(Cmds[i].cmdName);
            if(dstlen == srclen)
            {
                if(strncmp(cmdBuf, Cmds[i].cmdName, dstlen) == 0)
                {
                    (*Cmds[i].cmdFunc)(ParmPtr, ParseCmdParams(pParam));
                    found = true;
                    bufPos = 0;
                    ShowPrompt();
                }
            }
            i++;
        }
        if(!found)
        {
            bufPos = 0;
            ErrorDir(F("/bin/sh"));
            ShowPrompt();
        }
    }
    else
        ShowPrompt();
}

void microBox::cmdParser()
{
    if(watchMode)
    {
        if(Serial.available())
        {
            watchMode = false;
            csvMode = false;
        }
        else
        {
            if(isTimeout(&watchTimeout, 500))
                Cat_int(cmdBuf);

            return;
        }
    }
    while(Serial.available())
    {
        uint8_t ch;
        ch = Serial.read();
        if(ch == TELNET_IAC || stateTelnet != TELNET_STATE_NORMAL)
        {
            handleTelnet(ch);
            continue;
        }

        if(HandleEscSeq(ch))
            continue;

        if(ch == 0x7F || ch == 0x08)
        {
            if(bufPos > 0)
            {
                bufPos--;
                cmdBuf[bufPos] = 0;
                Serial.write(ch);
                Serial.print(F(" \x1B[1D"));
            }
            else
            {
                Serial.print(F("\a"));
            }
        }
        else if(ch == '\t')
        {
            HandleTab();
        }
        else if(ch != '\r' && bufPos < (MAX_CMD_BUF_SIZE-1))
        {
            if(ch != '\n')
            {
                if(locEcho)
                    Serial.write(ch);
                cmdBuf[bufPos++] = ch;
                cmdBuf[bufPos] = 0;
            }
        }
        else
        {
            ExecCommand();
        }
    }
}

bool microBox::HandleEscSeq(unsigned char ch)
{
    bool ret = false;

    if(ch == 27)
    {
        escSeq = ESC_STATE_START;
        ret = true;
    }
    else if(escSeq == ESC_STATE_START)
    {
        if(ch == 0x5B)
        {
            escSeq = ESC_STATE_CODE;
            ret = true;
        }
        else
            escSeq = ESC_STATE_NONE;
    }
    else if(escSeq == ESC_STATE_CODE)
    {
        if(ch == 0x41) // Cursor Up
        {
            HistoryUp();
        }
        else if(ch == 0x42) // Cursor Down
        {
            HistoryDown();
        }
        else if(ch == 0x43) // Cursor Right
        {
        }
        else if(ch == 0x44) // Cursor Left
        {
        }
        escSeq = ESC_STATE_NONE;
        ret = true;
    }
    return ret;
}

uint8_t microBox::ParCmp(uint8_t idx1, uint8_t idx2, bool cmd)
{
    uint8_t i=0;

    const char *pName1;
    const char *pName2;

    if(cmd)
    {
        pName1 = Cmds[idx1].cmdName;
        pName2 = Cmds[idx2].cmdName;
    }
    else
    {
        pName1 = Params[idx1].paramName;
        pName2 = Params[idx2].paramName;
    }

    while(pName1[i] != 0 && pName2[i] != 0)
    {
        if(pName1[i] != pName2[i])
            return i;
        i++;
    }
    return i;
}

int8_t microBox::GetCmdIdx(char* pCmd, int8_t startIdx)
{
    while(Cmds[startIdx].cmdName != NULL)
    {
        if(strncmp(Cmds[startIdx].cmdName, pCmd, strlen(pCmd)) == 0)
        {
            return startIdx;
        }
        startIdx++;
    }
    return -1;
}

void microBox::HandleTab()
{
    int8_t idx, idx2;
    char *pParam = NULL;
    uint8_t i, len = 0;
    uint8_t parlen, matchlen, inlen;

    for(i=0;i<bufPos;i++)
    {
        if(cmdBuf[i] == ' ')
            pParam = cmdBuf+i;
    }
    if(pParam != NULL)
    {
        pParam++;
        if(*pParam != 0)
        {
            idx = GetParamIdx(pParam, true, 0);
            if(idx >= 0)
            {
                parlen = strlen(Params[idx].paramName);
                matchlen = parlen;
                idx2=idx;
                while((idx2=GetParamIdx(pParam, true, idx2+1))!= -1)
                {
                    matchlen = ParCmp(idx, idx2);
                    if(matchlen < parlen)
                        parlen = matchlen;
                }
                pParam = GetFile(pParam);
                inlen = strlen(pParam);
                if(matchlen > inlen)
                {
                    len = matchlen - inlen;
                    if((bufPos + len) < MAX_CMD_BUF_SIZE)
                    {
                        strncat(cmdBuf, Params[idx].paramName + inlen, len);
                        bufPos += len;
                    }
                    else
                        len = 0;
                }
            }
        }
    }
    else if(bufPos)
    {
        pParam = cmdBuf;

        idx = GetCmdIdx(pParam);
        if(idx >= 0)
        {
            parlen = strlen(Cmds[idx].cmdName);
            matchlen = parlen;
            idx2=idx;
            while((idx2=GetCmdIdx(pParam, idx2+1))!= -1)
            {
                matchlen = ParCmp(idx, idx2, true);
                if(matchlen < parlen)
                    parlen = matchlen;
            }
            inlen = strlen(pParam);
            if(matchlen > inlen)
            {
                len = matchlen - inlen;
                if((bufPos + len) < MAX_CMD_BUF_SIZE)
                {
                    strncat(cmdBuf, Cmds[idx].cmdName + inlen, len);
                    bufPos += len;
                }
                else
                    len = 0;
            }
        }
    }
    if(len > 0)
    {
        Serial.print(pParam + inlen);
    }
}

void microBox::HistoryUp()
{
    if(historyBufSize == 0 || historyWrPos == 0)
        return;

    if(historyCursorPos == -1)
        historyCursorPos = historyWrPos-2;

    while(historyBuf[historyCursorPos] != 0 && historyCursorPos > 0)
    {
        historyCursorPos--;
    }
    if(historyCursorPos > 0)
        historyCursorPos++;

    strcpy(cmdBuf, historyBuf+historyCursorPos);
    HistoryPrintHlpr();
    if(historyCursorPos > 1)
        historyCursorPos -= 2;
}

void microBox::HistoryDown()
{
    int pos;
    if(historyCursorPos != -1 && historyCursorPos != historyWrPos-2)
    {
        pos = historyCursorPos+2;
        pos += strlen(historyBuf+pos) + 1;

        strcpy(cmdBuf, historyBuf+pos);
        HistoryPrintHlpr();
        historyCursorPos = pos - 2;
    }
}

void microBox::HistoryPrintHlpr()
{
    uint8_t i;
    uint8_t len;

    len = strlen(cmdBuf);
    for(i=0;i<bufPos;i++)
        Serial.print('\b');
    Serial.print(cmdBuf);
    if(len<bufPos)
    {
        Serial.print(F("\x1B[K"));
    }
    bufPos = len;
}

void microBox::AddToHistory(char *buf)
{
    uint8_t len;
    int blockStart = 0;

    len = strlen(buf);
    if(historyBufSize > 0)
    {
        if(historyWrPos+len+1 >= historyBufSize)
        {
            while(historyWrPos+len-blockStart >= historyBufSize)
            {
                blockStart += strlen(historyBuf + blockStart) + 1;
            }
            memmove(historyBuf, historyBuf+blockStart, historyWrPos-blockStart);
            historyWrPos -= blockStart;
        }
        strcpy(historyBuf+historyWrPos, buf);
        historyWrPos += len+1;
        historyBuf[historyWrPos] = 0;
    }
}

// 2 telnet methods derived from https://github.com/nekromant/esp8266-frankenstein/blob/master/src/telnet.c
void microBox::sendTelnetOpt(uint8_t option, uint8_t value)
{
    uint8_t tmp[4];
    tmp[0] = TELNET_IAC;
    tmp[1] = option;
    tmp[2] = value;
    tmp[3] = 0;
    Serial.write(tmp, 4);
}

void microBox::handleTelnet(uint8_t ch)
{
    switch (stateTelnet)
    {
    case TELNET_STATE_IAC:
        if(ch == TELNET_IAC)
        {
            stateTelnet = TELNET_STATE_NORMAL;
        }
        else
        {
            switch(ch)
            {
            case TELNET_WILL:
                stateTelnet = TELNET_STATE_WILL;
                break;
            case TELNET_WONT:
                stateTelnet = TELNET_STATE_WONT;
                break;
            case TELNET_DO:
                stateTelnet = TELNET_STATE_DO;
                break;
            case TELNET_DONT:
                stateTelnet = TELNET_STATE_DONT;
                break;
            default:
                stateTelnet = TELNET_STATE_NORMAL;
                break;
            }
        }
        break;
    case TELNET_STATE_WILL:
        sendTelnetOpt(TELNET_DONT, ch);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_WONT:
        sendTelnetOpt(TELNET_DONT, ch);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_DO:
        if(ch == TELNET_OPTION_ECHO)
        {
            sendTelnetOpt(TELNET_WILL, ch);
            sendTelnetOpt(TELNET_DO, ch);
            locEcho = true;
        }
        else if(ch == TELNET_OPTION_SGA)
            sendTelnetOpt(TELNET_WILL, ch);
        else
            sendTelnetOpt(TELNET_WONT, ch);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_DONT:
        sendTelnetOpt(TELNET_WONT, ch);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_NORMAL:
        if(ch == TELNET_IAC)
        {
            stateTelnet = TELNET_STATE_IAC;
        }
        break;
    }
}


void microBox::ErrorDir(const __FlashStringHelper *cmd)
{
    Serial.print(cmd);
    Serial.println(F(": File or directory not found\n"));
}

char *microBox::GetDir(char *pParam, bool useFile)
{
    uint8_t i=0;
    uint8_t len;
    char *tmp;

    dirBuf[0] = 0;
    if(pParam != NULL)
    {
        if(currentDir[1] != 0)
        {
            if(pParam[0] != '/')
            {
                if(!(pParam[0] == '.' && pParam[1] == '.'))
                {
                    return NULL;
                }
                else
                {
                    pParam += 2;
                    if(pParam[0] == 0)
                    {
                        dirBuf[0] = '/';
                        dirBuf[1] = 0;
                    }
                    else if(pParam[0] != '/')
                        return NULL;
                }
            }
        }
        if(pParam[0] == '/')
        {
            if(pParam[1] == 0)
            {
                dirBuf[0] = '/';
                dirBuf[1] = 0;
            }
            pParam++;
        }

        if((tmp=strchr(pParam, '/')) != 0)
        {
            len = tmp-pParam;
        }
        else
            len = strlen(pParam);
        if(len > 0)
        {
            while(pgm_read_byte_near(&dirList[i][0]) != 0)
            {
                if(strncmp_P(pParam, dirList[i], len) == 0)
                {
                    if(strlen_P(dirList[i]) == len)
                    {
                        dirBuf[0] = '/';
                        dirBuf[1] = 0;
                        strcat_P(dirBuf, dirList[i]);
                        return dirBuf;
                    }
                }
                i++;
            }
        }
    }
    if(dirBuf[0] != 0)
        return dirBuf;
    return NULL;
}

char *microBox::GetFile(char *pParam)
{
    char *file;
    char *t;

    file = pParam;
    while((t=strchr(file, '/')) != NULL)
    {
        file = t+1;
    }
    return file;
}

void microBox::ListDirHlp(bool dir, bool rw, int len)
{
    cmdBuf[1] = 'r';
    cmdBuf[3] = 0;
    if(dir)
        cmdBuf[0] = 'd';
    else
        cmdBuf[0] = '-';

    if(rw)
        cmdBuf[2] = 'w';
    else
        cmdBuf[2] = '-';

    Serial.print(cmdBuf);
    cmdBuf[0] = 0;

    Serial.print(F("xr-xr-x\t2 root\troot\t"));
    Serial.print(len);
    Serial.print(F(" "));
    Serial.print((const __FlashStringHelper*)fileDate);
    Serial.print(F(" "));
}

void microBox::ListDir(char **pParam, uint8_t parCnt, bool listLong)
{
    uint8_t i=0;
    char *dir;

    if(parCnt != 0)
    {
        dir = GetDir(pParam[0], false);
        if(dir == NULL)
        {
            if(listLong)
                ErrorDir(F("ll"));
            else
                ErrorDir(F("ls"));
            return;
        }
    }
    else
    {
        dir = currentDir;
    }

    if(dir[1] == 0)
    {
        while(pgm_read_byte_near(&dirList[i][0]) != 0)
        {
            if(listLong)
            {
                ListDirHlp(true);
            }
            Serial.print((__FlashStringHelper*)dirList[i]);
            if(listLong)
                Serial.println();
            else
                Serial.print(F("\t"));
            i++;
        }
        Serial.println();
    }
    else if(strcmp_P(dir, PSTR("/bin")) == 0)
    {
        while(Cmds[i].cmdName != NULL)
        {
            if(listLong)
            {
                ListDirHlp(false);
            }
            Serial.println(Cmds[i].cmdName);
            i++;
        }
    }
    else if(strcmp_P(dir, PSTR("/dev")) == 0)
    {
        while(Params[i].paramName != NULL)
        {
            if(listLong)
            {
                uint8_t size;
                if(Params[i].parType&PARTYPE_INT)
                    size=sizeof(int);
                else if(Params[i].parType&PARTYPE_DOUBLE)
                    size = sizeof(double);
                else
                    size = Params[i].len;

                ListDirHlp(false, Params[i].parType&PARTYPE_RW, size);
            }
            Serial.println(Params[i].paramName);
            i++;
        }
    }
}

void microBox::ChangeDir(char **pParam, uint8_t parCnt)
{
    char *dir;

    if(pParam[0] != NULL)
    {
        dir = GetDir(pParam[0], false);
        if(dir != NULL)
        {
            strcpy(currentDir, dir);
            return;
        }
    }
    ErrorDir(F("cd"));
}

void microBox::PrintParam(uint8_t idx)
{
    if(Params[idx].getFunc != NULL)
        (*Params[idx].getFunc)(Params[idx].id);

    if(Params[idx].parType&PARTYPE_INT)
        Serial.print(*((int*)Params[idx].pParam));
    else if(Params[idx].parType&PARTYPE_DOUBLE)
        Serial.print(*((double*)Params[idx].pParam), 8);
    else
        Serial.print(((char*)Params[idx].pParam));

    if(csvMode)
        Serial.print(F(";"));
    else
        Serial.println();
}

int8_t microBox::GetParamIdx(char* pParam, bool partStr, int8_t startIdx)
{
    int8_t i=startIdx;
    char *dir;
    char *file;

    if(pParam != NULL)
    {
        dir = GetDir(pParam, true);
        if(dir == NULL)
            dir = currentDir;
        if(dir != NULL)
        {
            if(strcmp_P(dir, PSTR("/dev")) == 0)
            {
                file = GetFile(pParam);
                if(file != NULL)
                {
                    while(Params[i].paramName != NULL)
                    {
                        if(partStr)
                        {
                            if(strncmp(Params[i].paramName, file, strlen(file))== 0)
                            {
                                return i;
                            }
                        }
                        else
                        {
                            if(strcmp(Params[i].paramName, file)== 0)
                            {
                                return i;
                            }
                        }
                        i++;
                    }
                }
            }
        }
    }
    return -1;
}

// Taken from Stream.cpp
double microBox::parseFloat(char *pBuf)
{
    boolean isNegative = false;
    boolean isFraction = false;
    long value = 0;
    unsigned char c;
    double fraction = 1.0;
    uint8_t idx = 0;

    c = pBuf[idx++];
    // ignore non numeric leading characters
    if(c > 127)
        return 0; // zero returned if timeout

    do{
        if(c == '-')
            isNegative = true;
        else if (c == '.')
            isFraction = true;
        else if(c >= '0' && c <= '9')  {      // is c a digit?
            value = value * 10 + c - '0';
            if(isFraction)
                fraction *= 0.1;
        }
        c = pBuf[idx++];
    }
    while( (c >= '0' && c <= '9')  || c == '.');

    if(isNegative)
        value = -value;
    if(isFraction)
        return value * fraction;
    else
        return value;
}

// echo 82.00 > /dev/param
void microBox::Echo(char **pParam, uint8_t parCnt)
{
    uint8_t idx;

    if((parCnt == 3) && (strcmp_P(pParam[1], PSTR(">")) == 0))
    {
        idx = GetParamIdx(pParam[2]);
        if(idx != -1)
        {
            if(Params[idx].parType & PARTYPE_RW)
            {
                if(Params[idx].parType & PARTYPE_INT)
                {
                    int val;

                    val = atoi(pParam[0]);
                    *((int*)Params[idx].pParam) = val;
                }
                else if(Params[idx].parType & PARTYPE_DOUBLE)
                {
                    double val;

                    val = parseFloat(pParam[0]);
                    *((double*)Params[idx].pParam) = val;
                }
                else
                {
                    if(strlen(pParam[0]) < Params[idx].len)
                        strcpy((char*)Params[idx].pParam, pParam[0]);
                }
                if(Params[idx].setFunc != NULL)
                    (*Params[idx].setFunc)(Params[idx].id);
            }
            else
                Serial.println(F("echo: File readonly"));
        }
        else
        {
            ErrorDir(F("echo"));
        }
    }
    else
    {
        for(idx=0;idx<parCnt;idx++)
        {
            Serial.print(pParam[idx]);
            Serial.print(F(" "));
        }
        Serial.println();
    }
}

void microBox::Cat(char** pParam, uint8_t parCnt)
{
    Cat_int(pParam[0]);
}

uint8_t microBox::Cat_int(char* pParam)
{
    int8_t idx;

    idx = GetParamIdx(pParam);
    if(idx != -1)
    {
        PrintParam(idx);
        return 1;
    }
    else
        ErrorDir(F("cat"));
    
    return 0;
}

void microBox::watch(char** pParam, uint8_t parCnt)
{
    if(parCnt == 2)
    {
        if(strncmp_P(pParam[0], PSTR("cat"), 3) == 0)
        {
            if(Cat_int(pParam[1]))
            {
                strcpy(cmdBuf, pParam[1]);
                watchMode = true;
            }
        }
    }
}

void microBox::watchcsv(char** pParam, uint8_t parCnt)
{
    watch(pParam, parCnt);
    if(watchMode)
        csvMode = true;
}

void microBox::ReadWriteParamEE(bool write)
{
    uint8_t i=0;
    uint8_t psize;
    int pos=0;

    while(Params[i].paramName != NULL)
    {
        if(Params[i].parType&PARTYPE_INT)
            psize = sizeof(uint16_t);
        else if(Params[i].parType&PARTYPE_DOUBLE)
            psize = sizeof(double);
        else
            psize = Params[i].len;

        if(write)
            eeprom_write_block(Params[i].pParam, (void*)pos, psize);
        else
            eeprom_read_block(Params[i].pParam, (void*)pos, psize);
        pos += psize;
        i++;
    }
}

void microBox::ListDirCB(char **pParam, uint8_t parCnt)
{
    microbox.ListDir(pParam, parCnt);
}

void microBox::ListLongCB(char **pParam, uint8_t parCnt)
{
    microbox.ListDir(pParam, parCnt, true);
}

void microBox::ChangeDirCB(char **pParam, uint8_t parCnt)
{
    microbox.ChangeDir(pParam, parCnt);
}

void microBox::EchoCB(char **pParam, uint8_t parCnt)
{
    microbox.Echo(pParam, parCnt);
}

void microBox::CatCB(char** pParam, uint8_t parCnt)
{
    microbox.Cat(pParam, parCnt);
}

void microBox::watchCB(char** pParam, uint8_t parCnt)
{
    microbox.watch(pParam, parCnt);
}

void microBox::watchcsvCB(char** pParam, uint8_t parCnt)
{
    microbox.watchcsv(pParam, parCnt);
}

void microBox::LoadParCB(char **pParam, uint8_t parCnt)
{
    microbox.ReadWriteParamEE(false);
}

void microBox::SaveParCB(char **pParam, uint8_t parCnt)
{
    microbox.ReadWriteParamEE(true);
}

