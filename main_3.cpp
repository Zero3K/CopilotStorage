{
	//m_btnOk.Create(L"OK", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON, 0, 50, 50, 100, 100, this->m_hwnd);
	CreateWindowA("BUTTON", "Mode", WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_GROUPBOX, 20, 10, 190, 60, m_hwnd, (HMENU)BTN_MODE, (HINSTANCE)GetWindowLong(m_hwnd, GWL_HINSTANCE), NULL);
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
	RECT textRect;
	HRGN bgRgn;
	HBRUSH hBrush;
	HPEN hPen;


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