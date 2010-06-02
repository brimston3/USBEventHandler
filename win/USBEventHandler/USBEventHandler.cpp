// USBEventHandler.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"
#include "com_sue_protocol_SerialPortObserverThread.h"
#include <windows.h>
#include <dbt.h>
#include <tchar.h>

#include <atlstr.h>
#include <windows.h>
#include <stdio.h>
#include <tchar.h>
#include <strsafe.h>
#include <dbt.h>
#include <shellapi.h>
#include <setupapi.h>


LRESULT OnMyDeviceChange(WPARAM wParam, LPARAM lParam);
void UpdateDevice(PDEV_BROADCAST_DEVICEINTERFACE pDevInf, WPARAM wParam);
BOOL FindDevice(HDEVINFO& hDevInfo, CString& szDevId, SP_DEVINFO_DATA& spDevInfoData);


// GUID for USB devices
GUID WceusbshGUID = { 0xA5DCBF10, 0x6530, 0x11D2, { 0x90, 0x1F, 0x00, 0xC0, 0x4F, 0xB9, 0x51, 0xED } };



JavaVM *cached_jvm;
jobject obj_USBEventHandler = NULL;
jmethodID mid_deviceRemoved = NULL;
jmethodID mid_deviceAdded = NULL;


//#define CALLTESTFUNCTION
#ifdef CALLTESTFUNCTION
jmethodID mid_test = NULL;
#endif


/*----------------------------------------------------------
 get_java_environment
 
 accept:      pointer to the virtual machine
 flag to know if we are attached
 return:      pointer to the Java Environment
 exceptions:  none
 comments:    see JNI_OnLoad.  For getting the JNIEnv in the thread
 used to monitor for output buffer empty.
 ----------------------------------------------------------*/
JNIEnv *JNI_GetEnv(jboolean *was_attached){
	JNIEnv *env = NULL;
	jint err;
	
	*was_attached = JNI_FALSE;
	
	if (cached_jvm == NULL) {
		fprintf(stderr, "JNI_GetEnv: cached_jvm == NULL\n");
		return env;
	}
	
	err = cached_jvm->GetEnv((void **)&env, JNI_VERSION_1_2);
	
	if (err == JNI_ERR) {
		fprintf(stderr, "JNI_GetEnv: unknown error getting the JNI Env!\n");
		return env;
	}

	if (err == JNI_EDETACHED) {
		err = cached_jvm->AttachCurrentThread((void**)&env, NULL);
	}
	
	if (err == JNI_OK) {
		//fprintf(stderr, "JNI_GetEnv: attached to Thread!\n");
		*was_attached = JNI_TRUE;
	} else {
		fprintf(stderr, "JNI_GetEnv: error %i\n", err);
	}
	
	return env;
}


//================================================================================================
//
//	JNI_OnLoad
//
//	You can invoke any JNI functions in an implementation of JNI_Onload. A typical use of the
//  JNI_OnLoad handler is caching the JavaVM  pointer, class references, or method and field IDs
//
//================================================================================================
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
	JNIEnv *env;
	cached_jvm = jvm;  /* cache the JavaVM pointer */
	
	fprintf(stderr, "\nUSBEventHandler JNI_OnLoad\n");
	
	if (jvm->GetEnv((void **)&env, JNI_VERSION_1_2)) {
		return JNI_ERR; /* JNI version not supported */
	}
	
	return JNI_VERSION_1_2;
}


//================================================================================================
//
//	JNI_OnUnload
//
//	The JNI_OnUnload function deletes the weak global reference to the C class created in the
//  JNI_OnLoad handler. We need not delete the method ID MID_C_g because the virtual machine
//  automatically reclaims the resources needed to represent C's method IDs when unloading its
//  defining class C.
//
//================================================================================================
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *jvm, void *reserved)
{
	JNIEnv *env;
	
	fprintf(stderr, "\nUSBEventHandler JNI_OnUnload\n");
	
	if (jvm->GetEnv((void **)&env, JNI_VERSION_1_2)) {
		return;
	}
	
	env->DeleteWeakGlobalRef(obj_USBEventHandler);
}


