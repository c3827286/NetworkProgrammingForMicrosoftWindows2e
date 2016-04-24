//
// This is a special implementation of 2 classes IPv6Address and IPv6EndPoint
// that can enable your .Net Sockets application to address IPv6 functionality 
// in the .Net Application Frameworks Version 1. Version 1 of the Frameworks 
// does not provide this addressing functionality. At some future release, this 
// functionality is expected to be natively available. When it does become
// available, this code should be considered obsolete.
//
using System;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Text;
using System.Collections;

public class IPv6Address {
   private const string WS2_32 = "ws2_32.dll";
    
    [StructLayout(LayoutKind.Sequential)]
    internal struct WSAData {
        public short wVersion;
        public short wHighVersion;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=257)]
        public string szDescription;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst=129)]
        public string szSystemStatus;
        public short iMaxSockets;
        public short iMaxUdpDg;
        public int lpVendorInfo;
    }
    
    [DllImport(WS2_32, CharSet=CharSet.Ansi, SetLastError=true)]
    internal static extern int WSAStartup(
        [In] short wVersionRequested,
        [Out] out WSAData lpWSAData);
    
    [StructLayout(LayoutKind.Sequential)]
    internal struct sockaddr_in6 {
        public short sin6_family;
        public ushort sin6_port;
        public uint sin6_flowinfo;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst=16)]
        public byte[] sin6_addr;
        public uint sin6_scope_id;
    }
    
    [DllImport(WS2_32, CharSet=CharSet.Ansi, SetLastError=true)]
    internal static extern int WSAStringToAddress(
        [In] string addressString,
        [In] AddressFamily addressFamily,
        [In] IntPtr lpProtocolInfo, // always passing in a 0
        [In, Out] ref sockaddr_in6 socketAddress,
        [In, Out] ref int socketAddressSize);
    
    [DllImport(WS2_32, CharSet=CharSet.Ansi, SetLastError = true)]
    internal static extern int WSAAddressToString(
        [In] ref sockaddr_in6 socketAddress,             
        [In] int AddressLength,              
        [In] IntPtr ProtocolInfo,  
        [Out] StringBuilder AddressString,       
        [In, Out] ref int AddressStringLength);
    
    internal const int IPv6AddressSize = 16;
    internal byte [] m_Address = new byte[IPv6AddressSize];
    internal uint m_ScopeIdentifier;
    
    public static readonly IPv6Address Any = IPv6Address.Parse("::");
    public static readonly IPv6Address Loopback = IPv6Address.Parse("::1");
    
    static IPv6Address() {
        WSAData wsaData = new WSAData(); 
    
        int ret = WSAStartup((short)0x0202, // we need 2.2
                             out wsaData);
    	if (ret != 0) {
    	    throw new ArgumentException("WSAStartup failed");
    	}
    }
    
    public AddressFamily AddressFamily {
        get {
            return AddressFamily.InterNetworkV6;
        }
    }
    
    internal IPv6Address() {
    	for (int i = 0; i < IPv6AddressSize; i++) {
    	    m_Address[i] = 0;
        }
    }
    
    public IPv6Address(byte[] address, uint scope) {
    	if (address == null)  {
    	    throw new ArgumentNullException("Bad byte address");
    	}
    
    	if (address.Length != IPv6AddressSize) {
    	    throw new ArgumentException("Bad byte address length");
    	}
    
    	for (int i = 0; i < IPv6AddressSize; i++) {
    	    m_Address[i] = address[i];
    	}
        m_ScopeIdentifier = scope;
    }
    
    public byte this [int index] {
        get {
            return m_Address[index];
        }
    }
    
    public uint Scope {
    	get {
    	    return m_ScopeIdentifier;
        }
    }
    
    private const string NumberFormat = "{0:X2}";
    
    public override string ToString() {
    
        int StrLength = 256;
        StringBuilder sb = new StringBuilder(StrLength);
    
        sockaddr_in6 saddr = new sockaddr_in6();
        saddr.sin6_family = (int)AddressFamily.InterNetworkV6;
        saddr.sin6_scope_id = m_ScopeIdentifier;
        saddr.sin6_addr = m_Address;
    
        int errorCode = WSAAddressToString(ref saddr, 28,
                                           IntPtr.Zero,
                                           sb,
                                           ref StrLength);
    
        return sb.ToString();
    }
    
    public static IPv6Address Parse(string address) {
        if (address == null) {
            throw new ArgumentNullException("Bad string address");
        }

        TryAgain:
    
        int Size = 28;
        sockaddr_in6 SockAddrIN6 = new sockaddr_in6();
    
        int ret = WSAStringToAddress(address, AddressFamily.InterNetworkV6,
                                     IntPtr.Zero, ref SockAddrIN6, ref Size); 
    	if (ret != 0) {
            int Err = Marshal.GetLastWin32Error();

            if (Err == 10093) { 
                WSAData wsaData = new WSAData(); 
    
                ret = WSAStartup((short)0x0202, out wsaData);
    	        if (ret != 0) {
    	            throw new ArgumentException("WSAStartup failed");
    	        }
                goto TryAgain;
            }

            throw new ArgumentException("WSAStringToAddress failed for " + address + " error " + Err.ToString());
        }
    
        IPv6Address instance = new IPv6Address(SockAddrIN6.sin6_addr, 
                                               SockAddrIN6.sin6_scope_id);
        return instance;
    }
}

