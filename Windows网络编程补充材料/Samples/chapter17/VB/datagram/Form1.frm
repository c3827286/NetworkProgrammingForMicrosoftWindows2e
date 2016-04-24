VERSION 5.00
Begin VB.Form Form1 
   Caption         =   "Form1"
   ClientHeight    =   5490
   ClientLeft      =   60
   ClientTop       =   345
   ClientWidth     =   11130
   LinkTopic       =   "Form1"
   ScaleHeight     =   5490
   ScaleWidth      =   11130
   StartUpPosition =   3  'Windows Default
   Begin VB.CheckBox chkUnique 
      Caption         =   "Unique (uncheck for group)"
      Height          =   255
      Left            =   6000
      TabIndex        =   24
      Top             =   120
      Value           =   1  'Checked
      Width           =   2655
   End
   Begin VB.TextBox txtLocalName 
      Height          =   285
      Left            =   4200
      TabIndex        =   23
      Text            =   "LocalName"
      Top             =   120
      Width           =   1575
   End
   Begin VB.ListBox recvList 
      Height          =   4740
      Left            =   7320
      TabIndex        =   21
      Top             =   600
      Width           =   3615
   End
   Begin VB.Frame Frame4 
      Caption         =   "Step 4: Select I/O attribute"
      Height          =   1095
      Left            =   3960
      TabIndex        =   16
      Top             =   1800
      Width           =   3255
      Begin VB.TextBox txtDelay 
         Height          =   285
         Left            =   2760
         TabIndex        =   19
         Text            =   "0"
         Top             =   720
         Width           =   375
      End
      Begin VB.TextBox txtNumDatagram 
         Height          =   285
         Left            =   2760
         TabIndex        =   17
         Text            =   "5"
         Top             =   240
         Width           =   375
      End
      Begin VB.Label Label4 
         Caption         =   "delay between sends"
         Height          =   255
         Left            =   120
         TabIndex        =   20
         Top             =   720
         Width           =   2055
      End
      Begin VB.Label Label3 
         Caption         =   "num of datagrams to send or recv:"
         Height          =   255
         Left            =   120
         TabIndex        =   18
         Top             =   360
         Width           =   2895
      End
   End
   Begin VB.Frame Frame3 
      Caption         =   "Step 2: Select role"
      Height          =   1215
      Left            =   1440
      TabIndex        =   11
      Top             =   480
      Width           =   5775
      Begin VB.CheckBox chkRecvAny 
         Caption         =   "Recv on any name?"
         Height          =   255
         Left            =   120
         TabIndex        =   15
         Top             =   720
         Width           =   1935
      End
      Begin VB.TextBox txtRecipient 
         Height          =   285
         Left            =   2280
         TabIndex        =   14
         Text            =   "RemoteName"
         Top             =   720
         Width           =   2535
      End
      Begin VB.OptionButton optSender 
         Caption         =   "Sender"
         Height          =   255
         Left            =   2280
         TabIndex        =   13
         Top             =   360
         Width           =   855
      End
      Begin VB.OptionButton optReceiver 
         Caption         =   "Receiver"
         Height          =   255
         Left            =   120
         TabIndex        =   12
         Top             =   360
         Value           =   -1  'True
         Width           =   1215
      End
      Begin VB.Label Label6 
         Caption         =   "Specify remote NB name, unnecessary for broadcast"
         Height          =   495
         Left            =   3480
         TabIndex        =   25
         Top             =   240
         Width           =   2175
      End
      Begin VB.Line Line1 
         X1              =   2160
         X2              =   2160
         Y1              =   120
         Y2              =   1200
      End
   End
   Begin VB.Frame Frame2 
      Caption         =   "Step 3: Select opeation mode"
      Height          =   1095
      Left            =   1440
      TabIndex        =   8
      Top             =   1800
      Width           =   2415
      Begin VB.OptionButton optBroadcast 
         Caption         =   "Broadcast Datagram"
         Height          =   255
         Left            =   120
         TabIndex        =   10
         Top             =   720
         Width           =   1935
      End
      Begin VB.OptionButton optDirect 
         Caption         =   "Direct Datagram"
         Height          =   255
         Left            =   120
         TabIndex        =   9
         Top             =   360
         Value           =   -1  'True
         Width           =   1815
      End
   End
   Begin VB.Frame Frame1 
      Caption         =   "Option for send, do you want to send on a specific lana"
      Height          =   855
      Left            =   1440
      TabIndex        =   4
      Top             =   3000
      Width           =   5775
      Begin VB.TextBox txtOneLana 
         Height          =   285
         Left            =   4320
         TabIndex        =   6
         Text            =   "0"
         Top             =   360
         Width           =   855
      End
      Begin VB.CheckBox chkOneLana 
         Caption         =   "Yes"
         Height          =   255
         Left            =   360
         TabIndex        =   5
         Top             =   360
         Width           =   855
      End
      Begin VB.Label Label2 
         Caption         =   "specify which lana to send from"
         Height          =   255
         Left            =   1320
         TabIndex        =   7
         Top             =   360
         Width           =   2535
      End
   End
   Begin VB.ListBox ListLana 
      Height          =   3375
      Left            =   120
      TabIndex        =   2
      Top             =   480
      Width           =   975
   End
   Begin VB.CommandButton cmdSendRecv 
      Caption         =   "Send/Recv"
      Height          =   855
      Left            =   360
      TabIndex        =   1
      Top             =   4200
      Width           =   3015
   End
   Begin VB.CommandButton Command1 
      Caption         =   "NCBASTAT on LANA 0"
      Height          =   855
      Left            =   3720
      TabIndex        =   0
      Top             =   4200
      Width           =   2775
   End
   Begin VB.Label Label5 
      Caption         =   "Step 1: Specify the local NB name:"
      Height          =   255
      Left            =   1560
      TabIndex        =   22
      Top             =   120
      Width           =   2775
   End
   Begin VB.Label Label1 
      Caption         =   "Available Lana"
      Height          =   255
      Left            =   120
      TabIndex        =   3
      Top             =   120
      Width           =   1215
   End
