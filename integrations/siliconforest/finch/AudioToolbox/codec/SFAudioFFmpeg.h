/* SPDX-License-Identifier: MIT */
#pragma once
#include <AudioToolbox/AudioToolbox.h>
#include <stddef.h>
#include <stdint.h>
typedef struct SFDecoder SFDecoder;
typedef struct {
    AudioFileTypeID type;
    AudioStreamBasicDescription format;
    AudioStreamPacketDescription *packets;
    uint8_t *data;
    size_t dataSize, packetCount;
    uint32_t maximumPacket;
    CFDataRef cookie;
    AudioFilePacketTableInfo packetInfo;
} SFCompressedFile;
bool SFCompressedFormat(AudioFormatID);
OSStatus SFCompressedOpen(AudioFile_ReadProc, void *, int64_t, SFCompressedFile *);
SFDecoder *SFDecoderNew(const AudioStreamBasicDescription *);
void SFDecoderDispose(SFDecoder *);
void SFDecoderReset(SFDecoder *);
OSStatus SFDecoderCookie(SFDecoder *, UInt32, const void *);
OSStatus SFDecoderRead(SFDecoder *, AudioConverterRef, AudioConverterComplexInputDataProc, void *, UInt32 *, AudioBufferList *);
void SFDecoderSkip(SFDecoder *, uint64_t);
void SFCodecPrimeConverter(AudioConverterRef, uint64_t);
OSStatus SFDecoderCopyCookie(SFDecoder *, UInt32 *, void *);
