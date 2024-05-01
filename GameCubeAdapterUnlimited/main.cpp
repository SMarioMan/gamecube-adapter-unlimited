#include <windows.h>
// Windows header must be defined before these to prevent build errors.
#include <ViGEm/Client.h>
#include <libusb/libusb.h>

#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

constexpr bool DEBUG = false;

// Disabling this will save resources at run time but disables adapter
// hotplugging.
constexpr bool ENABLE_HOTPLUGGING = true;

// The size of our adapters array.
// A fixed-size array is used to ensure it can be accessed in a thread-safe way
// without locks.
constexpr std::size_t MAX_ADAPTERS = 6;

static bool running = true;
BOOL WINAPI CtrlHandler(DWORD event) {
  if (event == CTRL_CLOSE_EVENT) {
    running = false;
    Sleep(20000);
    return TRUE;
  }
  return FALSE;
}

struct Controller {
#pragma pack(push, 1)
  struct GCInput {
    union {
      unsigned char Status;
      struct {
        unsigned char _pad : 1;
        // This bit is set when the grey USB cable is attached, to power rumble.
        unsigned char CanRumble : 1;
        unsigned char _pad2 : 2;
        unsigned char Wired : 1;
        unsigned char Wireless : 1;
      };
    };
    union {
      unsigned short Buttons;
      struct {
        unsigned short A : 1;
        unsigned short B : 1;
        unsigned short X : 1;
        unsigned short Y : 1;
        unsigned short DpadLeft : 1;
        unsigned short DpadRight : 1;
        unsigned short DpadDown : 1;
        unsigned short DpadUp : 1;
        unsigned short Start : 1;
        unsigned short Z : 1;
        unsigned short R : 1;
        unsigned short L : 1;
      };
    };
    unsigned char AnalogX;
    unsigned char AnalogY;
    unsigned char CStickX;
    unsigned char CStickY;
    unsigned char LeftTrigger;
    unsigned char RightTrigger;

    GCInput()
        : Status(0),
          Buttons(0),
          AnalogX(128),
          AnalogY(128),
          CStickX(128),
          CStickY(128),
          LeftTrigger(0),
          RightTrigger(0){};
    bool On() { return Wired || Wireless; }
  };
#pragma pack(pop)

  static _DS4_REPORT GCtoDS4(const GCInput& gc) {
    _DS4_REPORT ds4{};

    ds4.bThumbLX = gc.AnalogX;
    ds4.bThumbLY = ~gc.AnalogY;
    ds4.bThumbRX = gc.CStickX;
    ds4.bThumbRY = ~gc.CStickY;

    ds4.wButtons = 0;
    if (gc.Start) ds4.wButtons |= DS4_BUTTON_OPTIONS;
    if (gc.Z) ds4.wButtons |= DS4_BUTTON_SHARE;
    if (gc.R) ds4.wButtons |= DS4_BUTTON_SHOULDER_RIGHT;
    if (gc.L) ds4.wButtons |= DS4_BUTTON_SHOULDER_LEFT;
    if (gc.X) ds4.wButtons |= DS4_BUTTON_TRIANGLE;
    if (gc.A) ds4.wButtons |= DS4_BUTTON_CIRCLE;
    if (gc.B) ds4.wButtons |= DS4_BUTTON_CROSS;
    if (gc.Y) ds4.wButtons |= DS4_BUTTON_SQUARE;

    if (gc.DpadUp && gc.DpadLeft)
      ds4.wButtons |= DS4_BUTTON_DPAD_NORTHWEST;
    else if (gc.DpadDown && gc.DpadLeft)
      ds4.wButtons |= DS4_BUTTON_DPAD_SOUTHWEST;
    else if (gc.DpadDown && gc.DpadRight)
      ds4.wButtons |= DS4_BUTTON_DPAD_SOUTHEAST;
    else if (gc.DpadUp && gc.DpadRight)
      ds4.wButtons |= DS4_BUTTON_DPAD_NORTHEAST;
    else if (gc.DpadUp)
      ds4.wButtons |= DS4_BUTTON_DPAD_NORTH;
    else if (gc.DpadLeft)
      ds4.wButtons |= DS4_BUTTON_DPAD_WEST;
    else if (gc.DpadDown)
      ds4.wButtons |= DS4_BUTTON_DPAD_SOUTH;
    else if (gc.DpadRight)
      ds4.wButtons |= DS4_BUTTON_DPAD_EAST;
    else
      ds4.wButtons |= DS4_BUTTON_DPAD_NONE;

    ds4.bTriggerL = gc.LeftTrigger;
    ds4.bTriggerR = gc.RightTrigger;

    return ds4;
  }
};

