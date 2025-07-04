#define QOI_IMPLEMENTATION
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
#include <cstdint>

#define NOMINMAX
#include <Windows.h>
#include <windowsx.h>

#include <objidl.h>
#include "qoi.h"
#include "BasicBitmap.h"
#include "vite.h"
#include "vite.c"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <set> // DIRTY TILE
#include <algorithm> // DIRTY TILE
#pragma comment(lib, "Ws2_32.lib")

static inline void set_bit(uint8_t* bm, size_t idx) { bm[idx >> 3] |= (1 << (idx & 7)); }
static inline bool get_bit(const uint8_t* bm, size_t idx) { return (bm[idx >> 3] & (1 << (idx & 7))) != 0; }

enum class RemoteCtrlType : uint8_t {
	SetQuality = 1,
	SetFps = 2
};
#pragma pack(push, 1)
struct RemoteCtrlMsg {
	RemoteCtrlType type;
	uint8_t value; // quality: [1-4], fps: [5,10,20,30,40,60]
};

// --- DIRTY TILE STRUCT ---
// Replace this struct with your existing one if it differs
struct DirtyTile {
	int left, top, right, bottom;
};

constexpr int TILE_W = 32;
constexpr int TILE_H = 32;


// --- Per-window state using BasicBitmap for framebuffer (no hBitmap or dibBits needed) ---
// State for each streamed window
struct ScreenBitmapState {
	BasicBitmap* bmp = nullptr;
	int imgW = 0;
	int imgH = 0;
	CRITICAL_SECTION cs;
	SOCKET* psktInput = nullptr;
	ScreenBitmapState() { InitializeCriticalSection(&cs); }
	~ScreenBitmapState() { if (bmp) delete bmp; DeleteCriticalSection(&cs); }
};

/**
 * Compare two 32bpp RGBA framebuffers and collect dirty tiles.
 * A simple line-by-line approach: every run of changed pixels on a scanline is a tile.
 */
 // Tile detection with bitmask
void detect_dirty_tiles_bitmask(const uint8_t* prev, const uint8_t* curr, int width, int height, uint8_t* bitmask, size_t ntiles_x, size_t ntiles_y) {
	memset(bitmask, 0, ((ntiles_x * ntiles_y + 7) / 8));
	for (size_t ty = 0; ty < ntiles_y; ++ty) {
		for (size_t tx = 0; tx < ntiles_x; ++tx) {
			int tileLeft = tx * TILE_W;
			int tileTop = ty * TILE_H;
			int tileW = std::min(TILE_W, width - tileLeft);
			int tileH = std::min(TILE_H, height - tileTop);
			bool dirty = false;
			for (int row = 0; row < tileH && !dirty; ++row) {
				int y = tileTop + row;
				const uint8_t* prevRow = prev + (y * width + tileLeft) * 4;
				const uint8_t* currRow = curr + (y * width + tileLeft) * 4;
				if (memcmp(prevRow, currRow, tileW * 4) != 0) {
					dirty = true;
				}
			}
			if (dirty) set_bit(bitmask, ty * ntiles_x + tx);
		}
	}
}

// Extract a tile from a source RGBA buffer into a new BasicBitmap
BasicBitmap* extract_tile_basicbitmap(const uint8_t* rgba, int width, int height, const DirtyTile& r) {
	int rw = r.right - r.left, rh = r.bottom - r.top;
	BasicBitmap* tile = new BasicBitmap(rw, rh, BasicBitmap::A8R8G8B8);
	for (int row = 0; row < rh; ++row) {
		const uint8_t* src = rgba + ((r.top + row) * width + r.left) * 4;
		uint8_t* dst = tile->Bits() + row * rw * 4;
		memcpy(dst, src, rw * 4);
	}
	return tile;
}

// --- QOI encode a subimage ---
bool QOIEncodeSubimage_BasicBitmap(
	const std::vector<uint8_t>& rgba, int width, int height, const DirtyTile& r, std::vector<uint8_t>& outQoi
) {
	int rw = r.right - r.left, rh = r.bottom - r.top;
	BasicBitmap tile(rw, rh, BasicBitmap::A8R8G8B8);

	for (int row = 0; row < rh; ++row) {
		const uint8_t* src = &rgba[((r.top + row) * width + r.left) * 4];
		uint8_t* dst = tile.Bits() + row * rw * 4;
		memcpy(dst, src, rw * 4);
	}

	qoi_desc desc;
	desc.width = tile.Width();
	desc.height = tile.Height();
	desc.channels = 4;
	desc.colorspace = QOI_SRGB;
	int out_len = 0;
	void* qoi_data = qoi_encode(tile.Bits(), &desc, &out_len);
	if (!qoi_data) return false;
	outQoi.resize(out_len);
	memcpy(outQoi.data(), qoi_data, out_len);
	free(qoi_data);
	return true;
}

class MainWindow; // forward declaration, so 'extern MainWindow* ...' is legal
extern MainWindow* g_pMainWindow;


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
// --- Context menu command IDs for the remote screen window ---
#define IDM_VIDEO_QUALITY     6001
#define IDM_VIDEO_QUALITY_1   6002
#define IDM_VIDEO_QUALITY_2   6003
#define IDM_VIDEO_QUALITY_3   6004
#define IDM_VIDEO_QUALITY_4   6005
#define IDM_VIDEO_QUALITY_5   6006

#define IDM_VIDEO_FPS         6010
#define IDM_VIDEO_FPS_5       6011
#define IDM_VIDEO_FPS_10      6012
#define IDM_VIDEO_FPS_20      6013
#define IDM_VIDEO_FPS_30      6014
#define IDM_VIDEO_FPS_40      6015
#define IDM_VIDEO_FPS_60      6016

#define IDM_ALWAYS_ON_TOP     6020

#define IDM_SENDKEYS          6030
#define IDM_SENDKEYS_ALTF4    6031
#define IDM_SENDKEYS_CTRLESC  6032
#define IDM_SENDKEYS_CTRALTDEL 6033
#define IDM_SENDKEYS_PRNTSCRN 6034

std::atomic<int> g_streamingFps(SCREEN_STREAM_FPS);
std::atomic<int> g_streamingQuality(SCREEN_STREAM_QUALITY);

// --- State variables for menu ---
static bool g_alwaysOnTop = false;
static int g_screenStreamMenuQuality = SCREEN_STREAM_QUALITY / 20; // 1-4 (default 3)
static int g_screenStreamMenuFps = SCREEN_STREAM_FPS; // 5, 10, 20, 30, 40, 60
static int g_screenStreamActualQuality = SCREEN_STREAM_QUALITY;
static int g_screenStreamActualFps = SCREEN_STREAM_FPS;

// --- Function prototypes for menu logic ---
HMENU CreateScreenContextMenu();
void SetRemoteScreenQuality(HWND hwnd, int qualityLevel);
void SetRemoteScreenFps(HWND hwnd, int fps);
void SendRemoteKeyCombo(HWND hwnd, int combo);

// --- Helper for context menu creation ---
HMENU CreateScreenContextMenu() {
	HMENU hMenu = CreatePopupMenu();

	// Video Quality submenu (choose levels 1-4)
	HMENU hQualityMenu = CreatePopupMenu();
	AppendMenuA(hQualityMenu, MF_STRING | (g_screenStreamMenuQuality == 1 ? MF_CHECKED : 0), IDM_VIDEO_QUALITY_1, "1 (Low)");
	AppendMenuA(hQualityMenu, MF_STRING | (g_screenStreamMenuQuality == 2 ? MF_CHECKED : 0), IDM_VIDEO_QUALITY_2, "2");
	AppendMenuA(hQualityMenu, MF_STRING | (g_screenStreamMenuQuality == 3 ? MF_CHECKED : 0), IDM_VIDEO_QUALITY_3, "3");
	AppendMenuA(hQualityMenu, MF_STRING | (g_screenStreamMenuQuality == 4 ? MF_CHECKED : 0), IDM_VIDEO_QUALITY_4, "4");
	AppendMenuA(hQualityMenu, MF_STRING | (g_screenStreamMenuQuality == 5 ? MF_CHECKED : 0), IDM_VIDEO_QUALITY_5, "5 (High)");
	AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hQualityMenu, "Video Quality");

	// Video FPS submenu
	HMENU hFpsMenu = CreatePopupMenu();
	const int fpsVals[] = { 5, 10, 20, 30, 40, 60 };
	const int fpsIDs[] = { IDM_VIDEO_FPS_5, IDM_VIDEO_FPS_10, IDM_VIDEO_FPS_20, IDM_VIDEO_FPS_30, IDM_VIDEO_FPS_40, IDM_VIDEO_FPS_60 };
	for (int i = 0; i < 6; ++i) {
		AppendMenuA(hFpsMenu, MF_STRING | (g_screenStreamMenuFps == fpsVals[i] ? MF_CHECKED : 0), fpsIDs[i], std::to_string(fpsVals[i]).c_str());
	}
	AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hFpsMenu, "Video FPS");

	// Always On Top
	AppendMenuA(hMenu, MF_STRING | (g_alwaysOnTop ? MF_CHECKED : 0), IDM_ALWAYS_ON_TOP, "Always On Top");

	// Send Keys submenu
	HMENU hSendKeysMenu = CreatePopupMenu();
	AppendMenuA(hSendKeysMenu, MF_STRING, IDM_SENDKEYS_ALTF4, "Alt + F4");
	AppendMenuA(hSendKeysMenu, MF_STRING, IDM_SENDKEYS_CTRLESC, "Ctrl + Esc");
	AppendMenuA(hSendKeysMenu, MF_STRING, IDM_SENDKEYS_CTRALTDEL, "Ctrl + Alt + Del");
	AppendMenuA(hSendKeysMenu, MF_STRING, IDM_SENDKEYS_PRNTSCRN, "PrintScreen");
	AppendMenuA(hMenu, MF_POPUP, (UINT_PTR)hSendKeysMenu, "Send Keys");

	return hMenu;
}

// --- Helpers for setting quality/fps and sending keys ---
// These are called from the context menu handler.

void SetRemoteScreenQuality(HWND hwnd, int qualityLevel) {
	static const int levels[] = { 0, 20, 40, 60, 80 };
	g_screenStreamMenuQuality = qualityLevel;
	g_screenStreamActualQuality = levels[qualityLevel];

	// Find the input socket for this window
	SOCKET* psktInput = (SOCKET*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!psktInput || *psktInput == INVALID_SOCKET) return;

	// Send control message to server
	RemoteCtrlMsg msg = { RemoteCtrlType::SetQuality, (uint8_t)qualityLevel };
	send(*psktInput, (const char*)&msg, sizeof(msg), 0);
}

// This helper sends special key combos to the remote side
void SendRemoteKeyCombo(HWND hwnd, int combo) {
	// Find the input socket for this window
	SOCKET* psktInput = nullptr;
	// The input socket pointer is stored with WM_USER + 100
	// (see logic in ScreenWndProc and StartScreenRecv)
	psktInput = (SOCKET*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!psktInput || *psktInput == INVALID_SOCKET) return;

	INPUT input[6] = {};
	int n = 0;
	switch (combo) {
	case IDM_SENDKEYS_ALTF4:
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_MENU; n++;
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_F4; n++;
		input[n] = input[n - 1]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		input[n] = input[n - 3]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		break;
	case IDM_SENDKEYS_CTRLESC:
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_CONTROL; n++;
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_ESCAPE; n++;
		input[n] = input[n - 1]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		input[n] = input[n - 3]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		break;
	case IDM_SENDKEYS_CTRALTDEL:
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_CONTROL; n++;
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_MENU; n++;
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_DELETE; n++;
		input[n] = input[n - 1]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		input[n] = input[n - 2]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		input[n] = input[n - 3]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		break;
	case IDM_SENDKEYS_PRNTSCRN:
		input[n] = {}; input[n].type = INPUT_KEYBOARD; input[n].ki.wVk = VK_SNAPSHOT; n++;
		input[n] = input[n - 1]; input[n].ki.dwFlags = KEYEVENTF_KEYUP; n++;
		break;
	}
	for (int i = 0; i < n; ++i)
		send(*psktInput, (char*)&input[i], sizeof(INPUT), 0);
}

