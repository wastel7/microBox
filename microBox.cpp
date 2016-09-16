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
	{nullptr, nullptr}
};

const char microBox::dirList[][5] PROGMEM =
{
    "bin", "dev", "etc", "proc", "sbin", "var", "lib", "sys", "tmp", "usr", ""
};

microBox::microBox()
{
	bufferPosition = 0;
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
	delete[] dirBuf;
	delete[] ParmPtr;
	delete[] machName;
	if (historyBuffer != nullptr) {
		delete historyBuffer;
	}
}

void microBox::begin(PARAM_ENTRY *pParams, const char* hostName, bool localEcho, char *histBuf, int historySize)
{
	historyBuffer = histBuf;
	if(historyBuffer != nullptr && historySize != 0)
    {
        historyBufSize = historySize;
		historyBuffer[0] = 0;
		historyBuffer[1] = 0;
    }

    locEcho = localEcho;
    Params = pParams;
    machName = hostName;
	ParmPtr[0] = nullptr;
    strcpy(currentDir, "/");
    ShowPrompt();
}

bool microBox::AddCommand(const char *cmdName, void (*cmdFunc)(char **param, uint8_t parCnt))
{
    uint8_t idx = 0;

	while((Cmds[idx].cmdFunc != nullptr) && (idx < (MAX_CMD_NUM-1)))
    {
		++idx;
    }
    if(idx < (MAX_CMD_NUM-1))
    {
        Cmds[idx].cmdName = cmdName;
        Cmds[idx].cmdFunc = cmdFunc;
		++idx;
		Cmds[idx].cmdFunc = nullptr;
		Cmds[idx].cmdName = nullptr;
        return true;
    }
    return false;
}

