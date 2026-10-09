// The one translation unit that compiles miniaudio's implementation, with stb_vorbis for OGG
// Vorbis decoding. Order matters (from miniaudio's docs): stb_vorbis declarations first, then
// miniaudio (which sees them and enables its Vorbis decoder), then stb_vorbis' implementation.
#define STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING     // we only play sounds
#define MA_NO_GENERATION   // no built-in waveform/noise generators
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "stb_vorbis.c"