#define DEFAULT_PORT 27015
#define MAX_CLIENTS 10

// Server screen dim is nScreenWidth[0], nScreenHeight[0]
// Client screen dim is nScreenWidth[1], nScreenHeight[1]
int nScreenWidth[2] = { 1920 , 2560 };
int nScreenHeight[2] = { 1080 , 1440 };

const int nNormalized = 65535;

// --- Capture screen to BasicBitmap, with RGBA output ---
// --- Capture screen into a BasicBitmap (RGBA) ---
bool CaptureScreenToBasicBitmap(BasicBitmap*& outBmp) {
	int width = GetSystemMetrics(SM_CXSCREEN);
	int height = GetSystemMetrics(SM_CYSCREEN);
	HDC hScreenDC = GetDC(NULL);
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height;
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;
	void* pBits = nullptr;
	HBITMAP hBitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
	if (!hBitmap) { ReleaseDC(NULL, hScreenDC); return false; }
	HDC hMemDC = CreateCompatibleDC(hScreenDC);
	HGDIOBJ oldObj = SelectObject(hMemDC, hBitmap);
	BitBlt(hMemDC, 0, 0, width, height, hScreenDC, 0, 0, SRCCOPY);
	SelectObject(hMemDC, oldObj);
	DeleteDC(hMemDC);
	ReleaseDC(NULL, hScreenDC);

	BasicBitmap* bmp = new BasicBitmap(width, height, BasicBitmap::A8R8G8B8);
	uint8_t* src = static_cast<uint8_t*>(pBits);
	uint8_t* dst = bmp->Bits();
	for (int i = 0; i < width * height; ++i) {
		dst[i * 4 + 0] = src[i * 4 + 2]; // R
		dst[i * 4 + 1] = src[i * 4 + 1]; // G
		dst[i * 4 + 2] = src[i * 4 + 0]; // B
		dst[i * 4 + 3] = 255;
	}
	DeleteObject(hBitmap);
	outBmp = bmp;
	return true;
}



// Now, QOI expects raw RGBA data.
// Helper to convert HBITMAP (24bpp or 32bpp) to 32bpp RGBA buffer.
bool HBITMAPToRGBA(HBITMAP hBitmap, std::vector<uint8_t>& out, int& width, int& height) {
	BITMAP bmp;
	if (!hBitmap) return false;
	if (!GetObject(hBitmap, sizeof(bmp), &bmp)) return false;
	width = bmp.bmWidth;
	height = bmp.bmHeight;
	BITMAPINFO bmi = { 0 };
	bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bmi.bmiHeader.biWidth = width;
	bmi.bmiHeader.biHeight = -height; // top-down
	bmi.bmiHeader.biPlanes = 1;
	bmi.bmiHeader.biBitCount = 32;
	bmi.bmiHeader.biCompression = BI_RGB;

	std::vector<uint8_t> tmp(width * height * 4);
	HDC hdc = GetDC(NULL);
	int res = GetDIBits(hdc, hBitmap, 0, height, tmp.data(), &bmi, DIB_RGB_COLORS);
	ReleaseDC(NULL, hdc);
	if (res == 0) return false;

	// Convert BGRA to RGBA
	out.resize(width * height * 4);
	for (int i = 0; i < width * height; ++i) {
		out[i * 4 + 0] = tmp[i * 4 + 2]; // R
		out[i * 4 + 1] = tmp[i * 4 + 1]; // G
		out[i * 4 + 2] = tmp[i * 4 + 0]; // B
		out[i * 4 + 3] = 255;
	}
	return true;
}

// --- QOI encode a BasicBitmap ---
bool QOIEncodeBasicBitmap(const BasicBitmap* bmp, std::vector<uint8_t>& outQoi) {
	qoi_desc desc;
	desc.width = bmp->Width();
	desc.height = bmp->Height();
	desc.channels = 4;
	desc.colorspace = QOI_SRGB;
	int out_len = 0;
	void* qoi_data = qoi_encode(bmp->Bits(), &desc, &out_len);
	if (!qoi_data) return false;
	outQoi.resize(out_len);
	memcpy(outQoi.data(), qoi_data, out_len);
	free(qoi_data);
	return true;
}


// --- QOI decode to BasicBitmap ---
BasicBitmap* QOIDecodeToBasicBitmap(const uint8_t* data, size_t len) {
	qoi_desc desc;
	uint8_t* decoded = (uint8_t*)qoi_decode(data, len, &desc, 4);
	if (!decoded) return nullptr;
	BasicBitmap* bmp = new BasicBitmap(desc.width, desc.height, BasicBitmap::A8R8G8B8);
	memcpy(bmp->Bits(), decoded, desc.width * desc.height * 4);
	free(decoded);
	return bmp;
}

// SOCKETS function declarations for use in other translation units
int InitializeServer(SOCKET& sktListen, int port);
int InitializeScreenStreamServer(SOCKET& sktListen, int port);
int BroadcastInput(std::vector<SOCKET> vsktSend, INPUT* input);
int TerminateServer(SOCKET& sktListen, std::vector<SOCKET>& sktClients);
int InitializeClient();
int ConnectServer(SOCKET& sktConn, std::string serverAdd, int port);
int ConnectScreenStreamServer(SOCKET& sktConn, std::string serverAdd, int port);
int ReceiveServer(SOCKET sktConn, INPUT& data);
int CloseConnection(SOCKET* sktConn);

// Externs for screen streaming global state
std::atomic<bool> g_screenStreamActive(false);
std::atomic<size_t> g_screenStreamBytes(0);
std::atomic<int> g_screenStreamFPS(0);
std::atomic<int> g_screenStreamW(0);
std::atomic<int> g_screenStreamH(0);

// Streaming server/client declarations
void ScreenStreamServerThread(SOCKET sktClient);
LRESULT CALLBACK ScreenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void ScreenRecvThread(SOCKET skt, HWND hwnd, std::string ip);
void StartScreenRecv(std::string server_ip, int port);

void ServerInputRecvThread(SOCKET clientSocket);



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
		return 1;
	}

	//Create a SOCKET for the server to listen for client connections

	sktListen = socket(result->ai_family, result->ai_socktype, result->ai_protocol);


	if (sktListen == INVALID_SOCKET) {
		std::cout << "Error at socket(): " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		return 1;
	}

	//Setup the TCP listening socket
	iResult = bind(sktListen, result->ai_addr, (int)result->ai_addrlen);


	if (iResult == SOCKET_ERROR) {
		std::cout << "bind failed with error: " << WSAGetLastError() << std::endl;
		freeaddrinfo(result);
		return 1;
	}

	freeaddrinfo(result);
	return 0;
}

int InitializeScreenStreamServer(SOCKET& sktListen, int port) {
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
		return 1;
	}

	// Create a SOCKET for connecting to server
	sktConn = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (sktConn == INVALID_SOCKET) {
		std::cout << "socket failed with error: " << WSAGetLastError() << std::endl;
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
		return 1;
	}
	return 0;
}

int ConnectScreenStreamServer(SOCKET& sktConn, std::string serverAdd, int port) {
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
	return 0;
}

// =================== SCREEN STREAM SERVER =====================

// Optimized server thread: no whole-frame memcpy, uses row-by-row memcmp for dirty tile detection.
void ScreenStreamServerThread(SOCKET sktClient) {
	using namespace std::chrono;

	std::unique_ptr<BasicBitmap> prevBmp;
	std::unique_ptr<BasicBitmap> currBmp;
	bool first = true;
	static int frameCounter = 0;

	auto lastPrint = steady_clock::now();
	int frames = 0;
	size_t bytes = 0;

	g_screenStreamActive = true;
	g_screenStreamBytes = 0;
	g_screenStreamFPS = 0;

	while (g_screenStreamActive) {
		int fps = g_streamingFps.load();
		int frameInterval = 1000 / fps;
		auto start = steady_clock::now();

		// --- Capture screen into BasicBitmap ---
		BasicBitmap* pBmp = nullptr;
		if (!CaptureScreenToBasicBitmap(pBmp)) continue;
		currBmp.reset(pBmp);
		if (!currBmp) continue;

		int width = currBmp->Width();
		int height = currBmp->Height();
		const uint8_t* curr_rgba = currBmp->Bits();

		size_t ntiles_x = (width + TILE_W - 1) / TILE_W;
		size_t ntiles_y = (height + TILE_H - 1) / TILE_H;
		size_t ntiles = ntiles_x * ntiles_y;
		size_t bitmask_bytes = (ntiles + 7) / 8;
		std::vector<uint8_t> bitmask(bitmask_bytes);

		frameCounter++;
		bool forceFull = first || frameCounter % 60 == 0;
		if (forceFull) {
			std::fill(bitmask.begin(), bitmask.end(), 0xFF);
		}
		else if (prevBmp && prevBmp->Width() == width && prevBmp->Height() == height) {
			detect_dirty_tiles_bitmask(prevBmp->Bits(), curr_rgba, width, height, bitmask.data(), ntiles_x, ntiles_y);
		}
		else {
			std::fill(bitmask.begin(), bitmask.end(), 0xFF);
		}

		// --- ViteMap compression and send ---
		Vitemap* vm = vitemap_create((uint32_t)bitmask_bytes);
		memcpy(vm->input, bitmask.data(), bitmask_bytes);
		uint32_t compressed_size = vitemap_compress(vm, (uint32_t)bitmask_bytes);

		uint32_t compressed_size_net = htonl(compressed_size);
		if (send(sktClient, (const char*)&compressed_size_net, 4, 0) != 4) break;
		if (send(sktClient, (const char*)vm->output, compressed_size, 0) != (int)compressed_size) break;
		bytes += 4 + compressed_size;

		// --- Send only dirty tiles (header + QOI data) ---
		for (size_t idx = 0; idx < ntiles; ++idx) {
			if (!get_bit(bitmask.data(), idx)) continue;
			size_t tx = idx % ntiles_x, ty = idx / ntiles_x;
			int tileLeft = tx * TILE_W, tileTop = ty * TILE_H;
			int tileW = std::min(TILE_W, width - tileLeft), tileH = std::min(TILE_H, height - tileTop);

			BasicBitmap tile(tileW, tileH, BasicBitmap::A8R8G8B8);
			for (int row = 0; row < tileH; ++row) {
				const uint8_t* src = curr_rgba + ((tileTop + row) * width + tileLeft) * 4;
				uint8_t* dst = tile.Bits() + row * tileW * 4;
				memcpy(dst, src, tileW * 4);
			}
			std::vector<uint8_t> qoiData;
			QOIEncodeBasicBitmap(&tile, qoiData);

			uint32_t vals[5] = { htonl(tileLeft), htonl(tileTop), htonl(tileW), htonl(tileH), htonl((uint32_t)qoiData.size()) };
			if (send(sktClient, (const char*)vals, 5 * 4, 0) != 5 * 4) goto END;
			size_t offset = 0;
			while (offset < qoiData.size()) {
				int sent = send(sktClient, (const char*)(qoiData.data() + offset), qoiData.size() - offset, 0);
				if (sent <= 0) goto END;
				offset += sent;
			}
			bytes += qoiData.size() + 20;
		}
		vitemap_delete(vm);

		prevBmp = std::make_unique<BasicBitmap>(*currBmp);
		first = false;
		frames++;
		g_screenStreamW = width;
		g_screenStreamH = height;
		auto now = steady_clock::now();
		if (duration_cast<std::chrono::seconds>(now - lastPrint).count() >= 1) {
			g_screenStreamFPS = frames;
			g_screenStreamBytes = bytes;
			frames = 0;
			bytes = 0;
			lastPrint = now;
		}
		auto elapsed = duration_cast<std::chrono::milliseconds>(steady_clock::now() - start).count();
		if (elapsed < frameInterval) Sleep((DWORD)(frameInterval - elapsed));
	}
END:
	closesocket(sktClient);
	g_screenStreamActive = false;
}





