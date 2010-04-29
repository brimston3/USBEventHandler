//------------------------------------------------------------------------------
// File: SystemChangeNotifierWin.cpp       (c)2005 TCWorks Soft- & Hardware GmbH
//------------------------------------------------------------------------------
/**	@file	SystemChangeNotifierWin.cpp
	Windows implementation of class SystemChangeNotifier

	@history
	@h 15, July 2005, (Niels Buntrock) Initial version.
*/
#include <windows.h>
#include <dbt.h>
#include "PCoreMacros.h"
#include "PowerCore.h"
#include "PCoreMessages.h"
#include "IPCoreAPI.h"
#include "PCoreDevice.h"
#include "ChangeNotifier.h"

// our main global object
ChangeNotifier	g_Notifier;

UINT			ChangeNotifier::s_PCoreChangeMessage;

//-----------------------------------------------------------------------------
// Constants
//-----------------------------------------------------------------------------

#define GOO_DEFINE_GUID(name, l, w1, w2, b1, b2, b3, b4, b5, b6, b7, b8) \
		PCGuids::PCGuid PCGuids::name = { l, w1, w2, { b1, b2,  b3,  b4,  b5,  b6,  b7,  b8 } }

namespace
{
    class PCGuids
    {
    public:
	    typedef	const GUID				PCGuid;

	    static PCGuid					kPCORE_GUID_PCI;
	    static PCGuid					kPCORE_GUID_FW;
	    static PCGuid					kPCORE_GUID_PNP;
    };

    GOO_DEFINE_GUID (kPCORE_GUID_PCI, 0xb6d97689, 0x057e, 0x4c1a, 0xa3, 0x17, 0x04, 0x1a, 0x01, 0x3a, 0xd4, 0x28);
    GOO_DEFINE_GUID (kPCORE_GUID_FW,  0xa3e9921b, 0xd498, 0x48db, 0x93, 0x30, 0x4f, 0x79, 0xab, 0x84, 0x34, 0x92);
    GOO_DEFINE_GUID (kPCORE_GUID_PNP, 0x4e725747, 0x77ce, 0x46fb, 0x90, 0xa9, 0xd1, 0x95, 0xb3, 0x06, 0x80, 0x58);
}

//-----------------------------------------------------------------------------
// Static file scope
//-----------------------------------------------------------------------------

namespace
{
    LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {   
        LRESULT ret = 0;

        switch (message)
        {
            case WM_DEVICECHANGE:
            {
				switch (wParam)
				{
					case DBT_DEVICEARRIVAL:
					{
						PDEV_BROADCAST_DEVICEINTERFACE pBDI = (PDEV_BROADCAST_DEVICEINTERFACE) lParam;
						LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
						ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

						if (pBDI->dbcc_classguid == PCGuids::kPCORE_GUID_FW)
						{
						}
						pNotifier->OnSystemChange(NULL);
						break;
					}
					case DBT_DEVICEREMOVECOMPLETE:
					{
						PDEV_BROADCAST_DEVICEINTERFACE pBDI = (PDEV_BROADCAST_DEVICEINTERFACE) lParam;
						LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
						ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

						pNotifier->OnSystemChange(NULL);
						break;
					}
					case DBT_CUSTOMEVENT:
					{
						PDEV_BROADCAST_HANDLE	pBH = (PDEV_BROADCAST_HANDLE) lParam;
						LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
						ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

						pNotifier->OnSystemChange((TNativeHandle) pBH->dbch_handle);
						break;
					}
				}
	            break;
            }
            default:
            {
                ret = ::DefWindowProc(hWnd, message, wParam, lParam);
            }
        }
        return ret;
    }
}

//----------------------------------------------------------------------------------
// Constructor
//----------------------------------------------------------------------------------

ChangeNotifier::ChangeNotifier(void)
{
	m_CLDeque.clear();
    InstallNotification(NULL);
}  

//----------------------------------------------------------------------------------
// Destructor
//----------------------------------------------------------------------------------

ChangeNotifier::~ChangeNotifier()
{
    UninstallNotification(NULL);
	m_CLDeque.clear();
}

//----------------------------------------------------------------------------------
// AddListener
//----------------------------------------------------------------------------------

void ChangeNotifier::AttachListener(ChangeListener* pListener, PCORE_HANDLE hDevice)
{
	CLElement	element;

	element.pListener = pListener;
	element.hDevice = hDevice;
	element.hDevNotifyHandle = 0;

	if (hDevice != NULL)
	{
		// we want events for one specific device
		DEV_BROADCAST_HANDLE db = {0};

		db.dbch_size		= sizeof(db);
		db.dbch_devicetype	= DBT_DEVTYP_HANDLE;
		db.dbch_eventguid	= PCGuids::kPCORE_GUID_PNP;
		db.dbch_handle		= hDevice;

		if (pListener->isFW())
		{
			db.dbch_hdevnotify	= m_hDevFW;
		}
		else
		{
			db.dbch_hdevnotify	= m_hDevPCI;
		}
		element.hDevNotifyHandle = ::RegisterDeviceNotification ((HWND)m_NativeEventTarget, &db, DEVICE_NOTIFY_WINDOW_HANDLE);
	}
	m_CLDeque.push_back(element);
}

//----------------------------------------------------------------------------------
// RemoveListener
//----------------------------------------------------------------------------------