class Adapter {
  static const unsigned char ReadEndpoint = 1 | LIBUSB_ENDPOINT_IN;
  static const unsigned char WriteEndpoint = 2 | LIBUSB_ENDPOINT_OUT;

  libusb_device_handle* dev_handle;
  std::array<unsigned char, 5> rumblePayload;

  size_t failedReads = 0;

  bool ReadInterrupt(unsigned char* data, const int& length,
                     const int& timeoutMs) {
    int actual;
    const int interrupt = libusb_interrupt_transfer(
        dev_handle, ReadEndpoint, data, length, &actual, timeoutMs);
    if (interrupt < LIBUSB_SUCCESS) {
      std::cout << "libusb_interrupt_transfer failed: " << interrupt
                << std::endl;
    }
    return interrupt == LIBUSB_SUCCESS && length == actual;
  }
  bool WriteRumble() {
    unsigned char* data = rumblePayload.data();
    const int length = static_cast<int>(rumblePayload.size());
    return Write(data, length);
  }

 public:
  struct Inputs {
    // Padding to align inputs we get from the USB transfer with the Controller
    // struct.
    unsigned char _pad[1]{};
    Controller::GCInput Controllers[4]{};
  };
  Adapter(libusb_device_handle* dev_handle) {
    this->dev_handle = dev_handle;
    // This call makes Nyko-brand (and perhaps other) adapters work.
    // However it returns LIBUSB_ERROR_PIPE with Mayflash adapters.
    const int transfer = libusb_control_transfer(dev_handle, 0x21, 11, 0x0001,
                                                 0, nullptr, 0, 1000);
    if (transfer == LIBUSB_ERROR_PIPE) {
      std::cout << "Mayflash adapter detected." << std::endl;
    } else if (transfer < LIBUSB_SUCCESS) {
      std::cout << "libusb_control_transfer failed: " << transfer << std::endl;
    }
    const int claim = libusb_claim_interface(dev_handle, 0);
    if (claim < LIBUSB_SUCCESS) {
      std::cout << "libusb_claim_interface failed: " << claim << std::endl;
    }
    // Initialization payload.
    // Enable input polling by the adapter.
    // Inputs can be read by the read endpoint.
    unsigned char init = 0x13;
    Write(&init, 1);

    // Rumble should default to off.
    ResetRumble();
  }
  ~Adapter() {
    const int release = libusb_release_interface(dev_handle, 0);
    if (release < LIBUSB_SUCCESS) {
      std::cout << "libusb_release_interface failed: " << release << std::endl;
    }
    libusb_close(dev_handle);
  }
  bool DoesHandleMatch(libusb_device_handle* dev_handle) {
    return this->dev_handle == dev_handle;
  }
  bool DoesHandleMatch(Adapter* adapter) {
    if (!adapter) {
      std::cout << "Requested DoesHandleMatch on nullptr adapter" << std::endl;
      return false;
    }
    return DoesHandleMatch(adapter->dev_handle);
  }
  bool Write(unsigned char* data, int length) {
    int actual;
    const int bulk = libusb_bulk_transfer(dev_handle, WriteEndpoint, data,
                                          length, &actual, 0);
    if (bulk < LIBUSB_SUCCESS) {
      std::cout << "libusb_bulk_transfer failed: " << bulk << std::endl;
    }
    return bulk == LIBUSB_SUCCESS && length == actual;
  }
  bool GetInputs(Inputs& inputs) {
    const int timeoutMs = 16;
    return ReadInterrupt((unsigned char*)&inputs, sizeof(Inputs), timeoutMs);
  }
  // Detect timeouts due to multiple failed reads.
  bool ShouldDisconnect(const bool& gotLastInput) {
    if (gotLastInput) {
      failedReads = 0;
      return false;
    }
    if (failedReads++ > 20) {
      return true;
    }
    return false;
  }
  bool ResetRumble() {
    rumblePayload = {0x11, 0x0, 0x0, 0x0, 0x0};
    return WriteRumble();
  }
  bool SetRumble(ssize_t index, unsigned char val) {
    if (index < 0 || index >= 4) {
      std::cout << "Rumble index out of range: " << index << std::endl;
      return false;
    }

    // NOTE: Rumble should probably be disabled in the following circumstances,
    // but it seems to not matter, so we ignore it for now:
    // The controller is wireless. WaveBirds do not have rumble functionality.
    // The grey USB cable is disconnected. Without it, there isn't enough power
    // for the motors.
    // The controller is disconnected. Rumble for detached controllers is
    // pointless.

    rumblePayload[1 + index] = val;

    if (DEBUG) {
      std::cout << "Rumble payload: ";
      for (const auto& val : rumblePayload) {
        std::cout << "0x" << std::hex << static_cast<int>(val) << ", ";
      }
      std::cout << std::endl;
    }

    return WriteRumble();
  }
};