// Store a pointer to this struct in the window's GWLP_USERDATA
// You must set this on window creation.

// --- Client streaming: receive tiles, patch framebuffer using BasicBitmap ---
void ScreenRecvThread(SOCKET skt, HWND hwnd, std::string ip) {
	using namespace std::chrono;
	size_t bytesLastSec = 0;
	int framesLastSec = 0;
	auto lastSec = steady_clock::now();

	ScreenBitmapState* bmpState = reinterpret_cast<ScreenBitmapState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
	if (!bmpState) {
		closesocket(skt);
		return;
	}

	std::vector<RECT> invalidateRects;
	std::vector<uint8_t> qoiData;

	bool running = true;
	while (running) {
		// --- Receive compressed size and data ---
		uint32_t compressed_size_net = 0;
		int ret = recv(skt, (char*)&compressed_size_net, 4, MSG_WAITALL);
		if (ret != 4) break;
		uint32_t compressed_size = ntohl(compressed_size_net);

		std::vector<uint8_t> compressed_buf(compressed_size);
		size_t got = 0;
		while (got < compressed_size) {
			int r = recv(skt, (char*)compressed_buf.data() + got, compressed_size - got, 0);
			if (r <= 0) { running = false; break; }
			got += r;
		}
		bytesLastSec += 4 + compressed_size;
		if (!running) break;

		// --- Decompress bitmask ---
		uint32_t data_size = 0, buffer_size = 0;
		vitemap_extract_decompressed_sizes(compressed_buf.data(), &data_size, &buffer_size);
		std::vector<uint8_t> bitmask(buffer_size, 0);
		vitemap_decompress(compressed_buf.data(), compressed_size, bitmask.data());

		// --- Determine framebuffer size ---
		int width = bmpState->imgW ? bmpState->imgW : GetSystemMetrics(SM_CXSCREEN);
		int height = bmpState->imgH ? bmpState->imgH : GetSystemMetrics(SM_CYSCREEN);
		size_t ntiles_x = (width + TILE_W - 1) / TILE_W;
		size_t ntiles_y = (height + TILE_H - 1) / TILE_H;
		size_t ntiles = ntiles_x * ntiles_y;

		// --- Receive/patch only dirty tiles ---
		invalidateRects.clear();
		for (size_t idx = 0; idx < ntiles; ++idx) {
			if (!get_bit(bitmask.data(), idx)) continue;
			uint32_t vals[5];
			int got = recv(skt, (char*)vals, 5 * 4, MSG_WAITALL);
			if (got != 5 * 4) { running = false; break; }
			int x = ntohl(vals[0]), y = ntohl(vals[1]), w = ntohl(vals[2]), h = ntohl(vals[3]), qoi_len = ntohl(vals[4]);
			qoiData.resize(qoi_len);
			size_t qoiGot = 0;
			while (qoiGot < (size_t)qoi_len) {
				int n = recv(skt, (char*)qoiData.data() + qoiGot, qoi_len - qoiGot, 0);
				if (n <= 0) { running = false; break; }
				qoiGot += n;
			}
			if (!running) break;

			std::unique_ptr<BasicBitmap> tileBmp;
			{
				BasicBitmap* decoded = QOIDecodeToBasicBitmap(qoiData.data(), qoi_len);
				if (!decoded) continue;
				tileBmp.reset(decoded);
			}

			// Patch tile into framebuffer
			EnterCriticalSection(&bmpState->cs);
			int needW = std::max(bmpState->imgW, x + w);
			int needH = std::max(bmpState->imgH, y + h);
			bool needRealloc = (bmpState->bmp == nullptr) || (bmpState->imgW != needW) || (bmpState->imgH != needH);
			if (needRealloc) {
				if (bmpState->bmp) delete bmpState->bmp;
				bmpState->bmp = new BasicBitmap(needW, needH, BasicBitmap::A8R8G8B8);
				bmpState->imgW = needW;
				bmpState->imgH = needH;
			}
			for (int row = 0; row < h; ++row) {
				if ((y + row) >= bmpState->imgH || x >= bmpState->imgW) continue;
				uint8_t* dst = bmpState->bmp->Bits() + ((y + row) * bmpState->imgW + x) * 4;
				uint8_t* src = tileBmp->Bits() + row * w * 4;
				memcpy(dst, src, w * 4);
			}
			RECT tileRect = { x, y, x + w, y + h };
			invalidateRects.push_back(tileRect);
			LeaveCriticalSection(&bmpState->cs);

			bytesLastSec += 20 + qoi_len;
		}

		// Invalidate only changed rects
		for (const RECT& r : invalidateRects) {
			InvalidateRect(hwnd, &r, FALSE);
		}

		framesLastSec++;
		auto now = steady_clock::now();
		if (duration_cast<seconds>(now - lastSec).count() >= 1) {
			double mbps = (bytesLastSec * 8.0) / 1e6;
			RECT clientRect;
			GetClientRect(hwnd, &clientRect);
			int winW = clientRect.right - clientRect.left;
			int winH = clientRect.bottom - clientRect.top;
			char title[256];
			snprintf(title, sizeof(title), "Remote Screen | IP: %s | FPS: %d | Mbps: %.2f | Size: %dx%d",
				ip.c_str(), framesLastSec, mbps, winW, winH);
			PostMessage(hwnd, WM_USER + 2, 0, (LPARAM)title);
			bytesLastSec = 0;
			framesLastSec = 0;
			lastSec = now;
		}
	}
}



// BaseWindow was taken from
// https://github.com/microsoft/Windows-classic-samples/blob/master/Samples/Win7Samples/begin/LearnWin32/BaseWindow/cpp/main.cpp
// Slightly modified it by removing the template
class BaseWindow
{
public:
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
	{
		BaseWindow* pThis = NULL;

		if (uMsg == WM_NCCREATE)
		{
			CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
			pThis = (BaseWindow*)pCreate->lpCreateParams;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);

			pThis->m_hwnd = hwnd;
		}
		else
		{
			pThis = (BaseWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
		}
		if (pThis)
		{
			return pThis->HandleMessage(uMsg, wParam, lParam);
		}
		else
		{
			return DefWindowProc(hwnd, uMsg, wParam, lParam);
		}
	}

	BaseWindow() : m_hwnd(NULL), m_pParent(nullptr) { }

	BOOL Create(
		BaseWindow* parent,
		PCSTR lpWindowName,
		DWORD dwStyle,
		DWORD dwExStyle = 0,
		int x = CW_USEDEFAULT,
		int y = CW_USEDEFAULT,
		int nWidth = CW_USEDEFAULT,
		int nHeight = CW_USEDEFAULT,
		HWND hWndParent = 0,
		HMENU hMenu = NULL
	)
	{
		if (hWndParent == 0) {
			WNDCLASS wc = { 0 };

			wc.lpfnWndProc = BaseWindow::WindowProc;
			wc.hInstance = GetModuleHandle(NULL);
			wc.lpszClassName = ClassName();
			wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
			wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

			RegisterClass(&wc);
		}

		m_pParent = parent;

		m_hwnd = CreateWindowExA(
			dwExStyle, ClassName(), lpWindowName, dwStyle, x, y,
			nWidth, nHeight, hWndParent, hMenu, GetModuleHandle(NULL), this
		);

		return (m_hwnd ? TRUE : FALSE);
	}

	HWND Window() const { return m_hwnd; }

protected:

	virtual LPCSTR ClassName() const = 0;
	virtual LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) = 0;

	HWND m_hwnd;
	BaseWindow* m_pParent;
};
class Button : public BaseWindow
{
public:
	LPCSTR ClassName() const { return "button"; }
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(m_hwnd, uMsg, wParam, lParam); }
};
class InputBox : public BaseWindow
{
public:
	LPCSTR ClassName() const { return "edit"; }
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(m_hwnd, uMsg, wParam, lParam); }
};
class StaticBox : public BaseWindow
{
public:
	LPCSTR ClassName() const { return "static"; }
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(m_hwnd, uMsg, wParam, lParam); }
};
class EditBox : public BaseWindow
{
public:
	LPCSTR ClassName() const { return "static"; }
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam) { return DefWindowProc(m_hwnd, uMsg, wParam, lParam); }
};
class MainWindow : public BaseWindow
{
public:
	int m_savedFps = SCREEN_STREAM_FPS;
	bool m_savedAlwaysOnTop = false;
	int m_savedWinW = 477;
	int m_savedWinH = 340;

public:
	MainWindow();
	~MainWindow();

	LPCSTR ClassName() const { return "Remote Window Class"; }
	LRESULT HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam);

public:

	enum class MODE
	{
		SERVER,
		CLIENT,
		UNDEF,
	};

	int InitializeInputDevice();
	void UpdateInput();
	void ConvertInput(PRAWINPUT pRaw, INPUT* pInput);
	int RetrieveInput(UINT uMsg, WPARAM wParam, LPARAM lParam);
	int SetMode(MODE m);
	void UpdateGuiControls();

	int ServerStart();
	int ServerTerminate();

	int ClientConnect();
	int ClientDisconnect();

	int HandleCreate(UINT uMsg, WPARAM wParam, LPARAM lParam);
	int HandlePaint(UINT uMsg, WPARAM wParam, LPARAM lParam);
	int HandleCommand(UINT uMsg, WPARAM wParam, LPARAM lParam);
	int HandleClose(UINT uMsg, WPARAM wParam, LPARAM lParam);

	int SendThread(); // thread that sends the input to the clients
	int OutputThread(); // thread that processes the inputs received from the server
	int ListenThread();
	int ReceiveThread();

	bool SaveConfig();
	bool LoadConfig();

	void Log(std::string msg);
	void ServerLog(std::string msg);
	void ClientLog(std::string msg);

