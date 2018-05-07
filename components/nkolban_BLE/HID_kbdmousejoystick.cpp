/*
* Licensed to the Apache Software Foundation (ASF) under one
* or more contributor license agreements.  See the NOTICE file
* distributed with this work for additional information
* regarding copyright ownership.  The ASF licenses this file
* to you under the Apache License, Version 2.0 (the
* "License"); you may not use this file except in compliance
* with the License.  You may obtain a copy of the License at
* 
*   http://www.apache.org/licenses/LICENSE-2.0
* 
* Unless required by applicable law or agreed to in writing,
* software distributed under the License is distributed on an
* "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
* KIND, either express or implied.  See the License for the
* specific language governing permissions and limitations
* under the License.
* 
* Copyright Neil Kolban
 */

/** @file
 * @brief This file is the implementation for Neil Kolbans CPP utils
 * 
 * It initializes 3 queues for sending mouse, keyboard and joystick data
 * from within the C-side. C++ classes are instantiated here.
 * If you want to have a different BLE HID device, you need to adapt this file.
 * 
 * @note Thank you very much Neil Kolban for this impressive work!
*/

#include "BLEDevice.h"
#include "BLEServer.h"
#include "BLEUtils.h"
#include "BLE2902.h"
#include "BLEHIDDevice.h"
#include "SampleKeyboardTypes.h"
#include <esp_log.h>
#include <string>
#include <Task.h>
#include "HID_kbdmousejoystick.h"

#include "sdkconfig.h"

static char LOG_TAG[] = "HAL_BLE";

QueueHandle_t joystick_q;
QueueHandle_t keyboard_q;
QueueHandle_t mouse_q;

//configure the reports & tasks:

#define USE_MOUSE
///note: if you use joystick, you MUST use mouse (otherwise: compile error)
//#define USE_JOYSTICK

//test defines for valid config
#if defined(USE_JOYSTICK) && !defined(USE_MOUSE)
  #error "Please adjust the reportMap, if you want to use joystick but no mouse"
#endif

static BLEHIDDevice* hid;
BLEServer *pServer;
BLECharacteristic* inputKbd;
#ifdef USE_MOUSE
BLECharacteristic* inputMouse;
#endif
#ifdef USE_JOYSTICK
BLECharacteristic* inputJoystick;
#endif
BLECharacteristic* outputKbd;

class kbdOutputCB : public BLECharacteristicCallbacks {
	void onWrite(BLECharacteristic* me){
		uint8_t* value = (uint8_t*)(me->getValue().c_str());
		ESP_LOGI(LOG_TAG, "special keys: %d", *value);
	}
};

class KeyboardTask : public Task {
	void run(void*){
    vTaskDelay(2000/portTICK_PERIOD_MS);
    	const char* hello = "Hello world from esp32 hid keyboard!!!";
		while(*hello){
			KEYMAP map = keymap[(uint8_t)*hello];
			uint8_t a[] = {map.modifier, 0x0, map.usage, 0x0,0x0,0x0,0x0,0x0};
      ESP_LOGI(LOG_TAG,"Kbd: %c, keycode: 0x%2X, modifier: 0x%2X",hello[0],map.usage,map.modifier);
      inputKbd->setValue(a,sizeof(a));
      //test->executeCreate(hid->hidService());
      inputKbd->notify();
      uint8_t b[] = {0x0, 0x0, 0x00, 0x0,0x0,0x0,0x0,0x0};
			inputKbd->setValue(b,sizeof(b));
			inputKbd->notify();

			hello++;
      
      vTaskDelay(2);
		}
			uint8_t v[] = {0x00, 0x0, 0x0, 0x0,0x0,0x0,0x0,0x0};
			inputKbd->setValue(v, sizeof(v));
			inputKbd->notify();
		vTaskDelete(NULL);
	}
};
KeyboardTask *kbd; //instance for this task

