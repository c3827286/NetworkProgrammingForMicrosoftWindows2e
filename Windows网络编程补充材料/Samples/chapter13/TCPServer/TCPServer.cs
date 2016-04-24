using System;
using System.Net;
using System.Net.Sockets;
using System.Collections;

namespace TCPServer
{
	//    This sample illustrates how to develop a simple TCP ECHO server application
	//    that listens for a TCP connection on port 5150 and echos data received
	//    from a client connection. This sample is implemented as a console-style 
	//    application and simply prints status messages when a connection is accepted
	//    and closed by the server. This sample is designed to demonstrate how to
	//    use the asynchronous IO model of .Net Sockets.
	//
	//    The sample is simple in that for every IO operation per socket there is only
	//    one outstanding asynchronous Send or Receive call posted. Realistically for
	//    an application like this, it would be more desirable to post multiple Receives
	//    per socket and manage the IO in an ordered fashion as it is received. Since
	//    this is just a sample we decided to keep things simple to improve readability. 

	class TCPServerClass
	{
		private const int ListeningPort = 5150;
		private const int MaxAsyncAccepts = 5;
		private const int DataBufferSize = 8192;

		static void Main(string[] args)
		{
			int Port = ListeningPort;

			// Verify arguments
			if (args.Length < 1 || (args[0] != "IPv4" && args[0] != "IPv6"))
			{
				Console.WriteLine("Usage: TCPServer.exe <IPv4 | IPv6> [port]");
				return;
			}
			if (args.Length >= 2)
			{
				Port = System.Convert.ToInt32(args[1].ToString()); 
			}

			try
			{
				Socket ListeningSocket;
			
				if (args[0] == "IPv4")
				{				
					// Create a new socket to listening for client connections.

					ListeningSocket = new Socket(
						AddressFamily.InterNetwork, 
						SocketType.Stream, 
						ProtocolType.IP);

					// Setup a SOCKADDR_IN structure that will tell bind that we
					// want to listen for connections on all interfaces using port
					// 5150.

					IPEndPoint LocalEndPoint = new IPEndPoint(IPAddress.Any, Port);
							
					ListeningSocket.Bind(LocalEndPoint);
				}
				else // IPv6
				{
					// Create a new socket to listening for client connections.

					ListeningSocket = new Socket(
						AddressFamily.InterNetworkV6, 								
						SocketType.Stream, 
						ProtocolType.IP);

					IPv6EndPoint LocalEndPoint = new IPv6EndPoint(IPv6Address.Any, Port);
							
					ListeningSocket.Bind(LocalEndPoint);
				}

				ListeningSocket.Listen(5);

				Console.WriteLine("Server started - Press RETURN to stop the server.");
				Console.WriteLine("Awaiting socket connections...");

				AcceptInfo AI = new AcceptInfo(ListeningSocket);

				for(int i = 0; i < MaxAsyncAccepts; i++)
				{
					AI.ListeningSocket.BeginAccept(AI.AcceptCallback, AI);
				}

				Console.ReadLine();

				ListeningSocket.Close();
				AI.RemoveAllSockets();

				Console.WriteLine("Pending connections were closed - press RETURN to stop application");
				Console.ReadLine();
			}

			catch (SocketException err)
			{
				Console.WriteLine("Error: " + err.Message);
			}			
		}

		public class SocketInfo
		{
			public Socket s;
			public AsyncCallback AsyncReceiveCallback = new AsyncCallback(ProcessReceiveResults);
			public AsyncCallback AsyncSendCallback = new AsyncCallback(ProcessSendResults);
			public byte [] Buffer = new byte[DataBufferSize];
			public AcceptInfo AI;


			public SocketInfo(Socket NewSocket, AcceptInfo AI)
			{
				this.s = NewSocket;
				this.AI = AI;
			}

			static void ProcessReceiveResults(IAsyncResult ar)
			{
				SocketInfo SI = (SocketInfo) ar.AsyncState;

				try
				{
					int BytesReceived = SI.s.EndReceive(ar);

					if (BytesReceived == 0)
					{
						SI.AI.RemoveSocket(SI.s);
					}
					else
					{
						SI.s.BeginSend(SI.Buffer, 0, BytesReceived, SocketFlags.None,
							SI.AsyncSendCallback, SI);
					}
				}
				catch (ObjectDisposedException)
				{
					// Receiving socket was closed
					return;
				}
				catch (SocketException)
				{
					SI.AI.RemoveSocket(SI.s);
				}
			}

			static void ProcessSendResults(IAsyncResult ar)
			{
				SocketInfo SI = (SocketInfo) ar.AsyncState;

				int BytesSent;

				try
				{
					BytesSent = SI.s.EndSend(ar);

					SI.s.BeginReceive(SI.Buffer, 0, SI.Buffer.Length, 
						SocketFlags.None, SI.AsyncReceiveCallback, SI);
				}
				catch (ObjectDisposedException)
				{
					// Sending socket was closed
					return;
				}
				catch (SocketException)
				{
					SI.AI.RemoveSocket(SI.s);
				}
			}
		}

		public class AcceptInfo
		{
			public AsyncCallback AcceptCallback = new AsyncCallback(ProcessAcceptResults);
			public Socket ListeningSocket;
			private static ArrayList PendingSocketList = new ArrayList();

			public AcceptInfo(Socket ListeningSocket)
			{
				this.ListeningSocket = ListeningSocket;
			}

			public void RemoveSocket(Socket s)
			{
				Console.WriteLine("Closing Socket " + s.RemoteEndPoint.ToString());

				lock(PendingSocketList.SyncRoot) 
				{
					PendingSocketList.Remove(s);
				}
				s.Close();
			}

			public void RemoveAllSockets()
			{
				lock(PendingSocketList.SyncRoot) 
				{
					for(int i = 0; i < PendingSocketList.Count; i++)
					{
						Socket s = (Socket) PendingSocketList[i];
						Console.WriteLine("Closing Socket " + s.RemoteEndPoint.ToString());
						s.Close();
					}
				}
			}

			public static void AddSocket(Socket s)
			{
				lock(PendingSocketList.SyncRoot) 
				{
					PendingSocketList.Add(s);
				}
			}

			static void ProcessAcceptResults(IAsyncResult ar)
			{
				AcceptInfo AI = (AcceptInfo) ar.AsyncState;

				Socket AcceptedSocket;

				try
				{
					AcceptedSocket = AI.ListeningSocket.EndAccept(ar);
				}
				catch (ObjectDisposedException)
				{
					// Listening socket was closed
					return;
				}
				catch (SocketException)
				{
					return;
				}

				SocketInfo SI = new SocketInfo(AcceptedSocket, AI);

				Console.Write("Successfully received a connection from ");
				Console.WriteLine(AcceptedSocket.RemoteEndPoint.ToString());

				try
				{
					SI.s.BeginReceive(SI.Buffer, 0, SI.Buffer.Length, 
						SocketFlags.None, SI.AsyncReceiveCallback, SI);
				}
				catch (SocketException)
				{
					SI.AI.RemoveSocket(SI.s);
				}

				try
				{
					AI.ListeningSocket.BeginAccept(AI.AcceptCallback, AI);
				}
				catch (SocketException)
				{
					return;
				}

				AddSocket(AcceptedSocket);
			}
		}
	}
}
