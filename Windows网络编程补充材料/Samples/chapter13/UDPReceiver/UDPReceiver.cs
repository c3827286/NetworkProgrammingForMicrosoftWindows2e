using System;

namespace UDPReceiver
{
	//    This sample illustrates how to develop a simple UDP receiver application
	//    that awaits for 1 datagram on port 5150. This sample is implemented as a 
	//    console-style application and simply prints status messages when data is
	//    received.
	class UDPReceiverClass
	{
		static void Main(string[] args)
		{
			int Port = 5150;

			try
			{
				System.Net.IPAddress IPAddress;

				IPAddress = System.Net.IPAddress.Any;

				System.Net.IPEndPoint LocalEP = new System.Net.IPEndPoint(IPAddress, Port);

				System.Net.Sockets.Socket ReceivingSocket = new System.Net.Sockets.Socket(
					System.Net.Sockets.AddressFamily.InterNetwork, 
					System.Net.Sockets.SocketType.Dgram, 
					System.Net.Sockets.ProtocolType.Udp);

				ReceivingSocket.Bind(LocalEP);

				System.Console.WriteLine("Waiting for data...");
				
				byte [] buffer = new byte[1024];

				System.Net.IPAddress RemoteIPAddress = System.Net.IPAddress.Any;
				System.Net.IPEndPoint RemoteIPEndPoint = new System.Net.IPEndPoint(
					RemoteIPAddress, 0);
				System.Net.SocketAddress RemoteAddress = new System.Net.SocketAddress(
					System.Net.Sockets.AddressFamily.InterNetwork);
				System.Net.EndPoint RefRemoteEP = RemoteIPEndPoint.Create(RemoteAddress);

				int BytesReceived = ReceivingSocket.ReceiveFrom(buffer, ref RefRemoteEP);				

				System.Console.WriteLine("Successfully received " +
					BytesReceived.ToString() + " byte(s) from " +
					RefRemoteEP.ToString());

				ReceivingSocket.Close();
			}

			catch (System.Net.Sockets.SocketException err)
			{
				Console.WriteLine("Error: " + err.Message);
			}
		}
	}
}