//
// RegisterDeviceInterfaceToHwnd
//
// Routine Description:
//     Registers an HWND for notification of changes in the device interfaces
//     for the specified interface class GUID. 
// Parameters:
//     InterfaceClassGuid - The interface class GUID for the device 
//         interfaces. 
//     hWnd - Window handle to receive notifications.
//     hDeviceNotify - Receives the device notification handle. On failure, 
//         this value is NULL.
// Return Value:
//     If the function succeeds, the return value is TRUE.
//     If the function fails, the return value is FALSE.
// Note:
//     RegisterDeviceNotification also allows a service handle be used,
//     so a similar wrapper function to this one supporting that scenario
//     could be made from this template.
BOOL RegisterDeviceInterfaceToHwnd(IN GUID InterfaceClassGuid, IN HWND hWnd, OUT HDEVNOTIFY *hDeviceNotify)
{
    DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

    ZeroMemory( &NotificationFilter, sizeof(NotificationFilter) );
    NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
    NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    NotificationFilter.dbcc_classguid = InterfaceClassGuid;

    *hDeviceNotify = RegisterDeviceNotification( 
        hWnd,                       // events recipient
        &NotificationFilter,        // type of device
        DEVICE_NOTIFY_WINDOW_HANDLE // type of recipient handle
        );

    if ( NULL == *hDeviceNotify ) 
    {
        fprintf(stderr, "RegisterDeviceNotification");
        return FALSE;
    }

    return TRUE;
}


//
// MessagePump
//
// Routine Description:
//     Simple main thread message pump.
//
// Parameters:
//     hWnd - handle to the window whose messages are being dispatched
// Return Value:
//     None.
void MessagePump(HWND hWnd)
{
    MSG msg; 
    int retVal;

    // Get all messages for any window that belongs to this thread,
    // without any filtering. Potential optimization could be
    // obtained via use of filter values if desired.

    while( (retVal = GetMessage(&msg, NULL, 0, 0)) != 0 ) 
    { 
        if ( retVal == JNI_ERR )
        {
            fprintf(stderr, "GetMessage failed");
            break;
        }
        else
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    } 
}




LRESULT OnMyDeviceChange(WPARAM wParam, LPARAM lParam)
{
	if ( DBT_DEVICEARRIVAL == wParam || DBT_DEVICEREMOVECOMPLETE == wParam ) {
		PDEV_BROADCAST_HDR pHdr = (PDEV_BROADCAST_HDR)lParam;
		PDEV_BROADCAST_DEVICEINTERFACE pDevInf;
		PDEV_BROADCAST_HANDLE pDevHnd;
		PDEV_BROADCAST_OEM pDevOem;
		PDEV_BROADCAST_PORT pDevPort;
		PDEV_BROADCAST_VOLUME pDevVolume;
		switch( pHdr->dbch_devicetype ) {
			case DBT_DEVTYP_DEVICEINTERFACE:
				pDevInf = (PDEV_BROADCAST_DEVICEINTERFACE)pHdr;
				UpdateDevice(pDevInf, wParam);
				break;

			case DBT_DEVTYP_HANDLE:
				pDevHnd = (PDEV_BROADCAST_HANDLE)pHdr;
				break;

			case DBT_DEVTYP_OEM:
	
				pDevOem = (PDEV_BROADCAST_OEM)pHdr;
				break;

			case DBT_DEVTYP_PORT:
				pDevPort = (PDEV_BROADCAST_PORT)pHdr;
				break;

			case DBT_DEVTYP_VOLUME:
				pDevVolume = (PDEV_BROADCAST_VOLUME)pHdr;
				break;
		}
	}
	return 0;
}



void FindConnectedDevices() {
	CString szDevId = _T('USB\\VID_1F2E&PID_000A');
	CString szClass = _T('USB');

	HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL, szClass, NULL, DIGCF_ALLCLASSES | DIGCF_PRESENT);
	if( INVALID_HANDLE_VALUE == hDevInfo ) {
		return;
	}

	SP_DEVINFO_DATA spDevInfoData;
	if ( FindDevice(hDevInfo, szDevId, spDevInfoData) ) {

	}

}



