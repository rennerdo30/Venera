/*
 * Copyright 2026 Ake Rehnman <ake.rehnman@gmail.com>
 * SPDX-License-Identifier: MPL-2.0
 * 
 */

#pragma once

/* The lowest refresh rate (Hz) the display reports and its vblank timer runs at. A host
 * monitor with a higher rate raises it (from the EDID's preferred timing, see
 * UpdateRefreshFromEdid); never lower: the vblank paces the guest's compositor, and the host
 * shows the newest frame at its own refresh anyway. */
#ifndef VIOGPU_REFRESH_HZ
#define VIOGPU_REFRESH_HZ 240
#endif
#define VIOGPU_REFRESH_HZ_MAX 1000

#include "helper.h"
#include "viogpu.h"

class VioGpuAdapter;
class VioGpuAllocation;
class VioGpuObj;

typedef struct _CURRENT_MODE
{
    DXGK_DISPLAY_INFORMATION DispInfo;
    D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation;
    D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling;
    UINT SrcModeWidth;
    UINT SrcModeHeight;
    struct _CURRENT_MODE_FLAGS
    {
        UINT SourceNotVisible : 1;
        UINT FullscreenPresent : 1;
        UINT FrameBufferIsActive : 1;
        UINT DoNotMapOrUnmap : 1;
        UINT IsInternal : 1;
        UINT Unused : 27;
    } Flags;

    PHYSICAL_ADDRESS ZeroedOutStart;
    PHYSICAL_ADDRESS ZeroedOutEnd;

    union {
        VOID *Ptr;
        ULONG64 Force8Bytes;
    } FrameBuffer;
} CURRENT_MODE;

class VioGpuVidPN
{
  public:
    VioGpuVidPN(VioGpuAdapter *adapter);
    ~VioGpuVidPN();

    NTSTATUS Start(ULONG *pNumberOfViews, ULONG *pNumberOfChildren);
    VOID StartVsyncTimer(void);
    VOID StopVsyncTimer(void);
    NTSTATUS Stop(void);
    NTSTATUS AcquirePostDisplayOwnership();
    void ReleasePostDisplayOwnership(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, DXGK_DISPLAY_INFORMATION *pDisplayInfo);
    void Powerdown();

    NTSTATUS IsVidPnSourceModeFieldsValid(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode) const;
    NTSTATUS IsVidPnPathFieldsValid(CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath) const;

    NTSTATUS CommitVidPn(_In_ CONST DXGKARG_COMMITVIDPN *CONST pCommitVidPn);
    NTSTATUS
    UpdateActiveVidPnPresentPath(_In_ CONST DXGKARG_UPDATEACTIVEVIDPNPRESENTPATH *CONST pUpdateActiveVidPnPresentPath);

    NTSTATUS SetCurrentMode(ULONG Mode, CURRENT_MODE *pCurrentMode);
    CONST CURRENT_MODE *GetCurrentMode(D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId) const
    {
        return &m_CurrentModes[SourceId];
    }
    ULONG GetModeCount(void)
    {
        return m_ModeCount;
    }
    VOID BlackOutScreen(CURRENT_MODE *pCurrentMod);

    NTSTATUS GetModeList(DXGK_DISPLAY_INFORMATION *pDispInfo);

    void CreateFrameBufferObj(PVIDEO_MODE_INFORMATION pModeInfo, CURRENT_MODE *pCurrentMode);
    void DestroyFrameBufferObj(BOOLEAN bReset);

    BOOLEAN GpuObjectAttach(UINT res_id, VioGpuObj *obj);
    PBYTE GetEdidData(UINT Idx);

    PBYTE GetCTA861Data(void);
    void SetVideoModeInfo(UINT Idx, PVIOGPU_DISP_MODE pModeInfo);
    BOOLEAN GetDisplayInfo(void);
    int ProcessEdid(void);
    /* The refresh rate in use (Hz): reported in every mode and the vblank timer's rate. */
    ULONG RefreshHz(void) const
    {
        return m_refreshHz;
    }
    void FixEdid(void);
    BOOLEAN GetEdids(void);
    int AddEdidModes(void);
    BOOLEAN UpdateModes(USHORT xres, USHORT yres, int &cnt);
    void SetCustomDisplay(_In_ USHORT xres, _In_ USHORT yres);

    NTSTATUS EscapeCustomResoulution(VIOGPU_DISP_MODE *resolution);

    NTSTATUS IsSupportedVidPn(_Inout_ DXGKARG_ISSUPPORTEDVIDPN *pIsSupportedVidPn);
    NTSTATUS RecommendFunctionalVidPn(_In_ CONST DXGKARG_RECOMMENDFUNCTIONALVIDPN *CONST pRecommendFunctionalVidPn);
    NTSTATUS RecommendVidPnTopology(_In_ CONST DXGKARG_RECOMMENDVIDPNTOPOLOGY *CONST pRecommendVidPnTopology);
    NTSTATUS RecommendMonitorModes(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes);
    NTSTATUS EnumVidPnCofuncModality(_In_ CONST DXGKARG_ENUMVIDPNCOFUNCMODALITY *CONST pEnumCofuncModality);
    NTSTATUS SetVidPnSourceVisibility(_In_ CONST DXGKARG_SETVIDPNSOURCEVISIBILITY *pSetVidPnSourceVisibility);
    NTSTATUS QueryVidPnHWCapability(_Inout_ DXGKARG_QUERYVIDPNHWCAPABILITY *pVidPnHWCaps);

