/*
 * Copyright © 2022 Moore Threads Inc. All rights reserved.
 *
 */

#include <drm/drm.h>

#include "mtgpu_errors.h"

#pragma once
#define MT_IOCTL_PVR_SRVKM DRM_COMMAND_BASE + 0x0
#define MT_IOCTL_PVR_SYNC_RENAME DRM_COMMAND_BASE + 0x1

#define MT_IOCTL_MM 6UL
#define MT_IOCTL_MM_CMD_FIRST 0x0
#define MT_IOCTL_MM_PMRUNREFPMR MT_IOCTL_MM_CMD_FIRST + 7
#define MT_IOCTL_MM_PHYSMEMNEWRAMBACKEDPMR MT_IOCTL_MM_CMD_FIRST + 9
#define MT_IOCTL_MM_PHYSHEAPGETMEMINFO MT_IOCTL_MM_CMD_FIRST + 35
#define MT_IOCTL_MM_GETHEAPPHYSMEMUSAGE MT_IOCTL_MM_CMD_FIRST + 37
#define MT_IOCTL_MM_PHYSHEAPGETMEMINFOPKD MT_IOCTL_MM_CMD_FIRST + 40
#define MT_IOCTL_MM_GETHEAPPHYSMEMUSAGEPKD MT_IOCTL_MM_CMD_FIRST + 41

#define MT_IOCTL_RGXCMP 129UL
#define MT_IOCTL_RGXCMD_CMD_FIRST 0x0
#define MT_IOCTL_RGXCMP_RGXDESTROYCOMPUTECONTEXT MT_IOCTL_RGXCMD_CMD_FIRST + 1
#define MT_IOCTL_RGXCMP_RGXKICKCDM2 MT_IOCTL_RGXCMD_CMD_FIRST + 5

#define MT_IOCTL_DMA 26UL
#define MT_IOCTL_DMA_CMD_FIRST 0x0
#define MT_IOCTL_DMA_TRANSFER MT_IOCTL_DMA_CMD_FIRST + 0

#define MT_IOCTL_RGXTQ2 137UL
#define MT_IOCTL_RGXTQ2_CMD_FIRST 0x0
#define MT_IOCTL_RGXTQ2_RGXTDMSUBMITTRANSFER2 MT_IOCTL_RGXTQ2_CMD_FIRST + 0x4

// For MTML memory information
#define MTGPU_IPC_IOCTL_MESSAGE_TRANSMIT 0x1
#define IPC_EVENT_GET_DEVICE_INFO 125UL

#define PCI_VENDOR_ID_MT 0x1ED5

#define DEVICE_ID_MTT_S10 0x101
#define DEVICE_ID_MTT_S30_2_Core 0x102
#define DEVICE_ID_MTT_S30_4_Core 0x103
#define DEVICE_ID_MTT_S1000M 0x121
#define DEVICE_ID_MTT_S4000 0x124
#define DEVICE_ID_MTT_S50 0x105
#define DEVICE_ID_MTT_S60 0x106
#define DEVICE_ID_MTT_S100 0x111
#define DEVICE_ID_MTT_S1000 0x122
#define DEVICE_ID_MTT_S2000 0x123

#define DEVICE_ID_QUYUAN1 0x200
#define DEVICE_ID_MTT_S80 0x201
#define DEVICE_ID_MTT_S70 0x202
#define DEVICE_ID_MTT_S3000 0x222

#define MTGPU_DRIVER_NAME "mtgpu"

typedef struct mt_ioctl_srvkm_cmd_s mt_ioctl_srvkm_cmd_t;
struct mt_ioctl_srvkm_cmd_s
{
    uint32_t bridge_id;
    uint32_t bridge_func_id;
    uint64_t in_data_ptr;
    uint64_t out_data_ptr;
    uint32_t in_data_size;
    uint32_t out_data_size;
};

typedef struct mt_ioctl_dma_dmatransfer_s mt_ioctl_dma_dmatransfer_t;
struct mt_ioctl_dma_dmatransfer_s
{
    uint64_t *pui64Address;
    uint64_t *puiOffset;
    uint64_t *puiSize;
    uint64_t *phPMR;
    int32_t hUpdateTimeline;
    uint32_t ui32NumDMAs;
    uint32_t ui32uiFlags;
};

