Attribute VB_Name = "servervb"
Public Declare Function CreateMailslot Lib "kernel32" Alias "CreateMailslotA" (ByVal lpName As String, ByVal nMaxMessageSize As Long, ByVal lReadTimeout As Long, ByVal lpSecurityAttributes As Long) As Long
Public Declare Function ReadFile Lib "kernel32" (ByVal hFile As Long, ByVal lpBuffer As String, ByVal nNumberOfBytesToRead As Long, lpNumberOfBytesRead As Long, ByVal lpOverlapped As Long) As Long
Public Declare Function CloseHandle Lib "kernel32" (ByVal hObject As Long) As Long
Public Const INVALID_HANDLE_VALUE = &HFFFFFFFF
Public Const MAILSLOT_WAIT_FOREVER = (-1)

Sub Main()
   Dim Mailslot As Long, NumberOfBytesRead As Long
   Dim buffer As String
   Dim dwRet As Long

   MsgBox "Entering Sub Main..."
   
   MsgBox "This server doesn't have a UI. It does a ReadFile, display a message when it comes in, and exits"
   ' Create the mailslot
   Mailslot = CreateMailslot("\\.\mailslot\myslot", 0, _
      MAILSLOT_WAIT_FOREVER, 0)
     
   If Mailslot = INVALID_HANDLE_VALUE Then
      MsgBox "Failed to create a MailSlot " & Err.LastDllError
      Exit Sub
   End If
   
   buffer = String(256, 0)
   
   dwRet = ReadFile(Mailslot, buffer, 256, NumberOfBytesRead, 0)
   If dwRet <> 0 Then
      buffer = Left(buffer, NumberOfBytesRead)
      MsgBox buffer
   Else
      MsgBox "Failed to read a MailSlot " & Err.LastDllError
   End If
   
   CloseHandle Mailslot
   
   MsgBox "Exiting Sub Main..."
   
End Sub