void UpdateDevice(PDEV_BROADCAST_DEVICEINTERFACE pDevInf, WPARAM wParam)
{
	// pDevInf->dbcc_name: 
	// \\?\USB#Vid_04e8&Pid_503b#0002F9A9828E0F06#{a5dcbf10-6530-11d2-901f-00c04fb951ed}
	// szDevId: USB\Vid_04e8&Pid_503b\0002F9A9828E0F06
	// szClass: USB
	//ASSERT(lstrlen(pDevInf->dbcc_name) > 4);

	CString szDevId = pDevInf->dbcc_name+4;

	int idx = szDevId.ReverseFind(_T('#'));
	szDevId.Truncate(idx);
	szDevId.Replace(_T('#'), _T('\\'));
	szDevId.MakeUpper();

	// USB\VID_1F2E&PID_000A
	idx = szDevId.Find(_T('USB\\VID_1F2E&PID_000A'));
	if(idx == -1) {
		fprintf(stderr, "not a cube!!");
		return;
	}

	CString szClass;
	idx = szDevId.Find(_T('\\'));
	szClass = szDevId.Left(idx);

	// seems we should ignore "ROOT" type....
	if ( _T("ROOT") == szClass ) {
		return;
	}

	DWORD dwFlag = DBT_DEVICEARRIVAL != wParam ? 
		DIGCF_ALLCLASSES : (DIGCF_ALLCLASSES | DIGCF_PRESENT);
	HDEVINFO hDevInfo = SetupDiGetClassDevs(NULL,szClass,NULL,dwFlag);
	if( INVALID_HANDLE_VALUE == hDevInfo ) {
		//AfxMessageBox(CString("SetupDiGetClassDevs(): ") 
		//	+ _com_error(GetLastError()).ErrorMessage(), MB_ICONEXCLAMATION);
		return;
	}




	SP_DEVINFO_DATA spDevInfoData;
	if ( FindDevice(hDevInfo, szDevId, spDevInfoData) ) {
		// OK, device found
		DWORD DataT ;
		TCHAR buf[MAX_PATH];
		DWORD nSize = 0;

		// get Friendly Name or Device Description
		if ( SetupDiGetDeviceRegistryProperty(hDevInfo, &spDevInfoData, 
			SPDRP_FRIENDLYNAME, &DataT, (PBYTE)buf, sizeof(buf), &nSize) ) {
		} else if ( SetupDiGetDeviceRegistryProperty(hDevInfo, &spDevInfoData, 
			SPDRP_DEVICEDESC, &DataT, (PBYTE)buf, sizeof(buf), &nSize) ) {
		} else {
			lstrcpy(buf, _T("Unknown"));
		}

		// get COM port
		CString szTemp = buf;
		int left = szTemp.ReverseFind(_T('('))+1;
		int right = szTemp.ReverseFind(_T(')'));
		szTemp = szTemp.Mid(left, right-left);
		szTemp = szTemp.MakeUpper();

		//fprintf(stderr, "serial port: %s", szTemp);
		jboolean jvm_was_attached;
		JNIEnv *env = JNI_GetEnv(&jvm_was_attached);
		if (jvm_was_attached == JNI_FALSE) {
			fprintf(stderr, "Could not attach to JVM!\n");
			return;
		}
		jstring jSerialPort = env->NewString((jchar *)szTemp.GetBuffer(), (jsize)szTemp.GetLength());


		if ( DBT_DEVICEARRIVAL == wParam ) {

			// device added
			if (mid_deviceAdded != NULL && obj_USBEventHandler != NULL) {
				env->CallVoidMethod(obj_USBEventHandler, mid_deviceAdded, jSerialPort);
			} else {
				fprintf(stderr, "DeviceAdded can not call deviceAdded!\n");
			}


		} else {

			// device remove
			if (mid_deviceRemoved != NULL && obj_USBEventHandler != NULL) {
				env->CallVoidMethod(obj_USBEventHandler, mid_deviceRemoved, jSerialPort);
			} else {
				fprintf(stderr, "DeviceRemoved can not call deviceRemoved!\n");
			}


		}

				
		if (env->ExceptionOccurred()) {
			env->ExceptionDescribe();
		}
		cached_jvm->DetachCurrentThread();

	}

	SetupDiDestroyDeviceInfoList(hDevInfo);
}