#ifdef USE_MOUSE
class MouseTask : public Task {
	void run(void*){
    uint8_t step = 0;
    //<report id = 2>, <button>, <x>, <y>
    uint8_t a[] = {0x00,(uint8_t) -0, (uint8_t) -0,(uint8_t)-0};
    vTaskDelay(10000/portTICK_PERIOD_MS);
		while(1){
      //disabled until at least kbd works...
      //vTaskDelay(500000/portTICK_PERIOD_MS);
      switch(step) {
        case 0:
          a[0] = 0;
          a[1] = 50;
          step++;
          break;
        case 1:
          a[1] = -50;
          step++;
          break;
        case 2:
          a[2] = 50;
          step++;
          break;
        case 3:
          step++;
          a[2] = -50;
          break;
        default:
          a[0] = (1<<0);
          a[1] = 0;
          a[2] = 0;
          step = 0;
          break;
      }
      ESP_LOGI(LOG_TAG,"Sending mouse report: ");
      ESP_LOG_BUFFER_HEXDUMP(LOG_TAG,a,sizeof(a),ESP_LOG_INFO);
			inputMouse->setValue(a,sizeof(a));
			inputMouse->notify();
      vTaskDelay(2000/portTICK_PERIOD_MS);
		}
    //finally: send an empty report
    uint8_t v[] = {0x0,(uint8_t) -0, (uint8_t) -0};
    inputMouse->setValue(v, sizeof(v));
		inputMouse->notify();
		vTaskDelete(NULL);
	}
};
MouseTask *mouse; //instance for this task
#endif
#ifdef USE_JOYSTICK
class JoystickTask : public Task {
	void run(void*){
    vTaskDelay(1000/portTICK_PERIOD_MS);
		while(1){
        vTaskDelay(10000/portTICK_PERIOD_MS);
		}
			/*uint8_t v[] = {0x0, 0x0, 0x0, 0x0,0x0,0x0,0x0,0x0};
			inputJoystick->setValue(v, sizeof(v));
			inputJoystick->notify();*/
		vTaskDelete(NULL);
	}
};
JoystickTask *joystick; //instance for this task
#endif

class CBs: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer){
    BLE2902* desc;
    
    //enable notifications for input service, suggested by chegewara to support iOS/Win10
    //https://github.com/asterics/esp32_mouse_keyboard/issues/4#issuecomment-386558158
    desc = (BLE2902*) inputKbd->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(true);
   	kbd->start();
    #ifdef USE_MOUSE
    desc = (BLE2902*) inputMouse->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(true);
   	mouse->start();
    #endif
    #ifdef USE_JOYSTICK
    desc = (BLE2902*) inputJoystick->getDescriptorByUUID(BLEUUID((uint16_t)0x2902));
    desc->setNotifications(true);
   	joystick->start();
    #endif
  }

  void onDisconnect(BLEServer* pServer){
    //kbd->stop();
    #ifdef USE_MOUSE
   	//mouse->stop();
    #endif
    #ifdef USE_JOYSTICK
   	//joystick->stop();
    #endif
  }
};

uint32_t passKey = 0;
class CB_Security: public BLESecurityCallbacks {
  /*~CB_Security() {
    ESP_LOGE(LOG_TAG,"Sec dtor");
  }*/
  
  uint32_t onPassKeyRequest(){
    ESP_LOGE(LOG_TAG, "The passkey request %d", passKey);
    vTaskDelay(25000);
    return passKey;
  }
  
  void onPassKeyNotify(uint32_t pass_key){
    ESP_LOGE(LOG_TAG, "The passkey Notify number:%d", pass_key);
    passKey = pass_key;
  }
  
  bool onSecurityRequest(){
    return true;
  }

  /*CB_Security() {
    ESP_LOGE(LOG_TAG,"Sec ctor");
  }*/
  
  void onAuthenticationComplete(esp_ble_auth_cmpl_t auth_cmpl){
    if(auth_cmpl.success){
      ESP_LOGI(LOG_TAG, "remote BD_ADDR:");
      esp_log_buffer_hex(LOG_TAG, auth_cmpl.bd_addr, sizeof(auth_cmpl.bd_addr));
      ESP_LOGI(LOG_TAG, "address type = %d", auth_cmpl.addr_type);
    }
      ESP_LOGI(LOG_TAG, "pair status = %s", auth_cmpl.success ? "success" : "fail");
  }
  