// Globals.
// The list of GameCube Adapters.
std::array<std::atomic<Adapter*>, MAX_ADAPTERS> adapters;

// Forward declaration.
_Function_class_(EVT_VIGEM_DS4_NOTIFICATION) VOID
    UpdateRumble(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor,
                 UCHAR SmallMotor, DS4_LIGHTBAR_COLOR LightbarColor);

class ViGEmClient {
 public:
  PVIGEM_CLIENT client;

  ViGEmClient() {
    client = vigem_alloc();
    if (!client) {
      throw std::runtime_error("vigem_alloc failed");
    }
    const VIGEM_ERROR retval = vigem_connect(client);
    if (!VIGEM_SUCCESS(retval)) {
      std::stringstream ss;
      ss << "vigem_connect failed with error code: 0x" << std::hex << retval
         << std::endl;
      throw std::runtime_error(ss.str());
    }
  }
  ~ViGEmClient() {
    vigem_disconnect(client);
    vigem_free(client);
  }
  PVIGEM_TARGET AddController() {
    // Allocate handle to identify new pad.
    const PVIGEM_TARGET pad = vigem_target_ds4_alloc();
    // Add client to the bus, this equals a plug-in event.
    const VIGEM_ERROR add_err = vigem_target_add(client, pad);
    if (!VIGEM_SUCCESS(add_err)) {
      std::stringstream ss;
      ss << "vigem_target_add failed with error code: 0x" << std::hex << add_err
         << std::endl;
      throw std::runtime_error(ss.str());
    }
    const VIGEM_ERROR reg_err =
        vigem_target_ds4_register_notification(client, pad, &UpdateRumble);
    if (!VIGEM_SUCCESS(reg_err)) {
      std::stringstream ss;
      ss << "vigem_target_ds4_register_notification failed with error code: 0x"
         << std::hex << add_err << std::endl;
      throw std::runtime_error(ss.str());
    }
    return pad;
  }
  void RemoveController(PVIGEM_TARGET& pad) {
    vigem_target_ds4_unregister_notification(pad);
    vigem_target_remove(client, pad);
    vigem_target_free(pad);
    pad = nullptr;
  }
  VIGEM_ERROR UpdateController(const PVIGEM_TARGET& pad,
                               const DS4_REPORT& report) {
    return vigem_target_ds4_update(client, pad, report);
  }
};

class LibUSB {
  // The vendor and product IDs associated with GameCube controller adapters.
  // Any adapter placed in Wii U/Switch mode will appear with these IDs.
  static const int VENDOR_ID = 0x57E;
  static const int PRODUCT_ID = 0x337;

  libusb_context* context = nullptr;

