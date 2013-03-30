#include <tchar.h>
#include <iostream>
#include <cstdlib>
#include "CommAudio.h"
#include "client-file.h"
#include "libzplay.h"

using namespace std;
using namespace libZPlay;

// The Server to connect to 
//const char * server = "cookie-monster";
//char * server = "localhost";
char * server;

int song_sock;
ZPlay * netplay;
HINSTANCE hInst;
const string SERVICE_REQUEST_STRING = "list-services\n";

struct ChannelInfo {
   string name;
   SOCKET sock;   
   sockaddr_in addr;
};

int comm_connect (const char * host, int port) {
	hostent	*hp;
	sockaddr_in server;
	SOCKET sd = socket(AF_INET, SOCK_STREAM, 0);

	// Initialize and set up the address structure
	memset((char *)&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(port);
	
	if ((hp = gethostbyname(host)) == NULL)
	{
		cerr << "Unknown server address" << endl;
		exit(1);
	}

	// Copy the server address
	memcpy((char *)&server.sin_addr, hp->h_addr, hp->h_length);

	// Connecting to the server
	if (connect (sd, (struct sockaddr *)&server, sizeof(server)) == -1)
	{
		cerr << "Can't connect to server\n" << endl;
		exit(1);
	}

	return sd;
}

void request_services(SOCKET sock) {   
   send(sock, SERVICE_REQUEST_STRING.c_str(), SERVICE_REQUEST_STRING.length(), 0);
}

string recv_services (int sd) {
	string out;
	char data[BUFSIZE];
	while (true) {
		int bytes_recv = recv(sd, data, BUFSIZE, 0);
		if (bytes_recv > 0) {
			out.append(data, bytes_recv);
			// Look for the terminating line: its either M true\n or M false\n.
			size_t n = out.find("M true\n");
			if (n == string::npos)
				n = out.find("M false\n");
			// Found last line.
			if (n != string::npos)
				break;
		} else break;
	}
	return out;
}

void stream_song () {
	// Close stream
	netplay->Close();

	// Open new stream
	int i;

	// we open the zplay stream without any real data, and start playback when we actually get input.
	int result = netplay->OpenStream(1, 1, &i, 2, sfPCM);
	if(result == 0) {
		cerr << "Error: " <<  netplay->GetError() << endl;
		netplay->Release();
		return;
	}

	netplay->Play();

	// Mainloop: just recv() remote microphone data and push it to dynamic stream
	while ( true ) {
		// Create a buffer with maximum udp packet size, and recvfrom into it.
		char buf[65507];
		
		sockaddr_in server;
		int size = sizeof(server);
		int r = recvfrom (song_sock, buf, 65507, 0, (sockaddr*)&server, &size);
		if ( r == -1 ) {
			int err = WSAGetLastError();
			if (err == 10054)
				printf("Concoction recent by Pier.\n"); // Connection reset by peer.
			else printf("get last error %d\n", err);
			break;
		}
		
		//printf("%lu got %d \n", GetTickCount(), r);
		if (r == 0)
			continue;

		// Might need to protect this with mutex if client GUI modifies netplay as well.
		netplay->PushDataToStream(buf, r);

	}

	closesocket(song_sock);
}

DWORD WINAPI stream_song_proc(LPVOID lpParamter) {
	stream_song();
	return 0;
}

ChannelInfo extractChannelInfo(const string& channel) {
   ChannelInfo ci;

   ci.name = "The Peak"; // TODO: un-hardcode
   ci.addr.sin_family = AF_INET;
   ci.addr.sin_addr.s_addr = inet_addr("234.5.6.7");
   ci.addr.sin_port = htons(8910);
      
   return ci;
}

DWORD WINAPI join_channel(LPVOID lpParamter) {
   int error;
   bool reuseFlag = false;
   SOCKADDR_IN localAddr, sourceAddr;
   struct ip_mreq stMreq;

   // Parse channel info
   //ChannelInfo ci = extractChannelInfo(*channel);         
   ChannelInfo ci = extractChannelInfo(string("fsdfsdF"));         

   if ((ci.sock = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET) {
      // TODO: error handling
   }

   if ((error = setsockopt(ci.sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuseFlag, sizeof(reuseFlag))) == SOCKET_ERROR) {
      // TODO: error handling
   }

   localAddr.sin_family = AF_INET;
   localAddr.sin_addr.s_addr = htonl(INADDR_ANY);
   localAddr.sin_port = htons(8910);

   if ((error = bind(ci.sock, (struct sockaddr*)&localAddr, sizeof(localAddr))) == SOCKET_ERROR) {
      // TODO: error handling
   }

   // Join multicast group
   memset((char *)&stMreq, 0, sizeof(ip_mreq));
   stMreq.imr_multiaddr.s_addr = inet_addr("234.5.6.7");//ci.addr.sin_addr.s_addr;
   stMreq.imr_interface.s_addr = INADDR_ANY;   

   if ((error = setsockopt(ci.sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, (char*)&stMreq, sizeof(stMreq))) == SOCKET_ERROR) {
      // TODO: error handling
   }

   netplay->Play();
      
	size_t buffed = 0;

	while (true) {
      // Create a buffer with maximum udp packet size, and recvfrom into it.
		char * buf = new char[65507];

      int addr_size = sizeof(struct sockaddr_in);
      int r = recvfrom(ci.sock, buf, 65507, 0, (struct sockaddr*)&sourceAddr, &addr_size);
      		
		if ( r == -1 ) {
			int err = WSAGetLastError();
			if (err == 10054)
				printf("Concoction recent by Pier.\n"); // Connection reset by peer.
			else printf("get last error %d\n", err);
			break;
		}
		printf("%lu got %d \n", GetTickCount(), r);
		netplay->PushDataToStream(buf, r);
		buffed += r;
		
		//if (buffed > 800000)
		//	netplay->Play();


		delete buf;
		if (r == 0)
			break;
	}   

   return 0;
}

int APIENTRY _tWinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance, LPTSTR lpCmdLine, int nCmdShow) {
	MSG msg;
	HWND hwnd;
	HACCEL hAccelTable;

	// Open up a Winsock v2.2 session
	WSADATA wsaData;
	WORD wVersionRequested = MAKEWORD(2,2);
	WSAStartup(wVersionRequested, &wsaData);

	// Create song stream listening UDP socket.
	song_sock = create_udp_socket(1338);

	// Create zplay instance for playing remote mic
	netplay = CreateZPlay();
	netplay->SetSettings(sidSamplerate, 44100);// 44100 samples
	netplay->SetSettings(sidChannelNumber, 2);// 2 channel
	netplay->SetSettings(sidBitPerSample, 16);// 16 bit
	netplay->SetSettings(sidBigEndian, 0); // little endian

	netplay->Play();

	CreateThread(NULL, 0, stream_song_proc, (LPVOID)NULL, 0, NULL);

	MyRegisterClass(hInstance);

	// Perform application initialization:
	if (!InitInstance (hInstance, nCmdShow, hwnd)) {
		return FALSE;
	}

	hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_DCWIN1));

	// Main message loop:
	while (GetMessage(&msg, NULL, 0, 0) )
	{
		//
		//if(!IsDialogMessage(hwnd, &msg)) // Slow!
		if (!TranslateAccelerator(hwnd, hAccelTable, &msg) && !IsDialogMessage(hwnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	WSACleanup();
	return (int) msg.wParam;
}
