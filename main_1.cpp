#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#undef UNICODE

#include <iostream>
#include <string>
#include <queue>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <fstream>
#include <sstream>
#include <vector>
#include <atomic>
#include <chrono>

#include <Windows.h>
#include <windowsx.h>

#include <objidl.h>
#include <gdiplus.h>
#pragma comment (lib, "Gdiplus.lib")

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "Ws2_32.lib")

#define SCREEN_STREAM_PORT 27016
#define SCREEN_STREAM_FPS 20
#define SCREEN_STREAM_QUALITY 60 // JPEG quality

#define BTN_MODE 1
#define BTN_START 2
#define BTN_PAUSE 3
#define BTN_TERMINATE 4
#define BTN_CONNECT 5
#define BTN_DISCONNECT 6
#define EDIT_ADDRESS 7
#define BTN_SERVER 8
#define BTN_CLIENT 9
#define EDIT_PORT 10

#define MENU_FILE 10
#define MENU_SUB 11
#define MENU_EXIT 12
#define MENU_ABOUT 13

#define DEFAULT_PORT 27015
#define MAX_CLIENTS 10

// Server screen dim is nScreenWidth[0], nScreenHeight[0]
// Client screen dim is nScreenWidth[1], nScreenHeight[1]
int nScreenWidth[2] = { 1920 , 2560 };
int nScreenHeight[2] = { 1080 , 1440 };

const int nNormalized = 65535;