 public:
  LibUSB() { libusb_init(&context); }
  ~LibUSB() {
    if (context) {
      libusb_exit(context);
    }
  }
  void PollDevices() {
    libusb_device** list;
    // Hotplugging in Windows with libusb can only be done by getting the entire
    // device list.
    ssize_t num_devices = libusb_get_device_list(context, &list);
    for (ssize_t i = 0; i < num_devices; i++) {
      libusb_device* device = list[i];
      libusb_device_descriptor desc;
      int r = libusb_get_device_descriptor(device, &desc);
      if (r < 0) {
        continue;
      }
      if (desc.idVendor == VENDOR_ID && desc.idProduct == PRODUCT_ID) {
        libusb_device_handle* dev_handle = nullptr;
        int retval = libusb_open(device, &dev_handle);
        if (retval < 0) {
          if (DEBUG) {
            std::stringstream ss;
            ss << "libusb_open failed with error code: " << retval << std::endl;
            if (retval == LIBUSB_ERROR_ACCESS) {
              ss << "A program (Dolphin, Yuzu, this feeder, etc.) has already "
                    "claimed this adapter. Close it and restart the feeder."
                 << std::endl;
            }
            std::cout << ss.str();
          }
          continue;
        }
        if (!dev_handle) {
          std::stringstream ss;
          ss << "libusb_open returned a nullptr dev_handle" << std::endl;
          std::cout << ss.str();
          continue;
        }
        Adapter* adapterPtr = new Adapter(dev_handle);
        AddAdapter(adapterPtr);
      }
    }
    libusb_free_device_list(list, 1);
  }
  // Assign an adapter to the lowest available index.
  static void AddAdapter(Adapter* adapterPtr) {
    size_t idx = -1;
    // Find the next free spot to load an adapter into.
    const auto it = std::find(adapters.begin(), adapters.end(), nullptr);
    if (it != adapters.end()) {
      *it = adapterPtr;
      idx = std::distance(adapters.begin(), it);
      std::cout << "Adapter " << idx << " connected." << std::endl;
    }
    // No spot found.
    else {
      throw std::runtime_error(
          "No free spots left in adapters array. Please increase MAX_ADAPTERS "
          "and recompile.");
    }
    return;
  }
  static void RemoveAdapter(Adapter* adapterPtr) {
    // Find the associated adapter and deallocate it.
    for (std::atomic<Adapter*>& adapter : adapters) {
      if (!adapter.load()) {
        continue;
      }
      if (adapter.load()->DoesHandleMatch(adapterPtr)) {
        auto tmp = adapter.load();
        adapter.store(nullptr);
        delete tmp;
        return;
      }
    }
    std::cout << "Could not find target adapter for removal." << std::endl;
  }
  static size_t NumAdapters() {
    size_t count = 0;
    for (const Adapter* adapter : adapters) {
      if (adapter) {
        count++;
      }
    }
    return count;
  }
};

class AdapterThread {
 public:
  void SetupPads() {
    // Set up the virtual gamepads.
    while (pads.size() / 4 < adapters.size()) {
      for (size_t i = 0; i < 4; i++) {
        pads.push_back(vigemClient.AddController());
        // Initialize the inputs to nothing.
        const Controller::GCInput resetGCInput;
        const DS4_REPORT report = Controller::GCtoDS4(resetGCInput);
        vigemClient.UpdateController(pads.back(), report);
        // Initialize as disconnected, since we do not yet know if a controller
        // is there.
        isConnected.push_back(false);
      }
    }
  }

  void AddDeterministic() {
    // Set up the virtual gamepads.
    if (pads.size() / 4 < adapters.size()) {
      std::cout
          << "Adding virtual gamepads. Please wait. We are throttling the "
             "attachment rate to ensure deterministic port orderings."
          << std::endl;
    }
    while (pads.size() / 4 < adapters.size()) {
      for (size_t i = 0; i < 4; i++) {
        pads.push_back(vigemClient.AddController());
        std::cout << "Added controller " << pads.size() << "/"
                  << MAX_ADAPTERS * 4 << std::endl;
        // Delay plug-in to ensure controllers are added in the desired order.
        if (pads.size() != adapters.size() * 4) {
          std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        }
      }
    }
  }

  size_t GetPadIndex(PVIGEM_TARGET pad) {
    auto it = find(pads.begin(), pads.end(), pad);
    if (it != pads.end()) {
      return distance(pads.begin(), it);
    } else {
      return SIZE_MAX;
    }
  }

