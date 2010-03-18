#include <CoreFoundation/CoreFoundation.h>

#include <IOKit/IOKitLib.h>
#include <IOKit/IOMessage.h>
#include <IOKit/IOCFPlugIn.h>
#include <IOKit/usb/IOUSBLib.h>
#import "com_sue_usb_USBEventHandler.h"


// Change these two constants to match your device's idVendor and idProduct.
#define kMyVendorID			0x1F2E
#define kMyProductID		0x000A


typedef struct MyPrivateData {
    io_object_t				notification;
    IOUSBDeviceInterface	**deviceInterface;
    CFStringRef				deviceName;
	CFStringRef				serialPort;
//    UInt32					locationID;
} MyPrivateData;

static IONotificationPortRef	gNotifyPort;
static io_iterator_t			gAddedIter;
static CFRunLoopRef				gRunLoop;


JavaVM *cached_jvm;
jobject obj_USBEventHandler = NULL;
jmethodID mid_deviceRemoved = NULL;
jmethodID mid_deviceAdded = NULL;

//#define CALLTESTFUNCTION
#ifdef CALLTESTFUNCTION
jmethodID mid_test = NULL;
#endif


//================================================================================================
//
//	JNU_GetEnv
//
//	Given a cached JavaVM interface pointer it is trivial to implement a utility function that
//  allows the native code to obtain the JNIEnv  interface pointer for the current thread (¤8.1.4)
//
//================================================================================================
JNIEnv *JNI_GetEnv()
{
	JNIEnv *env;
	(*cached_jvm)->GetEnv(cached_jvm, (void **)&env, JNI_VERSION_1_2);
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
	
	if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_2)) {
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
	
	if ((*jvm)->GetEnv(jvm, (void **)&env, JNI_VERSION_1_2)) {
		return;
	}
	
	(*env)->DeleteWeakGlobalRef(env, obj_USBEventHandler);
}


//================================================================================================
//
//	getTestString
//
//	Create a string for testing.
//
//================================================================================================
#ifdef CALLTESTFUNCTION
jstring getTestString(JNIEnv *env) {
	CFMutableArrayRef names = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
	CFStringRef firstName = CFSTR("first");
	CFStringRef lastName = CFSTR("last");
	CFArrayAppendValue(names, (const void *)firstName);
	CFArrayAppendValue(names, (const void *)lastName);
	
	// merge the first and last names into one CFString
	// An empty string (size 0) are still safely created by the JNI; no check is necessary
	CFStringRef firstAndLastName = CFStringCreateByCombiningStrings(kCFAllocatorDefault, names, CFSTR(";"));
	CFIndex strLen = CFStringGetLength(firstAndLastName);
	UniChar uniStr[strLen];
	CFRange strRange;
	strRange.location = 0;
	strRange.length = strLen;
	CFStringGetCharacters(firstAndLastName, strRange, uniStr);
	
	// return a jstring from the full name's bytes
	jstring javaName = (*env)->NewString(env, (jchar *)uniStr, (jsize)strLen);
	
	CFRelease(names);
	CFRelease(firstAndLastName);
	
	return javaName;
}
#endif


//================================================================================================
//
//	DeviceNotification
//
//	This routine will get called whenever any kIOGeneralInterest notification happens.  We are
//	interested in the kIOMessageServiceIsTerminated message so that's what we look for.  Other
//	messages are defined in IOMessage.h.
//
//================================================================================================
void DeviceNotification(void *refCon, io_service_t service, natural_t messageType, void *messageArgument)
{
	kern_return_t	kr;
    MyPrivateData	*privateDataRef = (MyPrivateData *) refCon;
    
    if (messageType == kIOMessageServiceIsTerminated) {
        //fprintf(stderr, "Device removed.\n");
		
        // Dump our private data to stderr just to see what it looks like.
        //fprintf(stderr, "privateDataRef->deviceName: ");
		//CFShow(privateDataRef->deviceName);
		//fprintf(stderr, "privateDataRef->locationID: 0x%lx.\n\n", privateDataRef->locationID);
		
		
		// inform the Java USBEventHandler
		if (mid_deviceRemoved != NULL && obj_USBEventHandler != NULL) {
			JNIEnv *env = JNI_GetEnv();
			
			CFIndex strLen = CFStringGetLength(privateDataRef->deviceName);
			UniChar uniStr[strLen];
			CFRange strRange;
			strRange.location = 0;
			strRange.length = strLen;
			CFStringGetCharacters(privateDataRef->deviceName, strRange, uniStr);
			jstring jDeviceName = (*env)->NewString(env, (jchar *)uniStr, (jsize)strLen);
			//fprintf(stderr, "DeviceNotification attempts to call Java method!");
			(*env)->CallVoidMethod(env, obj_USBEventHandler, mid_deviceRemoved, jDeviceName);
		} else {
			fprintf(stderr, "DeviceNotification can not call deviceRemoved!");
		}
		
		
        // Free the data we're no longer using now that the device is going away
        CFRelease(privateDataRef->deviceName);
        
        if (privateDataRef->deviceInterface) {
            kr = (*privateDataRef->deviceInterface)->Release(privateDataRef->deviceInterface);
        }
        
        kr = IOObjectRelease(privateDataRef->notification);
        
        free(privateDataRef);
    }
}