// GDI+ helpers for screen streaming
ULONG_PTR gdiplusToken;
void InitGDIPlus() {
	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
}
void ShutdownGDIPlus() {
	Gdiplus::GdiplusShutdown(gdiplusToken);
}
HBITMAP CaptureScreenBitmap(int& width, int& height) {
	HDC hScreenDC = GetDC(NULL);
	HDC hMemDC = CreateCompatibleDC(hScreenDC);
	width = GetSystemMetrics(SM_CXSCREEN);
	height = GetSystemMetrics(SM_CYSCREEN);
	HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
	HGDIOBJ oldObj = SelectObject(hMemDC, hBitmap);
	BitBlt(hMemDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
	SelectObject(hMemDC, oldObj);
	DeleteDC(hMemDC);
	ReleaseDC(NULL, hScreenDC);
	return hBitmap;
}
bool BitmapToJPEGBuffer(HBITMAP hBitmap, std::vector<BYTE>& outBuffer, int quality = 60) {
	using namespace Gdiplus;
	Bitmap bmp(hBitmap, NULL);
	CLSID clsidEncoder;
	UINT num = 0, size = 0;
	GetImageEncodersSize(&num, &size);
	if (size == 0) return false;
	std::vector<BYTE> buffer(size);
	ImageCodecInfo* pImageCodecInfo = (ImageCodecInfo*)buffer.data();
	GetImageEncoders(num, size, pImageCodecInfo);
	for (UINT j = 0; j < num; ++j) {
		if (wcscmp(pImageCodecInfo[j].MimeType, L"image/jpeg") == 0) {
			clsidEncoder = pImageCodecInfo[j].Clsid;
			break;
		}
	}
	IStream* istream = NULL;
	CreateStreamOnHGlobal(NULL, TRUE, &istream);
	EncoderParameters params;
	params.Count = 1;
	params.Parameter[0].Guid = EncoderQuality;
	params.Parameter[0].Type = EncoderParameterValueTypeLong;
	params.Parameter[0].NumberOfValues = 1;
	ULONG q = quality;
	params.Parameter[0].Value = &q;
	if (bmp.Save(istream, &clsidEncoder, &params) == Ok) {
		STATSTG stat;
		istream->Stat(&stat, STATFLAG_NONAME);
		DWORD bufSize = (DWORD)stat.cbSize.QuadPart;
		outBuffer.resize(bufSize);
		LARGE_INTEGER li = { 0 };
		istream->Seek(li, STREAM_SEEK_SET, NULL);
		ULONG read = 0;
		istream->Read(outBuffer.data(), bufSize, &read);
		istream->Release();
		return true;
	}
	istream->Release();
	return false;
}
HBITMAP JPEGBufferToBitmap(const BYTE* data, size_t len) {
	using namespace Gdiplus;
	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
	memcpy(GlobalLock(hMem), data, len);
	GlobalUnlock(hMem);
	IStream* istream = NULL;
	CreateStreamOnHGlobal(hMem, FALSE, &istream);
	Bitmap* bmp = Bitmap::FromStream(istream, FALSE);
	HBITMAP hBitmap = NULL;
	bmp->GetHBITMAP(Gdiplus::Color(0, 0, 0), &hBitmap);
	delete bmp;
	istream->Release();
	GlobalFree(hMem);
	return hBitmap;
}

// ================================================
// =================WINDOWS SOCKETS================
// ================================================
// code largely taken from
// https://docs.microsoft.com/en-us/windows/win32/winsock/complete-server-code
// https://docs.microsoft.com/en-us/windows/win32/winsock/complete-client-code
int InitializeServer(SOCKET& sktListen, int port) {
	struct addrinfo* result = NULL;
	struct addrinfo hints;

	ZeroMemory(&hints, sizeof(hints));

	//AF_INET for IPV4
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;

	//Resolve the local address and port to be used by the server
	int iResult = getaddrinfo(NULL, std::to_string(port).c_str(), &hints, &result);

	if (iResult != 0) {
		std::cout << "getaddrinfo failed: " << iResult << std::endl;
		WSACleanup();
		return 1;
	}

	//Create a SOCKET for the server to listen for client connections

	sktListen = socket(result->ai_family, result->ai_socktype, result->ai_protocol);


	if (sktListen == INVALID_SOCKET) {
		std::cout << "Error at socket(): " << WSAGetLastError() << std::endl;
		WSACleanup();
		freeaddrinfo(result);
		return 1;
	}

	//Setup the TCP listening socket
	iResult = bind(sktListen, result->ai_addr, (int)result->ai_addrlen);


	if (iResult == SOCKET_ERROR) {
		std::cout << "bind failed with error: " << WSAGetLastError() << std::endl;
		WSACleanup();
		freeaddrinfo(result);
		return 1;
	}

	freeaddrinfo(result);
	return 0;
}

int InitializeScreenStreamServer(SOCKET & sktListen, int port) {
	return InitializeServer(sktListen, port);
}

int BroadcastInput(std::vector<SOCKET> vsktSend, INPUT* input) {
	int iResult = 0;

	for (auto& sktSend : vsktSend) {
		if (sktSend != INVALID_SOCKET) {

			iResult = send(sktSend, (char*)input, sizeof(INPUT), 0);
			if (iResult == SOCKET_ERROR) {
				std::cout << "send failed: " << WSAGetLastError() << std::endl;
			}
		}
	}
	return 0;
}
int TerminateServer(SOCKET& sktListen, std::vector<SOCKET>& sktClients) {

	int iResult;
	for (auto& client : sktClients) {
		if (client != INVALID_SOCKET) {
			iResult = shutdown(client, SD_SEND);
			if (iResult == SOCKET_ERROR) {
				std::cout << "shutdown failed: " << WSAGetLastError() << std::endl;
			}
			closesocket(client);
		}
	}
	closesocket(sktListen);
	//WSACleanup();
	return 0;
}
int InitializeClient() {
	//WSADATA wsaData;
	//int iResult;

	//// Initialize Winsock
	//iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	//if (iResult != 0) {
	//	std::cout << "WSAStartup failed with error: " << iResult << std::endl;
	//	return 1;
	//}
	return 0;
}
int ConnectServer(SOCKET& sktConn, std::string serverAdd, int port) {
	int iResult;
	struct addrinfo* result = NULL,
		* ptr = NULL,
		hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	// Resolve the server address and port
	iResult = getaddrinfo(serverAdd.c_str(), std::to_string(port).c_str(), &hints, &result);
	if (iResult != 0) {
		std::cout << "getaddrinfo failed with error: " << iResult << std::endl;
		WSACleanup();
		return 1;
	}

	// Create a SOCKET for connecting to server
	sktConn = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (sktConn == INVALID_SOCKET) {
		std::cout << "socket failed with error: " << WSAGetLastError() << std::endl;
		WSACleanup();
		return 1;
	}

	// Connect to server.
	iResult = connect(sktConn, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR) {
		closesocket(sktConn);
		sktConn = INVALID_SOCKET;
	}

	freeaddrinfo(result);

	if (sktConn == INVALID_SOCKET) {
		std::cout << "Unable to connect to server" << std::endl;
		WSACleanup();
		return 1;
	}
	return 0;
}

int ConnectScreenStreamServer(SOCKET & sktConn, std::string serverAdd, int port) {
	return ConnectServer(sktConn, serverAdd, port);
}

int ReceiveServer(SOCKET sktConn, INPUT& data) {
	INPUT* buff = new INPUT;
	//std::cout << "receiving..." << std::endl;
	int iResult = recv(sktConn, (char*)buff, sizeof(INPUT), 0);
	if (iResult == sizeof(INPUT))
	{
		//std::cout << "Bytes received: " << iResult << std::endl;
	}
	else if (iResult == 0) {
		std::cout << "Connection closed" << std::endl;
		delete buff;
		return 1;
	}
	else if (iResult < sizeof(INPUT))
	{
		int bytes_rec = iResult;
		//int count = 0;
		while (bytes_rec < sizeof(INPUT))
		{
			//std::cout << "Received partial input: " << count << " - " << bytes_rec << " bytes of " << sizeof(INPUT) << std::endl;
			bytes_rec += recv(sktConn, (char*)buff + bytes_rec, sizeof(INPUT) - bytes_rec, 0);
			//count++;
		}
	}
	else {
		std::cout << "Receive failed with error: " << WSAGetLastError() << std::endl;
		delete buff;
		return 1;
	}
	data = *buff;
	delete buff;
	return 0;
}
int CloseConnection(SOCKET* sktConn) {
	closesocket(*sktConn);
	//WSACleanup();
	return 0;
}

// =================== SCREEN STREAM SERVER =====================

std::atomic<bool> g_screenStreamActive(false);
std::atomic<size_t> g_screenStreamBytes(0);
std::atomic<int> g_screenStreamFPS(0);
std::atomic<int> g_screenStreamW(0);
std::atomic<int> g_screenStreamH(0);

void ScreenStreamServerThread(SOCKET sktClient) {
	 using namespace std::chrono;
	 int width = 0, height = 0;
	 int fps = SCREEN_STREAM_FPS;
	 int quality = SCREEN_STREAM_QUALITY;
	 int frameInterval = 1000 / fps;
	
	 g_screenStreamActive = true;
	 g_screenStreamBytes = 0;
	 g_screenStreamFPS = 0;
	
	 auto lastPrint = steady_clock::now();
	 int frames = 0;
	 size_t bytes = 0;
	
	 while (g_screenStreamActive) {
		 auto start = steady_clock::now();
		 HBITMAP hBitmap = CaptureScreenBitmap(width, height);
		 std::vector<BYTE> jpgBuffer;
		 if (!BitmapToJPEGBuffer(hBitmap, jpgBuffer, quality)) {
			 DeleteObject(hBitmap);
			 continue;
			}
		 DeleteObject(hBitmap);
		 uint32_t sz = jpgBuffer.size();
		 uint32_t szNet = htonl(sz);
		
		 int ret = send(sktClient, (const char*)&szNet, sizeof(szNet), 0);
		 if (ret != sizeof(szNet)) break;
		 size_t offset = 0;
		 while (offset < sz) {
			 int sent = send(sktClient, (const char*)(jpgBuffer.data() + offset), sz - offset, 0);
			 if (sent <= 0) goto END;
			 offset += sent;
			 }
		 frames++;
		 bytes += sz + sizeof(szNet);
		 g_screenStreamW = width;
		 g_screenStreamH = height;
		
		 auto now = steady_clock::now();
		 if (duration_cast<seconds>(now - lastPrint).count() >= 1) {
			 g_screenStreamFPS = frames;
			 g_screenStreamBytes = bytes;
			 frames = 0;
			 bytes = 0;
			 lastPrint = now;
			 }
		 auto elapsed = duration_cast<milliseconds>(steady_clock::now() - start).count();
	     if (elapsed < frameInterval) Sleep((DWORD)(frameInterval - elapsed));
		 }
	 END:
	 closesocket(sktClient);
	 g_screenStreamActive = false;
	 }

// ============ SCREEN STREAM CLIENT WINDOW ============

LRESULT CALLBACK ScreenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	 static HBITMAP hBitmap = NULL;
	 static int imgW = 0, imgH = 0;
	 switch (msg) {
	 case WM_USER + 1: {
		 if (hBitmap) DeleteObject(hBitmap);
		 hBitmap = (HBITMAP)lParam;
		 imgW = LOWORD(wParam);
		 imgH = HIWORD(wParam);
		 InvalidateRect(hwnd, NULL, FALSE);
		 break;
		 }
	 case WM_USER + 2: {
		 SetWindowTextA(hwnd, (const char*)lParam);
		 break;
		 }
	 case WM_PAINT: {
		 PAINTSTRUCT ps;
		 HDC hdc = BeginPaint(hwnd, &ps);
		 if (hBitmap) {
			 HDC hMem = CreateCompatibleDC(hdc);
			 HGDIOBJ oldObj = SelectObject(hMem, hBitmap);
			 BITMAP bm;
			 GetObject(hBitmap, sizeof(bm), &bm);
			 StretchBlt(hdc, 0, 0, ps.rcPaint.right, ps.rcPaint.bottom, hMem, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
			 SelectObject(hMem, oldObj);
			 DeleteDC(hMem);
			 }
		 EndPaint(hwnd, &ps);
		 break;
		 }
	 case WM_DESTROY:
		 if (hBitmap) DeleteObject(hBitmap);
		 break;
	 default:
		 return DefWindowProc(hwnd, msg, wParam, lParam);
		 }
	 return 0;
     }

void ScreenRecvThread(SOCKET skt, HWND hwnd, std::string ip) {
	 size_t bytesLastSec = 0;
	 int framesLastSec = 0;
	 int scrW = 0, scrH = 0;
	 auto lastSec = std::chrono::steady_clock::now();
	 while (true) {
		 uint32_t szNet = 0;
		 int ret = recv(skt, (char*)&szNet, sizeof(szNet), MSG_WAITALL);
		 if (ret != sizeof(szNet)) break;
		 uint32_t sz = ntohl(szNet);
		 std::vector<BYTE> buffer(sz);
		 size_t offset = 0;
		 while (offset < sz) {
			 int got = recv(skt, (char*)(buffer.data() + offset), sz - offset, 0);
			 if (got <= 0) goto END;
			 offset += got;
		     }
		 HBITMAP hBmp = JPEGBufferToBitmap(buffer.data(), buffer.size());
		 BITMAP bmInfo;
		 GetObject(hBmp, sizeof(bmInfo), &bmInfo);
		 scrW = bmInfo.bmWidth;
		 scrH = bmInfo.bmHeight;
		 PostMessage(hwnd, WM_USER + 1, MAKELPARAM(scrW, scrH), (LPARAM)hBmp);
		 bytesLastSec += sz + sizeof(szNet);
		 framesLastSec++;
		 auto now = std::chrono::steady_clock::now();
		 if (std::chrono::duration_cast<std::chrono::seconds>(now - lastSec).count() >= 1) {
			 double mbps = (bytesLastSec * 8.0) / 1e6;
			 char title[256];
			 snprintf(title, sizeof(title), "Remote Screen | IP: %s | FPS: %d | Mbps: %.2f | Size: %dx%d",
				 ip.c_str(), framesLastSec, mbps, scrW, scrH);
			 PostMessage(hwnd, WM_USER + 2, 0, (LPARAM)title);
			 bytesLastSec = 0;
			 framesLastSec = 0;
			 lastSec = now;
			 }
			}
	 END:
	 PostMessage(hwnd, WM_CLOSE, 0, 0);
	 closesocket(skt);
	}

void StartScreenRecv(std::string server_ip, int port) {
	 SOCKET skt = INVALID_SOCKET;
	 if (ConnectScreenStreamServer(skt, server_ip, port) != 0) {
		 MessageBoxA(NULL, "Failed to connect to screen stream server!", "Remote", MB_OK | MB_ICONERROR);
		 return;