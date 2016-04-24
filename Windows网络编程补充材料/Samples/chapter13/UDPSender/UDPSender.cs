using System;
using System.Net;
using System.Net.Sockets;

namespace UDPSender
{
	//    This sample illustrates how to develop a simple UDP sender application
	//    that can send a simple "hello" message to a UDP receiver awaiting datagrams
	//    on port 5150. This sample is implemented as a console-style application and 
	//    simply prints status messages as data is sent to the server.

	class UDPSenderClass
	{
		static void Main(string[] args)
		{
			int Port = 5150;

			// Verify arguments
			if (args.Length == 0)
			{
				Console.WriteLine("Usage: UDPClient.exe <server name | IP address> [port]");
				return;
			}
			if (args.Length >= 2)
			{
				Port = System.Convert.ToInt32(args[1].ToString()); 
			}

			try
			{
				IPHostEntry IPHost = Dns.GetHostByName(args[0].ToString());

				Console.Write("Sending data to: ");
				Console.Write(IPHost.AddressList.GetValue(0).ToString());
				Console.WriteLine(":" + Port.ToString());

				IPEndPoint ServerEp = new IPEndPoint(IPHost.AddressList[0], Port);

				Socket MySocket = new Socket(
					AddressFamily.InterNetwork, 
					SocketType.Dgram, 
					ProtocolType.Udp
				);

				String s = "Hello - This is a test";

				System.Byte[] buf = System.Text.Encoding.ASCII.GetBytes(s.ToCharArray());

				int BytesSent = MySocket.SendTo(buf, ServerEp);

				Console.WriteLine("Successfully sent " + BytesSent.ToString() + " byte(s)");

				MySocket.Close();
			}

			catch (System.Net.Sockets.SocketException err)
			{
				Console.WriteLine("Error: " + err.Message);
			}
		}
	}
}
