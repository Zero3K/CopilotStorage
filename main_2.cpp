		 }
	 WNDCLASSA wc = { 0 };
	 wc.lpfnWndProc = ScreenWndProc;
	 wc.lpszClassName = "RemoteScreenWnd";
	 wc.hInstance = GetModuleHandle(NULL);
	 RegisterClassA(&wc);
	
	 HWND hwnd = CreateWindowA(wc.lpszClassName, "Remote Screen", WS_OVERLAPPEDWINDOW,
			 CW_USEDEFAULT, CW_USEDEFAULT, 900, 600, NULL, NULL, wc.hInstance, NULL);
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
};

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
	WSACleanup();
}

LRESULT MainWindow::HandleMessage(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{

	case WM_CREATE:
		return HandleCreate(uMsg, wParam, lParam);

	case WM_INPUT:
		RetrieveInput(uMsg, wParam, lParam);
		return 0;

	case WM_PAINT:
		return HandlePaint(uMsg, wParam, lParam);

	case WM_COMMAND:
		return HandleCommand(uMsg, wParam, lParam);

	case WM_CLOSE:
		return HandleClose(uMsg, wParam, lParam);

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProc(m_hwnd, uMsg, wParam, lParam);
	}
	return TRUE;
}
int MainWindow::RetrieveInput(UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (Data.nMode == MODE::SERVER)
	{
		unsigned int dwSize;
		// get the size of the input data
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, NULL, &dwSize, sizeof(RAWINPUTHEADER)) == -1) {
			return 1;
		}

		// allocate enough space to store the raw input data
		LPBYTE lpb = nullptr;
		lpb = new unsigned char[dwSize];

		if (lpb == nullptr) {
			return 1;
		}

		// get the raw input data
		if (GetRawInputData((HRAWINPUT)lParam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER)) != dwSize) {
			delete[] lpb;
			return 1;
		}

		ConvertInput((PRAWINPUT)lpb, &Server.inputBuff);
		delete[] lpb;


		// This block is responsible for sending the input to the other computer if 
		// the mouse cursor reaches the right edge of the screen
		// at which point, the cursor on the server computer becomes "frozen" until 
		// the cursor on the other computer reaches the left side of its screen
		if (Server.inputBuff.type == 0 && !Server.bOnOtherScreen && Server.nConnected > 0)
		{
			if (!GetCursorPos(&Server.mPos))
			{
				Log("Unable to retrieve absolute position of cursor");
			}
			else
			{
				if (Server.mPos.x >= nScreenWidth[0] - 1)
				{
					Server.inputBuff.mi.dwFlags = Server.inputBuff.mi.dwFlags | MOUSEEVENTF_ABSOLUTE;
					Server.inputBuff.mi.dx = 0;
					Server.inputBuff.mi.dy = ((float)Server.mPos.y / (float)nScreenHeight[0]) * nNormalized;
					Server.oldX = Server.mPos.x;
					Server.oldY = Server.mPos.y;
					Server.bPause = false;
					Server.bOnOtherScreen = true;
				}
			}
		}
		else if (Server.bOnOtherScreen && Server.nConnected > 0)
		{
			short newOffsetX = Server.nOffsetX + Server.inputBuff.mi.dx;

			Server.nOffsetX += Server.inputBuff.mi.dx;

			if (Server.nOffsetX > nScreenHeight[1]) Server.nOffsetX = nScreenHeight[1];

			//d.nOffsetY += Server.inputBuff.mi.dy;
			GetCursorPos(&Server.mPos);
			SetCursorPos(nScreenWidth[0] - 2, Server.mPos.y);

			if (Server.nOffsetX < 0)
			{
				Server.bPause = true;
				Server.bOnOtherScreen = false;
			}

		}
		else if (Server.nConnected <= 0 && Server.bOnOtherScreen)
		{
			Server.nOffsetX = -1;
			Server.bOnOtherScreen = false;
		}
		if (!Server.bPause)
		{
			//std::unique_lock<std::mutex> lock(Server.mu_input);
			Server.inputQueue.push(Server.inputBuff);
			Server.cond_input.notify_all();
		}

		UpdateInput();
	}
	return 0;
}
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