/* Bridge in structure for memory allocation */
typedef struct mt_ioctl_memory_alloc_in_s mt_ioctl_memory_alloc_in_t;
struct mt_ioctl_memory_alloc_in_s
{
    // When sparse allocations are requested, this is the allocated chunk size.
    // For regular allocations, this will be the same as uiSize. (must be a multiple of page size)
    uint64_t uiChunkSize;
    // The size of the allocation (must be a multiple of page size)
    uint64_t uiSize;
    // When sparse allocations are requested, this is the list of the indices of each physically-backedvirtual chunk.
    // For regular allocations, this will be NULL.
    uint32_t *pui32MappingTable;
    // String describing the PMR (for debug). This should be passed into the function PMRCreatePMR().
    const char *puiAnnotation;
    uint32_t ui32AnnotationLength;
    // The physical pagesize in log2(bytes).
    uint32_t ui32Log2PageSize;
    // When sparse allocations are requested, this is the number of physical chunks to be allocated.
    // For regular allocations, this will be 1.
    uint32_t ui32NumPhysChunks;
    // When sparse allocations are requested, this is the number of virtual chunks covering the sparse allocation.
    // For regular allocations, this will be 1.
    uint32_t ui32NumVirtChunks;
    // The pdump flags.
    uint32_t ui32PDumpFlags;
    // The process ID that this allocation should be associated with.
    uint32_t ui32PID;
    // The allocation flags.
    uint64_t uiFlags;
};

typedef struct mt_ioctl_memory_alloc_out_s mt_ioctl_memory_alloc_out_t;
struct mt_ioctl_memory_alloc_out_s
{
    uint64_t hPMRPtr;
    PVRSRV_ERROR eError;
    uint64_t uiOutFlags;
};

/* Bridge in structure for PMRUnrefPMR */
typedef struct mt_ioctl_memory_free_in_s mt_ioctl_memory_free_in_t;
struct mt_ioctl_memory_free_in_s
{
    uint64_t hPMR;
};

/* Bridge out structure for PMRUnrefPMR */
typedef struct mt_ioctl_memory_free_out_s mt_ioctl_memory_free_out_t;
struct mt_ioctl_memory_free_out_s
{
    PVRSRV_ERROR eError;
};

/* Bridge in structure for PhysHeapGetMemInfo */
typedef struct _PHYS_HEAP_MEM_STATS_
{
    uint64_t ui64TotalSize;
    uint64_t ui64FreeSize;
    uint32_t ui32PhysHeapFlags;
} mt_ioctl_mem_stats_t;

typedef enum
{
    /* Services client accessible heaps */
    PVRSRV_PHYS_HEAP_DEFAULT = 0,     /* default phys heap for device memory allocations */
    PVRSRV_PHYS_HEAP_GPU_LOCAL = 1,   /* used for buffers with more GPU access than CPU */
    PVRSRV_PHYS_HEAP_CPU_LOCAL = 2,   /* used for buffers with more CPU access than GPU */
    PVRSRV_PHYS_HEAP_GPU_PRIVATE = 3, /* used for buffers that only required GPU read/write access, not visible to the CPU. */

    /* Services internal heaps */
    PVRSRV_PHYS_HEAP_FW_MAIN = 4,       /* runtime data, e.g. CCBs, sync objects */
    PVRSRV_PHYS_HEAP_EXTERNAL = 5,      /* used by some PMR import/export factories where the physical memory heap is not managed by the pvrsrv driver */
    PVRSRV_PHYS_HEAP_GPU_COHERENT = 6,  /* used for a cache coherent region */
    PVRSRV_PHYS_HEAP_GPU_SECURE = 7,    /* used by security validation */
    PVRSRV_PHYS_HEAP_FW_CONFIG = 8,     /* subheap of FW_MAIN, configuration data for FW init */
    PVRSRV_PHYS_HEAP_FW_CODE = 9,       /* used by security validation or dedicated fw */
    PVRSRV_PHYS_HEAP_FW_PRIV_DATA = 10, /* internal FW data (like the stack, FW control data structures, etc.) */
    PVRSRV_PHYS_HEAP_FW_PREMAP0 = 11,   /* Host OS premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP1 = 12,   /* Guest OS 1 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP2 = 13,   /* Guest OS 2 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP3 = 14,   /* Guest OS 3 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP4 = 15,   /* Guest OS 4 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP5 = 16,   /* Guest OS 5 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP6 = 17,   /* Guest OS 6 premap fw heap */
    PVRSRV_PHYS_HEAP_FW_PREMAP7 = 18,   /* Guest OS 7 premap fw heap */
    PVRSRV_PHYS_HEAP_LAST
} PVRSRV_PHYS_HEAP;