public class IPv6EndPoint : IPEndPoint {
    private IPv6Address m_Address;
    internal long m_FlowInformation;
    private const int SockAddrSize = 28;
    
    public override System.Net.Sockets.AddressFamily AddressFamily {
    	get {
            return System.Net.Sockets.AddressFamily.InterNetworkV6;
        }
    }
    
    public IPv6EndPoint(string address, int port) : base(0,0) {
        if (address == null) {
            throw new ArgumentNullException("address");
        }
        Port = port;
        m_Address = IPv6Address.Parse(address);
    }
    
    public IPv6EndPoint(IPv6Address address, int port) : base(0,0) {
        if (address==null) {
            throw new ArgumentNullException("address");
        }
        Port = port;
        m_Address = address;
    }
    
    public IPv6EndPoint(IPv6Address address, int port, long flowInformation) : base(0,0) {
        if (address==null) {
            throw new ArgumentNullException("address");
        }
        Port = port;
        m_Address = address;
        m_FlowInformation = flowInformation;
    }
    
    public new IPv6Address Address {
        get {
            return m_Address;
        }
        set {
            m_Address = value;
        }
    }
    
    public override string ToString() 
    {
        return "[" + Address.ToString() + "]" + ":" + Port.ToString();
    }
    
    public long FlowInformation 
    {
        get {
            return m_FlowInformation;
        }
        set {
            m_FlowInformation = value;
        }
    }
    
    public override SocketAddress Serialize() 
    {
        SocketAddress socketAddress = new SocketAddress(this.AddressFamily, SockAddrSize);
        //
        // populate it
        //
        socketAddress[2] = (byte)((this.Port>> 8) & 0xFF);
        socketAddress[3] = (byte)((this.Port    ) & 0xFF);
    
        socketAddress[4] = (byte)((this.FlowInformation>>24) & 0xFF);
        socketAddress[5] = (byte)((this.FlowInformation>>16) & 0xFF);
        socketAddress[6] = (byte)((this.FlowInformation>> 8) & 0xFF);
        socketAddress[7] = (byte)((this.FlowInformation    ) & 0xFF);
    
        for (int i = 0; i < IPv6Address.IPv6AddressSize; i++) {
            socketAddress[8 + i] = this.Address[i];
        }
    
        socketAddress[24] = (byte)((this.Address.m_ScopeIdentifier    ) & 0xFF);
        socketAddress[25] = (byte)((this.Address.m_ScopeIdentifier>> 8) & 0xFF);
        socketAddress[26] = (byte)((this.Address.m_ScopeIdentifier>>16) & 0xFF);
        socketAddress[27] = (byte)((this.Address.m_ScopeIdentifier>>24) & 0xFF);
    
        return socketAddress;
    }
    
    public override EndPoint Create(SocketAddress socketAddress) 
    {
        int port =
            (socketAddress[2]<< 8 & 0x0000FF00) |
            (socketAddress[3]     & 0x000000FF);
    
        int flowInformation =
            socketAddress[4]<<24 |
            socketAddress[5]<<16 |
            socketAddress[6]<< 8 |
            socketAddress[7];
    
        byte [] V6Array = new byte[IPv6Address.IPv6AddressSize];
    
        for (int i = 0; i < IPv6Address.IPv6AddressSize; i++)
            V6Array[i] = socketAddress[8 + i];
    
        uint scope = (uint)
            (socketAddress[27]<<24 |
            socketAddress[26]<<16 |
            socketAddress[25]<< 8 |
            socketAddress[24]);
    
        IPv6Address ipv6Address = new IPv6Address(V6Array, scope);
    
        IPv6EndPoint created = new IPv6EndPoint(ipv6Address, port, flowInformation);
        return created;
    }
}
