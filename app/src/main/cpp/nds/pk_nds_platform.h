// What melonDS's Platform layer (pk_nds_platform.cpp) calls back into: the embedder's save writer,
// stop notice and log sink. A PkNdsHost is the userdata passed to melonDS::NDS.
#pragma once

#include <cstdint>

struct PkNdsHost {
    // The game wrote its save: savedata is the whole save memory (savelen bytes), of which
    // [writeoffset, writeoffset + writelen) changed.
    void (*onSave)(PkNdsHost* host, const uint8_t* savedata, uint32_t savelen, uint32_t writeoffset, uint32_t writelen);
    // The emulated DS stopped (powered off, a bad exception region).
    void (*onStop)(PkNdsHost* host, int reason);
    void* user;
};

typedef void (*PkNdsLogFn)(int level, const char* message);

// Where melonDS's log lines go (null: dropped).
void pkNdsSetLog(PkNdsLogFn fn);