typedef struct mt_ioctl_memory_info_in_s mt_ioctl_memory_info_in_t;
struct mt_ioctl_memory_info_in_s
{
    mt_ioctl_mem_stats_t *pasapPhysHeapMemStats;
    PVRSRV_PHYS_HEAP *peaPhysHeapID;
    uint32_t ui32PhysHeapCount;
};

/* Bridge out structure for PhysHeapGetMemInfo */
typedef struct mt_ioctl_memory_info_out_s mt_ioctl_memory_info_out_t;
struct mt_ioctl_memory_info_out_s
{
    mt_ioctl_mem_stats_t *pasapPhysHeapMemStats;
    PVRSRV_ERROR eError;
};

/* Bridge in structure for RGXKickCDM2 */
typedef struct mt_ioctl_compute_in_s mt_ioctl_compute_in_t;
struct mt_ioctl_compute_in_s
{
    uint64_t ui64DeadlineInus;
    uint64_t hComputeContext;
    uint32_t *pui32ClientUpdateOffset;
    uint32_t *pui32ClientUpdateValue;
    uint32_t *pui32SyncPMRFlags;
    uint8_t *pui8DMCmd;
    char *puiUpdateFenceName;
    uint64_t *phClientUpdateUFOSyncPrimBlock;
    uint64_t *phSyncPMRs;
    int32_t hCheckFenceFd;
    int32_t hUpdateTimeline;
    uint32_t ui32ClientCacheOpSeqNum;
    uint32_t ui32ClientUpdateCount;
    uint32_t ui32CmdSize;
    uint32_t ui32ExtJobRef;
    uint32_t ui32NumOfWorkgroups;
    uint32_t ui32NumOfWorkitems;
    uint32_t ui32PDumpFlags;
    uint32_t ui32SyncPMRCount;
};

/* Bridge out structure for RGXKickCDM2 */
typedef struct mt_ioctl_compute_out_s mt_ioctl_compute_out_t;
struct mt_ioctl_compute_out_s
{
    PVRSRV_ERROR eError;
    int32_t hUpdateFence;
};

typedef struct mt_ioctl_rgxtq2_transfer2_in_s mt_ioctl_rgxtq2_transfer2_in_t;
struct mt_ioctl_rgxtq2_transfer2_in_s
{
    uint64_t ui64DeadlineInus;
    int32_t hTransferContext;
    uint32_t *pui32SyncPMRFlags;
    uint32_t *pui32UpdateSyncOffset;
    uint32_t *pui32UpdateValue;
    uint8_t *pui8FWCommand;
    char *puiUpdateFenceName;
    uint64_t *phSyncPMRs;
    uint64_t *phUpdateUFOSyncPrimBlock;
    int32_t hCheckFenceFD;
    int32_t hUpdateTimeline;
    uint32_t ui32Characteristic1;
    uint32_t ui32Characteristic2;
    uint32_t ui32ClientUpdateCount;
    uint32_t ui32CommandSize;
    uint32_t ui32ExternalJobReference;
    uint32_t ui32PDumpFlags;
    uint32_t ui32SyncPMRCount;
};

/* Bridge in structure for PhysHeapGetMemInfoPkd */
typedef struct _PHYS_HEAP_MEM_STATS_PKD_
{
    uint64_t ui64TotalSize;
    uint64_t ui64FreeSize;
    uint32_t ui32PhysHeapFlags;
    uint32_t ui32Dummy;
} mt_ioctl_mem_stats_pkd_t;

typedef struct mt_ioctl_memory_info_pkd_in_s mt_ioctl_memory_info_pkd_in_t;
struct mt_ioctl_memory_info_pkd_in_s
{
    mt_ioctl_mem_stats_pkd_t *pasapPhysHeapMemStats;
    PVRSRV_PHYS_HEAP *peaPhysHeapID;
    uint32_t ui32PhysHeapCount;
};

