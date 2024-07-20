#include "PluggableUSB.h"
#include "HID.h"
#define REPORT_COUNT 0x01

typedef struct 
{
  InterfaceDescriptor hid;
  HIDDescDescriptor   desc;
  EndpointDescriptor  in;
  EndpointDescriptor  out;
} HIDDescriptor_;

static const uint8_t PROGMEM hidReport[] = {
    0x06, 0x00, 0xff,   // USAGE_PAGE (vendor defined page 1)
    0x09, 0x00,         // USAGE (undefined)
    0xa1, 0x01,         // COLLECTION (Application)
    0x09, 0x00,         // USAGE (undefined)
    0x15, 0x00,         // LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,   // LOGICAL_MAXIMUM (255)
    0x75, 0x08,         // REPORT_SIZE (8)
    0x95, REPORT_COUNT, // REPORT_COUNT (report_count)
    0x81, 0x02,         // INPUT (Data, Var, Abs)

    // Output report
    0x09, 0x00,         // USAGE (undefined)
    0x15, 0x00,         // LOGICAL_MINIMUM (0)
    0x26, 0xff, 0x00,   // LOGICAL_MAXIMUM (255)
    0x75, 0x08,         // REPORT_SIZE (8)
    0x95, REPORT_COUNT, // REPORT_COUNT (report_count)
    0x91, 0x02,         // OUTPUT (Data, Var, Abs)

    0xc0                // END_COLLECTION
};

class HIDMixer : public PluggableUSBModule {
public:
  HIDMixer(void) : PluggableUSBModule(2, 1, epType) {
    epType[0] = EP_TYPE_INTERRUPT_IN;
    epType[1] = EP_TYPE_INTERRUPT_OUT;
    PluggableUSB().plug(this);
  }

  int getInterface(uint8_t *interfaceCount) {
    *interfaceCount += 1;
    HIDDescriptor_ interface = {
      D_INTERFACE(pluggedInterface, 2, USB_DEVICE_CLASS_HUMAN_INTERFACE, HID_SUBCLASS_NONE, HID_PROTOCOL_NONE),
      D_HIDREPORT(sizeof(hidReport)),
      D_ENDPOINT(USB_ENDPOINT_IN(pluggedEndpoint), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 16),
      D_ENDPOINT(USB_ENDPOINT_OUT(pluggedEndpoint + 1), USB_ENDPOINT_TYPE_INTERRUPT, USB_EP_SIZE, 16)
    };
    return USB_SendControl(0, &interface, sizeof(interface));
  }

  int getDescriptor(USBSetup& setup) {
    // Check if this is a HID Class Descriptor request
    if (setup.bmRequestType != REQUEST_DEVICETOHOST_STANDARD_INTERFACE) {
      return 0;
    }

    if (setup.wValueH != HID_REPORT_DESCRIPTOR_TYPE) {
      return 0;
    }

    // In a HID Class Descriptor wIndex contains the interface number
    if (setup.wIndex != pluggedInterface) {
      return 0;
    }

    return USB_SendControl(TRANSFER_PGM, hidReport, sizeof(hidReport));
  }

  bool setup (USBSetup &setup) {
    if (pluggedInterface != setup.wIndex) {
      return false;
    }

    if (setup.bRequest == REQUEST_DEVICETOHOST_CLASS_INTERFACE) {
      return true;
    }

    if (setup.bRequest == REQUEST_HOSTTODEVICE_CLASS_INTERFACE) {
      return true;
    }

    return false;
  }

  void sendReport(uint8_t *report, size_t len) {
    USB_Send(pluggedEndpoint | TRANSFER_RELEASE, report, len);
  }

  int recvReport(uint8_t *report, size_t len) {
    return USB_Recv(pluggedEndpoint + 1, report, len);
  }

private:
  uint8_t epType[2];
};

HIDMixer mixer;
int prevRawValue;
int rawValue;

void setup() {
  Serial.begin(9600);
  prevRawValue = analogRead(A1);
}

void decode(uint8_t buff[]) {
  if (buff[0] != 2) {
    return;
  } // return if buff isn't valid

  if (buff[1] == 3) {
    return;
  } // return if buff is empty

  uint8_t sinks[5][20] = {0, 0, 0, 0, 0};
  int sink_count = 0;
  int sink_name_index = 0;
  for (int i = 2; buff[i] != 3; ++i) {
      if (buff[i] == 1) {
        sinks[sink_count][sink_name_index] = 0; // null termination
        sink_count++;
        sink_name_index = 0;
      }
      sinks[sink_count][sink_name_index] = buff[i];
      sink_name_index++;
  }

  Serial.println("decoded:");

  for (int i = 0; i < 5; ++i) {
    String s = (char*)sinks[i];
    Serial.println(s);
  }


}

void loop() {
  rawValue = analogRead(A1);
  if (abs(rawValue - prevRawValue) > 3) {
    prevRawValue = rawValue;
    int mappedValue = map(rawValue, 0, 1024, 0, 100);
    uint8_t report[REPORT_COUNT] = {mappedValue};
    mixer.sendReport(report, sizeof(report));
  }
  uint8_t buff[100];
  int recvLen = mixer.recvReport(buff, sizeof(buff));
  if (recvLen > 0) {
    Serial.print("recieved ");
    Serial.print(recvLen);
    Serial.print(" bytes: [");
    for (int i = 0; i < recvLen; ++i) {
      Serial.print(" ");
      Serial.print(buff[i]);
    }
    Serial.print("]\n");
    decode(buff);
  }
}
