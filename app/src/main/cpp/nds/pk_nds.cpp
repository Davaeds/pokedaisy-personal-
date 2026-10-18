// The app's Nintendo DS core: melonDS (third_party/melonds, 1.1) behind pk_nds.h. See pk_nds.h for who calls what.
//
// Saves: the game writes its flash a sector at a time over many frames; each write marks the save dirty
// (pk_nds_platform.cpp's onSave), and it reaches the file once the writes have been quiet for SAVE_QUIET_FRAMES
// (or at pk_nds_flush_save: a state, a close). The whole file is rewritten each time - 512 KB for Gen 4.
//
// States: melonDS's own format behind a small header of ours carrying the save memory's CRC32 when the state was
// made, so pk_nds_state_matches_save can tell (like the GBA's pkStateMatchesSave) whether loading it would put an
// older save back: melonDS states carry the cart's save memory, and a load writes it to the file here as mGBA does.

#include "pk_nds.h"
#include "pk_nds_platform.h"
#include "pk_nds_field.h"
#include "pk_nds_call.h"
#include "pk_nds_wide.h"
#include "pk_nds_music.h"

#include <NDS.h>
#include <NDSCart.h>
#include <GPU.h>
#include <GPU3D_Soft.h>
#include <SPU.h>
#include <RTC.h>
#include <Savestate.h>
#include <Args.h>

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

#include <zlib.h>

#define LOG_TAG "pokedaisy/nds"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

using namespace melonDS;

namespace {

constexpr int SAVE_QUIET_FRAMES = 30;
constexpr u32 STATE_MAGIC = 0x53444B50; // "PKDS"
constexpr u32 STATE_VERSION = 1;
constexpr u32 ROM_WINDOW = 0x08000000; // pk_nds_read: the cart's ROM file from here

struct Ds {
    NDS* nds = nullptr;
    PkNdsHost host {};
    std::string savePath;
    bool saveReadOnly = false;
    std::vector<u8> saveCopy;      // the save memory as the game last wrote it
    bool saveDirty = false;
    int saveQuiet = 0;             // frames since the last write
    u32 keys = 0;
    u32 top[PK_NDS_W * PK_NDS_H] {};
    u32 bottom[PK_NDS_W * PK_NDS_H] {};
    std::vector<s16> audio;
    int audioFrames = 0;
    int sampleRate = 48000;
    const PkNdsFieldGame* field = nullptr;   // the field read's table entry for this cart, if any
    bool wide = false;                       // EXPAND on (pk_nds_wide_enable)
    bool draw = true, drawNext = true;       // this frame / the next one is shown (pk_nds_set_draw: fast-forward's skips)
    int fieldBits = 0;                       // the field read after the last frame
    bool wildOff = false;                    // WILD ENCOUNTERS OFF (pk_nds_field.cpp holds the map's rates at 0)
};

Ds* ds = nullptr;
// The stylus, set from any thread (the bottom screen's view): bit 31 = down, x in bits 8-15, y in bits 0-7.
std::atomic<u32> touch {0};

void onSave(PkNdsHost* host, const uint8_t* data, uint32_t len, uint32_t, uint32_t)
{
    if (!ds || host != &ds->host) return;
    ds->saveCopy.assign(data, data + len);
    ds->saveDirty = true;
    ds->saveQuiet = 0;
}

void onStop(PkNdsHost*, int reason) { LOGI("DS stopped (reason %d)", reason); }

void onLog(int level, const char* msg)
{
    if (level >= Platform::Warn) __android_log_print(level >= Platform::Error ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN, LOG_TAG, "%s", msg);
}

bool readFile(const char* path, std::vector<u8>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? n : 0);
    bool ok = n <= 0 || fread(out.data(), 1, n, f) == (size_t) n;
    fclose(f);
    return ok;
}

bool writeSaveFile()
{
    if (ds->savePath.empty() || ds->saveReadOnly || ds->saveCopy.empty()) return false;
    FILE* f = fopen(ds->savePath.c_str(), "wb");
    if (!f) {
        LOGE("can't write the save %s", ds->savePath.c_str());
        return false;
    }
    bool ok = fwrite(ds->saveCopy.data(), 1, ds->saveCopy.size(), f) == ds->saveCopy.size();
    ok = fflush(f) == 0 && ok;
    fclose(f);
    return ok;
}

