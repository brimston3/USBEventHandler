package com.sue.usb;

import java.io.File;
import java.util.logging.Logger;


public class USBEventHandler {

	private static USBEventHandler INSTANCE = null;
	private USBEventHandlerThread usbEventHandlerThread = null;
	
    static {
        System.loadLibrary("USBEventHandler");
    }

    
    private native int initHandler(long vendorID, long productID);

	
	private USBEventHandler() {
		if(usbEventHandlerThread == null) {
			usbEventHandlerThread = new USBEventHandlerThread();
		}
	}
	
	
	public static USBEventHandler getInstance() {
		if(INSTANCE == null) {
			INSTANCE = new USBEventHandler();
		}
		return INSTANCE;	
	}
	
	
	public void startUSBEventHandlerThread() {
		if(usbEventHandlerThread != null) {
			System.out.println("starting USBEventHandlerThread");
			usbEventHandlerThread.start();
		}
	}
	
	
	public void stopUSBEventHandlerThread() {
		if(usbEventHandlerThread != null) {
			System.out.println("stopping USBEventHandlerThread");
			usbEventHandlerThread.interrupt();
		}
	}
	
	
	private void test() {
		System.out.println("test callback was just called !!!!!!!!");
	}
	
	
	/**
	 * Callback is called from native code!
	 */
	private void deviceRemoved(String deviceDescription) {
		System.out.println("deviceRemoved: " + deviceDescription);
	}
	
	
	/**
	 * Callback is called from native code!
	 */
	private void deviceAdded(String deviceDescription) {
		System.out.println("deviceAdded: " + deviceDescription);
	}
	
	
	private class USBEventHandlerThread extends Thread {
		
		public USBEventHandlerThread() {
			super("USBEventHandlerThread");
		}

		@Override
		public void run() {
			try{
				// Device VendorID/ProductID:   0x090C/0x1000   (Silicon Motion, Inc. - Taiwan)
				initHandler(0x090C, 0x1000);
			} catch(Exception e) {
				System.out.println(e.getMessage());
			}
		}
		
	}
	
	
	public static void main (String[] args) {
		USBEventHandler usbeventhandler = USBEventHandler.getInstance();
		usbeventhandler.startUSBEventHandlerThread();
	}
	
}
