The vbnbdgrm sample demonstrates connectionless (datagram) communications 
using NetBios calls. The sample could run either as a sender or as a receiver.

To run as a receiver:
1. Specify the local Netbios name: Rcvr
2. Specify the role as Receiver (Default)
3. Click "Recv" Button. At this point, the app blocks until it has received 
   data from the sender. Otherwise you will need it kill it from the task manager

After the receiver is running, you can start run the app as the sender:
1. Specify the local Netbios name: Sndr
2. Specify the role as Sender, and specify Rcvr as the name to send to.
3. Click "Send" Button. At this point, the app blocks until it has completed the sends.

To run the sample to send and receive broadcast, select Broadcast in the operation mode. 
The sample also demonstrates using NCBASTAT to retrieve the MAC address on lana 0.