bool microBox::isTimeout(unsigned long &lastTime, const unsigned long intervall)
{
	unsigned long milliseconds;

	milliseconds = millis();
	if(((milliseconds - lastTime) >= intervall) || (lastTime > milliseconds))
    {
		lastTime = milliseconds;
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
	if(pParam != nullptr)
    {
		++idx;
		while((pParam = strchr(pParam, ' ')) != nullptr)
        {
            pParam[0] = 0;
			++pParam;
            ParmPtr[idx++] = pParam;
        }
    }
    return idx;
}

void microBox::ExecCommand()
{
    bool found = false;
    Serial.println();
	if(bufferPosition > 0)
    {
		uint8_t index = 0;
        uint8_t dstlen;
        uint8_t srclen;
        char *pParam;

		cmdBuf[bufferPosition] = 0;
        pParam = strchr(cmdBuf, ' ');
		if(pParam != nullptr)
        {
			++pParam;
            srclen = pParam - cmdBuf - 1;
        }
        else
			srclen = bufferPosition;

        AddToHistory(cmdBuf);
        historyCursorPos = -1;

		while(Cmds[index].cmdName != nullptr && found == false)
        {
			dstlen = strlen(Cmds[index].cmdName);
            if(dstlen == srclen)
            {
				if(strncmp(cmdBuf, Cmds[index].cmdName, dstlen) == 0)
                {
					(*Cmds[index].cmdFunc)(ParmPtr, ParseCmdParams(pParam));
                    found = true;
					bufferPosition = 0;
                    ShowPrompt();
                }
            }
			++index;
        }
        if(!found)
        {
			bufferPosition = 0;
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
		uint8_t character;
		character = Serial.read();
		if(character == TELNET_IAC || stateTelnet != TELNET_STATE_NORMAL)
        {
			handleTelnet(character);
            continue;
        }

		if(HandleEscSeq(character))
            continue;

		if(character == 0x7F || character == 0x08)
        {
			if(bufferPosition > 0)
            {
				bufferPosition--;
				cmdBuf[bufferPosition] = 0;
				Serial.write(character);
                Serial.print(F(" \x1B[1D"));
            }
            else
            {
                Serial.print(F("\a"));
            }
        }
		else if(character == '\t')
        {
            HandleTab();
        }
		else if(character != '\r' && bufferPosition < (MAX_CMD_BUF_SIZE-1))
        {
			if(character != '\n')
            {
                if(locEcho)
					Serial.write(character);
				cmdBuf[bufferPosition++] = character;
				cmdBuf[bufferPosition] = 0;
            }
        }
        else
        {
            ExecCommand();
        }
    }
}

bool microBox::HandleEscSeq(unsigned char character)
{
	bool result = false;

	if(character == 27)
    {
        escSeq = ESC_STATE_START;
		result = true;
    }
    else if(escSeq == ESC_STATE_START)
    {
		if(character == 0x5B)
        {
            escSeq = ESC_STATE_CODE;
			result = true;
        }
        else
            escSeq = ESC_STATE_NONE;
    }
    else if(escSeq == ESC_STATE_CODE)
    {
		if(character == 0x41) // Cursor Up
        {
            HistoryUp();
        }
		else if(character == 0x42) // Cursor Down
        {
            HistoryDown();
        }
		else if(character == 0x43) // Cursor Right
        {
        }
		else if(character == 0x44) // Cursor Left
        {
        }
        escSeq = ESC_STATE_NONE;
		result = true;
    }
	return result;
}

uint8_t microBox::ParCmp(uint8_t idx1, uint8_t idx2, const bool cmd)
{
	uint8_t index = 0;

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

	while(pName1[index] != 0 && pName2[index] != 0)
    {
		if(pName1[index] != pName2[index])
			return index;
		++index;
    }
	return index;
}

int8_t microBox::GetCmdIdx(char* pCmd, int8_t startIdx)
{
	while(Cmds[startIdx].cmdName != nullptr)
    {
        if(strncmp(Cmds[startIdx].cmdName, pCmd, strlen(pCmd)) == 0)
        {
            return startIdx;
        }
		++startIdx;
    }
    return -1;
}

void microBox::HandleTab()
{
    int8_t idx, idx2;
	char *pParam = nullptr;
    uint8_t i, len = 0;
    uint8_t parlen, matchlen, inlen;

	for(i=0; i < bufferPosition; i++)
    {
        if(cmdBuf[i] == ' ')
			pParam = cmdBuf + i;
    }
	if(pParam != nullptr)
    {
		++pParam;
        if(*pParam != 0)
        {
            idx = GetParamIdx(pParam, true, 0);
            if(idx >= 0)
            {
                parlen = strlen(Params[idx].paramName);
                matchlen = parlen;
				idx2 = idx;
				while((idx2 = GetParamIdx(pParam, true, idx2 + 1)) != -1)
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
					if((bufferPosition + len) < MAX_CMD_BUF_SIZE)
                    {
                        strncat(cmdBuf, Params[idx].paramName + inlen, len);
						bufferPosition += len;
                    }
                    else
                        len = 0;
                }
            }
        }
    }
	else if(bufferPosition)
    {
        pParam = cmdBuf;

        idx = GetCmdIdx(pParam);
        if(idx >= 0)
        {
            parlen = strlen(Cmds[idx].cmdName);
            matchlen = parlen;
			idx2 = idx;
			while((idx2 = GetCmdIdx(pParam, idx2+1)) != -1)
            {
                matchlen = ParCmp(idx, idx2, true);
                if(matchlen < parlen)
                    parlen = matchlen;
            }
            inlen = strlen(pParam);
            if(matchlen > inlen)
            {
                len = matchlen - inlen;
				if((bufferPosition + len) < MAX_CMD_BUF_SIZE)
                {
                    strncat(cmdBuf, Cmds[idx].cmdName + inlen, len);
					bufferPosition += len;
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

	while(historyBuffer[historyCursorPos] != 0 && historyCursorPos > 0)
    {
        historyCursorPos--;
    }
    if(historyCursorPos > 0)
        historyCursorPos++;

	strcpy(cmdBuf, historyBuffer+historyCursorPos);
    HistoryPrintHlpr();
    if(historyCursorPos > 1)
        historyCursorPos -= 2;
}

void microBox::HistoryDown()
{
	int position;
    if(historyCursorPos != -1 && historyCursorPos != historyWrPos-2)
    {
		position = historyCursorPos + 2;
		position += strlen(historyBuffer+position) + 1;

		strcpy(cmdBuf, historyBuffer + position);
        HistoryPrintHlpr();
		historyCursorPos = position - 2;
    }
}

void microBox::HistoryPrintHlpr()
{
	uint8_t length;

	length = strlen(cmdBuf);
	for(uint8_t i = 0; i < bufferPosition; ++i)
        Serial.print('\b');
    Serial.print(cmdBuf);
	if(length<bufferPosition)
    {
        Serial.print(F("\x1B[K"));
    }
	bufferPosition = length;
}

void microBox::AddToHistory(char *buffer)
{
	uint8_t length;
    int blockStart = 0;

	length = strlen(buffer);
    if(historyBufSize > 0)
    {
		if(historyWrPos+length + 1 >= historyBufSize)
        {
			while(historyWrPos + length-blockStart >= historyBufSize)
            {
				blockStart += strlen(historyBuffer + blockStart) + 1;
            }
			memmove(historyBuffer, historyBuffer + blockStart, historyWrPos - blockStart);
            historyWrPos -= blockStart;
        }
		strcpy(historyBuffer+historyWrPos, buffer);
		historyWrPos += length + 1;
		historyBuffer[historyWrPos] = 0;
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

void microBox::handleTelnet(uint8_t character)
{
    switch (stateTelnet)
    {
    case TELNET_STATE_IAC:
		if(character == TELNET_IAC)
        {
            stateTelnet = TELNET_STATE_NORMAL;
        }
        else
        {
			switch(character)
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
		sendTelnetOpt(TELNET_DONT, character);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_WONT:
		sendTelnetOpt(TELNET_DONT, character);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_DO:
		if(character == TELNET_OPTION_ECHO)
        {
			sendTelnetOpt(TELNET_WILL, character);
			sendTelnetOpt(TELNET_DO, character);
            locEcho = true;
        }
		else if(character == TELNET_OPTION_SGA)
			sendTelnetOpt(TELNET_WILL, character);
        else
			sendTelnetOpt(TELNET_WONT, character);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_DONT:
		sendTelnetOpt(TELNET_WONT, character);
        stateTelnet = TELNET_STATE_NORMAL;
        break;
    case TELNET_STATE_NORMAL:
		if(character == TELNET_IAC)
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
	uint8_t index = 0;
	uint8_t length;
    char *tmp;

    dirBuf[0] = 0;
	if(pParam != nullptr)
    {
        if(currentDir[1] != 0)
        {
            if(pParam[0] != '/')
            {
				if( !(pParam[0] == '.' && pParam[1] == '.') )
                {
					return nullptr;
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
						return nullptr;
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
			length = tmp - pParam;
        }
        else
			length = strlen(pParam);
		if(length > 0)
        {
			while(pgm_read_byte_near(&dirList[index][0]) != 0)
            {
				if(strncmp_P(pParam, dirList[index], length) == 0)
                {
					if(strlen_P(dirList[index]) == length)
                    {
                        dirBuf[0] = '/';
                        dirBuf[1] = 0;
						strcat_P(dirBuf, dirList[index]);
                        return dirBuf;
                    }
                }
				++index;
            }
        }
    }
    if(dirBuf[0] != 0)
        return dirBuf;
	return nullptr;
}

char *microBox::GetFile(char *pParam)
{
    char *file;
	char *character;

    file = pParam;
	while((character = strchr(file, '/')) != nullptr)
    {
		file = character + 1;
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

void microBox::ListDir(char **pParam, const uint8_t parCnt, const bool listLong)
{
	uint8_t index = 0;
	char *directory;

    if(parCnt != 0)
    {
		directory = GetDir(pParam[0], false);
		if(directory == nullptr)
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
		directory = currentDir;
    }

	if(directory[1] == 0)
    {
		while(pgm_read_byte_near(&dirList[index][0]) != 0)
        {
            if(listLong)
            {
                ListDirHlp(true);
            }
			Serial.print((__FlashStringHelper*) dirList[index]);
            if(listLong)
                Serial.println();
            else
                Serial.print(F("\t"));
			++index;
        }
        Serial.println();
    }
	else if(strcmp_P(directory, PSTR("/bin")) == 0)
    {
		while(Cmds[index].cmdName != nullptr)
        {
            if(listLong)
            {
                ListDirHlp(false);
            }
			Serial.println(Cmds[index].cmdName);
			++index;
        }
    }
	else if(strcmp_P(directory, PSTR("/dev")) == 0)
    {
		while(Params[index].paramName != nullptr)
        {
            if(listLong)
            {
                uint8_t size;
				if(Params[index].parType & PARTYPE_INT)
                    size=sizeof(int);
				else if(Params[index].parType & PARTYPE_DOUBLE)
                    size = sizeof(double);
                else
					size = Params[index].len;

				ListDirHlp(false, Params[index].parType & PARTYPE_RW, size);
            }
			Serial.println(Params[index].paramName);
			++index;
        }
    }
}

void microBox::ChangeDirectory(char **pParam, uint8_t parCnt)
{
	char *directory;

	if(pParam[0] != nullptr)
    {
		directory = GetDir(pParam[0], false);
		if(directory != nullptr)
        {
			strcpy(currentDir, directory);
            return;
        }
    }
    ErrorDir(F("cd"));
}

void microBox::PrintParam(const uint8_t idx)
{
	if(Params[idx].getFunc != nullptr)
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

int8_t microBox::GetParamIdx(char* pParam, const bool partStr, const int8_t startIdx)
{
	int8_t i = startIdx;
    char *dir;
    char *file;

	if(pParam != nullptr)
    {
        dir = GetDir(pParam, true);
		if(dir == nullptr)
            dir = currentDir;
		if(dir != nullptr)
        {
            if(strcmp_P(dir, PSTR("/dev")) == 0)
            {
                file = GetFile(pParam);
				if(file != nullptr)
                {
					while(Params[i].paramName != nullptr)
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
						++i;
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
	bool isNegative = false;
	bool isFraction = false;
    long value = 0;
	unsigned char character;
    double fraction = 1.0;
    uint8_t idx = 0;

	character = pBuf[idx++];
    // ignore non numeric leading characters
	if(character > 127)
        return 0; // zero returned if timeout

	do {
		if(character == '-')
            isNegative = true;
		else if (character == '.')
            isFraction = true;
		else if(character >= '0' && character <= '9')  {      // is c a digit?
			value = value * 10 + character - '0';
            if(isFraction)
                fraction *= 0.1;
        }
		character = pBuf[idx++];
    }
	while( (character >= '0' && character <= '9')  || character == '.');

    if(isNegative)
		value -= value;
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
				if(Params[idx].setFunc != nullptr)
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

void microBox::watch(char** pParam, const uint8_t parCnt)
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

void microBox::watchcsv(char** pParam, const uint8_t parCnt)
{
    watch(pParam, parCnt);
    if(watchMode)
        csvMode = true;
}

void microBox::ReadWriteParamEE(const bool write)
{
	uint8_t i = 0;
    uint8_t psize;
	int position = 0;

	while(Params[i].paramName != nullptr)
    {
		if(Params[i].parType & PARTYPE_INT)
            psize = sizeof(uint16_t);
		else if(Params[i].parType & PARTYPE_DOUBLE)
            psize = sizeof(double);
        else
            psize = Params[i].len;

        if(write)
			eeprom_write_block(Params[i].pParam, (void*)position, psize);
        else
			eeprom_read_block(Params[i].pParam, (void*)position, psize);
		position += psize;
		++i;
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
	microbox.ChangeDirectory(pParam, parCnt);
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