private:

	std::string configName = "config.txt";
	std::string sPort;
	int iPort;

	struct WindowData
	{
		std::string sKeyboardState;
		std::string sMouseState[2];
		std::string sLabels[2];

		RAWINPUTDEVICE rid[3] = { 0 }; // index 2 not used
		MODE nMode = MODE::UNDEF;

		RECT textRect = { 0 };
	} Data;

	struct ServerData
	{
		std::string ip;
		int maxClients;
		INPUT inputBuff;
		int nConnected = 0;
		bool isOnline = false;
		bool bAccepting = false;
		bool clientConnected = false;
		bool wasServer = false;
		std::string port;

		bool isRegistered = false;
		RAWINPUTDEVICE rid[3]; // index #2 not used
		std::queue<INPUT> inputQueue;

		bool bPause = true;

		struct ClientInfo
		{
			SOCKET socket;
			std::string ip;
			int id;
		};

		std::vector<ClientInfo> ClientsInformation;
		SOCKET sktListen = INVALID_SOCKET;

		std::thread tSend;
		std::thread tListen;
		std::condition_variable cond_listen;
		std::condition_variable cond_input;
		std::mutex mu_sktclient;
		std::mutex mu_input;


		bool bOnOtherScreen = false;
		short nOffsetX = 0;
		short nOffsetY = 0;
		int oldX = 0;
		int oldY = 0;
		POINT mPos;

	} Server;

	public:

	struct ClientData
	{
		std::string ip;
		INPUT recvBuff;
		bool isConnected = false;
		bool wasClient = false;

		std::thread tRecv;
		std::thread tSendInput;
		std::condition_variable cond_input;
		std::condition_variable cond_recv;
		std::mutex mu_input;
		std::mutex mu_recv;

		SOCKET sktServer = INVALID_SOCKET;

		std::queue<INPUT> inputQueue;

	} Client;

	HMENU m_hMenu;
	Button m_btnOk;
	Button m_btnPause;
	Button m_btnModeClient, m_btnConnect, m_btnDisconnect;
	Button m_btnModeServer, m_btnStart, m_btnTerminate;
	InputBox m_itxtIP;
	InputBox m_itxtPort;
	StaticBox m_stxtKeyboard, m_stxtMouse, m_stxtMouseBtn, m_stxtMouseOffset;
}; // <-- CLOSE CLASS DEFINITION

MainWindow::MainWindow()
{
	LoadConfig();
	WSADATA wsadata;
	int r = WSAStartup(MAKEWORD(2, 2), &wsadata);

	if (r != 0)
	{
		std::cout << "WSAStartup failed: " << r << std::endl;
	}
	else
	{
		hostent* host;
		host = gethostbyname("");
		char* wifiIP;
		// the index in the array is to be changed based on the adapter.
		// I currently have no way to determine which is the correct adapter.
		//inet_ntop(AF_INET, host->h_addr_list, wifiIP, 50);
		wifiIP = inet_ntoa(*(in_addr*)host->h_addr_list[0]);
		Server.ip = std::string(wifiIP);
	}
	nScreenWidth[0] = GetSystemMetrics(SM_CXSCREEN);
	nScreenHeight[0] = GetSystemMetrics(SM_CYSCREEN);
}
MainWindow::~MainWindow()
{
	SaveConfig();
}

MainWindow* g_pMainWindow = nullptr;

void SetRemoteScreenFps(HWND hwnd, int fps) {
	g_screenStreamMenuFps = fps;
	g_screenStreamActualFps = fps;

	SOCKET* psktInput = (SOCKET*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
	if (!psktInput || *psktInput == INVALID_SOCKET) return;

	RemoteCtrlMsg msg = { RemoteCtrlType::SetFps, (uint8_t)fps };
	send(*psktInput, (const char*)&msg, sizeof(msg), 0);

	// Save FPS to config
	if (g_pMainWindow) {
		g_pMainWindow->m_savedFps = fps;
		g_pMainWindow->SaveConfig();
	}
}

// ============ SCREEN STREAM CLIENT WINDOW ============

LRESULT CALLBACK ScreenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	// Retrieve per-window state
	ScreenBitmapState* bmpState = reinterpret_cast<ScreenBitmapState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

	switch (msg) {
	case WM_CREATE:
		bmpState = new ScreenBitmapState();
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)bmpState);
		break;

	case WM_USER + 100: // Store input socket pointer
		if (bmpState)
			bmpState->psktInput = (SOCKET*)lParam;
		return 0;

	case WM_USER + 1:   // New bitmap and size from thread
		// No longer handled: thread never allocates a new ScreenBitmapState, only updates its members
		return 0;

	case WM_USER + 2:
		SetWindowTextA(hwnd, (const char*)lParam);
		break;

		// --- Context menu handling (unchanged, but uses hwnd) ---
	case WM_CONTEXTMENU: {
		POINT pt;
		pt.x = LOWORD(lParam);
		pt.y = HIWORD(lParam);
		if (pt.x == -1 && pt.y == -1) {
			RECT rect;
			GetWindowRect(hwnd, &rect);
			pt.x = rect.left + (rect.right - rect.left) / 2;
			pt.y = rect.top + (rect.bottom - rect.top) / 2;
		}
		HMENU hMenu = CreateScreenContextMenu();
		int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
		if (cmd)
			PostMessage(hwnd, WM_COMMAND, cmd, 0);
		DestroyMenu(hMenu);
		break;
	}
	case WM_COMMAND:
		switch (LOWORD(wParam)) {
			// Video Quality
		case IDM_VIDEO_QUALITY_1: SetRemoteScreenQuality(hwnd, 1); break;
		case IDM_VIDEO_QUALITY_2: SetRemoteScreenQuality(hwnd, 2); break;
		case IDM_VIDEO_QUALITY_3: SetRemoteScreenQuality(hwnd, 3); break;
		case IDM_VIDEO_QUALITY_4: SetRemoteScreenQuality(hwnd, 4); break;
			// Video FPS
		case IDM_VIDEO_FPS_5: SetRemoteScreenFps(hwnd, 5); break;
		case IDM_VIDEO_FPS_10: SetRemoteScreenFps(hwnd, 10); break;
		case IDM_VIDEO_FPS_20: SetRemoteScreenFps(hwnd, 20); break;
		case IDM_VIDEO_FPS_30: SetRemoteScreenFps(hwnd, 30); break;
		case IDM_VIDEO_FPS_40: SetRemoteScreenFps(hwnd, 40); break;
		case IDM_VIDEO_FPS_60: SetRemoteScreenFps(hwnd, 60); break;
			// Always On Top
		case IDM_ALWAYS_ON_TOP:
			g_alwaysOnTop = !g_alwaysOnTop;
			if (g_pMainWindow) {
				g_pMainWindow->m_savedAlwaysOnTop = g_alwaysOnTop;
				g_pMainWindow->SaveConfig();
			}
			SetWindowPos(hwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
			break;
			// Send Keys
		case IDM_SENDKEYS_ALTF4:    SendRemoteKeyCombo(hwnd, IDM_SENDKEYS_ALTF4); break;
		case IDM_SENDKEYS_CTRLESC:  SendRemoteKeyCombo(hwnd, IDM_SENDKEYS_CTRLESC); break;
		case IDM_SENDKEYS_CTRALTDEL:SendRemoteKeyCombo(hwnd, IDM_SENDKEYS_CTRALTDEL); break;
		case IDM_SENDKEYS_PRNTSCRN: SendRemoteKeyCombo(hwnd, IDM_SENDKEYS_PRNTSCRN); break;
		}
		break;

		// --- Input handling uses bmpState->psktInput ---
	case WM_LBUTTONDOWN:
	case WM_LBUTTONUP:
	case WM_RBUTTONDOWN:
	case WM_RBUTTONUP:
	case WM_MBUTTONDOWN:
	case WM_MBUTTONUP:
	case WM_MOUSEMOVE:
	{
		if (bmpState && bmpState->psktInput && *bmpState->psktInput != INVALID_SOCKET) {
			INPUT input = {};
			input.type = INPUT_MOUSE;

			POINT pt;
			pt.x = GET_X_LPARAM(lParam);
			pt.y = GET_Y_LPARAM(lParam);

			RECT rect;
			GetClientRect(hwnd, &rect);
			int winW = rect.right - rect.left, winH = rect.bottom - rect.top;
			int normX = 0, normY = 0;
			if (winW > 0 && winH > 0) {
				normX = (int)((pt.x / (double)winW) * nNormalized);
				normY = (int)((pt.y / (double)winH) * nNormalized);
			}

			input.mi.dx = normX;
			input.mi.dy = normY;
			input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;
			if (msg == WM_LBUTTONDOWN) input.mi.dwFlags |= MOUSEEVENTF_LEFTDOWN;
			if (msg == WM_LBUTTONUP)   input.mi.dwFlags |= MOUSEEVENTF_LEFTUP;
			if (msg == WM_RBUTTONDOWN) input.mi.dwFlags |= MOUSEEVENTF_RIGHTDOWN;
			if (msg == WM_RBUTTONUP)   input.mi.dwFlags |= MOUSEEVENTF_RIGHTUP;
			if (msg == WM_MBUTTONDOWN) input.mi.dwFlags |= MOUSEEVENTF_MIDDLEDOWN;
			if (msg == WM_MBUTTONUP)   input.mi.dwFlags |= MOUSEEVENTF_MIDDLEUP;

			send(*bmpState->psktInput, (const char*)&input, sizeof(INPUT), 0);
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		if (bmpState && bmpState->psktInput && *bmpState->psktInput != INVALID_SOCKET) {
			INPUT input = {};
			input.type = INPUT_MOUSE;
			input.mi.dwFlags = MOUSEEVENTF_WHEEL;
			input.mi.mouseData = GET_WHEEL_DELTA_WPARAM(wParam);
			send(*bmpState->psktInput, (const char*)&input, sizeof(INPUT), 0);
		}
		break;
	}
	case WM_KEYDOWN:
	case WM_KEYUP:
	{
		if (bmpState && bmpState->psktInput && *bmpState->psktInput != INVALID_SOCKET) {
			INPUT input = {};
			input.type = INPUT_KEYBOARD;
			input.ki.wVk = (WORD)wParam;
			input.ki.wScan = MapVirtualKeyA((UINT)wParam, MAPVK_VK_TO_VSC);
			input.ki.dwFlags = (msg == WM_KEYUP) ? KEYEVENTF_KEYUP : 0;
			send(*bmpState->psktInput, (const char*)&input, sizeof(INPUT), 0);
		}
		break;
	}

	case WM_ERASEBKGND:
		return 1; // Prevent flicker

// --- In WM_PAINT: blit BasicBitmap to window (using DIBSection for output) ---
	case WM_PAINT: {
		PAINTSTRUCT ps;
		HDC hdc = BeginPaint(hwnd, &ps);

		RECT clientRect;
		GetClientRect(hwnd, &clientRect);
		int destW = clientRect.right - clientRect.left;
		int destH = clientRect.bottom - clientRect.top;

		static HBITMAP hDoubleBufBmp = NULL;
		static void* pDoubleBufBits = NULL;
		static int doubleBufW = 0, doubleBufH = 0;

		if (destW <= 0 || destH <= 0) {
			EndPaint(hwnd, &ps);
			break;
		}

		if (!hDoubleBufBmp || doubleBufW != destW || doubleBufH != destH) {
			if (hDoubleBufBmp) {
				DeleteObject(hDoubleBufBmp);
				hDoubleBufBmp = NULL;
				pDoubleBufBits = NULL;
			}
			BITMAPINFO bmi = { 0 };
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = destW;
			bmi.bmiHeader.biHeight = -destH;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			hDoubleBufBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pDoubleBufBits, NULL, 0);
			doubleBufW = destW;
			doubleBufH = destH;
			if (pDoubleBufBits)
				memset(pDoubleBufBits, 0xCC, destW * destH * 4);
		}

		HDC hdcBuf = CreateCompatibleDC(hdc);
		HGDIOBJ oldBufBmp = SelectObject(hdcBuf, hDoubleBufBmp);

		HBRUSH brush = CreateSolidBrush(RGB(0, 0, 0));
		FillRect(hdcBuf, &clientRect, brush);
		DeleteObject(brush);

		ScreenBitmapState* bmpState = reinterpret_cast<ScreenBitmapState*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		if (bmpState && bmpState->bmp) {
			EnterCriticalSection(&bmpState->cs);
			int srcW = bmpState->bmp->Width();
			int srcH = bmpState->bmp->Height();
			double srcAspect = (double)srcW / srcH;
			double destAspect = (double)destW / destH;
			int drawW, drawH, offsetX, offsetY;
			if (destAspect > srcAspect) {
				drawH = destH;
				drawW = (int)(drawH * srcAspect);
				offsetX = (destW - drawW) / 2;
				offsetY = 0;
			}
			else {
				drawW = destW;
				drawH = (int)(drawW / srcAspect);
				offsetX = 0;
				offsetY = (destH - drawH) / 2;
			}

			// 1. Allocate a temp DIBSection for the BasicBitmap image, if needed.
			BITMAPINFO bmi = { 0 };
			bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmi.bmiHeader.biWidth = srcW;
			bmi.bmiHeader.biHeight = -srcH;
			bmi.bmiHeader.biPlanes = 1;
			bmi.bmiHeader.biBitCount = 32;
			bmi.bmiHeader.biCompression = BI_RGB;
			void* pBits = nullptr;
			HBITMAP hBmp = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);

			// 2. Copy RGBA from BasicBitmap to BGRA for the DIBSection.
			uint8_t* src = bmpState->bmp->Bits();
			uint8_t* dst = (uint8_t*)pBits;
			for (int i = 0; i < srcW * srcH; ++i) {
				dst[i * 4 + 0] = src[i * 4 + 2]; // B
				dst[i * 4 + 1] = src[i * 4 + 1]; // G
				dst[i * 4 + 2] = src[i * 4 + 0]; // R
				dst[i * 4 + 3] = 255;            // A
			}

			// 3. Draw/scaledraw the temp DIBSection to the double buffer.
			HDC hMem = CreateCompatibleDC(hdcBuf);
			HGDIOBJ oldObj = SelectObject(hMem, hBmp);
			SetStretchBltMode(hdcBuf, COLORONCOLOR);
			StretchBlt(hdcBuf, offsetX, offsetY, drawW, drawH, hMem, 0, 0, srcW, srcH, SRCCOPY);
			SelectObject(hMem, oldObj);
			DeleteDC(hMem);
			DeleteObject(hBmp);
			LeaveCriticalSection(&bmpState->cs);
		}
		BitBlt(hdc, 0, 0, destW, destH, hdcBuf, 0, 0, SRCCOPY);
		SelectObject(hdcBuf, oldBufBmp);
		DeleteDC(hdcBuf);
		EndPaint(hwnd, &ps);
		break;
	}
    	case WM_DESTROY: {
		static HBITMAP hDoubleBufBmp = NULL;
		static void* pDoubleBufBits = NULL;
		if (hDoubleBufBmp) {
			DeleteObject(hDoubleBufBmp);
			hDoubleBufBmp = NULL;
			pDoubleBufBits = NULL;
		}
		if (bmpState) {
			delete bmpState;
			SetWindowLongPtr(hwnd, GWLP_USERDATA, 0);
		}
		break;
	}
	default:
		return DefWindowProc(hwnd, msg, wParam, lParam);
	}
	return 0;
}