/* Bridge out structure for PhysHeapGetMemInfoPkd */
typedef struct mt_ioctl_memory_info_pkd_out_s mt_ioctl_memory_info_pkd_out_t;
struct mt_ioctl_memory_info_pkd_out_s
{
    mt_ioctl_mem_stats_pkd_t *pasapPhysHeapMemStats;
    PVRSRV_ERROR eError;
};

// for misc mtgpu device
typedef struct ipcMsgHdr_st
{
    uint64_t event_type : 5; /* type id with maxium 32 types messages */
    uint64_t event_id : 7;   /* event id, can put into SGI IRQ event */
    uint64_t event_pri : 2;  /* priority */
    uint64_t msg_id : 8;     /* msg_id, for a pair of request/complete message,the msg_id are same */
    uint64_t msg_type : 1;   /* 0: request message, 1: response message */
    uint64_t msg_sync : 1;   /* 0:asnyc, 1:sync */
    uint64_t msg_ack : 1;    /* the message ack */
    uint64_t response : 1;   /* 0:don't response, 1:need response message */
    uint64_t pack : 1;       /* 0 - one payload, 1 - multi-payload */
    uint64_t location : 2;   /* 0:payload place into share sram, 1:vDDR, 2:system DDR, 3:reserve */
    uint64_t source : 8;     /* souce ID */
    uint64_t target : 8;     /* target ID */
    uint64_t data_size : 8;  /* payload size, 1 unit means 1 uint32(4 bytes), minimum value is 1 */
    uint64_t rsv : 11;       /* reseved */
} ipcMsgHdr_t;

#define PCIE_IPC_MSG_MAX_DATA_SIZE (256)

/* use fixed payload data size, valid data size must not bigger than PCIE_IPC_MSG_MAX_DATA_SIZE */
typedef struct ipcMsg_st
{
    struct ipcMsgHdr_st header;                     /* message header */
    unsigned char data[PCIE_IPC_MSG_MAX_DATA_SIZE]; /* payload */
} ipcMsg_t;

//---------------------------------------------------------
// entryTable board info {
//---------------------------------------------------------
struct BoardSystemInfo
{
    uint8_t boardCapValidFlag_; /* 0xEE -valid, other: invalid*/
    uint8_t strapPinValid_;     /* 0:invalid 1:valid */
    uint8_t efuseValid_;        /* 0:invalid 1:valid */
    uint8_t boardType_;         /* */
    uint8_t primaryBootMode_;   /* 0-rom, 1-HW XIP */
    uint8_t nextBootMode_;      /* 0:flash 1:UART 2:BIF */
    uint8_t clockSet_;          /* 0-400M, 1-50M */
    uint8_t wdtSet_;            /* 0-disable, 1-enable */
    uint8_t uartBoundrate_;
    uint8_t signatureMode_; /* 0:crc 1:RSA-2048/SHA256 2:SM2/SM3 */
    uint8_t ipcType_;
    uint8_t pwm0Type_;
    uint8_t pwm1Type_;
    uint8_t pwm2Type_;
    uint8_t pvtType_;
    uint8_t spiType_;
    uint8_t uart0Type_;
    uint8_t uart1Type_;
    uint8_t pmucType_;
    uint8_t pmPolicy_;
    uint8_t timer0_;
    uint8_t timer1_;
    uint8_t wdt_;
    uint8_t rsv_;
};

struct PcieConfigureInfo
{
    uint16_t vendorId_;      /* pcie vendor id 0x1ED5 */
    uint16_t deviceIdPf0_;   /* GPU device ID */
    uint16_t deviceIdPf1_;   /* audio device ID */
    uint8_t generationType_; /* 0x0:Gen1 mode; 0x1:Gen2 mode; 0x2:Gen3 mode; 0x3:Gen4 mode. */
    uint8_t laneCount_;      /* 0x0:x1; 0x1:x2; 0x2:x4; 0x3:x8; 0x4:x16. */
    uint8_t resizeBarType_;
    uint8_t aspmCtrl_; /* 0-disable, 1- enable */
    uint8_t pcieIntTypePf0_;
    uint8_t pcieIntTypePf1_;
};

struct DdrConfigureInfo
{
    uint8_t llcType_;
    uint8_t supportMaxChannelCount_;
    uint8_t validChannelCount_;
    uint8_t validChannelMask_;
    uint8_t ddrInterleavingMode_;
    uint8_t ddrFrequency_;
    uint8_t ddrSize_;
    uint8_t ddrPcsCapacity_;
    uint8_t ddrPcsCount_;
    uint8_t ddrType_;
    uint8_t rsv_[2];
};

