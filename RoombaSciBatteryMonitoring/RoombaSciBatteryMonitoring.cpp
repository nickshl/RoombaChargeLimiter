// RoombaSciBatteryMonitoring.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <string>
#include <Windows.h>
#include <stdio.h>
#include <conio.h>

const char* GetStateName(int state)
{
	const char* result = "Unknown";
	switch (state)
	{
		case 0:
			result = "Not charging";
			break;

		case 1:
			result = "Reconditioning Charging";
			break;

		case 2:
			result = "Full Charging";
			break;

		case 3:
			result = "Trickle Charging";
			break;

		case 4:
			result = "Waiting";
			break;

		case 5:
			result = "Charging Fault Condition";
			break;

			default :
			result = "Unknown";
			break;
	}
	return result;
}

int GetIntFromBuf(const char* buf)
{
	unsigned char a1 = buf[0u];
	unsigned char a2 = buf[1u];
	short result = 0u;
	result |= ((short)a1 << 8u);
	result |= (short)a2;
	return result;
}

unsigned int GetUIntFromBuf(const char* buf)
{
	unsigned char a1 = buf[0u];
	unsigned char a2 = buf[1u];
	unsigned int result = 0u;
	result |= ((unsigned int)a1 << 8u);
	result |= (unsigned int)a2;
	return result;
}

void PollData(HANDLE& hComm, uint32_t Timeout)
{
	bool   Status = true;
	char   SendBuf[256] = { 0 };  // lpBuffer should be  char or byte array, otherwise write wil fail
	char   SerialBuffer[256];     // Buffer Containing Rxed Data
	DWORD  dNoOFBytestoWrite;     // No of bytes to write into the port
	DWORD  dNoOfBytesWritten = 0; // No of bytes written to the port

	uint64_t start_time = GetTickCount64();
	uint64_t timestamp = start_time;

	// Request Sensors list(6 total): Charging State, Voltage, Current, Temperature, Battery Charge, Battery Capacity
	snprintf(SendBuf, sizeof(SendBuf), "%c%c%c%c%c%c%c%c", 149, 6, 21, 22, 23, 24, 25, 26);
	dNoOFBytestoWrite = strlen(SendBuf);// 8;//  sizeof(SendBuf); // Calculating the no of bytes to write into the port

	uint32_t cnt = 0u; // Counter to detect stall data

	while (1)
	{
		Status = WriteFile(hComm,              // Handle to the Serialport
						   SendBuf,            // Data to be written to the port 
						   dNoOFBytestoWrite,  // No of bytes to write into the port
						   &dNoOfBytesWritten, // No of bytes written to the port
						   NULL);

		if (Status != TRUE)
		{
			printf("%6llu.%1llu Error %d in Writing to Serial Port\n", (timestamp - start_time) / 1000u, ((timestamp - start_time) % 1000u) / 100u, GetLastError());
		}

		Sleep(50);

		if (Status == TRUE)
		{
			//------------------------------------ Setting WaitComm() Event   ----------------------------------------

			//Status = WaitCommEvent(hComm, &dwEventMask, NULL); // Wait for the character to be received

			// Response:
			// 1 byte  unsigned: State 0 Not charging, 1 Reconditioning Charging, 2 Full Charging, 3 Trickle Charging, 4 Waiting, 5 Charging Fault Condition
			// 2 bytes unsigned: Voltage 0 – 65535 mV
			// 2 bytes signed:   Current -32768 – 32767 mA
			// 1 byte  signed:   Temperature -128 – 127
			// 2 bytes unsigned: Battery Charge 0 – 65535 mAh
			// 2 bytes unsigned: Battery Capacity 0 – 65535 mAh

			uint32_t i = 0u;

			for(i = 0u; i < 10u; i++)
			{
				char TempChar; // Temperory Character
				DWORD NoBytesRead; // Bytes read by ReadFile()
				Status = ReadFile(hComm, &TempChar, sizeof(TempChar), &NoBytesRead, NULL);
				SerialBuffer[i] = TempChar;
				if (NoBytesRead == 0u) break;
			}

			PurgeComm(hComm, PURGE_RXCLEAR);

			if (i == 10u)
			{
				//printf("%6d(%d): ", cnt, i);
				//for (uint32_t x = 0u; x < i; x++) printf("%X ", (unsigned int)((unsigned char)SerialBuffer[x]));
				//printf("\n");

				printf("%6llu.%1llu State: %u %-24s, %5u mV, %6d mA, %3d C, %5u mAh, %5u mAh\n",
					(timestamp - start_time) / 1000u,
					((timestamp - start_time) % 1000u) / 100u,
					*(uint8_t*)(SerialBuffer + 0u),
					GetStateName(*(uint8_t*)(SerialBuffer + 0u)),
					GetUIntFromBuf(SerialBuffer + 1u),
					GetIntFromBuf(SerialBuffer + 3u),
					*(int8_t*)(SerialBuffer + 5u),
					GetUIntFromBuf(SerialBuffer + 6u),
					GetUIntFromBuf(SerialBuffer + 8u));
			}
			else
			{
				printf("%6llu.%1llu Error! %d bytes remaining!\n", (timestamp - start_time) / 1000u, ((timestamp - start_time) % 1000u) / 100u, 10u - i);
			}
		}
		cnt++;

		// Wait timeout
		while (GetTickCount64() < timestamp + Timeout)
		{
			Sleep(1);
		}
		// Increase timestamp
		timestamp += Timeout;
	}
}