End
Attribute VB_Name = "Form1"
Attribute VB_GlobalNameSpace = False
Attribute VB_Creatable = False
Attribute VB_PredeclaredId = True
Attribute VB_Exposed = False
Option Explicit

Dim bOneLana As Boolean
Dim dwOneLana As Long
Dim bBroadcast As Boolean
Dim bSender As Boolean
Dim bUniqueName As Boolean
Dim bRecvAny As Boolean
Dim dwNumDatagrams As Long
Dim dwDelay As Long
Dim dwNum(255) As Long
Dim lenum As LANA_ENUM
Dim dwBytesRead As Long, dwErr As Long
Dim szMessage As String
Dim i As Long, j As Long
Dim byteMessage As UserBuffer 'used in sends

Dim byteRecvMessage(254) As UserBuffer 'used in recvs
Dim ncbRecv(254) As NCB ' used in recv
Dim eventRecv(254) As Long 'used in recv




Private Sub chkOneLana_Click()
    bOneLana = CBool(chkOneLana.Value)
    dwOneLana = CLng(txtOneLana.Text)
    End Sub

Private Sub chkRecvAny_Click()
bRecvAny = CBool(chkRecvAny.Value)
End Sub

Private Sub chkUnique_Click()
    bUniqueName = CBool(chkUnique.Value)
End Sub




   Sub Command1_Click()
       Dim myNcb As NCB
       Dim bRet As Byte
       ZeroMemory myNcb, Len(myNcb)
       myNcb.ncb_command = NCBRESET
       bRet = Netbios(myNcb)

       myNcb.ncb_command = NCBASTAT
       myNcb.ncb_lana_num = 0
       myNcb.ncb_callname(0) = Asc("*")

       Dim myASTAT As ASTAT, tempASTAT As ASTAT
       Dim pASTAT As Long
       myNcb.ncb_length = Len(myASTAT)
       Debug.Print Err.LastDllError
       pASTAT = HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS _
                Or HEAP_ZERO_MEMORY, myNcb.ncb_length)
       If pASTAT = 0 Then
          Debug.Print "memory allcoation failed!"
          Exit Sub
       End If
       myNcb.ncb_buffer = pASTAT
       bRet = Netbios(myNcb)
       Debug.Print Err.LastDllError
       CopyMemory myASTAT, myNcb.ncb_buffer, Len(myASTAT)
       MsgBox Hex(myASTAT.adapt.adapter_address(0)) & " " & _
              Hex(myASTAT.adapt.adapter_address(1)) _
              & " " & Hex(myASTAT.adapt.adapter_address(2)) & " " _
              & Hex(myASTAT.adapt.adapter_address(3)) _
              & " " & Hex(myASTAT.adapt.adapter_address(4)) & " " _
              & Hex(myASTAT.adapt.adapter_address(5))
       HeapFree GetProcessHeap(), 0, pASTAT
   End Sub


