using System;
using System.Net;
using System.Net.Sockets;

namespace TCPClient
{
	//    This sample illustrates how to develop a simple TCP client application
	//    that can send a simple "hello" message to a TCP server listening on port 5150.
	//    This sample is implemented as a console-style application and simply prints
	//    status messages as a connection is made and when data is sent to the server.

	class TCPClientClass
	{
		static void Main(string[] args)
		{
			int Port = 5150;

			// Verify arguments
			if (args.Length < 2 || (args[0] != "IPv4" && args[0] != "IPv6"))
			{
				Console.WriteLine("Usage: TCPClient.exe <IPv4 | IPv6>  <server name | IPv4 address | IPv6 address> [port]");
				return;
			}
			if (args.Length >= 3)
			{
				Port = System.Convert.ToInt32(args[2].ToString()); 
			}

			try
			{
				Socket MySocket;

				if (args[0] == "IPv4")
				{
					IPHostEntry IPHost = Dns.GetHostByName(args[1].ToString());

					Console.Write("Connecting to: ");
					Console.Write(IPHost.AddressList.GetValue(0).ToString());
					Console.WriteLine(":" + Port.ToString());

					IPEndPoint ServerEndPoint = new IPEndPoint(IPHost.AddressList[0], Port);

					MySocket = new Socket(
						AddressFamily.InterNetwork, 
						SocketType.Stream, 
						ProtocolType.IP);

					MySocket.Connect(ServerEndPoint);
				}
				else // IPv6
				{
					IPv6Address ServerAddress = IPv6Address.Parse(args[1]);
					IPv6EndPoint ServerEndPoint = new IPv6EndPoint(ServerAddress, Port);

					MySocket = new Socket(
						AddressFamily.InterNetworkV6, 
						SocketType.Stream, 
						ProtocolType.IP);

					MySocket.Connect(ServerEndPoint);
				}

				String s = "Hello - This is a test";

				Byte[] buf = System.Text.Encoding.ASCII.GetBytes(s.ToCharArray());

				int BytesSent = MySocket.Send(buf);

				System.Console.WriteLine("Successfully sent " + BytesSent.ToString() + " byte(s)");

				MySocket.Close();
			}

			catch (System.Net.Sockets.SocketException err)
			{
				Console.WriteLine("Error: " + err.Message);
			}
		}
	}
}
