#define WIN32_LEAN_AND_MEAN
#include "windows.h"
#include "IgorUSB.h"

#include "libusbk.h"

#include <stdbool.h>
#include <stdint.h>
#include <tchar.h>
#include <wchar.h>

#define VENDOR_ATMEL        0x03eb
#define DEVICE_IGORPLUG     0x0002

#define DO_SET_INFRA_BUFFER_EMPTY   0x01
#define DO_GET_INFRA_CODE           0x02

#define IGOR_HEADER_LENGTH      3
#define IGOR_MAX_BUFFER_SIZE    256

#define UNUSED(name)    (void)(name)

typedef enum tagTransferStatus {
    NoError,
    ErrorNoDevice,
    ErrorTransfer
} TransferStatus;

// Globals
KUSB_DRIVER_API g_usb;
KUSB_HANDLE g_usbHandle;     // device interface usbHandle (the opened USB device)

//
bool OpenDevice();
void CloseDevice();
static TransferStatus TransferDevice(UCHAR dir, UCHAR fn, USHORT value, USHORT index, unsigned char* buf, UINT bufSize, UINT* readSize);
static TransferStatus ReadDevice(UCHAR fn, USHORT value, USHORT index, unsigned char* buf, UINT bufSize, UINT* readSize);
static TransferStatus WriteDevice(UCHAR fn, USHORT value, USHORT index, unsigned char *buf, UINT bufSize, UINT *readSize);

// implementation
bool OpenDevice()
{
    if(g_usbHandle)
        return true;

    bool result = false;
    KLST_HANDLE deviceList = NULL;
    KLST_DEVINFO_HANDLE deviceInfo = NULL;

    // Get the device list
    if(!LstK_Init(&deviceList, KLST_FLAG_NONE))
    {
        OutputDebugString(TEXT("Error initializing device list.\n"));
        return false;
    }

    UINT deviceCount = 0;
    LstK_Count(deviceList, &deviceCount);
    do
    {
        if(!deviceCount)
        {
            OutputDebugString(TEXT("Device list empty.\n"));
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            break;
        }

        LstK_FindByVidPid(deviceList, VENDOR_ATMEL, DEVICE_IGORPLUG, &deviceInfo);
        if(deviceInfo)
        {
            // Report the connection state of the example device
            TCHAR infoStr[512];
            _stprintf_s(infoStr, 512,
                TEXT("Using %04X:%04X (%hs): %hs - %hs\n"),
                deviceInfo->Common.Vid,
                deviceInfo->Common.Pid,
                deviceInfo->Common.InstanceID,
                deviceInfo->DeviceDesc,
                deviceInfo->Mfg);
            OutputDebugString(infoStr);
        }
        else
        {
            OutputDebugString(TEXT("Device not fount by VID/PID.\n"));
            SetLastError(ERROR_DEVICE_NOT_CONNECTED);
            break;
        }

        // This example will use the dynamic driver api so that it can be used with all supported drivers.
        if(!LibK_LoadDriverAPI(&g_usb, deviceInfo->DriverID))
        {
            OutputDebugString(TEXT("Driver API not loaded.\n"));
            break;
        }

        // Open the device. This creates the physical USB device handle.
        if(!g_usb.Init(&g_usbHandle, deviceInfo))
        {
            DWORD errorCode = GetLastError();
            TCHAR infoStr[255];
            _stprintf_s(infoStr, 255,
                TEXT("Open device failed. Win32Error=%u (0x%08X)\n"),
                errorCode,
                errorCode);
            OutputDebugString(infoStr);
            break;
        }

        // All OK
        OutputDebugString(TEXT("Device opened successfully!\n"));
        result = true;
    }
    while(0);

    // If LstK_Init returns TRUE, the list must be freed.
    LstK_Free(deviceList);
    return result;
}

void CloseDevice()
{
    if(g_usbHandle)
    {
        g_usb.Free(g_usbHandle);
        g_usbHandle = NULL;
        OutputDebugString(TEXT("Device closed.\n"));
    }
}

TransferStatus TransferDevice(UCHAR dir, UCHAR fn, USHORT value, USHORT index, unsigned char *buf, UINT bufSize, UINT *readSize)
{
    if (readSize)
        *readSize = 0;

    if (bufSize > UINT16_MAX)
        OutputDebugString(TEXT("Buffer size too big, truncated to 16 bits!\n"));

    if (OpenDevice())
    {
        KUSB_SETUP_PACKET setupPacket;
        memset(&setupPacket, 0, sizeof(KUSB_SETUP_PACKET));

        setupPacket.BmRequest.Dir = dir;
        setupPacket.BmRequest.Type = BMREQUEST_TYPE_VENDOR;
        setupPacket.BmRequest.Recipient = BMREQUEST_RECIPIENT_DEVICE;
        setupPacket.Request = fn;
        setupPacket.Length = (USHORT)bufSize;
        setupPacket.Value = value;
        setupPacket.Index = index;

        BOOL success = g_usb.ControlTransfer(g_usbHandle, *((WINUSB_SETUP_PACKET*)&setupPacket), buf, bufSize, readSize, NULL);
        if (!success)
        {
            DWORD errorCode = GetLastError();
            TCHAR infoStr[255];
            _stprintf_s(infoStr, 255, 
                TEXT("Transfer device failed. Win32Error=%u (0x%08X)\n"),
                errorCode,
                errorCode);
            OutputDebugString(infoStr);

            CloseDevice();
            return ErrorTransfer;
        }
        return NoError;
    }
    return ErrorNoDevice;
}

