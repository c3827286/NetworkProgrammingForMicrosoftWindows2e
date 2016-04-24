Attribute VB_Name = "clientvb"
Option Explicit
Public Declare Function CreateFile Lib "kernel32" Alias "CreateFileA" (ByVal lpFileName As String, ByVal dwDesiredAccess As Long, ByVal dwShareMode As Long, ByVal lpSecurityAttributes As Long, ByVal dwCreationDisposition As Long, ByVal dwFlagsAndAttributes As Long, ByVal hTemplateFile As Long) As Long
Public Declare Function WriteFile Lib "kernel32" (ByVal hFile As Long, ByVal lpBuffer As String, ByVal nNumberOfBytesToWrite As Long, lpNumberOfBytesWritten As Long, ByVal lpOverlapped As Long) As Long
Public Declare Function CloseHandle Lib "kernel32" (ByVal hObject As Long) As Long
Public Const GENERIC_WRITE = &H40000000
Public Const GENERIC_READ = &H80000000
Public Const FILE_SHARE_READ = &H1
Public Const FILE_SHARE_WRITE = &H2
Public Const CREATE_ALWAYS = 2
Public Const FILE_ATTRIBUTE_NORMAL = &H80
Public Const INVALID_HANDLE_VALUE = &HFFFFFFFF
Public Const OPEN_EXISTING = 3

Sub Main()
    Dim Mailslot As Long, BytesWritten As Long
    Dim ServerName As String
    Dim dwRet As Long
    
    MsgBox "Entering Sub Main..."
    
    ServerName = InputBox("Please enter the server name: ")
    ServerName = "\\" & ServerName & "\mailslot\myslot"
    
    Mailslot = CreateFile(ServerName, GENERIC_WRITE, _
        FILE_SHARE_READ, 0, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, _
        0)
    
    If Mailslot = INVALID_HANDLE_VALUE Then
        MsgBox "CreateFile failed with error " & Err.LastDllError
        Exit Sub
    End If

    dwRet = WriteFile(Mailslot, "This is a test", 14, BytesWritten, 0)
    
    If dwRet = 0 Then
        MsgBox "WriteFile failed with error " & Err.LastDllError
        Exit Sub
    End If
    
    MsgBox "Wrote " & BytesWritten & " bytes"

    CloseHandle Mailslot
    
    MsgBox "Exiting Sub Main..."
End Sub