// melonDS draws 0xAARRGGBB words; the screen (EmulatorView) uploads RGBA bytes, i.e. 0xAABBGGRR words.
void convert(const u32* src, u32* dst)
{
    if (!src) return;
    for (int i = 0; i < PK_NDS_W * PK_NDS_H; i++) {
        u32 p = src[i];
        dst[i] = 0xFF000000u | ((p & 0xFF) << 16) | (p & 0xFF00) | ((p >> 16) & 0xFF);
    }
}

u32 saveCrc(const u8* data, size_t len) { return (u32) crc32(0, data, (uInt) len); }

void setClock()
{
    time_t now = time(nullptr);
    struct tm t {};
    localtime_r(&now, &t);
    ds->nds->RTC.SetDateTime(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec);
}

} // namespace

extern "C" {

bool pk_nds_is_rom(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    u8 h[0x160];
    bool ok = fread(h, 1, sizeof(h), f) == sizeof(h);
    fclose(f);
    // The Nintendo logo's CRC16, the same on every DS cart.
    return ok && h[0x15C] == 0x56 && h[0x15D] == 0xCF;
}

bool pk_nds_init(const char* romPath, const char* savePath, int sampleRate)
{
    pk_nds_deinit();
    // Read once, into the buffer melonDS keeps (a DS ROM is up to 512 MB: no second copy).
    std::unique_ptr<u8[]> rom;
    u32 romLen = 0;
    if (FILE* f = fopen(romPath, "rb")) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (n >= 0x200 && n <= (512L << 20)) {
            rom = std::make_unique<u8[]>((size_t) n);
            romLen = fread(rom.get(), 1, (size_t) n, f) == (size_t) n ? (u32) n : 0;
        }
        fclose(f);
    }
    if (!romLen) {
        LOGE("can't read the ROM");
        return false;
    }
    ds = new Ds();
    ds->host = PkNdsHost {onSave, onStop, nullptr};
    ds->sampleRate = sampleRate;
    pkNdsSetLog(onLog);

    std::vector<u8> save;
    if (savePath) {
        ds->savePath = savePath;
        readFile(savePath, save);
        FILE* probe = fopen(savePath, save.empty() ? "ab" : "r+b");
        ds->saveReadOnly = probe == nullptr;
        if (probe) fclose(probe);
        if (ds->saveReadOnly) LOGI("save opened read-only (new saves won't persist): %s", savePath);
    }

    NDSArgs args;
    args.OutputSampleRate = sampleRate;
    // The JIT without fast memory: fast memory catches faults with a signal handler, which would sit beside
    // crash reporting's and pk_wide.c's.
    args.JIT = JITArgs {32, true, true, false};
    ds->nds = new NDS(std::move(args), &ds->host);
    ds->nds->Reset();

    NDSCart::NDSCartArgs cartArgs;
    if (!save.empty()) {
        cartArgs.SRAM = std::make_unique<u8[]>(save.size());
        memcpy(cartArgs.SRAM.get(), save.data(), save.size());
        cartArgs.SRAMLength = (u32) save.size();
    }
    auto cart = NDSCart::ParseROM(std::move(rom), romLen, &ds->host, std::move(cartArgs));
    if (!cart) {
        LOGE("not a DS ROM melonDS can run");
        pk_nds_deinit();
        return false;
    }
    ds->nds->SetNDSCart(std::move(cart));
    ds->nds->Reset();
    ds->nds->SetupDirectBoot("rom.nds");
    static_cast<SoftRenderer&>(ds->nds->GPU.GetRenderer3D()).SetThreaded(true, ds->nds->GPU);
    setClock();
    ds->nds->Start();
    if (const u8* mem = ds->nds->GetNDSSave()) ds->saveCopy.assign(mem, mem + ds->nds->GetNDSSaveLength());
    ds->audio.resize(8192 * 2);
    {
        const auto& h = ds->nds->NDSCartSlot.GetCart()->GetHeader();
        ds->field = pk_nds_field_game(h.GameCode, h.ROMVersion);
    }
    LOGI("DS up: save %u bytes, %d Hz", ds->nds->GetNDSSaveLength(), sampleRate);
    return true;
}

void pk_nds_deinit(void)
{
    if (!ds) return;
    if (ds->nds) {
        pk_nds_flush_save();
        pk_nds_wide_set(nullptr, false);
        pk_nds_music_end(true);
        ds->nds->Stop();
        delete ds->nds;
    }
    delete ds;
    ds = nullptr;
    touch.store(0);
}

