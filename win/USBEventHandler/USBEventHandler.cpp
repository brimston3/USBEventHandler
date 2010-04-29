// USBEventHandler.cpp : Definiert die exportierten Funktionen für die DLL-Anwendung.
//

#include "stdafx.h"
#include "com_sue_protocol_SerialPortObserverThread.h"
#include <windows.h>
#include <dbt.h>
#include <tchar.h>


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

		//static PCGuid					kPCORE_GUID_CDC;
		static PCGuid					kPCORE_GUID_CUBE;
	    //static PCGuid					kPCORE_GUID_PCI;
	    //static PCGuid					kPCORE_GUID_FW;
	    //static PCGuid					kPCORE_GUID_PNP;
    };

	// usbser.inf
	// ClassGuid={4D36E978-E325-11CE-BFC1-08002BE10318}           
	GOO_DEFINE_GUID (kPCORE_GUID_CUBE, 0x4D36E978, 0xE325, 0x11CE, 0xBF, 0xC1, 0x08, 0x00, 0x2B, 0xE1, 0x03, 0x18);
    //GOO_DEFINE_GUID (kPCORE_GUID_CDC, 0x25dbce51, 0x6c8f, 0x4a72, 0x8a, 0x6d, 0xb5, 0x4c, 0x2b, 0x4f, 0xc8, 0x35);
    
	//GOO_DEFINE_GUID (kPCORE_GUID_FW,  0xa3e9921b, 0xd498, 0x48db, 0x93, 0x30, 0x4f, 0x79, 0xab, 0x84, 0x34, 0x92);
    //GOO_DEFINE_GUID (kPCORE_GUID_PNP, 0x4e725747, 0x77ce, 0x46fb, 0x90, 0xa9, 0xd1, 0x95, 0xb3, 0x06, 0x80, 0x58);
}





JavaVM *cached_jvm;
jobject obj_USBEventHandler = NULL;
jmethodID mid_deviceRemoved = NULL;
jmethodID mid_deviceAdded = NULL;


#define CALLTESTFUNCTION
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




LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{   
	LRESULT ret = 0;
	jboolean jvm_was_attached;


	switch (message)
	{
		case WM_DEVICECHANGE:
		{
			switch (wParam)
			{
				case DBT_DEVICEARRIVAL:
				{
					fprintf(stderr, "DBT_DEVICEARRIVAL\n");
					PDEV_BROADCAST_DEVICEINTERFACE pBDI = (PDEV_BROADCAST_DEVICEINTERFACE) lParam;
					if (pBDI->dbcc_classguid != PCGuids::kPCORE_GUID_CUBE)
					{
						fprintf(stderr, "Class GUID not matching!\n");
						break;
					}


					if (mid_deviceAdded != NULL && obj_USBEventHandler != NULL) {

						//CFShow(bsdPathAsCFString);
						JNIEnv *env = JNI_GetEnv(&jvm_was_attached);
						if (jvm_was_attached == JNI_FALSE) {
							fprintf(stderr, "Could not attach to JVM!\n");
							break;
						}

						/*
						CFIndex strLen = CFStringGetLength(bsdPathAsCFString);
						UniChar uniStr[strLen];
						CFRange strRange;
						strRange.location = 0;
						strRange.length = strLen;
						CFStringGetCharacters(bsdPathAsCFString, strRange, uniStr);
						*/

						//TCHAR comPortChar[]  = TEXT("COM4");
						jstring jBSDPath = env->NewString((jchar *)"COM4", (jsize)4);
						//fprintf(stderr, "DeviceAdded attempts to call Java method!\n");
						env->CallVoidMethod(obj_USBEventHandler, mid_deviceAdded, jBSDPath);
						
						if (env->ExceptionOccurred()) {
							env->ExceptionDescribe();
						}
						cached_jvm->DetachCurrentThread();
						
					} else {
						fprintf(stderr, "DeviceAdded can not call deviceAdded!\n");
					}

/*						LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
					ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

					if (pBDI->dbcc_classguid == PCGuids::kPCORE_GUID_FW)
					{
					}
					pNotifier->OnSystemChange(NULL);
*/						break;
				}


				case DBT_DEVICEREMOVECOMPLETE:
				{
					//fprintf(stderr, "DBT_DEVICEREMOVECOMPLETE\n");
					PDEV_BROADCAST_DEVICEINTERFACE pBDI = (PDEV_BROADCAST_DEVICEINTERFACE) lParam;
					if (pBDI->dbcc_classguid != PCGuids::kPCORE_GUID_CUBE)
					{
						fprintf(stderr, "Class GUID not matching!\n");
						break;
					}


					if (mid_deviceRemoved != NULL && obj_USBEventHandler != NULL) {

						JNIEnv *env = JNI_GetEnv(&jvm_was_attached);
						if (jvm_was_attached == JNI_FALSE) {
							fprintf(stderr, "Could not attach to JVM!\n");
							break;
						}

						/* 
						CFIndex strLen = CFStringGetLength(privateDataRef->bsdPath);
						UniChar uniStr[strLen];
						CFRange strRange;
						strRange.location = 0;
						strRange.length = strLen;
						CFStringGetCharacters(privateDataRef->bsdPath, strRange, uniStr);
						*/

						jstring jDeviceName = env->NewString((jchar *)"COM4", (jsize)4);
			//			fprintf(stderr, "DeviceRemoved attempts to call Java method!");
						//fprintf(stderr, "DeviceRemoved env: %p, obj_USBEventHandler: %p, mid_deviceRemoved: %p!\n", env, &obj_USBEventHandler, &mid_deviceRemoved);
						env->CallVoidMethod(obj_USBEventHandler, mid_deviceRemoved, jDeviceName);
						
						if (env->ExceptionOccurred()) {
							env->ExceptionDescribe();
						}
						cached_jvm->DetachCurrentThread();

					} else {
						fprintf(stderr, "DeviceRemoved can not call deviceRemoved!\n");
					}

/*						LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
					ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

					pNotifier->OnSystemChange(NULL);
*/						break;
				}
/*					case DBT_CUSTOMEVENT:
				{
					PDEV_BROADCAST_HANDLE	pBH = (PDEV_BROADCAST_HANDLE) lParam;
					LONG_PTR pObject = ::GetWindowLongPtr(hWnd, GWLP_USERDATA);
					ChangeNotifier*	pNotifier = reinterpret_cast<ChangeNotifier*>(pObject);

					pNotifier->OnSystemChange((TNativeHandle) pBH->dbch_handle);
					break;
				}
*/				}
			break;
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
        // TODO handle exception
//		MessageBox (NULL, TEXT ("RegisterClassEx failed"), TEXT("PowerCore"), MB_ICONERROR);
        return -1;
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

    // store "this"-pointer as user data in the window handle
//    ::SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) this);

	// we want global system events
	DEV_BROADCAST_DEVICEINTERFACE 	db;

	memset (&db, 0, sizeof(db));
	db.dbcc_size = sizeof(db);
	db.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
	db.dbcc_classguid = PCGuids::kPCORE_GUID_CUBE;

	HDEVNOTIFY hdev = ::RegisterDeviceNotification (hwnd, &db, DEVICE_NOTIFY_WINDOW_HANDLE);

//	bool idle=true;
/*
	MSG msg;
	while(1){
		::PeekMessage(&msg,0,0,0,PM_REMOVE));
		::TranslateMessage(&msg);
		::DispatchMessage(&msg);
//		idle=false;
	}
//	if(idle)
//		::Sleep(20);
*/

//	s_PCoreChangeMessage = RegisterWindowMessage(szPCoreWinMessageName);

	return 0;
}