void ChangeNotifier::RemoveListener(ChangeListener* pListener)
{
	TCLIterator	i;

	try
	{
		bool	bRepeat;

		do
		{
			// not sure what happens with our for loop if element in the list
			// is erased, so in that case break and start it again. Might be
			// safer until I'm more familiar with stdlib iterators
			bRepeat = false;
			for (i = m_CLDeque.begin(); i != m_CLDeque.end(); i++)
			{
				if ((*i).pListener == pListener)
				{
					if ((*i).hDevice != NULL)
					{
						::UnregisterDeviceNotification((HDEVNOTIFY) (*i).hDevNotifyHandle);
					}
					m_CLDeque.erase(i);
					bRepeat = true;
					break;
				}
			}
		}
		while(bRepeat);
	}
	catch(...)
	{
	}
}

//------------------------------------------------------------------------------
// InstallNotification										[virtual][protected]
//------------------------------------------------------------------------------

void ChangeNotifier::InstallNotification(CLElement* pElement)
{
    static const TCHAR szClassName[]  = "ChangeNotifierPoCoAPI";
    static const DWORD dwWindowStyles = (WS_CHILDWINDOW | WS_CLIPSIBLINGS);

    HINSTANCE hInstance = ::GetModuleHandle(NULL);

    WNDCLASSEX wndcls;
    wndcls.cbSize			= sizeof(WNDCLASSEX); 
	wndcls.style			= CS_HREDRAW | CS_VREDRAW;
	wndcls.lpfnWndProc		= WndProc;
	wndcls.cbClsExtra		= NULL;
	wndcls.cbWndExtra		= NULL;
	wndcls.hInstance		= hInstance;
	wndcls.hIcon			= NULL;
	wndcls.hCursor			= NULL;
	wndcls.hbrBackground	= NULL;
	wndcls.lpszMenuName		= NULL;
	wndcls.lpszClassName	= szClassName;
	wndcls.hIconSm			= NULL;

    // register class only once
    static bool isRegistered = false;

    if (!isRegistered && !::RegisterClassEx(&wndcls))
    {
        // TODO handle exception
//		MessageBox (NULL, TEXT ("RegisterClassEx failed"), TEXT("PowerCore"), MB_ICONERROR);
        return;
    }

    isRegistered = true;

    m_NativeEventTarget = (TNativeHandle)::CreateWindowEx(0,
		szClassName, 
		TEXT ("message-only window"),
		dwWindowStyles,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		HWND_MESSAGE, // message-only window
		NULL,
		hInstance,
		NULL);

    // store "this"-pointer as user data in the window handle
    ::SetWindowLongPtr((HWND)m_NativeEventTarget, GWLP_USERDATA, (LONG_PTR) this);

	// we want global system events
	DEV_BROADCAST_DEVICEINTERFACE 	db;

	memset (&db, 0, sizeof(db));
	db.dbcc_size = sizeof(db);
	db.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	db.dbcc_classguid = PCGuids::kPCORE_GUID_PCI;

	m_hDevPCI = ::RegisterDeviceNotification ((HWND)m_NativeEventTarget, &db, DEVICE_NOTIFY_WINDOW_HANDLE);

	memset (&db, 0, sizeof(db));
	db.dbcc_size = sizeof(db);
	db.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	db.dbcc_classguid = PCGuids::kPCORE_GUID_FW;

	m_hDevFW = ::RegisterDeviceNotification ((HWND)m_NativeEventTarget, &db, DEVICE_NOTIFY_WINDOW_HANDLE);

	s_PCoreChangeMessage = RegisterWindowMessage(szPCoreWinMessageName);
}

//------------------------------------------------------------------------------
// UninstallNotification                                    [virtual][protected]
//------------------------------------------------------------------------------

void ChangeNotifier::UninstallNotification(CLElement* pElement)
{
    if (m_hDevPCI)
    {
        ::UnregisterDeviceNotification((HDEVNOTIFY) m_hDevPCI);
    }
    if (m_hDevFW)
    {
        ::UnregisterDeviceNotification((HDEVNOTIFY) m_hDevFW);
    }
    if(m_NativeEventTarget)
	{
		::DestroyWindow( (HWND) m_NativeEventTarget );
	}
}

//------------------------------------------------------------------------------
// OnSystemChange											   [virtual][public]
//------------------------------------------------------------------------------

void ChangeNotifier::OnSystemChange(TNativeHandle hDevice)
{
	TCLIterator	i;

	int ct=0;
	for (i = m_CLDeque.begin(); i != m_CLDeque.end(); i++)
	{
		ct++;

		if ((*i).hDevice == hDevice)
		{
			(*i).pListener->SystemChanged();
		}
	}
}

//------------------------------------------------------------------------------
// BroadcastMessage											   [virtual][public]
//------------------------------------------------------------------------------

void ChangeNotifier::BroadcastMessage(int deviceIndex)
{
	HWND	hTarget = NULL;

	if (deviceIndex >= 0)
	{
		hTarget = ::FindWindowEx(HWND_MESSAGE, hTarget, "DeviceChangeNotifier", NULL);
		while (hTarget != NULL)
		{
			::PostMessage(hTarget, s_PCoreChangeMessage, (WPARAM) deviceIndex, (LPARAM) 0);
			hTarget = ::FindWindowEx(HWND_MESSAGE, hTarget, "DeviceChangeNotifier", NULL);
		}
	}
	else
	{
		hTarget = ::FindWindowEx(HWND_MESSAGE, NULL, "SystemChangeNotifier", NULL);
		if (hTarget != NULL)
		{
			::PostMessage(hTarget, s_PCoreChangeMessage, (WPARAM) -1, (LPARAM) 0);
		}
	}
}