    NTSTATUS SystemDisplayEnable(_In_ D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId,
                                 _In_ PDXGKARG_SYSTEM_DISPLAY_ENABLE_FLAGS Flags,
                                 _Out_ UINT *pWidth,
                                 _Out_ UINT *pHeight,
                                 _Out_ D3DDDIFORMAT *pColorFormat);
    VOID SystemDisplayWrite(_In_reads_bytes_(SourceHeight *SourceStride) VOID *pSource,
                            _In_ UINT SourceWidth,
                            _In_ UINT SourceHeight,
                            _In_ UINT SourceStride,
                            _In_ INT PositionX,
                            _In_ INT PositionY);

    void Flip();
    static void VsyncNotifyTimerDpc(KDPC *dpc, PVOID deferredContext, PVOID systemArg1, PVOID systemArg2);
    PHYSICAL_ADDRESS GetCurrentSourceAddress() const
    {
        return m_sourceAddress;
    }
    
    /* res_id a flip's DMA buffer last put on the scanout and flushed, until the
     * vsync Flip() consumes it; 0 after any other scanout change. */
    volatile LONG m_flippedResId = 0;

    NTSTATUS SetVidPnSourceAddress(const DXGKARG_SETVIDPNSOURCEADDRESS *pSetVidPnSourceAddress);

  private:
    void UpdateRefreshFromEdid(void);
    ULONG m_refreshHz = VIOGPU_REFRESH_HZ;
    /* One vblank period at m_refreshHz (100 ns units). */
    LONGLONG m_period100ns = 10000000LL / VIOGPU_REFRESH_HZ;
    NTSTATUS SetSourceModeAndPath(CONST D3DKMDT_VIDPN_SOURCE_MODE *pSourceMode,
                                  CONST D3DKMDT_VIDPN_PRESENT_PATH *pPath);
    NTSTATUS AddSingleMonitorMode(_In_ CONST DXGKARG_RECOMMENDMONITORMODES *CONST pRecommendMonitorModes);
    NTSTATUS AddSingleSourceMode(_In_ CONST DXGK_VIDPNSOURCEMODESET_INTERFACE *pVidPnSourceModeSetInterface,
                                 D3DKMDT_HVIDPNSOURCEMODESET hVidPnSourceModeSet,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    NTSTATUS AddSingleTargetMode(_In_ CONST DXGK_VIDPNTARGETMODESET_INTERFACE *pVidPnTargetModeSetInterface,
                                 D3DKMDT_HVIDPNTARGETMODESET hVidPnTargetModeSet,
                                 _In_opt_ CONST D3DKMDT_VIDPN_SOURCE_MODE *pVidPnPinnedSourceModeInfo,
                                 D3DDDI_VIDEO_PRESENT_SOURCE_ID SourceId);
    D3DDDI_VIDEO_PRESENT_SOURCE_ID FindSourceForTarget(D3DDDI_VIDEO_PRESENT_TARGET_ID TargetId, BOOLEAN DefaultToZero);
    VOID BuildVideoSignalInfo(D3DKMDT_VIDEO_SIGNAL_INFO *pVideoSignalInfo, PVIDEO_MODE_INFORMATION pModeInfo);

    VioGpuAdapter *m_pAdapter;
    DXGKRNL_INTERFACE *m_pDxgkInterface;

    CURRENT_MODE m_CurrentModes[MAX_VIEWS];

    PVIDEO_MODE_INFORMATION m_ModeInfo;
    ULONG m_ModeCount;
    PUSHORT m_ModeNumbers;
    USHORT m_CurrentModeIndex;
    USHORT m_CustomModeIndex;
    BYTE m_EDIDs[MAX_CHILDREN][EDID_RAW_BLOCK_SIZE];
    BOOLEAN m_bEDID;

    DXGK_DISPLAY_INFORMATION m_SystemDisplayInfo;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID m_SystemDisplaySourceId;

    VioGpuObj *m_pFrameBuf;

    PHYSICAL_ADDRESS m_sourceAddress = {0};
    VioGpuAllocation *m_sourceRes = NULL;
    volatile LONG m_shouldFlip = 0;
    /* Every flip is shown and reported from this DPC right away (see FlipDpc). */
    KDPC m_flipDpc;
    static void FlipDpc(KDPC *dpc, PVOID deferredContext, PVOID systemArg1, PVOID systemArg2);
    /* The pending flip asked for FlipImmediate (shown even inside the minimum vblank interval). */
    volatile LONG m_immediatePending = 0;
    /* When the last vblank was reported (system time, 100 ns); under m_vsyncTimerLock. */
    LONGLONG m_lastVblank = 0;
    /* Reports a vblank now unless the last one was less than a period ago; re-arms the idle tick. */
    void VblankIfDue(bool fromFlip);


    volatile LONG m_shouldFlipStop = 1;
    KSPIN_LOCK m_vsyncTimerLock;
    KTIMER m_vsyncNotifyTimer;
    KDPC m_vsyncNotifyDpc;
    ULONG m_timerRes = 0;
    LARGE_INTEGER next_vsync_time = {0};
};