Private Sub cmdSendRecv_Click()
    Dim dwRet As Long
    
    cmdSendRecv.Enabled = False
    If bOneLana = True Then
        If bUniqueName = True Then
            AddName dwOneLana, txtLocalName.Text, dwNum(0)
        Else
            AddGroupName dwOneLana, txtLocalName.Text, dwNum(0)
        End If
    Else
        For i = 0 To lenum.length - 1
            If bUniqueName = True Then
                AddName lenum.lana(i), txtLocalName, dwNum(i)
            Else
                AddGroupName lenum.lana(i), txtLocalName, dwNum(i)
            End If
        Next
    End If
    
    If bSender = True Then 'as a sender
        If bBroadcast = True Then 'broadcast
            If bOneLana = True Then
                For j = 0 To dwNumDatagrams - 1
                    szMessage = "Test datagram No. " & j
                    
                    lstrcpyn VarPtr(byteMessage), szMessage, Len(szMessage) + 1
                    dwRet = DatagramSendBC(dwOneLana, dwNum(0), VarPtr(byteMessage), Len(szMessage) + 1)
                    If dwRet <> NRC_GOODRET Then
                        MsgBox "DatagramSendBC failed with " & dwRet & " on Lana " & dwOneLana
                        'Exit Sub
                    Else
                        Sleep dwDelay
                    End If
                Next
            Else 'broadcast the message on every LANA
                For j = 0 To dwNumDatagrams - 1
                For i = 0 To lenum.length - 1
                
                    szMessage = "Test datagram No. " & j
                   
                    lstrcpyn VarPtr(byteMessage), szMessage, Len(szMessage) + 1
                    dwRet = DatagramSendBC(lenum.lana(i), dwNum(i), VarPtr(byteMessage), Len(szMessage) + 1)
                    If dwRet <> NRC_GOODRET Then
                        MsgBox "DatagramSendBC failed with " & dwRet & " on Lana " & lenum.lana(i)
                        'Exit Sub
                    Else
                        Sleep dwDelay
                    End If
                Next
                Next
            End If
        Else 'unicast
            If bOneLana = True Then
                For j = 0 To dwNumDatagrams - 1
                    szMessage = "Test datagram No. " & j
                    
                    lstrcpyn VarPtr(byteMessage), szMessage, Len(szMessage) + 1
                    dwRet = DatagramSend(dwOneLana, dwNum(0), txtRecipient.Text, VarPtr(byteMessage), Len(szMessage) + 1)
                    If dwRet <> NRC_GOODRET Then
                        MsgBox "DatagramSend failed with " & dwRet & " on Lana " & dwOneLana
                        'Exit Sub
                    Else
                        Sleep dwDelay
                    End If
                Next
            Else
                For j = 0 To dwNumDatagrams - 1
                For i = 0 To lenum.length - 1
                
                    szMessage = "Test datagram No. " & j
                   
                    lstrcpyn VarPtr(byteMessage), szMessage, Len(szMessage) + 1
                    dwRet = DatagramSend(lenum.lana(i), dwNum(i), txtRecipient.Text, VarPtr(byteMessage), Len(szMessage) + 1)
                    If dwRet <> NRC_GOODRET Then
                        MsgBox "DatagramSend failed with " & dwRet & " on Lana " & lenum.lana(i)
                        'Exit Sub
                    Else
                        Sleep dwDelay
                    End If
                Next
                Next
           End If
        End If
    Else 'as a receiver''''''''''''''''''''''''''''''''''''''''''''''''
        For i = 0 To lenum.length - 1
            eventRecv(i) = CreateEvent(0, 1, 0, vbNullString)
        Next
        
        If bBroadcast = True Then 'start of broadcast
            If bOneLana = True Then
                'post synchronous broadcast recv on the LANA specified
                For j = 0 To dwNumDatagrams - 1
                    dwRet = DatagramRecvBC(ncbRecv(0), dwOneLana, dwNum(0), VarPtr(byteRecvMessage(0)), MAX_DATAGRAM_SIZE, 0)
                    If dwRet <> NRC_GOODRET Then
                        MsgBox "DatagramRecvBC failed with " & dwRet & " on LANA " & dwOneLana
                        Exit Sub
                    Else
                        Dim tempMessageStr As String
                        Dim tempSenderStr As String
                        
                        tempMessageStr = String(512, 0)
                        tempSenderStr = String(512, 0)
                        FormatNetbiosName ncbRecv(0).ncb_callname, tempSenderStr
                       
                        lstrcpy tempMessageStr, VarPtr(byteRecvMessage(0))
                        recvList.AddItem "LANA " & ncbRecv(0).ncb_lana_num & " recv from: " & tempSenderStr & ": " & tempMessageStr
                    End If
                Next
            Else
                'post asyncrhonous broadcast recv
                For j = 0 To dwNumDatagrams - 1
                    For i = 0 To lenum.length - 1
                        dwBytesRead = MAX_DATAGRAM_SIZE
                        dwRet = DatagramRecvBC(ncbRecv(i), lenum.lana(i), dwNum(i), VarPtr(byteRecvMessage(i)), MAX_DATAGRAM_SIZE, eventRecv(i))
                        If dwRet <> NRC_GOODRET Then
                            MsgBox "DatagramRecvBC failed with " & dwRet & " on LANA " & dwOneLana
                            Exit Sub
                        End If
                    Next
                    
                    dwRet = WaitForMultipleObjects(lenum.length, eventRecv(0), 0, INFINITE)
                    
                    If dwRet = WAIT_FAILED Then
                        MsgBox "WaitForMultipleObjects failed"
                        Exit Sub
                    End If
                    
                    For i = 0 To lenum.length - 1
                        If ncbRecv(i).ncb_cmd_cplt = NRC_PENDING Then
                            Cancel ncbRecv(i)
                        Else
                       
                            tempMessageStr = String(512, 0)
                            tempSenderStr = String(512, 0)
                            FormatNetbiosName ncbRecv(i).ncb_callname, tempSenderStr
                       
                            lstrcpy tempMessageStr, VarPtr(byteRecvMessage(i))
                            recvList.AddItem "LANA " & ncbRecv(i).ncb_lana_num & " recv from: " & tempSenderStr & ": " & tempMessageStr
                            ResetEvent eventRecv(i)
                        End If
                         
                    Next
                Next
            End If
        Else 'unicast recv
            If bOneLana = True Then
                For j = 0 To dwNumDatagrams - 1
                    If bRecvAny = True Then
                        dwRet = DatagramRecv(ncbRecv(0), dwOneLana, &HFF, VarPtr(byteRecvMessage(0)), MAX_DATAGRAM_SIZE, 0)
                        If dwRet <> NRC_GOODRET Then
                            MsgBox "DatagramRecv failed with " & dwRet & " on LANA " & dwOneLana
                            Exit Sub
                        End If
                    Else
                        dwRet = DatagramRecv(ncbRecv(0), dwOneLana, dwNum(0), VarPtr(byteRecvMessage(0)), MAX_DATAGRAM_SIZE, 0)
                        If dwRet <> NRC_GOODRET Then
                            MsgBox "DatagramRecv failed with " & dwRet & " on LANA " & dwOneLana
                            Exit Sub
                        End If
                   
                    End If
                        
                    tempMessageStr = String(512, 0)
                    tempSenderStr = String(512, 0)
                    FormatNetbiosName ncbRecv(0).ncb_callname, tempSenderStr
                       
                    lstrcpy tempMessageStr, VarPtr(byteRecvMessage(0))
                    recvList.AddItem "LANA " & ncbRecv(0).ncb_lana_num & " recv from: " & tempSenderStr & ": " & tempMessageStr
                Next
            
            Else
            'post asynchronous recv on each lana
                For j = 0 To dwNumDatagrams - 1
                    For i = 0 To lenum.length - 1
                        If bRecvAny = True Then
                            dwRet = DatagramRecv(ncbRecv(i), lenum.lana(i), &HFF, VarPtr(byteRecvMessage(i)), MAX_DATAGRAM_SIZE, eventRecv(i))
                            If dwRet <> NRC_GOODRET Then
                                MsgBox "DatagramRecv failed with " & dwRet & " on LANA " & dwOneLana
                                Exit Sub
                            End If
                        Else
                            dwRet = DatagramRecv(ncbRecv(i), lenum.lana(i), dwNum(i), VarPtr(byteRecvMessage(i)), MAX_DATAGRAM_SIZE, eventRecv(i))
                            If dwRet <> NRC_GOODRET Then
                                MsgBox "DatagramRecv failed with " & dwRet & " on LANA " & dwOneLana
                                Exit Sub
                            End If
                        End If
                    Next 'i
                    
                    dwRet = WaitForMultipleObjects(lenum.length, eventRecv(0), 0, INFINITE)
                    
                    If dwRet = WAIT_FAILED Then
                        MsgBox "WaitForMultipleObjects failed"
                        Exit Sub
                    End If
                    
                    For i = 0 To lenum.length - 1
                        If ncbRecv(i).ncb_cmd_cplt = NRC_PENDING Then
                            Cancel ncbRecv(i)
                        Else
                        
                            tempMessageStr = String(512, 0)
                            tempSenderStr = String(512, 0)
                            FormatNetbiosName ncbRecv(i).ncb_callname, tempSenderStr
                       
                            lstrcpy tempMessageStr, VarPtr(byteRecvMessage(i))
                            recvList.AddItem "LANA " & ncbRecv(i).ncb_lana_num & " recv from: " & tempSenderStr & ": " & tempMessageStr
                            ResetEvent eventRecv(i)
                        End If
                    Next i
                Next 'j
            End If
        End If 'end if bBroadcast recv
        
        For i = 0 To lenum.length - 1
            CloseHandle eventRecv(i)
        Next
    
    End If 'end of recevier
                
    If bOneLana = True Then
        DelName dwOneLana, txtLocalName.Text
    Else
        For i = 0 To lenum.length - 1
            DelName lenum.lana(i), txtLocalName.Text
        Next
    End If
    
        
    MsgBox "Done this round of send/recvs. "
    
    cmdSendRecv.Enabled = True
    
    
End Sub

Private Sub Form_Load()

    bOneLana = False
    chkOneLana.Value = 0
    dwOneLana = 0
    bBroadcast = False
    bSender = False
    bUniqueName = True
    bRecvAny = False
    dwNumDatagrams = 5
    dwDelay = 0
    cmdSendRecv.Caption = "Recv"
   
    dwErr = LanaEnum(lenum)
    If dwErr <> NRC_GOODRET Then
        MsgBox "LanaEnum failed: " & dwErr
        Exit Sub
    End If
    
    For i = 0 To lenum.length - 1
        ListLana.AddItem lenum.lana(i)
    Next i
    
    dwErr = ResetAll(lenum, MAX_SESSIONS, MAX_NAMES, False)
    If dwErr <> NRC_GOODRET Then
        MsgBox "ResetAll failed: " & dwErr
        Exit Sub
    End If
    

End Sub


Private Sub optBroadcast_Click()
    bBroadcast = True
End Sub

Private Sub optDirect_Click()
    bBroadcast = False
End Sub

Private Sub optReceiver_Click()
    bSender = False
    cmdSendRecv.Caption = "Recv"
    
End Sub

Private Sub optSender_Click()
    bSender = True
    cmdSendRecv.Caption = "Send"
End Sub

Private Sub txtDelay_Change()
    dwDelay = CLng(txtDelay.Text)
End Sub



Private Sub txtNumDatagram_Change()
    dwNumDatagrams = CLng(txtNumDatagram.Text)
End Sub

Private Sub txtOneLana_Change()
    dwOneLana = CLng(txtOneLana.Text)
End Sub
