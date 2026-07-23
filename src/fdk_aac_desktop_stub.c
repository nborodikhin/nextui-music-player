// Desktop-only stub for the FDK-AAC decoder.
//
// Fraunhofer's libfdk-aac is not carried by Debian (its license is not
// DFSG-compatible) and is not built for the `desktop` platform target, so
// M4A/AAC playback is unavailable there. The FDK headers are still bundled in
// include/fdk-aac/, so player.c compiles unchanged; this translation unit just
// satisfies the linker in place of libfdk-aac.
//
// aacDecoder_Open() returns NULL, which drives player.c's existing "failed to
// init AAC decoder" error path — M4A/AAC files are rejected cleanly at open
// time and every other format keeps working. The remaining stubs exist only so
// the switch-case bodies that reference these symbols link; they are never
// reached at runtime because no decoder handle is ever created.
//
// Compiled only for PLATFORM=desktop (see src/Makefile); device builds link the
// real libfdk-aac.

#include <stddef.h>
#include <fdk-aac/aacdecoder_lib.h>

HANDLE_AACDECODER aacDecoder_Open(TRANSPORT_TYPE transportFmt, UINT nrOfLayers) {
    (void)transportFmt;
    (void)nrOfLayers;
    return NULL;
}

AAC_DECODER_ERROR aacDecoder_ConfigRaw(HANDLE_AACDECODER self, UCHAR* conf[],
                                       const UINT length[]) {
    (void)self;
    (void)conf;
    (void)length;
    return AAC_DEC_UNKNOWN;
}

AAC_DECODER_ERROR aacDecoder_Fill(HANDLE_AACDECODER self, UCHAR* pBuffer[],
                                  const UINT bufferSize[], UINT* bytesValid) {
    (void)self;
    (void)pBuffer;
    (void)bufferSize;
    (void)bytesValid;
    return AAC_DEC_UNKNOWN;
}

AAC_DECODER_ERROR aacDecoder_DecodeFrame(HANDLE_AACDECODER self, INT_PCM* pTimeData,
                                         const INT timeDataSize, const UINT flags) {
    (void)self;
    (void)pTimeData;
    (void)timeDataSize;
    (void)flags;
    return AAC_DEC_UNKNOWN;
}

AAC_DECODER_ERROR aacDecoder_SetParam(const HANDLE_AACDECODER self,
                                      const AACDEC_PARAM param, const INT value) {
    (void)self;
    (void)param;
    (void)value;
    return AAC_DEC_UNKNOWN;
}

CStreamInfo* aacDecoder_GetStreamInfo(HANDLE_AACDECODER self) {
    (void)self;
    return NULL;
}

void aacDecoder_Close(HANDLE_AACDECODER self) {
    (void)self;
}