bool pk_nds_active(void) { return ds != nullptr; }

void pk_nds_set_keys(uint32_t keys)
{
    if (ds) ds->keys = keys & 0xFFF;
}

void pk_nds_touch(bool down, int x, int y)
{
    u32 cx = x < 0 ? 0 : x > PK_NDS_W - 1 ? PK_NDS_W - 1 : x;
    u32 cy = y < 0 ? 0 : y > PK_NDS_H - 1 ? PK_NDS_H - 1 : y;
    touch.store((down ? 0x80000000u : 0u) | (cx << 8) | cy);
}

void pk_nds_run_frame(void)
{
    if (!ds) return;
    NDS* nds = ds->nds;
    nds->SetKeyMask(~ds->keys & 0xFFF);
    u32 t = touch.load();
    if (t & 0x80000000u) nds->TouchScreen((u16) ((t >> 8) & 0xFF), (u16) (t & 0xFF));
    else nds->ReleaseScreen();
    // A frame no one will see (fast-forward shows one of each group) isn't drawn at all: no 2D lines, no 3D.
    nds->GPU.SkipDraw = !ds->draw;
    nds->GPU.SkipDrawNext = !ds->drawNext;
    nds->RunFrame();
    int fb = nds->GPU.FrontBuffer;
    if (ds->draw) {
        convert(nds->GPU.Framebuffer[fb][0].get(), ds->top);
        convert(nds->GPU.Framebuffer[fb][1].get(), ds->bottom);
    }
    ds->audioFrames = nds->SPU.ReadOutput(ds->audio.data(), (int) ds->audio.size() / 2);
    ds->fieldBits = pk_nds_field_bits();
    pk_nds_call_frame(nds, ds->field);
    if (ds->wide) {
        // The overworld only: the field map on screen with nothing over it (a text box keeps it).
        bool overworld = (ds->fieldBits & PK_NDS_FIELD_SCREEN) && !(ds->fieldBits & (PK_NDS_FIELD_MENU | PK_NDS_FIELD_BATTLE));
        pk_nds_wide_frame(nds, nds->GPU.Framebuffer[fb][0].get(), ds->draw, overworld, ds->drawNext,
            (ds->fieldBits & PK_NDS_FIELD_BATTLE) != 0);
    }
    if (ds->saveDirty && ++ds->saveQuiet >= SAVE_QUIET_FRAMES) pk_nds_flush_save();
}

uint32_t* pk_nds_top(void) { return ds ? ds->top : nullptr; }
uint32_t* pk_nds_bottom(void) { return ds ? ds->bottom : nullptr; }

int pk_nds_read_audio(int16_t* out, int maxFrames)
{
    if (!ds) return 0;
    int n = ds->audioFrames < maxFrames ? ds->audioFrames : maxFrames;
    memcpy(out, ds->audio.data(), (size_t) n * 2 * sizeof(s16));
    ds->audioFrames = 0;
    return n;
}

void pk_nds_set_audio_rate(double rate)
{
    // Output samples per emulated second = OutputSampleRate / skew.
    if (ds && rate > 0) ds->nds->SPU.SetOutputSkew(ds->sampleRate / rate);
}

bool pk_nds_flush_save(void)
{
    if (!ds || !ds->saveDirty) return false;
    ds->saveDirty = false;
    writeSaveFile();
    return true;
}

int pk_nds_finish_save(int maxFrames)
{
    if (!ds) return 0;
    int n = 0;
    u32 keys = ds->keys;
    u32 t = touch.exchange(0);
    ds->keys = 0;
    while (ds->saveDirty && n < maxFrames) {
        pk_nds_run_frame();
        n++;
    }
    pk_nds_flush_save();
    ds->keys = keys;
    touch.store(t);
    return n;
}

bool pk_nds_save_state(const char* path)
{
    if (!ds) return false;
    Savestate st;
    if (!ds->nds->DoSavestate(&st) || st.Error) return false;
    st.Finish();
    const u8* save = ds->nds->GetNDSSave();
    u32 saveLen = ds->nds->GetNDSSaveLength();
    u32 header[4] = {STATE_MAGIC, STATE_VERSION, save ? saveCrc(save, saveLen) : 0, saveLen};
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = fwrite(header, sizeof(header), 1, f) == 1 && fwrite(st.Buffer(), 1, st.Length(), f) == st.Length();
    ok = fclose(f) == 0 && ok;
    return ok;
}

