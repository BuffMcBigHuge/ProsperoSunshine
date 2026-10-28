# SPDX-License-Identifier: GPL-3.0-only
"""Resolve libSceAvcap2 export NIDs by hashing generated candidate names.

Reads the export NIDs from .cache/re/libSceAvcap2.elf (never embeds them) and
tries `sceAvcap2` + verb + noun (+ suffix) combinations. Prints matches.
"""
import sys, os, itertools
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from elfdump import Elf, nid

CACHE = os.path.join(os.path.dirname(__file__), "..", "..", ".cache", "re")
VERBS = """Open Close Read Get Set Start Stop Request Force Insert Generate Update Change Reset Flush Pause Resume Query
Register Unregister Initialize Terminate Enable Disable Notify Wait Poll Release Acquire Map Unmap Allocate Free Create
Destroy Configure Encode Send Submit Cancel Abort Check Is Has Add Remove Clear Begin End Lock Unlock Attach Detach Bind
Unbind Select Skip Drop Insert Mark Invalidate Refresh Sync Apply Load Save Prepare Finish Take Capture Snapshot
Control Adjust Modify Switch Toggle Mute Unmute Trigger Signal Post Peek Fetch Retrieve Take Push Pop Dequeue Enqueue
Reserve Commit Setup Cleanup Dump Report Reconfigure Restart Reopen Pend Join Leave Subscribe Unsubscribe""".split()
NOUNS = """Video Audio Idr IdrFrame KeyFrame Keyframe Bitrate BitRate FrameRate Framerate Fps Config Param Params Parameter
Parameters VideoParam VideoParams AudioParam AudioParams EncodeParam EncodeParams EncoderParam EncoderParams
EncoderConfig EncodeConfig VideoConfig AudioConfig Resolution Stream NalUnitStream NonVclNalUnitStream VideoOutMode
AudioOutMode Status Info Event Callback Buffer Memory Handle Channel Frame Picture Gop GopSize IdrInterval
IntraFrame IntraRefresh ReferenceFrame ReferenceFrameInvalidation Rfi RateControl RateControlConfig
PictureConfig VideoEncodeParam VideoEncoderParam VideoBitrate VideoBitRate VideoFrameRate AudioBitrate AudioFormat
Timestamp Pts Time Clock Mode Capability Capabilities Version Header HeaderStream Sps Pps Vps Nal NalUnit
Packet Sample Samples Data Source Target Region Area Mic Microphone Voice Speaker Output Input Device Session
VideoStream AudioStream VideoFrame AudioFrame EventFlag Queue Ready Readiness Handles Wait VideoMemory FrameBuffer
DirectMemory ExternalMemory DmaBuffer Privacy Mute Volume Level Filter Format Codec Encoder Decoder Quality
Preset Profile Statistics Stats Counter Error LastError Result Fence Vsync Sync Mask Flag Flags Option Options
Property Properties Attribute Attributes Setting Settings Limit Limits Range Size Length Count Index Id Name
Priority Policy Scene Screen ScanIn ScanInMode Output Display Hdr Sdr ColorSpace Color Cursor Overlay
BitrateMode QpRange Qp Slice Level Tier Cabac Entropy Bframe BFrames Ref RefFrames NumRef Lookahead
Temporal TemporalLayer Layer SvcLayer""".split()
SUFFIXES = ["", "2", "Ex", "Internal", "Async", "Sync", "ForRemotePlay", "ForGameLiveStreaming"]
EXTRA = """sceAvcap2OpenAudio sceAvcap2ReadAudio sceAvcap2OpenHaptic sceAvcap2OpenMicrophone sceAvcap2OpenSnapshot
sceAvcap2OpenVideoSecond sceAvcap2WaitForData sceAvcap2WaitData sceAvcap2WaitReady sceAvcap2WaitHandles
sceAvcap2GetEventFlag sceAvcap2PollData sceAvcap2Poll sceAvcap2Select sceAvcap2GetVideoMemory sceAvcap2GetVideoBuffer
sceAvcap2ClearEventFlag sceAvcap2CloseAudio sceAvcap2StartAudio sceAvcap2StopAudio sceAvcap2ForceIdr
sceAvcap2RequestIdr sceAvcap2RequestIdrFrame sceAvcap2RequestKeyFrame sceAvcap2InsertIdr sceAvcap2SetBitrate
sceAvcap2SetVideoBitrate sceAvcap2ChangeBitrate sceAvcap2ChangeVideoBitrate sceAvcap2SetVideoEncodeParam
sceAvcap2SetEncodeParam sceAvcap2SetVideoEncoderParam sceAvcap2UpdateVideoParam sceAvcap2UpdateEncodeParam
sceAvcap2ChangeVideoParam sceAvcap2SetRateControl sceAvcap2SetRateControlConfig sceAvcap2SetReferenceFrameInvalidation
sceAvcap2InvalidateReferenceFrame sceAvcap2InvalidateReferenceFrames sceAvcap2SetFrameRate sceAvcap2SetPictureConfig
sceAvcap2GetVideoParam sceAvcap2GetVideoConfig sceAvcap2GetAudioOutMode sceAvcap2GetAudioParam
sceAvcap2GetNalUnitStream sceAvcap2GetNonVclNalUnit sceAvcap2GetHeader sceAvcap2GetVideoHeader
sceAvcap2ChangeScanInMode sceAvcap2SetScanInMode sceAvcap2SetOutputMode sceAvcap2NotifyOutputMode""".split()
def candidates():
    for n in EXTRA: yield n
    for v in VERBS:
        for n in NOUNS:
            for s in SUFFIXES:
                yield "sceAvcap2" + v + n + s
        yield "sceAvcap2" + v
    for a in ("Video", "Audio"):
        for v in VERBS:
            for n in NOUNS:
                yield "sceAvcap2" + v + a + n
                yield "sceAvcap2" + v + n + a

if __name__ == "__main__":
    lib = Elf(os.path.join(CACHE, "libSceAvcap2.elf"))
    want = {s["name"].split("#")[0] for s in lib.symbols() if s["value"]}
    found = {}
    total = 0
    for c in candidates():
        total += 1
        h = nid(c)
        if h in want and h not in found:
            found[h] = c
            print("%s %s" % (h, c))
    print("tried %d candidates, %d/%d resolved" % (total, len(found), len(want)), file=sys.stderr)