  void run() {
    while (running) {
      // Allocate new virtual pads as needed.
      SetupPads();
      // Read inputs and update virtual gamepads.
      for (size_t i = 0; i < adapters.size(); i++) {
        // Missing adapters are skipped.
        if (!adapters[i]) {
          continue;
        }
        Adapter::Inputs inputs;
        // If we fail to get the latest inputs, remove the lost adapter.
        const bool gotLastInput = adapters[i].load()->GetInputs(inputs);
        if (adapters[i].load()->ShouldDisconnect(gotLastInput)) {
          LibUSB::RemoveAdapter(adapters[i]);
          std::cout << "Adapter " << i << " disconnected" << std::endl;
          // Associated pads are marked as disconnected.
          // NOTE: This assumes inputs.Controllers[j].On() remains true.
          for (size_t j = 0; j < 4; j++) {
            size_t index = i * 4 + j;
            isConnected[index] = false;
          }
        }
        // Do not update the virtual gamepads if the adapter failed to report
        // new inputs.
        if (!gotLastInput) {
          continue;
        }
        // Update the inputs of each virtual gamepad.
        for (size_t j = 0; j < 4; j++) {
          const size_t index = i * 4 + j;
          // Check for a connection change.
          if (isConnected[index] != (bool)inputs.Controllers[j].On()) {
            isConnected[index] = (bool)inputs.Controllers[j].On();
            if (isConnected[index]) {
              std::cout << "Controller " << index << " connected";
              if (inputs.Controllers[j].Wired)
                std::cout << " (wired)" << std::endl;
              if (inputs.Controllers[j].Wireless)
                std::cout << " (wireless)" << std::endl;
            } else {
              // Disconnected controllers are reset.
              const Controller::GCInput resetGCInput;
              const DS4_REPORT report = Controller::GCtoDS4(resetGCInput);
              vigemClient.UpdateController(pads[index], report);
              std::cout << "Controller " << index << " disconnected"
                        << std::endl;
            }
          }
          if (!inputs.Controllers[j].On()) {
            continue;
          }
          if (pads.size() < index) {
            throw std::out_of_range(
                "Not enough virtual pads allocated to handle adapter inputs.");
          }
          DS4_REPORT report = Controller::GCtoDS4(inputs.Controllers[j]);
          vigemClient.UpdateController(pads[index], report);
        }
      }
    }
    // Tear down gamepads when the loop is over.
    for (PVIGEM_TARGET& pad : pads) {
      vigemClient.RemoveController(pad);
    }
  }
  // The list of virtual gamepads.
  std::vector<PVIGEM_TARGET> pads;
  // The ViGEmClient. Shared between adapters.
  ViGEmClient vigemClient = ViGEmClient();
  // The controller connection state from the previous loop.
  // Used to detect connection status changes.
  // Corresponds directly to the pads vector.
  std::vector<bool> isConnected;
};

// Global adapter thread.
// Must be global to allow UpdateRumble() to access required fields.
AdapterThread adapterThread = AdapterThread();

_Function_class_(EVT_VIGEM_DS4_NOTIFICATION) VOID
    UpdateRumble(PVIGEM_CLIENT Client, PVIGEM_TARGET Target, UCHAR LargeMotor,
                 UCHAR SmallMotor, DS4_LIGHTBAR_COLOR LightbarColor) {
  if (Client != adapterThread.vigemClient.client) {
    std::stringstream ss;
    ss << "VIGEM_DS4_NOTIFICATION failed: "
       << "client did not match." << std::endl;
    throw std::runtime_error(ss.str());
  }
  // Identify the target controller.
  size_t index = adapterThread.GetPadIndex(Target);
  if (index == SIZE_MAX) {
    std::stringstream ss;
    ss << "Could not find the requested vigemClient gamepad." << std::endl;
    throw std::runtime_error(ss.str());
  }
  Adapter* adapter = adapters[index / 4].load();
  if (adapter) {
    bool motor = SmallMotor || LargeMotor;
    adapter->SetRumble(index % 4, motor);
  }
}

int main(int argc, char* argv[]) {
  // This should ideally be enabled for the first run of the application.
  // Subsequent runs shouldn't matter. If Windows butchers the device orderings,
  // then grab the latest version of devcon.exe from here:
  // https://github.com/SMarioMan/devcon/releases
  // and run the following command as admin:
  // devcon.exe removeall *VID_054C*
  // Then run:
  // GameCubeAdapterUnlimited.exe --det
  if (argc > 1 && strcmp(argv[1], "--det") == 0) {
    adapterThread.AddDeterministic();
    return 0;
  }
  std::cout << "Input feeder started" << std::endl;

  // Set a handler to gracefully close on Ctrl+C.
  SetConsoleCtrlHandler(CtrlHandler, TRUE);

  LibUSB libUsb = LibUSB();

  // Start the adapter thread to update inputs.
  // Multithreading ensures that polling for new adapters doesn't stall input
  // updates.
  adapterThread.SetupPads();
  std::thread thread(&AdapterThread::run, &adapterThread);

  do {
    if (libUsb.NumAdapters() < MAX_ADAPTERS) {
      // Poll for new adapter connections.
      libUsb.PollDevices();
    }
    // Only check for new controllers at a fixed interval.
    // This prevents busy polling from maxing out a thread.
    std::this_thread::sleep_for(std::chrono::milliseconds(5000));
  } while (running && ENABLE_HOTPLUGGING);

  // Wait for the adapter thread to finish gracefully.
  thread.join();

  return 0;
}