void StartScreenRecv(std::string server_ip, int port) {
	// Reuse main input socket if possible
	SOCKET* psktInput = nullptr;
	if (g_pMainWindow && g_pMainWindow->Client.isConnected)
		psktInput = &g_pMainWindow->Client.sktServer;

	SOCKET skt = INVALID_SOCKET;
	if (ConnectScreenStreamServer(skt, server_ip, port) != 0) {
		MessageBoxA(NULL, "Failed to connect to screen stream server!", "Remote", MB_OK | MB_ICONERROR);
		return;
	}
	WNDCLASSA wc = { 0 };
	wc.lpfnWndProc = ScreenWndProc;
	wc.lpszClassName = "RemoteScreenWnd";
	wc.hInstance = GetModuleHandle(NULL);
	RegisterClassA(&wc);

	HWND hwnd = CreateWindowA(wc.lpszClassName, "Remote Screen", WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, NULL, NULL, wc.hInstance, NULL);

	// Pass input socket pointer to the window
	SendMessage(hwnd, WM_USER + 100, 0, (LPARAM)psktInput);

	ShowWindow(hwnd, SW_SHOWNORMAL);

	std::thread t(ScreenRecvThread, skt, hwnd, server_ip);
	t.detach();

	MSG msg = { 0 };
	while (IsWindow(hwnd) && GetMessage(&msg, NULL, 0, 0)) {
		if (!IsDialogMessage(hwnd, &msg)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		if (!IsWindow(hwnd)) break;
	}
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CREATE:
		return HandleCreate(uMsg, wParam, lParam);
	case WM_INPUT:
		// CLIENT mode: capture input, send to server
		if (Data.nMode == MODE::CLIENT && Client.isConnected) {
			unsigned int dwSize;
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) == -1) break;
			LPBYTE lpb = new unsigned char[dwSize];
			if (!lpb) break;
			if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
				delete[] lpb; break;
			}
			INPUT inputBuff;
			ConvertInput((PRAWINPUT)lpb, &inputBuff);
			delete[] lpb;

			// Send to server
			send(Client.sktServer, (char*)&inputBuff, sizeof(INPUT), 0);
		}
		return 0;
	case WM_PAINT:
		return HandlePaint(uMsg, wParam, lParam);
	case WM_COMMAND:
		return HandleCommand(uMsg, wParam, lParam);
	case WM_CLOSE:
		return HandleClose(uMsg, wParam, lParam);
	case WM_SIZE:
	{
		RECT r;
		if (m_hwnd && GetWindowRect(m_hwnd, &r)) {
			m_savedWinW = r.right - r.left;
			m_savedWinH = r.bottom - r.top;
			SaveConfig();
			break;
		}
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	default:
		return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
	}
	return TRUE;
	}
}

// Removed RetrieveInput logic (no longer used on server)

int MainWindow::HandleCommand(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (HIWORD(wParam)) {
	case BN_CLICKED:
		switch (LOWORD(wParam)) {

		case BTN_START:
			return ServerStart();

		case BTN_PAUSE:
			Server.bPause = !Server.bPause;
			Log(((!Server.bPause) ? "Resumed" : "Paused"));
			SetWindowText(m_btnPause.Window(), (Server.bPause) ? "Resume" : "Pause");
			return 0;

		case BTN_TERMINATE:
			return ServerTerminate();

		case BTN_CONNECT:
			return ClientConnect();

		case BTN_DISCONNECT:
			return ClientDisconnect();

		case EDIT_ADDRESS:

			break;

		case BTN_SERVER:
			return SetMode(MODE::SERVER);

		case BTN_CLIENT:
			return SetMode(MODE::CLIENT);

		case MENU_FILE:

			break;

		case MENU_SUB:

			break;

		case MENU_EXIT:
			PostMessage(m_hwnd, WM_CLOSE, 0, 0);
			return 0;

		case MENU_ABOUT:

			break;

		default:
			//MessageBox(m_hwnd, "A Button was clicked", "My application", MB_OKCANCEL);
			break;
		}
		return 0;

	default:
		break;
	}
}
int MainWindow::HandleCreate(UINT uMsg, WPARAM wParam, LPARAM lParam)

{
	//m_btnOk.Create(L"OK", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, 50, 50, 100, 100, this->m_hwnd);
	CreateWindowA("BUTTON", "Mode", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 20, 10, 190, 60, m_hwnd, (HMENU)BTN_MODE, (HINSTANCE)GetWindowLong(m_hwnd, GWLP_HINSTANCE), NULL);
	m_btnModeServer.Create(this, "Server", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 0, 30, 35, 70, 20, m_hwnd, (HMENU)BTN_SERVER);
	m_btnModeClient.Create(this, "Client", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_AUTORADIOBUTTON, 0, 130, 35, 70, 20, m_hwnd, (HMENU)BTN_CLIENT);

	m_btnStart.Create(this, "Start", WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 0, 20, 80, 50, 20, m_hwnd, (HMENU)BTN_START);
	m_btnPause.Create(this, "Pause", WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 0, 80, 80, 50, 20, m_hwnd, (HMENU)BTN_PAUSE);
	m_btnTerminate.Create(this, "Terminate", WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 0, 140, 80, 70, 20, m_hwnd, (HMENU)BTN_TERMINATE);

	m_btnConnect.Create(this, "Connect", WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 0, 35, 80, 60, 20, m_hwnd, (HMENU)BTN_CONNECT);
	m_btnDisconnect.Create(this, "Disconnect", WS_TABSTOP | WS_CHILD | BS_PUSHBUTTON, 0, 115, 80, 80, 20, m_hwnd, (HMENU)BTN_DISCONNECT);

	//hLog = CreateWindowEx(0, "edit", txtLog.c_str(), WS_VISIBLE | WS_CHILD | ES_READONLY | ES_MULTILINE, 250, 10, 200, 260, m_hwnd, NULL, (HINSTANCE)GetWindowLong(m_hwnd, GWL_HINSTANCE), NULL);

	m_itxtIP.Create(this, Client.ip.c_str(), WS_VISIBLE | WS_CHILD | ES_READONLY, 0, 130, 120, 100, 20, m_hwnd, (HMENU)EDIT_ADDRESS);
	m_itxtPort.Create(this, sPort.c_str(), WS_VISIBLE | WS_CHILD | ES_READONLY, 0, 130, 150, 100, 20, m_hwnd, (HMENU)EDIT_PORT);

	m_stxtKeyboard.Create(this, "", WS_VISIBLE | WS_CHILD, 0, 130, 180, 170, 20, m_hwnd, NULL);
	m_stxtMouse.Create(this, "", WS_VISIBLE | WS_CHILD, 0, 130, 210, 170, 20, m_hwnd, NULL);
	m_stxtMouseOffset.Create(this, "", WS_VISIBLE | WS_CHILD, 0, 130, 230, 170, 20, m_hwnd, NULL);
	m_stxtMouseBtn.Create(this, "", WS_VISIBLE | WS_CHILD, 0, 130, 250, 170, 20, m_hwnd, NULL);

	PostMessage(m_itxtPort.Window(), EM_SETREADONLY, (WPARAM)false, 0);
	Data.sLabels[1] = "Port: ";
	return 0;
}
int MainWindow::HandlePaint(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;
	RECT clientRect;
	HBRUSH hBrush;


	hdc = BeginPaint(m_hwnd, &ps);
	GetClientRect(m_hwnd, &clientRect);

	hBrush = CreateSolidBrush(RGB(255, 255, 255));
	//FillRect(hdc, &clientRect, hBrush);
	FillRect(hdc, &ps.rcPaint, (HBRUSH)(COLOR_WINDOW + 1));

	TextOut(hdc, 20, 120, Data.sLabels[0].c_str(), Data.sLabels[0].length());
	TextOut(hdc, 20, 150, Data.sLabels[1].c_str(), Data.sLabels[1].length());

	TextOut(hdc, 20, 180, "Keyboard Input:", 15);
	TextOut(hdc, 20, 210, "Mouse Input:", 12);

	EndPaint(m_hwnd, &ps);
	return 0;
}
int MainWindow::HandleClose(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	//if (MessageBox(m_hwnd, "Really quit?", "Remote", MB_OKCANCEL) == IDOK) {
	//	Data.nMode = MODE::UNDEF;
	//	DestroyWindow(m_hwnd);
	//}
	switch (Data.nMode)
	{
	case MODE::SERVER:
		if (Server.isOnline)
		{
			ServerTerminate();
		}
		break;

	case MODE::CLIENT:
		char out_ip[50];
		GetWindowText(m_itxtIP.Window(), out_ip, 50);
		Client.ip = out_ip;
		if (Client.isConnected)
		{
			ClientDisconnect();
		}
		break;

	default:
		break;
	}

	DestroyWindow(m_hwnd);
	return 0;
}