//================================================================================================
//
//	DeviceAdded
//
//	This routine is the callback for our IOServiceAddMatchingNotification.  When we get called
//	we will look at all the devices that were added and we will:
//
//	1.  Create some private data to relate to each device (in this case we use the service's name
//	    and the location ID of the device
//	2.  Submit an IOServiceAddInterestNotification of type kIOGeneralInterest for this device,
//	    using the refCon field to store a pointer to our private data.  When we get called with
//	    this interest notification, we can grab the refCon and access our private data.
//
//================================================================================================
void DeviceAdded(void *refCon, io_iterator_t iterator)
{
    kern_return_t		kr;
    io_service_t		usbDevice;
    IOCFPlugInInterface	**plugInInterface = NULL;
    SInt32				score;
    HRESULT 			res;
    
    while ((usbDevice = IOIteratorNext(iterator))) {
        io_name_t		deviceName;
        CFStringRef		deviceNameAsCFString;	
        MyPrivateData	*privateDataRef = NULL;
        //UInt32			locationID;
		//UInt8			snsi;
		
        
        //printf("Device added.\n");
		
		
        // Add some app-specific information about this device.
        // Create a buffer to hold the data.
        privateDataRef = malloc(sizeof(MyPrivateData));
        bzero(privateDataRef, sizeof(MyPrivateData));
        
		
        // Get the USB device's name.
        kr = IORegistryEntryGetName(usbDevice, deviceName);
		if (KERN_SUCCESS != kr) {
            deviceName[0] = '\0';
        }
        
        deviceNameAsCFString = CFStringCreateWithCString(kCFAllocatorDefault, deviceName, kCFStringEncodingASCII);
		
        // Dump our data to stderr just to see what it looks like.
        //fprintf(stderr, "deviceName: ");
        //CFShow(deviceNameAsCFString);
        
        // Save the device's name to our private data.        
        privateDataRef->deviceName = deviceNameAsCFString;
		
        // Now, get the locationID of this device. In order to do this, we need to create an IOUSBDeviceInterface 
        // for our device. This will create the necessary connections between our userland application and the 
        // kernel object for the USB Device.
        kr = IOCreatePlugInInterfaceForService(usbDevice, kIOUSBDeviceUserClientTypeID, kIOCFPlugInInterfaceID,
                                               &plugInInterface, &score);
		
        if ((kIOReturnSuccess != kr) || !plugInInterface) {
            fprintf(stderr, "IOCreatePlugInInterfaceForService returned 0x%08x.\n", kr);
            continue;
        }
		
        // Use the plugin interface to retrieve the device interface.
        res = (*plugInInterface)->QueryInterface(plugInInterface, CFUUIDGetUUIDBytes(kIOUSBDeviceInterfaceID),
                                                 (LPVOID*) &privateDataRef->deviceInterface);
        
        // Now done with the plugin interface.
        (*plugInInterface)->Release(plugInInterface);
		
        if (res || privateDataRef->deviceInterface == NULL) {
            fprintf(stderr, "QueryInterface returned %d.\n", (int) res);
            continue;
        }


        // Now that we have the IOUSBDeviceInterface, we can call the routines in IOUSBLib.h.
        // In this case, fetch the locationID. The locationID uniquely identifies the device
        // and will remain the same, even across reboots, so long as the bus topology doesn't change.
/*        
        kr = (*privateDataRef->deviceInterface)->GetLocationID(privateDataRef->deviceInterface, &locationID);
        if (KERN_SUCCESS != kr) {
            fprintf(stderr, "GetLocationID returned 0x%08x.\n", kr);
            continue;
        } else {
            fprintf(stderr, "Location ID: 0x%lx\n\n", locationID);
        }
		
        privateDataRef->locationID = locationID;
*/		
		
/*************************************************************************************************/
		
		// IOReturn (*GetLocationID)(void *self, UInt32 *locationID);
		// IOReturn (*USBGetSerialNumberStringIndex)(void *self, UInt8 *snsi);
		
		CFTypeRef serialNumberRef;
		serialNumberRef = IORegistryEntryCreateCFProperty(usbDevice, CFSTR("USB Serial Number"), kCFAllocatorDefault, 0);
		
		
		// inform the Java USBEventHandler
		if (mid_deviceAdded != NULL && obj_USBEventHandler != NULL) {
			JNIEnv *env = JNI_GetEnv();
			
/*			
			CFIndex strLen = CFStringGetLength(serialNumberRef);
			UniChar uniStr[strLen];
			CFRange strRange;
			strRange.location = 0;
			strRange.length = strLen;
			CFStringGetCharacters(serialNumberRef, strRange, uniStr);
			jstring jDeviceName = (*env)->NewString(env, (jchar *)uniStr, (jsize)strLen);
			//fprintf(stderr, "DeviceAdded attempts to call Java method!");
*/
			CFMutableArrayRef usbModemPathArray = CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks);
			CFArrayAppendValue(usbModemPathArray, (const void *) CFSTR("/dev/tty.usbmodem"));
			CFArrayAppendValue(usbModemPathArray, (const void *) serialNumberRef);
			
			CFStringRef usbModemPath = CFStringCreateByCombiningStrings(kCFAllocatorDefault, usbModemPathArray, CFSTR(""));
			CFIndex strLen = CFStringGetLength(usbModemPath);
			UniChar uniStr[strLen];
			CFRange strRange;
			strRange.location = 0;
			strRange.length = strLen;
			CFStringGetCharacters(usbModemPath, strRange, uniStr);
			
			CFRelease(usbModemPathArray);
			CFRelease(usbModemPath);
			
			jstring jusbModemPath = (*env)->NewString(env, (jchar *)uniStr, (jsize)strLen);
			
			(*env)->CallVoidMethod(env, obj_USBEventHandler, mid_deviceAdded, jusbModemPath);
		} else {
			fprintf(stderr, "DeviceNotification can not call deviceAdded!");
		}
		
		
		CFRelease(serialNumberRef);


/*************************************************************************************************/
		
		
        // Register for an interest notification of this device being removed. Use a reference to our
        // private data as the refCon which will be passed to the notification callback.
        kr = IOServiceAddInterestNotification(gNotifyPort,						// notifyPort
											  usbDevice,						// service
											  kIOGeneralInterest,				// interestType
											  DeviceNotification,				// callback
											  privateDataRef,					// refCon
											  &(privateDataRef->notification)	// notification
											  );
		
        if (KERN_SUCCESS != kr) {
            printf("IOServiceAddInterestNotification returned 0x%08x.\n", kr);
        }
        
        // Done with this USB device; release the reference added by IOIteratorNext
        kr = IOObjectRelease(usbDevice);
    }
}