int main(int argc, char *argv[])
{
	HANDLE hComm;                          // Handle to the Serial port
	wchar_t ComPortName[128] = L"\\\\.\\COM1"; // Name of the Serial port(May Change) to be opened,
	uint32_t Timeout = 0u;
	bool Status = true;

	printf("+==========================================+\n");
	printf("|  Roomba SCI Battery Monitoring           |\n");
	printf("+==========================================+\n\n");

	if (argc != 3)
	{
		printf("Usage: RoombaBatMon.exe <Port> <Timout(ms)>\n");
		return -1;
	}

	// Generate UART name
	wsprintf(ComPortName, L"\\\\.\\%S", argv[1]);
	sscanf_s(argv[2], "%u", &Timeout);

	if ((Timeout < 100u) || (Timeout % 100))
	{
		printf("Error: Timeout should be in 100 ms intervals and can't be 0\n");
		return -1;
	}

	//----------------------------------- Opening the Serial Port --------------------------------------------

	hComm = CreateFile(ComPortName,        // Name of the Port to be Opened
		GENERIC_READ | GENERIC_WRITE,      // Read/Write Access
		0,                                 // No Sharing, ports cant be shared
		NULL,                              // No Security
		OPEN_EXISTING,                     // Open existing port only
		0,                                 // Non Overlapped I/O
		NULL);                             // Null for Comm Devices

	if (hComm == INVALID_HANDLE_VALUE)
	{
		printf("Error: Port %ls can't be opened\n", ComPortName);
		Status = false;
	}
	else
	{
		printf("Port %ls Opened\n\n", ComPortName);
		Status = true;
	}

	//------------------------------- Setting the Parameters for the SerialPort ------------------------------

	if (Status == true)
	{
		DCB dcbSerialParams = { 0 };                        // Initializing DCB structure
		dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

		Status = GetCommState(hComm, &dcbSerialParams);     // Retreives  the current settings

		if (Status == false)
		{
			printf("Error: GetCommState() failed\n");
		}
		else
		{
			dcbSerialParams.BaudRate = CBR_115200; // Setting BaudRate = 9600
			dcbSerialParams.ByteSize = 8;          // Setting ByteSize = 8
			dcbSerialParams.StopBits = ONESTOPBIT; // Setting StopBits = 1
			dcbSerialParams.Parity = NOPARITY;     // Setting Parity = None 

			Status = SetCommState(hComm, &dcbSerialParams);  //Configuring the port according to settings in DCB

			if (Status == false)
			{
				printf("Error: SetCommState() failed, probably wrong Setting DCB Structure\n");
			}
			else
			{
				printf("Setting DCB Structure Successfull\n");
				printf(" Baudrate = %d\n", dcbSerialParams.BaudRate);
				printf(" ByteSize = %d\n", dcbSerialParams.ByteSize);
				printf(" StopBits = %d\n", dcbSerialParams.StopBits);
				printf(" Parity   = %d\n\n", dcbSerialParams.Parity);
			}
		}
	}

	//------------------------------------ Setting Timeouts --------------------------------------------------

	if (Status == true)
	{
		COMMTIMEOUTS timeouts = { 0 };

		timeouts.ReadIntervalTimeout = 50;
		timeouts.ReadTotalTimeoutConstant = 50;
		timeouts.ReadTotalTimeoutMultiplier = 10;
		timeouts.WriteTotalTimeoutConstant = 50;
		timeouts.WriteTotalTimeoutMultiplier = 10;

		Status = SetCommTimeouts(hComm, &timeouts);

		if (Status == false)
		{
			printf("Error: SetCommTimeouts() failed\n");
		}
		else
		{
			printf("Setting Serial Port Timeouts Successfull\n\n");
		}
	}

	//------------------------------------ Setting Receive Mask ----------------------------------------------

	if (Status == true)
	{
		Status = SetCommMask(hComm, EV_RXCHAR); // Configure Windows to Monitor the serial device for Character Reception

		if (Status == false)
		{
			printf("Error: SetCommMask() failed\n");
		}
		else
		{
			printf("Setting CommMask successfull\n\n");
		}
	}

	//----------------------------- Writing a Character to Serial Port----------------------------------------

	if (Status == true)
	{
		BYTE StartBuf[1] = { 128 };
		DWORD  dNoOfBytesWritten = 0; // No of bytes written to the port

		Status = WriteFile(hComm, // Handle to the Serialport
			StartBuf,             // Data to be written to the port 
			1u,                   // No of bytes to write into the port
			&dNoOfBytesWritten,   // No of bytes written to the port
			NULL);

		Sleep(200);
	}

	//----------------------------- Pool data from Roomba ----------------------------------------

	if (Status == true)
	{
		PollData(hComm, Timeout);
	}

	CloseHandle(hComm); // Closing the Serial Port

	return 0;
}