void MainWindow::Log(std::string msg)
{
	switch (Data.nMode)
	{
	case MODE::CLIENT: ClientLog(msg); break;
	case MODE::SERVER: ServerLog(msg); break;
	case MODE::UNDEF: std::cout << msg << std::endl; break;
	}
}
void MainWindow::ServerLog(std::string msg)
{
	std::cout << "Server - " << msg << std::endl;
}
void MainWindow::ClientLog(std::string msg)
{
	std::cout << "Client - " << msg << std::endl;
}


int MainWindow::InitializeInputDevice() {

	// keyboard
	Server.rid[0].dwFlags = RIDEV_INPUTSINK;
	Server.rid[0].usUsagePage = 1;
	Server.rid[0].usUsage = 6;
	Server.rid[0].hwndTarget = m_hwnd;

	// mouse
	Server.rid[1].dwFlags = RIDEV_INPUTSINK;
	Server.rid[1].usUsagePage = 1;
	Server.rid[1].usUsage = 2;
	Server.rid[1].hwndTarget = m_hwnd;

	if (!RegisterRawInputDevices(Server.rid, 2, sizeof(RAWINPUTDEVICE)))
	{
		return 1;
	}
	return 0;
}

std::string VKeyToString(unsigned int vk)
{
	//https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
	switch (vk)
	{
	case VK_BACK: return "BACK";
	case VK_TAB: return "TAB";
	case VK_CLEAR: return "CLEAR";
	case VK_RETURN: return "ENTER";
	case VK_SHIFT: return "SHIFT";
	case VK_CONTROL: return "CONTROL";
	case VK_MENU: return "ALT";
	case VK_CAPITAL: return "CAP LOCK";
	case VK_KANA: return "IME Kana";
		//case VK_HANGUEL: return "IME Hnaguel";
		//case VK_HANGUL: return "IME Hangul";
		//case VK_IME_ON: return "IME On";
	case VK_JUNJA: return "IME JUNA";
	case VK_FINAL: return "IME FINAL";
	case VK_HANJA: return "IME HANJA";
		//case VK_KANJI: return "IME Kanji";
		//case VK_IME_OFF: return "IME Off";
	case VK_ESCAPE: return "ESC";
	case VK_CONVERT: return "IME CONVERT";
	case VK_NONCONVERT: return "IME NONCONVERT";
	case VK_ACCEPT: return "IME ACCEPT";
	case VK_MODECHANGE: return "IME CHANGE MODE";
	case VK_SPACE: return "SPACE";
	case VK_PRIOR: return "PAGE UP";
	case VK_NEXT: return "PAGE DOWN";
	case VK_END: return "END";
	case VK_HOME: return "HOME";
	case VK_LEFT: return "LEFT ARROW";
	case VK_UP: return "UP ARROW";
	case VK_RIGHT: return "RIGHT ARROW";
	case VK_DOWN: return "DOWN ARROW";
	case VK_SELECT: return "SELECT";
	case VK_PRINT: return "PRINT";
	case VK_EXECUTE: return "EXECUTE";
	case VK_SNAPSHOT: return "PRINT SCREEN";
	case VK_INSERT: return "INSERT";
	case VK_DELETE: return "DELETE";
	case VK_HELP: return "HELP";
	case 0x30: return "0";
	case 0x31: return "1";
	case 0x32: return "2";
	case 0x33: return "3";
	case 0x34: return "4";
	case 0x35: return "5";
	case 0x36: return "6";
	case 0x37: return "7";
	case 0x38: return "8";
	case 0x39: return "9";
	case 0x41: return "A";
	case 0x42: return "B";
	case 0x43: return "C";
	case 0x44: return "D";
	case 0x45: return "E";
	case 0x46: return "F";
	case 0x47: return "G";
	case 0x48: return "H";
	case 0x49: return "I";
	case 0x4A: return "J";
	case 0x4B: return "K";
	case 0x4C: return "L";
	case 0x4D: return "M";
	case 0x4E: return "N";
	case 0x4F: return "O";
	case 0x50: return "P";
	case 0x51: return "Q";
	case 0x52: return "R";
	case 0x53: return "S";
	case 0x54: return "T";
	case 0x55: return "U";
	case 0x56: return "V";
	case 0x57: return "W";
	case 0x58: return "X";
	case 0x59: return "Y";
	case 0x5A: return "Z";
	case VK_LWIN: return "LEFT WINDOWS";
	case VK_RWIN: return "RIGHT WINDOWS";
	case VK_APPS: return "APPLICATION";
	case VK_SLEEP: return "SLEEP";
	case VK_NUMPAD0: return "NUMPAD 0";
	case VK_NUMPAD1: return "NUMPAD 1";
	case VK_NUMPAD2: return "NUMPAD 2";
	case VK_NUMPAD3: return "NUMPAD 3";
	case VK_NUMPAD4: return "NUMPAD 4";
	case VK_NUMPAD5: return "NUMPAD 5";
	case VK_NUMPAD6: return "NUMPAD 6";
	case VK_NUMPAD7: return "NUMPAD 7";
	case VK_NUMPAD8: return "NUMPAD 8";
	case VK_NUMPAD9: return "NUMPAD 9";
	case VK_MULTIPLY: return "MULTIPLY";
	case VK_ADD: return "ADD";
	case VK_SEPARATOR: return "SEPARATOR";
	case VK_SUBTRACT: return "SUBTRACT";
	case VK_DECIMAL: return "DECIMAL";
	case VK_DIVIDE: return "DIVIDE";
	case VK_F1: return "F1";
	case VK_F2: return "F2";
	case VK_F3: return "F3";
	case VK_F4: return "F4";
	case VK_F5: return "F5";
	case VK_F6: return "F6";
	case VK_F7: return "F7";
	case VK_F8: return "F8";
	case VK_F9: return "F9";
	case VK_F10: return "F10";
	case VK_F11: return "F11";
	case VK_F12: return "F12";
	case VK_F13: return "F13";
	case VK_F14: return "F14";
	case VK_F15: return "F15";
	case VK_F16: return "F16";
	case VK_F17: return "F17";
	case VK_F18: return "F18";
	case VK_F19: return "F19";
	case VK_F20: return "F20";
	case VK_F21: return "F21";
	case VK_F22: return "F22";
	case VK_F23: return "F23";
	case VK_F24: return "F24";
	case VK_NUMLOCK: return "NUM LOCK";
	case VK_SCROLL: return "SCROLL LOCK";
	case 0x92: return "OEM KEY 1";
	case 0x93: return "OEM KEY 2";
	case 0x94: return "OEM KEY 3";
	case 0x95: return "OEM KEY 4";
	case 0x96: return "OEM KEY 5";
	case VK_LSHIFT: return "LEFT SHIFT";
	case VK_RSHIFT: return "RIGHT SHIFT";
	case VK_LCONTROL: return "LEFT CONTROL";
	case VK_RCONTROL: return "RIGHT CONTROL";
	case VK_LMENU: return "LEFT MENU";
	case VK_RMENU: return "RIGHT MENU";
	case VK_BROWSER_BACK: return "BROWSER BACK";
	case VK_BROWSER_FORWARD: return "BROWSER FORWARD";
	case VK_BROWSER_REFRESH: return "BROWSER REFRESH";
	case VK_BROWSER_STOP: return "BROWSER STOP";
	case VK_BROWSER_SEARCH: return "BROWSER SEARCH";
	case VK_BROWSER_FAVORITES: return "BROWSER FAVORITES";
	case VK_BROWSER_HOME: return "BROWSER HOME";
	case VK_VOLUME_MUTE: return "VOLUME MUTE";
	case VK_VOLUME_DOWN: return "VOLUME DOWN";
	case VK_VOLUME_UP: return "VOLUME UP";
	case VK_MEDIA_NEXT_TRACK: return "NEXT TRACK";
	case VK_MEDIA_PREV_TRACK: return "PREVIOUS TRACK";
	case VK_MEDIA_STOP: return "STOP MEDIA";
	case VK_MEDIA_PLAY_PAUSE: return "PLAY/PAUSE MEDIA";
	case VK_LAUNCH_MAIL: return "LAUNCH MAIL";
	case VK_LAUNCH_MEDIA_SELECT: return "SELECT MEDIA";
	case VK_LAUNCH_APP1: return "START APP 1";
	case VK_LAUNCH_APP2: return "START APP 2";
	case VK_OEM_1: return "MISC CHAR 1";
	case VK_OEM_PLUS: return "PLUS";
	case VK_OEM_COMMA: return "COMMA";
	case VK_OEM_MINUS: return "MINUS";
	case VK_OEM_PERIOD: return "PERIOD";
	case VK_OEM_2: return "MISC CHAR 2";
	case VK_OEM_3: return "MISC CHAR 3";
	case VK_OEM_4: return "MISC CHAR 4";
	case VK_OEM_5: return "MISC CHAR 5";
	case VK_OEM_6: return "MISC CHAR 6";
	case VK_OEM_7: return "MISC CHAR 7";
	case VK_OEM_8: return "MISC CHAR 8";
	case 0xE1: return "OEM KEY 6";
	case VK_ATTN: return "ATTN KEY";
	case VK_CRSEL: return "CRSEL KEY";
	case VK_EXSEL: return "EXSEL KEY";
	case VK_EREOF: return "ERASE EOF";
	case VK_PLAY: return "PLAY";
	case VK_ZOOM: return "ZOOM";
	case VK_NONAME: return "NO NAME KEY";
	case VK_PA1: return "PA1 KEY";
	case VK_OEM_CLEAR: return "CLEAR KEY";
	default: return "UNKNOWN";
	}
}