  bool onConfirmPIN(uint32_t pin)
  {
    ESP_LOGE(LOG_TAG, "Confirm pin: %d", pin);
    return true;
  }
  
};


class BLE_HOG: public Task {
	void run(void *data) {
		ESP_LOGD(LOG_TAG, "Initialising BLE HID device.");

		kbd = new KeyboardTask();
		kbd->setStackSize(8096);
    #ifdef USE_MOUSE
		mouse = new MouseTask();
		mouse->setStackSize(8096);
    #endif
    #ifdef USE_JOYSTICK
		joystick = new JoystickTask();
		joystick->setStackSize(8096);
    #endif
		BLEDevice::init("FABI/FLipMouse");
		pServer = BLEDevice::createServer();
		pServer->setCallbacks(new CBs());
		//BLEDevice::setEncryptionLevel(ESP_BLE_SEC_ENCRYPT_NO_MITM);
		BLEDevice::setSecurityCallbacks(new CB_Security());

		/*
		 * Instantiate hid device
		 */
		hid = new BLEHIDDevice(pServer);
    
    /*
     * Initialize reports for keyboard
     */
    inputKbd = hid->inputReport(1);
    outputKbd = hid->outputReport(1);
    outputKbd->setCallbacks(new kbdOutputCB());
    
    /*
     * Initialize reports for mouse
     */
    #ifdef USE_MOUSE
    inputMouse = hid->inputReport(2);
    #endif
    /*
     * Initialize reports for joystick
     */
    #ifdef USE_JOYSTICK
    inputJoystick = hid->inputReport(3);
    #endif
		/*
		 * Set manufacturer name
		 * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.manufacturer_name_string.xml
		 */
		std::string name = "AsTeRICS Foundation";
		hid->manufacturer()->setValue(name);

		/*
		 * Set pnp parameters
		 * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.pnp_id.xml
		 */
    //hid->pnp(0x01,0xE502,0xA111,0x0210); //BT SIG assigned VID
    hid->pnp(0x02,0xE502,0xA111,0x0210); //USB assigned VID

		/*
		 * Set hid informations
		 * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.hid_information.xml
		 */
		//const uint8_t val1[] = {0x01,0x11,0x00,0x03};
		//hid->hidInfo()->setValue((uint8_t*)val1, 4);
    hid->hidInfo(0x00,0x01);

		/*
		 * Keyboard + mouse + joystick
		 */
		const uint8_t reportMap[] = {
      /*++++ Keyboard - report id 1 ++++*/
			USAGE_PAGE(1),      0x01,       // Generic Desktop Ctrls
			USAGE(1),           0x06,       // Keyboard
			COLLECTION(1),      0x01,       // Application
        REPORT_ID(1),       0x01,
        
        #if 1
        //report equal to usb_bridge
        REPORT_SIZE(1),     0x01,
        REPORT_COUNT(1),    0x08,
        USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
        USAGE_MINIMUM(1),   0xE0,
        USAGE_MAXIMUM(1),   0xE7,
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 0x01,
        INPUT(1),           0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
        REPORT_COUNT(1),    0x01,
        REPORT_SIZE(1),     0x08,
        INPUT(1),           0x03,
        REPORT_COUNT(1),    0x05,
        REPORT_SIZE(1),     0x01,
        USAGE_PAGE(1),      0x08,       //   LEDs
        USAGE_MINIMUM(1),   0x01,       //   Num Lock
        USAGE_MAXIMUM(1),   0x05,       //   Kana
        OUTPUT(1),          0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
        REPORT_COUNT(1),    0x01,       //   3 bits (Padding)
        REPORT_SIZE(1),     0x03,
        OUTPUT(1),          0x03,
        REPORT_COUNT(1),    0x06,       //   6 bytes (Keys)
        REPORT_SIZE(1),     0x08,
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 104,       //   104 keys
        USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
        USAGE_MINIMUM(1),   0x00,       //   Num Lock
        USAGE_MAXIMUM(1),   104,       //   Kana
        INPUT(1),           0x00,
        #endif
        
        
        //report by nkolban / chegewara
        #if 0
        REPORT_COUNT(1),    0x08,
        REPORT_SIZE(1),     0x01,
        USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
        USAGE_MINIMUM(1),   0xE0,
        USAGE_MAXIMUM(1),   0xE7,
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 0x01,
        REPORT_SIZE(1),     0x01,       //   1 byte (Modifier)
        REPORT_COUNT(1),    0x08,
        INPUT(1),           0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position
        REPORT_COUNT(1),    0x01,       //   1 byte (Reserved)
        REPORT_SIZE(1),     0x08,
        INPUT(1),           0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
        REPORT_COUNT(1),    0x05,       //   5 bits (Num lock, Caps lock, Scroll lock, Compose, Kana)
        REPORT_SIZE(1),     0x01,
        USAGE_PAGE(1),      0x08,       //   LEDs
        USAGE_MINIMUM(1),   0x01,       //   Num Lock
        USAGE_MAXIMUM(1),   0x05,       //   Kana
        OUTPUT(1),          0x02,       //   Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
        REPORT_COUNT(1),    0x01,       //   3 bits (Padding)
        REPORT_SIZE(1),     0x03,
        OUTPUT(1),          0x01,       //   Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile
        REPORT_COUNT(1),    0x06,       //   6 bytes (Keys)
        REPORT_SIZE(1),     0x08,
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 0x65,       //   101 keys
        USAGE_PAGE(1),      0x07,       //   Kbrd/Keypad
        USAGE_MINIMUM(1),   0x00,
        USAGE_MAXIMUM(1),   0x65,
        INPUT(1),           0x00,       //   Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position
        #endif
        
        
			#ifndef USE_MOUSE
			END_COLLECTION(0)
      #else
      END_COLLECTION(0),
      /*++++ Mouse - report id 2 ++++*/
      USAGE_PAGE(1), 			0x01,
			USAGE(1), 				  0x02,
      COLLECTION(1),			0x01,
        REPORT_ID(1),       0x02,
        //REPORT_COUNT(1),    0x08,
        //REPORT_SIZE(1),     0x01,
        USAGE(1),				    0x01,
        COLLECTION(1),			0x00,
          USAGE_PAGE(1),			0x09,
          USAGE_MINIMUM(1),		0x1,
          //USAGE_MAXIMUM(1),		0x3,
          USAGE_MAXIMUM(1),		0x8,
          LOGICAL_MINIMUM(1),	0x0,
          LOGICAL_MAXIMUM(1),	0x1,
          REPORT_COUNT(1),		0x8,
          REPORT_SIZE(1),	  	0x1,
          INPUT(1), 				  0x2,		// (Data, Variable, Absolute), ;8 button bits
          //REPORT_COUNT(1),		0x1,
          //REPORT_SIZE(1),		  0x5,
          //INPUT(1), 				  0x1,		//(Constant), ;5 bit padding
          USAGE_PAGE(1), 		  0x1,		//(Generic Desktop),
          USAGE(1),				    0x30,   //X
          USAGE(1),				    0x31,   //Y
          USAGE(1),				    0x38,   //wheel
          LOGICAL_MINIMUM(1),	0x81,
          LOGICAL_MAXIMUM(1),	0x7f,
          REPORT_SIZE(1),	  	0x8,
          REPORT_COUNT(1),		0x03,   //3 bytes: X/Y/wheel
          INPUT(1), 				  0x6,		//(Data, Variable, Relative), ;3 position bytes (X & Y & wheel)
        END_COLLECTION(0),
      #endif
      #if !defined(USE_JOYSTICK) && defined(USE_MOUSE)
			END_COLLECTION(0)
      #endif
      
      #ifdef USE_JOYSTICK
      END_COLLECTION(0),
      /*++++ Joystick - report id 3 ++++*/
      USAGE_PAGE(1), 			0x01,
			USAGE(1), 				  0x04,
      COLLECTION(1),			0x01,
        REPORT_ID(1),       0x03,
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 0x01,
        REPORT_COUNT(1),		0x20, /* 32 */
        REPORT_SIZE(1),	  	0x01,
        USAGE_PAGE(1),      0x09,
        USAGE_MINIMUM(1),   0x01,
        USAGE_MAXIMUM(1),   0x20, /* 32 */
        INPUT(1),           0x02, // variable | absolute
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(1), 0x07,
        PHYSICAL_MINIMUM(1),0x01,
        PHYSICAL_MAXIMUM(2),(315 & 0xFF), ((315>>8) & 0xFF),
        REPORT_SIZE(1),	  	0x04,
        REPORT_COUNT(1),	  0x01,
        UNIT(1),            20,
        USAGE_PAGE(1), 			0x01,
        USAGE(1), 				  0x39,  //hat switch
        INPUT(1),           0x42, //variable | absolute | null state
        USAGE_PAGE(1), 			0x01,
        USAGE(1), 				  0x01,  //generic pointer
        COLLECTION(1),      0x00,
          LOGICAL_MINIMUM(1), 0x00,
          LOGICAL_MAXIMUM(4), (1023 & 0xFF), ((1023>>8) & 0xFF), ((1023>>16) & 0xFF), ((1023>>24) & 0xFF),
          REPORT_COUNT(1),		0x04,
          REPORT_SIZE(1),	  	0x0A, /* 10 */
          USAGE(1), 				  0x30,  // X axis
          USAGE(1), 				  0x31,  // Y axis
          USAGE(1), 				  0x32,  // Z axis
          USAGE(1), 				  0x35,  // Z-rotator axis
          INPUT(1),           0x02, // variable | absolute
        END_COLLECTION(0),
        LOGICAL_MINIMUM(1), 0x00,
        LOGICAL_MAXIMUM(4), (1023 & 0xFF), ((1023>>8) & 0xFF), ((1023>>16) & 0xFF), ((1023>>24) & 0xFF),
        REPORT_COUNT(1),		0x02,
        REPORT_SIZE(1),	  	0x0A, /* 10 */
        USAGE(1), 				  0x36,  // generic slider
        USAGE(1), 				  0x36,  // generic slider
        INPUT(1),           0x02, // variable | absolute
      END_COLLECTION(0)
      #endif
		};
		/*
		 * Set report map (here is initialized device driver on client side)
		 * https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.characteristic.report_map.xml
		 */
		hid->reportMap((uint8_t*)reportMap, sizeof(reportMap));
		//hid->reportMap((uint8_t*)reportMapKbd, sizeof(reportMapKbd));

		/*
		 * We are prepared to start hid device services. Before this point we can change all values and/or set parameters we need.
		 * Also before we start, if we want to provide battery info, we need to prepare battery service.
		 * We can setup characteristics authorization
		 */
		hid->startServices();

		/*
		 * Its good to setup advertising by providing appearance and advertised service. This will let clients find our device by type
		 */
		BLEAdvertising *pAdvertising = pServer->getAdvertising();
		//pAdvertising->setAppearance(HID_KEYBOARD);
		pAdvertising->setAppearance(GENERIC_HID);
		pAdvertising->addServiceUUID(hid->hidService()->getUUID());
		pAdvertising->start();


		BLESecurity *pSecurity = new BLESecurity();
		pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
		//pSecurity->setCapability(ESP_IO_CAP_NONE);
		pSecurity->setCapability(ESP_IO_CAP_OUT);
		//pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

		ESP_LOGI(LOG_TAG, "Advertising started!");
    while(1) { delay(1000000); }
	}
};


extern "C" {
  
  /** @brief Main init function to start HID interface
   * 
   * @note After init, just use the queues! */
  esp_err_t HID_kbdmousejoystick_init(void)
  {
    //init FreeRTOS queues
    mouse_q = xQueueCreate(32,sizeof(mouse_command_t));
    keyboard_q = xQueueCreate(32,sizeof(keyboard_command_t));
    joystick_q = xQueueCreate(32,sizeof(joystick_command_t));
    
    //init Neil Kolban's HOG task
    BLE_HOG* blehid = new BLE_HOG();
    blehid->setStackSize(16192);
    blehid->start();
    return ESP_OK;
  }
}