//================================================================================================
//
//	SignalHandler
//
//	This routine will get called when we interrupt the program (usually with a Ctrl-C from the
//	command line).
//
//================================================================================================
void SignalHandler(int sigraised)
{
    fprintf(stderr, "\nInterrupted.\n");
    exit(0);
}




//================================================================================================
//	main
//================================================================================================
JNIEXPORT jint JNICALL Java_com_sue_usb_USBEventHandler_initHandler(JNIEnv *env, jobject obj, jlong usbVendor, jlong usbProduct) {
	
	CFMutableDictionaryRef 	matchingDict;
    CFRunLoopSourceRef		runLoopSource;
    CFNumberRef				numberRef;
    kern_return_t			kr;
    sig_t					oldHandler;
	
	
	
	
	
	jclass cls = (*env)->GetObjectClass(env, obj);
	if (cls == NULL) {
		return JNI_ERR;
	}
	
	obj_USBEventHandler = (*env)->NewWeakGlobalRef(env, obj);
	if (obj_USBEventHandler == NULL) {
		fprintf(stderr, "Can not create weak reference to USBEventHandler object!");
		return JNI_ERR;
	}
	
	if (mid_deviceRemoved == NULL) {
		mid_deviceRemoved = (*env)->GetMethodID(env, cls, "deviceRemoved", "(Ljava/lang/String;)V");
		if (mid_deviceRemoved == NULL) {
			fprintf(stderr, "Callback method deviceRemoved not found! Callback will NEVER be called!");
			//return; /* method not found, exception already thrown */
		}
	}
	
	if (mid_deviceAdded == NULL) {
		mid_deviceAdded = (*env)->GetMethodID(env, cls, "deviceAdded", "(Ljava/lang/String;)V");
		if (mid_deviceAdded == NULL) {
			fprintf(stderr, "Callback method deviceAdded not found! Callback will NEVER be called!");
			//return; /* method not found, exception already thrown */
		}
	}
	
#ifdef CALLTESTFUNCTION
	if (mid_test == NULL) {
		mid_test = (*env)->GetMethodID(env, cls, "test", "()V");
		if (mid_test == NULL) {
			fprintf(stderr, "Callback method test not found! Callback will NEVER be called!");
			//return; /* method not found, exception already thrown */
		} else {
			(*env)->CallVoidMethod(env, obj_USBEventHandler, mid_test);
		}
	}
#endif
	
	
	
	
	
	
	
	
    // Set up a signal handler so we can clean up when we're interrupted from the command line
    // Otherwise we stay in our run loop forever.
    oldHandler = signal(SIGINT, SignalHandler);
    if (oldHandler == SIG_ERR) {
        fprintf(stderr, "Could not establish new signal handler.");
	}
	
    fprintf(stderr, "Looking for devices matching vendor ID=%ld and product ID=%ld.\n", usbVendor, usbProduct);
	
    // Set up the matching criteria for the devices we're interested in. The matching criteria needs to follow
    // the same rules as kernel drivers: mainly it needs to follow the USB Common Class Specification, pp. 6-7.
    // See also Technical Q&A QA1076 "Tips on USB driver matching on Mac OS X" 
	// <http://developer.apple.com/qa/qa2001/qa1076.html>.
    // One exception is that you can use the matching dictionary "as is", i.e. without adding any matching 
    // criteria to it and it will match every IOUSBDevice in the system. IOServiceAddMatchingNotification will 
    // consume this dictionary reference, so there is no need to release it later on.
    
    matchingDict = IOServiceMatching(kIOUSBDeviceClassName);	// Interested in instances of class
	// IOUSBDevice and its subclasses
    if (matchingDict == NULL) {
        fprintf(stderr, "IOServiceMatching returned NULL.\n");
        return -1;
    }
    
    // We are interested in all USB devices (as opposed to USB interfaces).  The Common Class Specification
    // tells us that we need to specify the idVendor, idProduct, and bcdDevice fields, or, if we're not interested
    // in particular bcdDevices, just the idVendor and idProduct.  Note that if we were trying to match an 
    // IOUSBInterface, we would need to set more values in the matching dictionary (e.g. idVendor, idProduct, 
    // bInterfaceNumber and bConfigurationValue.
    
    // Create a CFNumber for the idVendor and set the value in the dictionary
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbVendor);
    CFDictionarySetValue(matchingDict, 
                         CFSTR(kUSBVendorID), 
                         numberRef);
    CFRelease(numberRef);
    
    // Create a CFNumber for the idProduct and set the value in the dictionary
    numberRef = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt32Type, &usbProduct);
    CFDictionarySetValue(matchingDict, 
                         CFSTR(kUSBProductID), 
                         numberRef);
    CFRelease(numberRef);
    numberRef = NULL;
	
    // Create a notification port and add its run loop event source to our run loop
    // This is how async notifications get set up.
    
    gNotifyPort = IONotificationPortCreate(kIOMasterPortDefault);
    runLoopSource = IONotificationPortGetRunLoopSource(gNotifyPort);
    
    gRunLoop = CFRunLoopGetCurrent();
    CFRunLoopAddSource(gRunLoop, runLoopSource, kCFRunLoopDefaultMode);
    
    // Now set up a notification to be called when a device is first matched by I/O Kit.
    kr = IOServiceAddMatchingNotification(gNotifyPort,					// notifyPort
                                          kIOFirstMatchNotification,	// notificationType
                                          matchingDict,					// matching
                                          DeviceAdded,					// callback
                                          NULL,							// refCon
                                          &gAddedIter					// notification
                                          );		
	
    // Iterate once to get already-present devices and arm the notification    
    DeviceAdded(NULL, gAddedIter);	
	
    // Start the run loop. Now we'll receive notifications.
    fprintf(stderr, "Starting run loop.\n\n");
    CFRunLoopRun();
	
    // We should never get here
    fprintf(stderr, "Unexpectedly back from CFRunLoopRun()!\n");
    return 0;
}