void MainWindow::UpdateInput() {

	std::string key_out("");
	std::string mouse_out("");

	if (Server.inputBuff.type == INPUT_KEYBOARD) {
		key_out = VKeyToString(Server.inputBuff.ki.wVk);
		//key_out += MapVirtualKeyA(Server.inputBuff.ki.wVk, MAPVK_VK_TO_CHAR);
		if (Server.inputBuff.ki.dwFlags == KEYEVENTF_KEYUP) {
			key_out += " UP";
		}
		else {
			key_out += " DOWN";
		}

		SetWindowText(m_stxtKeyboard.Window(), key_out.c_str());
	}
	else if (Server.inputBuff.type == INPUT_MOUSE) {
		if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_LEFTDOWN) {
			SetWindowText(m_stxtMouseBtn.Window(), "Left Pressed");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_RIGHTDOWN) {
			SetWindowText(m_stxtMouseBtn.Window(), "Right Pressed");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_MIDDLEDOWN) {
			SetWindowText(m_stxtMouseBtn.Window(), "Middle Pressed");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_LEFTUP) {
			SetWindowText(m_stxtMouseBtn.Window(), "Left Released");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_RIGHTUP) {
			SetWindowText(m_stxtMouseBtn.Window(), "Right Released");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_MIDDLEUP) {
			SetWindowText(m_stxtMouseBtn.Window(), "Middle Released");
		}
		else if (Server.inputBuff.mi.dwFlags == MOUSEEVENTF_WHEEL) {
			mouse_out = "Wheel delta=" + std::to_string((int16_t)Server.inputBuff.mi.mouseData);
			SetWindowText(m_stxtMouseBtn.Window(), mouse_out.c_str());
		}
		else {
			SetWindowText(m_stxtMouseBtn.Window(), "");
		}
		Data.sMouseState[0] = std::to_string(Server.inputBuff.mi.dx);
		Data.sMouseState[1] = std::to_string(Server.inputBuff.mi.dy);
		std::string mouse_pos = "(" + Data.sMouseState[0] + ", " + Data.sMouseState[1] + ")";
		std::string mouse_offset = "(" + std::to_string(Server.nOffsetX) + ")";
		SetWindowText(m_stxtMouse.Window(), mouse_pos.c_str());
		SetWindowText(m_stxtMouseOffset.Window(), mouse_offset.c_str());
	}
}
void MainWindow::UpdateGuiControls()
{
	switch (Data.nMode)
	{
	case MODE::CLIENT:
	{
		if (!Client.isConnected && !Client.wasClient) {
			Button_Enable(m_btnStart.Window(), false);
			Button_Enable(m_btnTerminate.Window(), false);
			Button_Enable(m_btnPause.Window(), false);
			ShowWindow(m_btnStart.Window(), SW_HIDE);
			ShowWindow(m_btnTerminate.Window(), SW_HIDE);
			ShowWindow(m_btnPause.Window(), SW_HIDE);

			Button_Enable(m_btnConnect.Window(), true);
			Button_Enable(m_btnDisconnect.Window(), false);
			ShowWindow(m_btnConnect.Window(), SW_SHOW);
			ShowWindow(m_btnDisconnect.Window(), SW_SHOW);

			Data.sLabels[0] = "Server Address: ";
			//Data.sLabels[1] = "Connected: ";

			SetRect(&Data.textRect, 20, 120, 129, 170);
			InvalidateRect(m_hwnd, &Data.textRect, true);

			PostMessage(m_itxtIP.Window(), EM_SETREADONLY, (WPARAM)false, 0);
			SetWindowText(m_itxtIP.Window(), Client.ip.c_str());
			//SetWindowLong(hTxtIP, GWL_STYLE, GetWindowLong(hTxtIP, GWL_STYLE) ^ ES_READONLY);
			//UpdateWindow(hTxtIP);
			Client.wasClient = true;
			Server.wasServer = false;
			UpdateWindow(m_hwnd);
		}
		else if (Client.isConnected)
		{
			Button_Enable(m_btnConnect.Window(), false);
			Button_Enable(m_btnDisconnect.Window(), true);
			Button_Enable(m_btnModeServer.Window(), false);
			Button_Enable(m_btnModeClient.Window(), false);
		}
		//else if (!Client.isConnected)
		//{
		//	Button_Enable(m_btnConnect.Window(), true);
		//	Button_Enable(m_btnDisconnect.Window(), false);
		//	Button_Enable(m_btnModeServer.Window(), true);
		//	Button_Enable(m_btnModeClient.Window(), true);
		//}
	}
	break;

	case MODE::SERVER:
	{
		if (!Server.isOnline && !Server.wasServer) {
			Button_Enable(m_btnStart.Window(), true);
			Button_Enable(m_btnPause.Window(), false);
			Button_Enable(m_btnTerminate.Window(), false);
			ShowWindow(m_btnStart.Window(), SW_SHOW);
			ShowWindow(m_btnTerminate.Window(), SW_SHOW);
			ShowWindow(m_btnPause.Window(), SW_SHOW);

			Button_Enable(m_btnConnect.Window(), false);
			Button_Enable(m_btnDisconnect.Window(), false);
			ShowWindow(m_btnConnect.Window(), SW_HIDE);
			ShowWindow(m_btnDisconnect.Window(), SW_HIDE);

			Data.sLabels[0] = "IP Address: ";
			//Data.sLabels[1] = "NB Connected: ";

			SetRect(&Data.textRect, 20, 120, 129, 170);
			InvalidateRect(m_hwnd, &Data.textRect, true);
			char out_ip[50];
			GetWindowText(m_itxtIP.Window(), out_ip, 50);
			Client.ip = out_ip;

			PostMessage(m_itxtIP.Window(), EM_SETREADONLY, (WPARAM)true, 0);
			SetWindowText(m_itxtIP.Window(), Server.ip.c_str());

			//SetWindowLong(hTxtIP, GWL_STYLE, GetWindowLong(hTxtIP, GWL_STYLE) ^ ES_READONLY);
			//UpdateWindow(hTxtIP);
			Client.wasClient = false;
			Server.wasServer = true;
			UpdateWindow(m_hwnd);
		}
		else if (Server.isOnline)
		{
			Button_Enable(m_btnStart.Window(), false);
			Button_Enable(m_btnTerminate.Window(), true);
			Button_Enable(m_btnPause.Window(), true);
			Button_Enable(m_btnModeServer.Window(), false);
			Button_Enable(m_btnModeClient.Window(), false);
		}
		//else if (!Server.isOnline)
		//{
		//	Button_Enable(m_btnStart.Window(), false);
		//	Button_Enable(m_btnTerminate.Window(), true);
		//	Button_Enable(m_btnPause.Window(), true);
		//	Button_Enable(m_btnModeServer.Window(), false);
		//	Button_Enable(m_btnModeClient.Window(), false);
		//}
	}
	break;

	case MODE::UNDEF:
		break;
	}
}

void MainWindow::ConvertInput(PRAWINPUT pRaw, INPUT* pInput) {
	if (pRaw->header.dwType == RIM_TYPEMOUSE) {
		pInput->type = INPUT_MOUSE;
		pInput->mi.dx = pRaw->data.mouse.lLastX;
		pInput->mi.dy = pRaw->data.mouse.lLastY;
		pInput->mi.mouseData = 0;
		pInput->mi.dwFlags = 0;
		pInput->mi.time = 0;
		if (pRaw->data.mouse.lLastX != 0 || pRaw->data.mouse.lLastY != 0) {
			pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_MOVE;
		}

		else if (pRaw->data.mouse.usFlags == MOUSE_MOVE_ABSOLUTE) {
			pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_ABSOLUTE;
		}
		else {
			switch (pRaw->data.mouse.usButtonFlags) {
			case RI_MOUSE_LEFT_BUTTON_DOWN:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_LEFTDOWN;
				break;

			case RI_MOUSE_LEFT_BUTTON_UP:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_LEFTUP;
				break;

			case RI_MOUSE_MIDDLE_BUTTON_DOWN:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_MIDDLEDOWN;
				break;

			case RI_MOUSE_MIDDLE_BUTTON_UP:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_MIDDLEUP;
				break;

			case RI_MOUSE_RIGHT_BUTTON_DOWN:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_RIGHTDOWN;
				break;

			case RI_MOUSE_RIGHT_BUTTON_UP:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_RIGHTUP;
				break;

			case RI_MOUSE_WHEEL:
				pInput->mi.dwFlags = pInput->mi.dwFlags | MOUSEEVENTF_WHEEL;
				pInput->mi.mouseData = pRaw->data.mouse.usButtonData;
				break;
			}
		}

	}
	else if (pRaw->header.dwType == RIM_TYPEKEYBOARD) {
		pInput->type = INPUT_KEYBOARD;
		pInput->ki.wVk = pRaw->data.keyboard.VKey;
		pInput->ki.wScan = MapVirtualKeyA(pRaw->data.keyboard.VKey, MAPVK_VK_TO_VSC);
		pInput->ki.dwFlags = KEYEVENTF_SCANCODE;
		pInput->ki.time = 0;
		if (pRaw->data.keyboard.Message == WM_KEYUP) {
			pInput->ki.dwFlags = KEYEVENTF_KEYUP;
		}
		else if (pRaw->data.keyboard.Message == WM_KEYDOWN) {
			pInput->ki.dwFlags = 0;
		}
	}
}

int MainWindow::SetMode(MODE m)
{
	switch (m)
	{
	case MODE::SERVER:
		if (Client.wasClient)
		{

		}
		Log("Mode server");
		Data.nMode = MODE::SERVER;
		UpdateGuiControls();
		return 0;

	case MODE::CLIENT:

		if (Server.wasServer)
		{

		}
		Log("Mode client");
		Data.nMode = MODE::CLIENT;
		UpdateGuiControls();
		return 0;

	default:
		Log("Mode Unknown");
		Data.nMode = MODE::UNDEF;
		return 0;
	}
	return 0;
}

int MainWindow::ServerStart()
{
	char out_port[50];
	GetWindowText(m_itxtPort.Window(), out_port, 50);
	sPort = out_port;
	SaveConfig();
	if (!Server.isRegistered)
	{
		InitializeInputDevice();
		Log("Input Device Registered");
		Server.isRegistered = true;
	}
	Log("Initializing");
	int error = InitializeServer(Server.sktListen, std::stoi(sPort));
	if (error == 1) {
		Log("Could not initialize server");
		if (MessageBox(m_hwnd, "Could not initialize server", "Remote - Error", MB_ABORTRETRYIGNORE | MB_DEFBUTTON1 | MB_ICONERROR) == IDRETRY) {
			PostMessage(m_hwnd, WM_COMMAND, MAKEWPARAM(BTN_START, BN_CLICKED), 0);
			return 1;
		}
	}
	else {
		Log("Server initialized");
		Server.ClientsInformation.resize(MAX_CLIENTS);
		for (auto& c : Server.ClientsInformation)
		{
			c.socket = INVALID_SOCKET;
			c.ip = "";
			c.id = -1;
		}
		Log("Sockets initialized");
		Server.isOnline = true;
		UpdateGuiControls();
		// start listening thread
		Log("Starting listening thread");
		Server.tListen = std::thread(&MainWindow::ListenThread, this);

		// SCREEN STREAM: Start a screen stream server socket on SCREEN_STREAM_PORT
		SOCKET* sktScreenListenPtr = new SOCKET(INVALID_SOCKET);
		if (InitializeScreenStreamServer(*sktScreenListenPtr, SCREEN_STREAM_PORT) == 0) {
			std::thread([sktScreenListenPtr]() {
				while (true) {
					if (listen(*sktScreenListenPtr, 1) == SOCKET_ERROR) break;
					sockaddr_in client_addr;
					int addrlen = sizeof(client_addr);
					SOCKET sktClient = accept(*sktScreenListenPtr, (sockaddr*)&client_addr, &addrlen);
					if (sktClient == INVALID_SOCKET) continue;
					std::thread(ScreenStreamServerThread, sktClient).detach();
				}
				closesocket(*sktScreenListenPtr);
				delete sktScreenListenPtr;
				}).detach();
				Log("Screen streaming server started");
		}
		else {
			delete sktScreenListenPtr;
			Log("Could not start screen streaming server");
		}
		Server.tListen.detach();
	}
	return 0;
}

int MainWindow::ServerTerminate()
{		
    if (Server.isOnline)
	{
		int error = 1;
		Log("Terminate");
		//MessageBox(m_hwnd, "Terminate", "Remote", MB_OK);
		std::vector<SOCKET> skt_clients;
		for (auto& skt : Server.ClientsInformation)
		{
			skt_clients.push_back(skt.socket);
		}
		TerminateServer(Server.sktListen, skt_clients);
		Server.nConnected = 0;
		Server.isOnline = false;
		Server.cond_listen.notify_all();
		Server.cond_input.notify_all();

		//UpdateGuiControls();
		Button_Enable(m_btnStart.Window(), true);
		Button_Enable(m_btnTerminate.Window(), false);
		Button_Enable(m_btnPause.Window(), false);
		Button_Enable(m_btnModeServer.Window(), true);
		Button_Enable(m_btnModeClient.Window(), true);
		// Also stop screen streaming
		g_screenStreamActive = false;
		return 0;
	}
	return 1;
}

