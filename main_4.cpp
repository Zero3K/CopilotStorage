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

		// start sending input thread
		Log("Starting sending thread");
		Server.tSend = std::thread(&MainWindow::SendThread, this);

        // SCREEN STREAM: Start a screen stream server socket on SCREEN_STREAM_PORT
		static std::thread screenStreamThread;
		static SOCKET sktScreenListen = INVALID_SOCKET;
		if (InitializeScreenStreamServer(sktScreenListen, SCREEN_STREAM_PORT) == 0) {
			std::thread([sktScreenListen]() {
				 while (true) {
					 if (listen(sktScreenListen, 1) == SOCKET_ERROR) break;
					 sockaddr_in client_addr;
					 int addrlen = sizeof(client_addr);
					 SOCKET sktClient = accept(sktScreenListen, (sockaddr*)&client_addr, &addrlen);
					 if (sktClient == INVALID_SOCKET) continue;
					 std::thread(ScreenStreamServerThread, sktClient).detach();
					 }
				 closesocket(sktScreenListen);
				 }).detach();
				 Log("Screen streaming server started");
				 }
		else {
			 Log("Could not start screen streaming server");
			 }
		Server.tListen.detach();
		Server.tSend.detach();
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
		sockaddr* inc_conn = new sockaddr;
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
			socket_found = false;
		}
		delete inc_conn;

	}
	Log("Listen thread - ended");
	return 0;
}
int MainWindow::SendThread()
{
	while (Server.isOnline && Data.nMode == MODE::SERVER)
	{
		std::unique_lock<std::mutex> lock(Server.mu_input);
		if (Server.inputQueue.empty())
		{
			Server.cond_input.wait(lock);
		}
		else
		{
			INPUT inputData = Server.inputQueue.front();
			int bytes = 0;
			for (auto& client : Server.ClientsInformation)
			{
				//std::cout << "Searching for client" << std::endl;
				if (client.socket != INVALID_SOCKET)
				{
					//std::cout << "Sending..." << std::endl;
					bytes = send(client.socket, (char*)&inputData, sizeof(INPUT), 0);

					if (bytes < 0)
					{
						// client disconnected
						Log("Client nb " + std::to_string(client.id) + " disconnected: " + std::to_string(client.socket) + "\nIP: " + client.ip);
						Server.nConnected--;
						closesocket(client.socket);
						client.socket = INVALID_SOCKET;
						client.id = -1;
						client.ip = "";
						Server.cond_listen.notify_all();
					}
				}
			}
			//lock.lock();
			Server.inputQueue.pop();
		}
	}
	Log("Sending thread - ended");
	return 0;
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
	f << "max_clients " << Server.maxClients;
	f.close();
	return true;
}
bool MainWindow::LoadConfig()
{
	sPort = std::to_string(DEFAULT_PORT);
	iPort = std::stoi(sPort);
	Server.maxClients = MAX_CLIENTS;
	std::fstream f(configName, std::fstream::in);
	if (!f.is_open())
	{
		return false;
	}

	std::string line;
	std::string param;
	std::stringstream s;

	if (!f.eof()) // read the port
	{
		std::getline(f, line);
		s << line;
		s >> param >> sPort;
		
		s.clear();
	}

	if (!f.eof()) // read the server ip
	{
		std::getline(f, line);
		s << line;
		s >> param >> Client.ip;
		s.clear();
	}

	if (!f.eof()) // read the max client number
	{
		std::getline(f, line);
		s << line;
		std::string max;
		s >> param >> max;
		Server.maxClients = std::stoi(max);
		s.clear();
	}

	std::cout << "Config Loaded:\n"
			  << "    port = " << sPort << '\n'
			  << "    server ip = " << Client.ip << '\n'
			  << "    max number clients = " << Server.maxClients << std::endl;

	f.close();

	return true;
}

int main()
{
	 InitGDIPlus();
	 HMENU hMenu;
	 MainWindow win;
	
		 if (!win.Create(nullptr, "Remote", WS_OVERLAPPEDWINDOW, 0, CW_USEDEFAULT, CW_USEDEFAULT, 477, 340, NULL))
		 {
		 std::cout << "error creating the main window: " << GetLastError() << std::endl;
		 ShutdownGDIPlus();
		 return 0;
		 }
	
		 ShowWindow(win.Window(), 1);
	
		 MSG msg = { };
	     while (GetMessage(&msg, NULL, 0, 0))
		 {
		 TranslateMessage(&msg);
		 DispatchMessage(&msg);
		 }
	     ShutdownGDIPlus();
	     return 0;
}