TransferStatus ReadDevice(UCHAR fn, USHORT value, USHORT index, unsigned char *buf, UINT bufSize, UINT *readSize)
{
    return TransferDevice(BMREQUEST_DIR_DEVICE_TO_HOST, fn, value, index, buf, bufSize, readSize);
}

TransferStatus WriteDevice(UCHAR fn, USHORT value, USHORT index, unsigned char *buf, UINT bufSize, UINT *readSize)
{
    return TransferDevice(BMREQUEST_DIR_HOST_TO_DEVICE, fn, value, index, buf, bufSize, readSize);
}

// IgorPlug API
IGORUSB_API int __stdcall DoSetInfraBufferEmpty()
{
    int result = IGORUSB_DEVICE_NOT_PRESENT;

    unsigned char buf[2] = {0};

    if(WriteDevice(DO_SET_INFRA_BUFFER_EMPTY, 0, 0, buf, 1, NULL) == NoError)
        result = IGORUSB_NO_ERROR;
    return result;
}

IGORUSB_API int __stdcall DoGetInfraCode(unsigned char * TimeCodeDiagram, int DummyInt, int * DiagramLength)
{
    static int lastRead = -1;

    unsigned char buf[IGOR_MAX_BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));

    UNUSED(DummyInt);

    if(DiagramLength)
        *DiagramLength = 0;

    UINT recvd;
    if(ReadDevice(DO_GET_INFRA_CODE, 0, 0, buf, 3, &recvd) != NoError)
    {
        lastRead = -1;
        return IGORUSB_DEVICE_NOT_PRESENT;
    }

    if(recvd != 3)
    {
        // Nothing to do
        return NO_ERROR;
    }

    UINT bytesToRead = buf[0];
    if(bytesToRead == 0)
        return NO_ERROR;

    int msgIdx = buf[1];
    int lastWrittenIdx = buf[2];

    UINT bufSize;
    UINT i = 0;
    while(i < bytesToRead)
    {
        bufSize = bytesToRead - i;
        if(bufSize > IGOR_MAX_BUFFER_SIZE)
        {
            OutputDebugString(TEXT("Buffer is too small."));
            break;
        }

        USHORT offset = (USHORT)i + IGOR_HEADER_LENGTH;
        TransferStatus stat = ReadDevice(DO_GET_INFRA_CODE, offset, 0, &buf[i], bufSize, &recvd);
        if (stat != NoError)
        {
            lastRead = -1;
            return (stat == ErrorNoDevice)? IGORUSB_DEVICE_NOT_PRESENT : DoSetInfraBufferEmpty();
        }

        i += recvd;
    }

    if(msgIdx != lastRead)
    {
        // new message
        UINT j = lastWrittenIdx % bytesToRead;
        int k = 0;
        for (i = j; i < bytesToRead; ++i)
            TimeCodeDiagram[k++] = buf[i];

        for (i = 0; i < j; ++i)
            TimeCodeDiagram[k++] = buf[i];

        if(DiagramLength)
            *DiagramLength = bytesToRead;

        lastRead = msgIdx;
    }
    else
    {
        // message is repeated (has same index as before)
        // -> do nothing
        if (DiagramLength)
            *DiagramLength = 0;
    }

    return DoSetInfraBufferEmpty();
}

IGORUSB_API int __stdcall DoSetDataPortDirection(unsigned char DirectionByte)
{
    UNUSED(DirectionByte);
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetDataPortDirection(unsigned char * DataDirectionByte)
{
    if (DataDirectionByte)
        *DataDirectionByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoSetOutDataPort(unsigned char DataOutByte)
{
    UNUSED(DataOutByte);
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetOutDataPort(unsigned char * DataOutByte)
{
    if (DataOutByte)
        *DataOutByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetInDataPort(unsigned char * DataInByte)
{
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoEEPROMRead(unsigned char Address, unsigned char * DataInByte)
{
    UNUSED(Address);
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoEEPROMWrite(unsigned char Address, unsigned char DataOutByte)
{
    UNUSED(Address);
    UNUSED(DataOutByte);
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoRS232Send(unsigned char DataOutByte)
{
    UNUSED(DataOutByte);
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoRS232Read(unsigned char * DataInByte)
{
    if (DataInByte)
        *DataInByte = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoSetRS232Baud(int BaudRate)
{
    UNUSED(BaudRate);
    return IGORUSB_NOT_IMPLEMENTED;
}

IGORUSB_API int __stdcall DoGetRS232Baud(int * BaudRate)
{
    if (BaudRate)
        *BaudRate = 0;
    return IGORUSB_NOT_IMPLEMENTED;
}