int MainWindow::ClientConnect()
  {
	char out_ip[50];
	char out_port[50];
	int error = 1;
	GetWindowText(m_itxtIP.Window(), out_ip, 50);
	GetWindowText(m_itxtPort.Window(), out_port, 50);
	Client.ip = out_ip;
	sPort = out_port;
	SaveConfig();
	//Log("Initializing client ");
	InitializeClient();
	Log("Connecting to server: " + Client.ip + ":" + sPort);
	error = ConnectServer(Client.sktServer, Client.ip, std::stoi(sPort));
	if (error == 1) {
		Log("Couldn't connect");
		//MessageBox(NULL, "couldn't connect", "Remote", MB_OK);
		}
	else {
		Log("Connected!");
		Client.isConnected = true;
		UpdateGuiControls();
		
		//start receive thread that will receive data
		Log("Starting receive thread");
		Client.tRecv = std::thread(&MainWindow::ReceiveThread, this);
		
		// start send input thread that sends the received input
		Log("Starting input thread");
		Client.tSendInput = std::thread(&MainWindow::OutputThread, this);
		
		Client.tSendInput.detach();
		Client.tRecv.detach();
		// SCREEN STREAM: Start a new window to receive the screen stream
		std::thread([ip = Client.ip](){
		StartScreenRecv(ip, SCREEN_STREAM_PORT);
		}).detach();
		Log("Screen streaming client started");
		}
	return 0;
	}
int MainWindow::ClientDisconnect()
   {
	Log("Disconnect");
	CloseConnection(&Client.sktServer);
	//MessageBox(m_hwnd, "Disconnect", "Remote", MB_OK);
	Log("Ending receive thread");
	Client.isConnected = false;
	Client.cond_input.notify_all();
	Client.cond_recv.notify_all();

	//UpdateGuiControls();
	Button_Enable(m_btnConnect.Window(), true);
	Button_Enable(m_btnDisconnect.Window(), false);
	Button_Enable(m_btnModeServer.Window(), true);
	Button_Enable(m_btnModeClient.Window(), true);

	return 0;
}

int MainWindow::ListenThread()
 {
	bool socket_found = false;
	int index = 0;
	while (Server.isOnline && Data.nMode == MODE::SERVER)
		 {
		std::unique_lock<std::mutex> lock(Server.mu_sktclient);
		if (Server.nConnected >= Server.maxClients)
			 {
			Server.cond_listen.wait(lock);
			}
		if (!socket_found) {
			for (int i = 0; i < Server.ClientsInformation.size(); i++)
				{
				if (Server.ClientsInformation[i].socket == INVALID_SOCKET)
					 {
					socket_found = true;
					index = i;
					}
				 }
			}
		lock.unlock();
		if (listen(Server.sktListen, 1) == SOCKET_ERROR) {
			Log("Listen failed with error: " + std::to_string(WSAGetLastError()));
			}
		sockaddr * inc_conn = new sockaddr;
		int sosize = sizeof(sockaddr);
		Server.ClientsInformation[index].socket = accept(Server.sktListen, inc_conn, &sosize);
		if (Server.ClientsInformation[index].socket == INVALID_SOCKET)
			{
			Log("accept failed: " + std::to_string(WSAGetLastError()));
			}
		else
			{
			Log("Connection accepted");
			Server.nConnected++;
            // Start input receive/inject thread per client
			std::thread(ServerInputRecvThread, Server.ClientsInformation[index].socket).detach();
			socket_found = false;
			}
		 delete inc_conn;
		 }
	Log("Listen thread - ended");
	return 0;
	}

// Removed SendThread logic (no longer needed on server)
 
// --- Add server-side input receiving and injection thread ---

// New function: receive INPUT structs from each client and inject locally

void ServerInputRecvThread(SOCKET clientSocket) {
	while (true) {
		char buffer[sizeof(INPUT) > sizeof(RemoteCtrlMsg) ? sizeof(INPUT) : sizeof(RemoteCtrlMsg)];
		int received = recv(clientSocket, buffer, sizeof(buffer), 0);
		if (received <= 0) break;

		if (received == sizeof(RemoteCtrlMsg)) {
			// Handle control messages from client
			RemoteCtrlMsg* msg = (RemoteCtrlMsg*)buffer;
			if (msg->type == RemoteCtrlType::SetQuality) {
				static const int levels[] = { 20, 40, 60, 80, 100 };
				int q = msg->value;
				if (q >= 1 && q <= 5) {
					g_streamingQuality = levels[q];
					std::cout << "Set streaming quality to level " << q << " (" << levels[q] << ")\n";
				}
			}
			else if (msg->type == RemoteCtrlType::SetFps) {
				int fps = msg->value;
				if (fps == 5 || fps == 10 || fps == 20 || fps == 30 || fps == 40 || fps == 60) {
					g_streamingFps = fps;
					std::cout << "Set streaming FPS to " << fps << "\n";
				}
			}
			continue;
		}

		if (received == sizeof(INPUT)) {
			SendInput(1, (INPUT*)buffer, sizeof(INPUT));
		}
	}
	closesocket(clientSocket);
}

int MainWindow::ReceiveThread()
{
	while (Client.isConnected && Data.nMode == MODE::CLIENT)
	{
		int error = 1;
		error = ReceiveServer(Client.sktServer, Client.recvBuff);
		if (error == 0)
		{
			std::unique_lock<std::mutex> lock(Client.mu_input);
			Client.inputQueue.emplace(Client.recvBuff);
			lock.unlock();
			Client.cond_input.notify_all();
		}
		else
		{
			Client.isConnected = false;
			Log("No input received, disconnecting");
			PostMessage(m_hwnd, WM_COMMAND, MAKEWPARAM(BTN_DISCONNECT, BN_CLICKED), 0);
		}
	}
	return 0;
}
	 int MainWindow::OutputThread()
{
		 while (Client.isConnected && Data.nMode == MODE::CLIENT)
	{
		std::unique_lock<std::mutex> lock(Client.mu_input);
		if (Client.inputQueue.empty())
		{
			Client.cond_input.wait(lock);
		}
		else
		{
			int sz = Client.inputQueue.size();
			INPUT* tInputs = new INPUT[sz];
			for (int i = 0; i < sz; ++i)
			{
				tInputs[i] = Client.inputQueue.front();
				Client.inputQueue.pop();
			}
			lock.unlock();
			UpdateInput();
			if (tInputs->mi.mouseData != 0)
			{
				tInputs->mi.mouseData = (int16_t)tInputs->mi.mouseData;
			}
			//std::cout << "sending input" << std::endl;
			SendInput(sz, tInputs, sizeof(INPUT));
			delete[] tInputs;
		}
	}
	Log("Receive thread - ended");
	return 0;
}


	 // --- SaveConfig: now saves FPS, AlwaysOnTop, and window size ---
	 bool MainWindow::SaveConfig()
	 {
		 std::fstream f(configName, std::fstream::out | std::fstream::trunc);
		 if (!f.is_open())
		 {
			 std::cout << "can't save" << std::endl;
			 return false;
		 }
		 f << "port " << sPort << std::endl;
		 f << "server_ip " << Client.ip << std::endl;
		 f << "max_clients " << Server.maxClients << std::endl;
		 f << "fps " << g_streamingFps.load() << std::endl;
		 f << "always_on_top " << (g_alwaysOnTop ? 1 : 0) << std::endl;
		 RECT r;
		 if (m_hwnd && GetWindowRect(m_hwnd, &r)) {
			 f << "window_size " << (r.right - r.left) << " " << (r.bottom - r.top) << std::endl;
		 }
		 else {
			 f << "window_size " << m_savedWinW << " " << m_savedWinH << std::endl;
		 }
		 f.close();
		 return true;
	 }

	 // --- LoadConfig: now loads FPS, AlwaysOnTop, and window size ---
	 bool MainWindow::LoadConfig()
	 {
		 sPort = std::to_string(DEFAULT_PORT);
		 iPort = std::stoi(sPort);
		 Server.maxClients = MAX_CLIENTS;
		 m_savedFps = SCREEN_STREAM_FPS;
		 m_savedAlwaysOnTop = false;
		 m_savedWinW = 477;
		 m_savedWinH = 340;
		 std::fstream f(configName, std::fstream::in);
		 if (!f.is_open())
		 {
			 return false;
		 }

		 std::string line;
		 std::string param;
		 std::stringstream s;

		 while (std::getline(f, line))
		 {
			 s.clear();
			 s.str(line);
			 s >> param;
			 if (param == "port") {
				 s >> sPort;
			 }
			 else if (param == "server_ip") {
				 s >> Client.ip;
			 }
			 else if (param == "max_clients") {
				 std::string max;
				 s >> max;
				 Server.maxClients = std::stoi(max);
			 }
			 else if (param == "fps") {
				 int fps;
				 s >> fps;
				 if (fps >= 5 && fps <= 60) {
					 m_savedFps = fps;
					 g_streamingFps = fps;
					 g_screenStreamMenuFps = fps;
					 g_screenStreamActualFps = fps;
				 }
			 }
			 else if (param == "always_on_top") {
				 int atop = 0;
				 s >> atop;
				 m_savedAlwaysOnTop = (atop != 0);
				 g_alwaysOnTop = m_savedAlwaysOnTop;
			 }
			 else if (param == "window_size") {
				 int w = 477, h = 340;
				 s >> w >> h;
				 if (w > 100 && h > 100) {
					 m_savedWinW = w;
					 m_savedWinH = h;
				 }
			 }
		 }

		 std::cout << "Config Loaded:\n"
			 << "    port = " << sPort << '\n'
			 << "    server ip = " << Client.ip << '\n'
			 << "    max number clients = " << Server.maxClients << std::endl
			 << "    fps = " << m_savedFps << std::endl
			 << "    always_on_top = " << (m_savedAlwaysOnTop ? "true" : "false") << std::endl
			 << "    window size = " << m_savedWinW << "x" << m_savedWinH << std::endl;

		 f.close();
		 return true;
	 }



	 int main()
	 {
		 // ---- Ensure WSAStartup is called ONCE here ----
		 WSADATA wsadata;
		 int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsadata);
		 if (wsaResult != 0) {
			 std::cout << "WSAStartup failed: " << wsaResult << std::endl;
			 return 1;
		 }
		 MainWindow win;
		 g_pMainWindow = &win; // <-- Set the global pointer after win is defined

		 // Use loaded size from config
		 int winW = win.m_savedWinW;
		 int winH = win.m_savedWinH;
		 if (!win.Create(nullptr, "Remote", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, winW, winH, NULL))
		 {
			 std::cout << "error creating the main window: " << GetLastError() << std::endl;
			 WSACleanup();
			 return 0;
		 }

		 // Set Always On Top on restore
		 if (win.m_savedAlwaysOnTop) {
			 SetWindowPos(win.Window(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
		 }

		 ShowWindow(win.Window(), 1);

		 MSG msg = { };
		 while (GetMessage(&msg, NULL, 0, 0))
		 {
			 TranslateMessage(&msg);
			 DispatchMessage(&msg);
		 }
		 WSACleanup();
		 return 0;
	 }
