# GameCube Adapter Unlimited

GameCube Adapter Feeder via ViGEm, with support for unlimited adapters

This feeder uses ViGEm to present GameCube controllers as DualShock 4 controllers in Windows.
Unlike the alternatives, which are usually limited to a single adapter, this tool supports unlimited adapters, or at least up to the USB spec limit.
This is particularly useful for massively multiplayer sessions, such as Mario Kart splitscreen:

[![Mario Kart DS 8 Player Splitscreen Multiplayer [4K 60FPS]!
](https://img.youtube.com/vi/6uR6WGDYfug/0.jpg)](https://youtu.be/6uR6WGDYfug)

## Features
* An unlimited number of controllers and adapters can be used at once, for local massively multiplayer sessions.
Controllers are presented as (DirectInput) DualShock 4 controllers to avoid controller limits.
* Hotpluggable adapters and controllers:
Adapters and controllers can be freely attached and detached without restarting the application.
Ports maintain the same controller assignment and adapters fill in missing slots.
* Rumble support.
* Support for the GBA (via the [Nintendo GameCube Game Boy Advance Cable](https://en.wikipedia.org/wiki/GameCube_%E2%80%93_Game_Boy_Advance_link_cable) and the `controller-gc.gba` ROM included in the "extra package" part of the [Game Boy Interface](https://www.gc-forever.com/wiki/index.php?title=Game_Boy_Interface/Standard_Edition)).

## Libraries Used
* [libusb](https://github.com/libusb/libusb)
* [ViGEm](https://github.com/nefarius/ViGEmBus)

## Build (optional)
1. Download and install [Visual Studio 2022](https://visualstudio.microsoft.com/).
1. Clone [the repository](https://github.com/SMarioMan/gamecube-adapter-unlimited).
1. Open in Visual Studio and build the project.

## Install
1. Install the ViGEm driver: https://github.com/ViGEm/ViGEmBus/releases/
1. Install the WinUSB driver using Zadig: https://dolphin-emu.org/docs/guides/how-use-official-gc-controller-adapter-wii-u/#Using_Zadig
1. (Optional) Overclock your adapters using the [overclocking guide](https://docs.google.com/document/d/1cQ3pbKZm_yUtcLK9ZIXyPzVbTJkvnfxKIyvuFMwzWe0/). This will improve the controller poll rate.
1. Download and extract the feeder: https://github.com/SMarioMan/gamecube-adapter-unlimited/releases

## Run
1. Open `GameCubeAdapterUnlimited.exe`.
1. On launch, the program prints `Input feeder started`.
1. Attach adapters in the desired port order. The feeder notifies you when each adapter and controller is connected or disconnected.
1. If a controller or adapter is disconnected, you can reattach it and port assignments should be preserved, no restart required.

## Fixing Controller Ordering
Sometimes, Windows will change the established order of the virtual controllers.
This is problematic because assigned ports may correspond to different instances than originally configured.
To correct this, you must remove all of the virtual controllers from the Windows device manager, then re-add the controllers.
To do this, simply run `GameCubeAdapterUnlimited.exe` as admin.
From there, port orderings should be maintained, at least until the next reboot.