bool pk_nds_load_state(const char* path)
{
    if (!ds) return false;
    std::vector<u8> data;
    if (!readFile(path, data) || data.size() < 16) return false;
    u32 header[4];
    memcpy(header, data.data(), sizeof(header));
    if (header[0] != STATE_MAGIC) return false;
    Savestate st(data.data() + 16, (u32) (data.size() - 16), false);
    if (st.Error || !ds->nds->DoSavestate(&st) || st.Error) {
        LOGE("loadstate failed: %s", path);
        return false;
    }
    pk_nds_call_reset();
    // The state put its save memory back: the file follows, as on the GBA.
    if (const u8* mem = ds->nds->GetNDSSave()) {
        ds->saveCopy.assign(mem, mem + ds->nds->GetNDSSaveLength());
        ds->saveDirty = true;
        pk_nds_flush_save();
    }
    setClock();
    return true;
}

int pk_nds_state_matches_save(const char* path)
{
    if (!ds) return -1;
    FILE* f = fopen(path, "rb");
    if (!f) return -1;
    u32 header[4];
    bool ok = fread(header, sizeof(header), 1, f) == 1;
    fclose(f);
    if (!ok || header[0] != STATE_MAGIC || header[3] == 0) return -1;
    if (ds->savePath.empty() || ds->saveReadOnly) return 1;
    std::vector<u8> file;
    readFile(ds->savePath.c_str(), file);
    file.resize(header[3], 0xFF);   // flash the file hasn't got yet reads erased
    return saveCrc(file.data(), file.size()) == header[2] ? 1 : 0;
}

void pk_nds_read(uint32_t addr, uint8_t* dst, int len)
{
    if (!ds) {
        memset(dst, 0, (size_t) len);
        return;
    }
    NDS* nds = ds->nds;
    if (addr >= ROM_WINDOW) {
        auto* cart = nds->NDSCartSlot.GetCart();
        const u8* rom = cart ? cart->GetROM() : nullptr;
        u32 romLen = cart ? cart->GetROMLength() : 0;
        for (int i = 0; i < len; i++) {
            u32 off = addr - ROM_WINDOW + (u32) i;
            dst[i] = rom && off < romLen ? rom[off] : 0xFF;
        }
        return;
    }
    if ((addr & 0xFF000000u) == 0x02000000u) {
        for (int i = 0; i < len; i++) dst[i] = nds->MainRAM[(addr + (u32) i) & nds->MainRAMMask];
        return;
    }
    for (int i = 0; i < len; i++) dst[i] = nds->ARM9Read8(addr + (u32) i);
}

bool pk_nds_write(uint32_t addr, const uint8_t* src, int len)
{
    if (!ds || (addr & 0xFF000000u) != 0x02000000u || len <= 0) return false;
    for (int i = 0; i < len; i++) ds->nds->MainRAM[(addr + (u32) i) & ds->nds->MainRAMMask] = src[i];
    return true;
}

static uint32_t mainRead32(uint32_t addr)
{
    if ((addr & 0xFF000000u) != 0x02000000u) return 0;
    const u8* m = ds->nds->MainRAM;
    u32 mask = ds->nds->MainRAMMask;
    return (u32) m[addr & mask] | ((u32) m[(addr + 1) & mask] << 8) | ((u32) m[(addr + 2) & mask] << 16) |
        ((u32) m[(addr + 3) & mask] << 24);
}

static void mainWrite32(uint32_t addr, uint32_t v)
{
    if ((addr & 0xFF000000u) != 0x02000000u) return;
    u8* m = ds->nds->MainRAM;
    u32 mask = ds->nds->MainRAMMask;
    for (int i = 0; i < 4; i++) m[(addr + i) & mask] = (u8) (v >> (8 * i));
}

int pk_nds_field_bits(void)
{
    if (!ds || !ds->field) return 0;
    int bits = pk_nds_field_state(ds->field, mainRead32, mainWrite32, ds->wildOff);
    if ((bits & PK_NDS_FIELD_TEXT) && !pk_nds_bg3_tiles(ds->nds)) bits &= ~PK_NDS_FIELD_TEXT;
    if ((bits & PK_NDS_FIELD_BATTLE) && pk_nds_battle_command(ds->field, mainRead32) == PK_NDS_BATTLE_SELECTING)
        bits |= PK_NDS_FIELD_BATTLE_INPUT;
    return bits;
}

