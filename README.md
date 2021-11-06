# IgorUSB_libusbK

IgorUSB_libusbK is a binary compatible replacement for the original [IgorUSB.dll of Igor Češko](http://www.cesko.host.sk/IgorPlugUSB/IgorPlug-USB%20%28AVR%29_eng.htm). It is based on  [IgorUSB_libusb] (https://github.com/proog128/IgorUSB_libusb) but uses [libusbK](http://sourceforge.net/projects/libusbk/) to communicate with the IR receiver hardware.

## Installation
1. Install a libusbK-compatible driver. Use the InfWizard provided in the bin and dev-kit packages to generate and optionally install the inf driver package automatically. Or use [Zadig](https://zadig.akeo.ie/).
2. Copy IgorUSB.dll to the installation folder of your favourite remote control application.
3. Enjoy.

## License

MIT License