struct GpuConfigureInfo
{
    uint8_t gpuType_;
    uint8_t mcCoreCount_;
    uint8_t mcPrimaryCore_;
    uint8_t mcValidCore_;
    uint16_t gpuFrequency_; /* MHz*/
    uint8_t rsv_[2];
};

struct I2cConfigureInfo
{
    uint8_t i2c0Type_;
    uint8_t i2c0Speed_;
    uint8_t i2c1Type_;
    uint8_t i2c1Speed_;
    uint8_t i2csType_;
    uint8_t i2csSpeed_;
    uint8_t smbusBaseAddr_;
    uint8_t smbusStrapPin_;
};

struct DispConfigureInfo
{
    uint8_t dp0Type_;
    uint8_t dp0Ssc_;
    uint8_t dp0DisplayPriority_;
    uint8_t dp1Type_;
    uint8_t dp1Ssc_;
    uint8_t dp1DisplayPriority_;
    uint8_t dp2Type_;
    uint8_t dp2Ssc_;
    uint8_t dp2DisplayPriority_;
    uint8_t dp3Type_;
    uint8_t dp3Ssc_;
    uint8_t dp3DisplayPriority_;
    uint8_t hdmiType_;
    uint8_t hdmiDisplayPriority_;

    uint16_t disp0MaxHres_;
    uint16_t disp0MaxVres_;
    uint16_t disp0MaxClk_;
    uint16_t disp1MaxHres_;
    uint16_t disp1MaxVres_;
    uint16_t disp1MaxClk_;
    uint16_t disp2MaxHres_;
    uint16_t disp2MaxVres_;
    uint16_t disp2MaxClk_;
    uint16_t disp3MaxHres_;
    uint16_t disp3MaxVres_;
    uint16_t disp3MaxClk_;
    uint16_t rsv_;
};

/* board capabilities info entry */
typedef struct BoardCapInfo
{
    struct BoardSystemInfo boardSys_;  /* system info */
    struct PcieConfigureInfo pcieCfg_; /* pcie info */
    struct DdrConfigureInfo ddrCfg_;   /* ddr info */
    struct GpuConfigureInfo mcCfg_;    /* gpu info */
    struct I2cConfigureInfo i2cCfg_;   /* i2c info */
    struct DispConfigureInfo dispCfg_; /* dispalay info */
} boardCapInfo_t;


struct LogoCofigureInfoExtra
{
   uint32_t manuFacturer_;
   uint8_t manuDesc_[32];
   uint32_t vendor_;
   uint8_t vendorDesc_[32];
   uint32_t gpuModel_;
   uint8_t gpuModelDesc_[32];
   uint32_t board_;
   uint8_t boardDesc_[32];
   uint32_t subVendor_;
   uint8_t subVendorDesc_[32];
   uint32_t subId_;
   uint8_t subIdDesc_[32];
   uint8_t rsv_[128];
};

struct BoardSystemInfoExtra
{
    uint8_t boardCapValidFlag_;    /* 0xEE -valid, other: invalid*/
    uint8_t strapPinValid_;        /* 0:invalid 1:valid */
    uint8_t efuseValid_;           /* 0:invalid 1:valid */
    uint8_t pad_;                  /* for padding */
    uint32_t boardType_;            /* 0x10 start */
    uint8_t packageType_;          /* 1-small pkg M2022; 0-big pkg M2021 */
    uint8_t nextBootMode_;         /* 0:flash 1:UART 2:BIF */
    uint8_t clockSet_;             /* 0-25M, 1-400M */
    uint8_t wdtSet_;               /* 0-disable, 1-enable */
    uint8_t uartBoundrate_;        /* 0-115200bps, 1-921600bps */
    uint8_t signatureMode_;        /* 0:crc 1:RSA-2048/SHA256 2:SM2/SM3 */
    uint8_t ipcType_;
    uint8_t pwm0Type_;
    uint8_t pwm1Type_;
    uint8_t pwm2Type_;
    uint8_t pvtType_;
    uint8_t spiType_;
    uint8_t uart0Type_;
    uint8_t uart1Type_;
    uint8_t pmucType_;
    uint8_t pmPolicy_;
    uint8_t timer0_;
    uint8_t timer1_;
    uint16_t sysPllSet_;
    uint8_t rsv_[64];
};

