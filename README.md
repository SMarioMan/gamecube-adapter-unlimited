# GameCube Adapter Unlimited

GameCube Adapter Feeder via ViGEm, with support for unlimited adapters

This feeder uses ViGEm to present GameCube controllers as DualShock 4 controllers.
Unlike the alternatives, which are usually limited to a single adapter, this tool supports unlimited adapters, or at least up to the USB spec limit.
This is particularly useful for massively multiplayer sessions, such as 24-player Mario Kart Wii splitscreen:

[![Insane 24-Player Mario Kart Wii Split Screen Test!
](https://img.youtube.com/vi/LgkHVOwAPvo/0.jpg)](https://youtu.be/LgkHVOwAPvo)

## Features
* An unlimited number of controllers and adapters can be used at once, for local massively multiplayer sessions.
* Hotpluggable adapters and controllers: Adapters and controllers can be freely attached and detached without restarting the application. Ports maintain the same controller assignment and adapters fill in missing slots.

## Libraries Used
* libusb
* ViGEm

## Build (optional)
1. Download and install Visual Studio 2022.
1. Clone the repository.
1. Open in Visual Studio and build the project.

## Install
1. Install ViGEm Driver https://github.com/ViGEm/ViGEmBus/releases/
1. Install the WinUSB driver using Zadig: https://dolphin-emu.org/docs/guides/how-use-official-gc-controller-adapter-wii-u/#Using_Zadig
1. (Optional) Overclock your adapters using the [overclocking guide](https://docs.google.com/document/d/1cQ3pbKZm_yUtcLK9ZIXyPzVbTJkvnfxKIyvuFMwzWe0/). This will improve the controller poll rate.
1. Download and extract the feeder: https://github.com/SMarioMan/gamecube-adapter-unlimited/releases

## Run
1. Open `GameCubeAdapterUnlimited.exe`.
1. On launch, the program prints `Input feeder started`.
1. Attach adapters in the desired port order. The feeder notifies you when an adapter is connected.
1. If a controller or adapter is disconnected, you can reattach it and port assignments should be preserved, no restart required.

## Fixing Controller Ordering
Sometimes, Windows will change the established order of the virtual controllers. This is problematic because assigned ports may correspond to different instances than originally configured. To correct this, you must remove all of the virtual controllers from the Windows device manager, then re-add the controllers. I have developed a streamlined technique to handle this.

1. Download `devcon.exe` from https://github.com/SMarioMan/devcon/releases
1. Open a terminal as admin and run the following command: `devcon.exe removeall *VID_054C*`. This will remove all PS4 controllers (including the virtual controllers created by GameCube Adapter Unlimited) from the device manager.
1. Run `GameCubeAdapterUnlimited.exe --det`. This command slowly connects each of the virtual devices in order, ensuring proper device ordering.
1. Run `GameCubeAdapterUnlimited.exe` as per usual. Port orderings should be maintained, at least until the next reboot.
