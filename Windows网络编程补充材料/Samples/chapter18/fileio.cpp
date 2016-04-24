// Module Name: fileio.cpp
//
// Purpose:
//     This is a simple sample that demonstrates how to create a
//     file using UNC names.
//
// Compile:
//     cl -o fileio fileio.cpp
//
// Command Line Options:
//     None
//

#include <windows.h>
#include <stdio.h>

void main(void) {

	HANDLE FileHandle;
	DWORD BytesWritten;

	// Open a handle to file \\MYSERVER\MYSHARE\sample.txt
	if ((FileHandle = CreateFile("\\\\MYSERVER\\MYSHARE\\sample.txt",
		GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
		NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
	{
		printf("CreateFile failed with error %d\n", GetLastError());
		return;
	}


	// Write 14 bytes to our new file
	if (WriteFile(FileHandle, "This is a test", 14, &BytesWritten, NULL) == 0)
	{
		printf("WriteFile failed with error %d\n", GetLastError());
		return;
	}

	if (CloseHandle(FileHandle) == 0)
	{
		printf("CloseHandle failed with error %d\n", GetLastError());
		return;
	}
}