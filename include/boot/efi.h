#ifndef STRATUM_BOOT_EFI_H
#define STRATUM_BOOT_EFI_H

#include <stdint.h>

#ifndef NULL
#define NULL ((void*)0)
#endif

#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 1
#define EFI_INVALID_PARAMETER 2
#define EFI_UNSUPPORTED 3
#define EFI_BAD_BUFFER_SIZE 4
#define EFI_BUFFER_TOO_SMALL 5
#define EFI_NOT_READY 6
#define EFI_DEVICE_ERROR 7
#define EFI_WRITE_PROTECTED 8
#define EFI_OUT_OF_RESOURCES 9
#define EFI_NOT_FOUND 14

typedef uint64_t EFI_STATUS;
typedef void *EFI_HANDLE;
typedef void *EFI_EVENT;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

/* Simple Text Output Protocol */
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef EFI_STATUS (*EFI_TEXT_RESET)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint8_t ExtendedVerification);
typedef EFI_STATUS (*EFI_TEXT_STRING)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const int16_t *String);
typedef EFI_STATUS (*EFI_TEXT_TEST_STRING)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, const int16_t *String);
typedef EFI_STATUS (*EFI_TEXT_QUERY_MODE)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint64_t ModeNumber, uint64_t *Columns, uint64_t *Rows);
typedef EFI_STATUS (*EFI_TEXT_SET_MODE)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint64_t ModeNumber);
typedef EFI_STATUS (*EFI_TEXT_SET_ATTRIBUTE)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, uint64_t Attribute);
typedef EFI_STATUS (*EFI_TEXT_CLEAR_SCREEN)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET          Reset;
    EFI_TEXT_STRING         OutputString;
    EFI_TEXT_TEST_STRING    TestString;
    EFI_TEXT_QUERY_MODE     QueryMode;
    EFI_TEXT_SET_MODE       SetMode;
    EFI_TEXT_SET_ATTRIBUTE  SetAttribute;
    EFI_TEXT_CLEAR_SCREEN   ClearScreen;
    void                   *SetCursorPosition;
    void                   *EnableCursor;
    void                   *Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

/* Memory Types */
typedef enum {
    EfiReservedMemoryType,
    EfiLoaderCode,
    EfiLoaderData,
    EfiBootServicesCode,
    EfiBootServicesData,
    EfiRuntimeServicesCode,
    EfiRuntimeServicesData,
    EfiConventionalMemory,
    EfiUnusableMemory,
    EfiACPIReclaimMemory,
    EfiACPIMemoryNVS,
    EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace,
    EfiPalCode,
    EfiPersistentMemory,
    EfiMaxMemoryType
} EFI_MEMORY_TYPE;

typedef enum {
    AllocateAnyPages,
    AllocateMaxAddress,
    AllocateAddress,
    MaxAllocateType
} EFI_ALLOCATE_TYPE;

typedef struct {
    uint32_t Type;
    uint32_t Pad;
    uint64_t PhysicalStart;
    uint64_t VirtualStart;
    uint64_t NumberOfPages;
    uint64_t Attribute;
} EFI_MEMORY_DESCRIPTOR;

/* Graphics Output Protocol */
typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    uint64_t SizeOfInfo;
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    void *QueryMode;
    void *SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

/* File System Protocols */
#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL

struct _EFI_FILE_PROTOCOL;
typedef EFI_STATUS (*EFI_FILE_OPEN)(struct _EFI_FILE_PROTOCOL *This, struct _EFI_FILE_PROTOCOL **NewHandle, const int16_t *FileName, uint64_t OpenMode, uint64_t Attributes);
typedef EFI_STATUS (*EFI_FILE_CLOSE)(struct _EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (*EFI_FILE_DELETE)(struct _EFI_FILE_PROTOCOL *This);
typedef EFI_STATUS (*EFI_FILE_READ)(struct _EFI_FILE_PROTOCOL *This, uint64_t *BufferSize, void *Buffer);
typedef EFI_STATUS (*EFI_FILE_WRITE)(struct _EFI_FILE_PROTOCOL *This, uint64_t *BufferSize, const void *Buffer);
typedef EFI_STATUS (*EFI_FILE_GET_POSITION)(struct _EFI_FILE_PROTOCOL *This, uint64_t *Position);
typedef EFI_STATUS (*EFI_FILE_SET_POSITION)(struct _EFI_FILE_PROTOCOL *This, uint64_t Position);
typedef EFI_STATUS (*EFI_FILE_GET_INFO)(struct _EFI_FILE_PROTOCOL *This, const EFI_GUID *InformationType, uint64_t *BufferSize, void *Buffer);

typedef struct _EFI_FILE_PROTOCOL {
    uint64_t               Revision;
    EFI_FILE_OPEN          Open;
    EFI_FILE_CLOSE         Close;
    EFI_FILE_DELETE        Delete;
    EFI_FILE_READ          Read;
    EFI_FILE_WRITE         Write;
    EFI_FILE_GET_POSITION  GetPosition;
    EFI_FILE_SET_POSITION  SetPosition;
    EFI_FILE_GET_INFO      GetInfo;
    void                  *SetInfo;
    void                  *Flush;
} EFI_FILE_PROTOCOL;

struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef EFI_STATUS (*EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME)(struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *This, EFI_FILE_PROTOCOL **Root);

typedef struct _EFI_SIMPLE_FILE_SYSTEM_PROTOCOL {
    uint64_t Revision;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_OPEN_VOLUME OpenVolume;
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

/* Loaded Image Protocol */
typedef struct {
    uint32_t   Revision;
    EFI_HANDLE ParentHandle;
    void      *SystemTable;
    EFI_HANDLE DeviceHandle;
    void      *FilePath;
    void      *Reserved;
    uint32_t   LoadOptionsSize;
    void      *LoadOptions;
    void      *ImageBase;
    uint64_t   ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    void      *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

/* Boot Services Table */
typedef EFI_STATUS (*EFI_ALLOCATE_PAGES)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, uint64_t Pages, uint64_t *Memory);
typedef EFI_STATUS (*EFI_FREE_PAGES)(uint64_t Memory, uint64_t Pages);
typedef EFI_STATUS (*EFI_GET_MEMORY_MAP)(uint64_t *MemoryMapSize, EFI_MEMORY_DESCRIPTOR *MemoryMap, uint64_t *MapKey, uint64_t *DescriptorSize, uint32_t *DescriptorVersion);
typedef EFI_STATUS (*EFI_ALLOCATE_POOL)(EFI_MEMORY_TYPE PoolType, uint64_t Size, void **Buffer);
typedef EFI_STATUS (*EFI_FREE_POOL)(void *Buffer);
typedef EFI_STATUS (*EFI_HANDLE_PROTOCOL)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void **Interface);
typedef EFI_STATUS (*EFI_LOCATE_PROTOCOL)(const EFI_GUID *Protocol, void *Registration, void **Interface);
typedef EFI_STATUS (*EFI_EXIT_BOOT_SERVICES)(EFI_HANDLE ImageHandle, uint64_t MapKey);

typedef struct {
    EFI_TABLE_HEADER Hdr;

    void *RaiseTPL;
    void *RestoreTPL;

    EFI_ALLOCATE_PAGES   AllocatePages;
    EFI_FREE_PAGES       FreePages;
    EFI_GET_MEMORY_MAP   GetMemoryMap;
    EFI_ALLOCATE_POOL    AllocatePool;
    EFI_FREE_POOL        FreePool;

    void *CreateEvent;
    void *SetTimer;
    void *WaitForEvent;
    void *SignalEvent;
    void *CloseEvent;
    void *CheckEvent;

    void *InstallProtocolInterface;
    void *ReinstallProtocolInterface;
    void *UninstallProtocolInterface;
    EFI_HANDLE_PROTOCOL  HandleProtocol;
    void *Pcr;
    void *RegisterProtocolNotify;
    void *LocateHandle;
    void *LocateDevicePath;
    void *InstallConfigurationTable;

    void *LoadImage;
    void *StartImage;
    void *Exit;
    void *UnloadImage;
    EFI_EXIT_BOOT_SERVICES ExitBootServices;

    void *GetNextMonotonicCount;
    void *Stall;
    void *SetWatchdogTimer;

    void *ConnectController;
    void *DisconnectController;

    void *OpenProtocol;
    void *CloseProtocol;
    void *OpenProtocolInformation;

    void *ProtocolsPerHandle;
    void *LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
} EFI_BOOT_SERVICES;

/* System Table */
typedef struct {
    EFI_GUID VendorGuid;
    void    *VendorTable;
} EFI_CONFIGURATION_TABLE;

typedef struct {
    EFI_TABLE_HEADER                 Hdr;
    const int16_t                   *FirmwareVendor;
    uint32_t                         FirmwareRevision;
    EFI_HANDLE                       ConsoleInHandle;
    void                            *ConIn;
    EFI_HANDLE                       ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE                       StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    void                            *RuntimeServices;
    EFI_BOOT_SERVICES               *BootServices;
    uint64_t                         NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE         *ConfigurationTable;
} EFI_SYSTEM_TABLE;

/* GUID Definitions */
static const EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID =
    { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

static const EFI_GUID EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID =
    { 0x0964E5B22, 0x6459, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

static const EFI_GUID EFI_FILE_INFO_ID =
    { 0x09576E92, 0x6D3F, 0x11D2, { 0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

static const EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID =
    { 0x9042A9DE, 0x23DC, 0x4A38, { 0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A } };

static const EFI_GUID EFI_ACPI_20_TABLE_GUID =
    { 0x8868E871, 0xE4F1, 0x11D3, { 0xBC, 0x22, 0x00, 0x80, 0xC7, 0x3C, 0x88, 0x81 } };

static const EFI_GUID EFI_ACPI_10_TABLE_GUID =
    { 0xEB9D2D30, 0x2D88, 0x11D3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } };

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    uint64_t CreateTime;
    uint64_t LastAccessTime;
    uint64_t ModificationTime;
    uint64_t Attribute;
    int16_t  FileName[1];
} EFI_FILE_INFO;

#endif /* STRATUM_BOOT_EFI_H */