struct PcieConfigureInfoExtra
{
    uint16_t vendorId_;         /* pcie vendor id 0x1ED5 */
    uint16_t deviceIdPf0_;      /* GPU device ID */
    uint16_t deviceIdPf1_;      /* audio device ID */
    uint8_t generationType_;   /* 0x0:Gen1 mode; 0x1:Gen2 mode; 0x2:Gen3 mode; 0x3:Gen4 mode. */
    uint8_t laneCount_;        /* 0x0:x1; 0x1:x2; 0x2:x4; 0x3:x8; 0x4:x16. */
    uint8_t resizeBarType_;    /* Bar0-5 size of pf0 is controlled by resize bar Ctrl reg */
    uint8_t aspmCtrl_;         /* 0-disable, 1- enable */
    uint8_t pcieIntTypePf0_;
    uint8_t pcieIntTypePf1_;
    uint8_t pcieEnablePf0_;
    uint8_t pcieEnablePf1_;
    uint8_t pad_[2];
    uint32_t classCode_;
    uint8_t rsv_[64];
};

struct DdrConfigureInfoExtra
{
    uint8_t llcType_;
    uint8_t supportMaxChannelCount_;
    uint8_t validChannelCount_;
    uint8_t memType_;
    uint32_t validChannelMask_;
    uint16_t ddrFrequency_;
    uint8_t ddrInterleavingMode_;
    uint8_t ddrSize_;
    uint8_t ddrPcsCapacity_;
    uint8_t ddrPcsCount_;
    uint8_t ddrType_;
    uint8_t rsv_[33];
};

struct GpuConfigureInfoExtra
{
    uint32_t gpuType_;
    uint32_t mcValidCore_;
    uint16_t gpuFrequency_;         /* MHz*/
    uint8_t mcCoreCount_;
    uint8_t rsv_[33];
};

struct I2cConfigureInfoExtra
{
    uint8_t i2c0Type_;
    uint8_t i2c0Speed_;
    uint16_t i2c0Addr_;
    uint8_t i2c1Type_;
    uint8_t i2c1Speed_;
    uint16_t i2c1Addr_;
    uint8_t i2csType_;
    uint8_t i2csSpeed_;
    uint8_t smbusBaseAddr_;
    uint8_t smbusStrapPin_;
    uint8_t rsv_[32];
};

struct DispConfigureInfoExtra
{
    uint8_t dp0Type_;
    uint8_t dp0Ssc_;
    uint8_t dp0DisplayPriority_;
    uint8_t dp1Type_;
    uint8_t dp1Ssc_;
    uint8_t dp1DisplayPriority_;
    uint8_t dp2Type_;
    uint8_t dp2Ssc_;
    uint8_t dp2DisplayPriority_;
    uint8_t dp3Type_;
    uint8_t dp3Ssc_;
    uint8_t dp3DisplayPriority_;
    uint8_t hdmiType_;              /* QY not support */
    uint8_t hdmiDisplayPriority_;   /* QY not support */
    uint16_t disp0MaxHres_;
    uint16_t disp0MaxVres_;
    uint16_t disp0MaxClk_;
    uint16_t disp1MaxHres_;
    uint16_t disp1MaxVres_;
    uint16_t disp1MaxClk_;
    uint16_t disp2MaxHres_;
    uint16_t disp2MaxVres_;
    uint16_t disp2MaxClk_;
    uint16_t disp3MaxHres_;
    uint16_t disp3MaxVres_;
    uint16_t disp3MaxClk_;
    uint8_t rsv_[66];
};

typedef struct BoardCapInfoExtra
{
    struct BoardSystemInfoExtra boardSys_;  /* system info */
    struct PcieConfigureInfoExtra pcieCfg_; /* pcie info */
    struct DdrConfigureInfoExtra ddrCfg_;   /* ddr info */
    struct GpuConfigureInfoExtra mcCfg_;    /* gpu info */
    struct LogoCofigureInfoExtra logoCfg_;         /* OEM info */
    uint8_t data_[4];                      /* for extension in future */
} boardCapInfoExtra_t;

typedef struct DeviceInfoMsg {
    uint64_t infoBuff_;
    uint32_t reserved_;
    uint32_t status_;
} deviceInfoMsg_t;