int pk_nds_bgm(void)
{
    if (!ds || !ds->field || !ds->field->currentBgm) return -1;
    return (int) (mainRead32(ds->field->currentBgm) & 0xFFFF);
}

bool pk_nds_bgm_fading(void)
{
    if (!ds || !ds->field || !ds->field->seqPlayers) return false;
    // NNSSndSeqPlayer: the fader { origin, target, counter, frame } at 0x1C, status (0 = stopped) at 0x2C. A fade is
    // run by the ARM9 a frame at a time (NNS_SndPlayerMoveVolume), so a copy taken mid-fade would stay there.
    for (u32 i = 0; i < 16; i++) {
        u32 p = ds->field->seqPlayers + i * 0x44;
        if ((mainRead32(p + 0x2C) & 0xFF) && (s32) mainRead32(p + 0x24) < (s32) mainRead32(p + 0x28)) return true;
    }
    return false;
}

bool pk_nds_music_start(void) { return ds && pk_nds_music_begin(ds->nds, ds->sampleRate); }

int pk_nds_music_read(int16_t* out, int maxFrames) { return ds ? pk_nds_music_frame(out, maxFrames) : 0; }

void pk_nds_music_fade_out(int frames) { pk_nds_music_fade(frames); }

void pk_nds_music_stop(void) { pk_nds_music_end(); }

int pk_nds_action(int action, const uint32_t args[4])
{
    return ds ? pk_nds_call_action(ds->nds, ds->field, action, args) : 0;
}

bool pk_nds_action_results(int seq, uint32_t* out)
{
    return pk_nds_call_results(seq, out);
}

int pk_nds_action_status(int seq)
{
    return pk_nds_call_status(seq);
}

void pk_nds_set_wild_off(bool off)
{
    if (ds) ds->wildOff = off;
}

void pk_nds_set_draw(bool draw, bool drawNext)
{
    if (!ds) return;
    ds->draw = draw;
    ds->drawNext = drawNext;
}

void pk_nds_wide_enable(bool on)
{
    if (!ds || ds->wide == on) return;
    ds->wide = on;
    pk_nds_wide_set(ds->nds, on);
}

bool pk_nds_wide_draw(uint32_t* out, int x, int y, int w, int h)
{
    if (!ds || !ds->wide) return false;
    pk_nds_wide_view(out, x, y, w, h);
    return true;
}

void pk_nds_wide_window(int x, int y, int w, int h)
{
    pk_nds_wide_need(-x, x + w - PK_NDS_W, -y, y + h - PK_NDS_H);
}

int pk_nds_wide_scene(void)
{
    // WideCamera.SCENE_FIELD 0, _TEXT 1, _OFF 4, _FADE 5, _MENU 6
    int b = ds ? ds->fieldBits : 0;
    if (b & PK_NDS_FIELD_BATTLE) return 4;
    // The top screen faded all the way to black or white (master brightness, mode 1 / 2 at 16): between two maps (a
    // door, a warp - the field process stops meanwhile) or two screens. Decides nothing: the zoom holds through it.
    u32 mb = ds ? ds->nds->GPU.GPU2D_A.MasterBrightness : 0;
    if ((mb >> 14) == 1 || (mb >> 14) == 2) {
        if ((mb & 0x1F) >= 16) return 5;
    }
    if (b & PK_NDS_FIELD_MENU) return 6;
    if (!(b & PK_NDS_FIELD_SCREEN)) return 4;
    return (b & PK_NDS_FIELD_TEXT) ? 1 : 0;
}

bool pk_nds_rom_code(char out[5])
{
    if (!ds) return false;
    auto* cart = ds->nds->NDSCartSlot.GetCart();
    if (!cart) return false;
    memcpy(out, cart->GetHeader().GameCode, 4);
    out[4] = 0;
    return true;
}

int pk_nds_rom_revision(void)
{
    auto* cart = ds ? ds->nds->NDSCartSlot.GetCart() : nullptr;
    return cart ? cart->GetHeader().ROMVersion : -1;
}

uint32_t pk_nds_rom_size(void)
{
    auto* cart = ds ? ds->nds->NDSCartSlot.GetCart() : nullptr;
    return cart ? cart->GetROMLength() : 0;
}

} // extern "C"