BOOL FindDevice(HDEVINFO& hDevInfo, CString& szDevId, SP_DEVINFO_DATA& spDevInfoData)
{
	spDevInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
	for(int i=0; SetupDiEnumDeviceInfo(hDevInfo, i, &spDevInfoData); i++) {
		DWORD nSize=0 ;
		TCHAR buf[MAX_PATH];

		if ( !SetupDiGetDeviceInstanceId(hDevInfo, &spDevInfoData, buf, sizeof(buf), &nSize) ) {
			fprintf(stderr, "SetupDiGetDeviceInstanceId() error");
			//TRACE(CString("SetupDiGetDeviceInstanceId(): ") 
			//	+ _com_error(GetLastError()).ErrorMessage());
			return FALSE;
		} 
		if ( szDevId == buf ) {
			// OK, device found
			return TRUE;
		}
	}
	return FALSE;
}





LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{   
	LRESULT ret = 0;

	switch (message)
	{
		case WM_DEVICECHANGE:
		{
			OnMyDeviceChange(wParam, lParam);
		}
		default:
		{
			ret = ::DefWindowProc(hWnd, message, wParam, lParam);
		}
	}
	return ret;
}




//================================================================================================
//	main
//================================================================================================
JNIEXPORT jint JNICALL Java_com_sue_protocol_SerialPortObserverThread_initHandler(JNIEnv *env, jobject obj) {

	static HDEVNOTIFY hDeviceNotify;
	static const TCHAR szClassName[]  = L"ChangeNotifierPoCoAPI";
	static const DWORD dwWindowStyles = (WS_CHILDWINDOW | WS_CLIPSIBLINGS);


	//printf("Java_com_sue_protocol_SerialPortObserverThread_initHandler");


	// init JNI
	jclass cls = env->GetObjectClass(obj);
	if (cls == NULL) {
		return JNI_ERR;
	}

	obj_USBEventHandler = env->NewWeakGlobalRef(obj);
	if (obj_USBEventHandler == NULL) {
		fprintf(stderr, "Can not create weak reference to USBEventHandler object!\n");
		return JNI_ERR;
	}

	if (mid_deviceRemoved == NULL) {
		mid_deviceRemoved = env->GetMethodID(cls, "deviceRemoved", "(Ljava/lang/String;)V");
		if (mid_deviceRemoved == NULL) {
			fprintf(stderr, "Callback method deviceRemoved not found! Callback will NEVER be called!\n");
			//return; /* method not found, exception already thrown */
		}
	}

	if (mid_deviceAdded == NULL) {
		mid_deviceAdded = env->GetMethodID(cls, "deviceAdded", "(Ljava/lang/String;)V");
		if (mid_deviceAdded == NULL) {
			fprintf(stderr, "Callback method deviceAdded not found! Callback will NEVER be called!\n");
			//return; /* method not found, exception already thrown */
		}
	}

#ifdef CALLTESTFUNCTION
	if (mid_test == NULL) {
		mid_test = env->GetMethodID(cls, "test", "()V");
		if (mid_test == NULL) {
			fprintf(stderr, "Callback method test not found! Callback will NEVER be called!");
			//return; /* method not found, exception already thrown */
		} else {
			env->CallVoidMethod(obj_USBEventHandler, mid_test);
		}
	}
#endif





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
        return JNI_ERR;
    }

    isRegistered = true;

    HWND hwnd=::CreateWindowEx(0,
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


	if ( ! RegisterDeviceInterfaceToHwnd(WceusbshGUID, hwnd, &hDeviceNotify) )
    {
        fprintf(stderr, "RegisterDeviceInterfaceToHwnd");
		return JNI_ERR;
    }

	
	MessagePump(hwnd);

	if ( ! UnregisterDeviceNotification(hDeviceNotify) )
    {
       fprintf(stderr, "UnregisterDeviceNotification");
    }


	return JNI_OK;
}

