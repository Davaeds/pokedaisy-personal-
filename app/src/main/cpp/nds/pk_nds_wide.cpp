// See pk_nds_wide.h. The polygon set-up below is GPU3D::SubmitPolygon's own (melonDS 1.1), from clipping on, writing
// into the slices' lists instead of the game's: keep it in step with that function when the submodule moves.

#include "pk_nds_wide.h"

#include <NDS.h>
#include <GPU.h>
#include <GPU3D.h>
#include <GPU3D_Soft.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

using namespace melonDS;

namespace {

constexpr int MAX_POLYS = 2048;
constexpr int MAX_VERTS = 8192;   // a clipped polygon has up to 10 vertices; no strip sharing here
constexpr int W3D = 256;
constexpr int H3D = 192;
// The slices overlap the screen by this much: a polygon cut at a slice's own edge gets that edge outlined (edge
// marking, antialiasing), so the margin is taken from inside the slice, never its border.
constexpr int OVERLAP = 4;
// Columns / rows drawn past the outermost one shown: edge marking compares a pixel with its neighbours, and an
// undrawn neighbour outlined the last one.
constexpr int SPARE = 2;
constexpr int MX = PK_NDS_WIDE_MAX_MARGIN_X, MY = PK_NDS_WIDE_MAX_MARGIN_Y;
constexpr int OW = PK_NDS_WIDE_MAX_W, OH = PK_NDS_WIDE_MAX_H;
// A battle sprite's edge row goes on into the margin only where this many pixels in from the screen's edge are one colour,
// and only a sprite that has kept its place across the screen EDGE_STEADY frames (a box that has slid in; not one sliding).
constexpr int EDGE_BAND = 5;
constexpr int EDGE_STEADY = 8;
constexpr u32 NONE = 0xFFFFFFFFu;   // not drawn (a 3D colour's alpha is 5 bits: never this)
// Threads drawing the slices besides the caller's: the slices are independent, and the 3x zoom step's (eight of them,
// ~4.7 screens) took ~6 ms a frame on one thread on an M-series Mac, more on a handheld.
constexpr int HELPERS = 3;

// A slice's picture as drawn: 256 x 192 in the 3D engine's colours (RGB6 + alpha5), the rectangle drawn in the
// screen's pixels.
struct Image {
    std::vector<u32> px;
    int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    bool valid = false;
};

struct Slice {
    int sx = 0, sy = 0;   // -1 / 0 / 1: left / screen / right, above / screen / below
    bool active = false;  // gets polygons and is drawn (the window reaches it)
    std::vector<Vertex> verts[2];
    std::vector<Polygon> polys[2];
    int bank = 0;
    u32 numVerts = 0, numPolys = 0, numOpaque = 0;
    std::array<Polygon*, MAX_POLYS> render {};
    u32 renderNum = 0;
    Image image;   // drawn after the last frame: what the next composed frame shows
    Image shown;   // what the last composed frame used (pk_nds_wide_check)
    int lastCount = 0;
};

// A renderer of its own and its scratch: one per thread drawing slices.
struct Drawer {
    std::unique_ptr<SoftRenderer> renderer;
    std::vector<Polygon*> visible;   // a slice's polygons that reach what it draws
};

// The helper threads: each frame's slices go into [jobs], every thread (the caller too) takes the next one until none
// are left, and the caller waits for the rest before the frame goes on (the game's VRAM and 3D registers are only read,
// and nothing runs the game meanwhile).
struct Pool {
    std::vector<std::thread> threads;
    std::mutex lock;
    std::condition_variable wake, done;
    u64 generation = 0;
    bool quit = false;
    int busy = 0;
    std::vector<Slice*> jobs;
    std::atomic<int> next {0};
};

struct Wide {
    NDS* nds = nullptr;
    bool on = false;
    Slice slices[9];      // (sy + 1) * 3 + sx + 1; 4, the screen itself, unused
    Drawer drawers[HELPERS + 1];   // [0] the caller's
    Pool pool;
    std::vector<u32> out;   // OW x OH, the game frame's (0, 0) at (MX, MY)
    int cx0 = 0, cx1 = 0, cy0 = 0, cy1 = 0;   // what the last composed frame covers, in the game frame's pixels
    // The sprites last frame, by what they are (tiles, palette, shape, size, x - not y, nor their OAM slot, which the
    // game reshuffles as sprites come and go), and the frames each has held: only a sprite that has stopped (a box
    // that slid in) carries its edge, not one sliding in.
    std::vector<std::pair<u64, u8>> objHeld, objHeldNext;
    // Each row's edge pixel, left / right, last frame, and the frames it has held (give or take a row: a box bobbing a
    // pixel still holds) - a sprite that stays put while its picture changes (the trainer throwing) doesn't.
    u32 edgeSeen[2][192] = {};
    u8 edgeSteady[2][192] = {};
    int needL = 0, needR = 0, needT = 0, needB = 0;
};

Wide wide;

inline Slice& sliceAt(int sx, int sy) { return wide.slices[(sy + 1) * 3 + sx + 1]; }

void addPolygon(GPU3D& g, Slice& s, Vertex* cv, int nverts, bool facingview, int polytype)
{
    if (s.numPolys >= MAX_POLYS || s.numVerts + (u32) nverts > MAX_VERTS) return;

    // compute screen coordinates
    for (int i = 0; i < nverts; i++) {
        Vertex* vtx = &cv[i];
        vtx->Position[3] &= 0x00FFFFFF;
        u32 posX, posY;
        u32 w = vtx->Position[3];
        if (w == 0) {
            posX = 0;
            posY = 0;
        } else {
            posX = vtx->Position[0] + w;
            posY = -vtx->Position[1] + w;
            u32 den = w;
            if (w > 0xFFFF) {
                posX >>= 1;
                posY >>= 1;
                den >>= 1;
            }
            den <<= 1;
            posX = ((posX * g.Viewport[4]) / den) + g.Viewport[0];
            posY = ((posY * g.Viewport[5]) / den) + g.Viewport[3];
        }
        vtx->FinalPosition[0] = posX & 0x1FF;
        vtx->FinalPosition[1] = posY & 0xFF;
        if (w != 0) {
            posX = ((((s64)(vtx->Position[0] + w) * g.Viewport[4]) << 4) / (((s64) w) << 1)) + (g.Viewport[0] << 4);
            posY = ((((s64)(-vtx->Position[1] + w) * g.Viewport[5]) << 4) / (((s64) w) << 1)) + (g.Viewport[3] << 4);
            vtx->HiresPosition[0] = posX & 0x1FFF;
            vtx->HiresPosition[1] = posY & 0xFFF;
        }
    }

    // zero-dot W check
    if (!(g.CurPolygonAttr & (1 << 13))) {
        bool zerodot = true, allbehind = true;
        for (int i = 0; i < nverts; i++) {
            Vertex* vtx = &cv[i];
            if (vtx->FinalPosition[0] != cv[0].FinalPosition[0] || vtx->FinalPosition[1] != cv[0].FinalPosition[1]) {
                zerodot = false;
                break;
            }
            if (vtx->Position[3] <= (s32) g.ZeroDotWLimit) {
                allbehind = false;
                break;
            }
        }
        if (zerodot && allbehind) return;
    }

    Polygon* poly = &s.polys[s.bank][s.numPolys++];
    poly->NumVertices = 0;
    poly->Attr = g.CurPolygonAttr;
    poly->TexParam = g.TexParam;
    poly->TexPalette = g.TexPalette;
    poly->Degenerate = false;
    poly->FacingView = facingview;
    u32 texfmt = (g.TexParam >> 26) & 0x7;
    u32 polyalpha = (g.CurPolygonAttr >> 16) & 0x1F;
    poly->Translucent = (texfmt == 1 || texfmt == 6) || (polyalpha > 0 && polyalpha < 31);
    poly->IsShadowMask = ((g.CurPolygonAttr & 0x3F000030) == 0x00000030);
    poly->IsShadow = ((g.CurPolygonAttr & 0x30) == 0x30) && !poly->IsShadowMask;
    if (!poly->Translucent) s.numOpaque++;
    poly->Type = polytype;

    for (int i = 0; i < nverts; i++) {
        Vertex* vtx = &s.verts[s.bank][s.numVerts++];
        *vtx = cv[i];
        poly->Vertices[i] = vtx;
        poly->NumVertices++;
        for (int c = 0; c < 3; c++) {
            vtx->FinalColor[c] = vtx->Color[c] >> 12;
            if (vtx->FinalColor[c]) vtx->FinalColor[c] = ((vtx->FinalColor[c] << 4) + 0xF);
        }
    }

    // bounds, W shift and normalisation
    u32 vtop = 0, vbot = 0;
    s32 ytop = 192, ybot = 0;
    s32 xtop = 256, xbot = 0;
    u32 wsize = 0;
    for (int i = 0; i < nverts; i++) {
        Vertex* vtx = poly->Vertices[i];
        if (vtx->FinalPosition[1] < ytop) {
            xtop = vtx->FinalPosition[0];
            ytop = vtx->FinalPosition[1];
            vtop = i;
        }
        if (vtx->FinalPosition[1] > ybot || (vtx->FinalPosition[1] == ybot && vtx->FinalPosition[0] > xbot)) {
            xbot = vtx->FinalPosition[0];
            ybot = vtx->FinalPosition[1];
            vbot = i;
        }
        u32 w = (u32) vtx->Position[3];
        if (w == 0) poly->Degenerate = true;
        while ((w >> wsize) && (wsize < 32)) wsize += 4;
    }
    poly->VTop = vtop; poly->VBottom = vbot;
    poly->YTop = ytop; poly->YBottom = ybot;
    poly->XTop = xtop; poly->XBottom = xbot;
    if (ybot > 192) poly->Degenerate = true;
    poly->SortKey = (ybot << 8) | ytop;
    if (poly->Translucent) poly->SortKey |= 0x10000;
    poly->WBuffer = (g.FlushAttributes & 0x2);

    for (int i = 0; i < nverts; i++) {
        Vertex* vtx = poly->Vertices[i];
        s32 w, wshifted;
        if (wsize < 16) {
            w = vtx->Position[3] << (16 - wsize);
            wshifted = w >> (16 - wsize);
        } else {
            w = vtx->Position[3] >> (wsize - 16);
            wshifted = w << (wsize - 16);
        }
        s32 z;
        if (g.FlushAttributes & 0x2)
            z = wshifted;
        else if (vtx->Position[3])
            z = ((((s64) vtx->Position[2] * 0x4000) / vtx->Position[3]) + 0x3FFF) * 0x200;
        else
            z = 0x7FFE00;
        if (z < 0) z = 0;
        else if (z > 0xFFFFFF) z = 0xFFFFFF;
        poly->FinalZ[i] = z;
        poly->FinalW[i] = w;
    }
}

void onSubmit(GPU3D& g, const Vertex* in, int nverts, bool facingview, int polytype, void*)
{
    // Only a full-screen viewport continues past the screen's edges.
    if (g.Viewport[0] != 0 || g.Viewport[4] != 256 || g.Viewport[3] != 0 || g.Viewport[5] != 192) return;
    // The slice left of the screen is clip-space x in [-3w, -w]: moved by +2w it's what [-w, w] was. Moved by a little
    // less (OVERLAP pixels: 2w * (256 - OVERLAP) / 256), the slice's own edge sits inside the screen. Above / below
    // the same in y (clip-space y runs up the screen).
    s64 shiftX[10], shiftY[10];
    for (int i = 0; i < nverts; i++) {
        shiftX[i] = (s64) in[i].Position[3] * (W3D - OVERLAP) / (W3D / 2);
        shiftY[i] = (s64) in[i].Position[3] * (H3D - OVERLAP) / (H3D / 2);
    }
    for (Slice& s : wide.slices) {
        if (!s.active) continue;
        Vertex cv[10];
        bool fits = true;
        // Wholly past one of the slice's sides (every vertex in front of the eye): nothing to clip.
        bool allL = true, allR = true, allT = true, allB = true;
        for (int i = 0; i < nverts; i++) {
            cv[i] = in[i];
            s64 w = cv[i].Position[3];
            s64 x = (s64) cv[i].Position[0] - s.sx * shiftX[i];
            s64 y = (s64) cv[i].Position[1] + s.sy * shiftY[i];
            if (x > INT32_MAX || x < INT32_MIN || y > INT32_MAX || y < INT32_MIN) fits = false;
            cv[i].Position[0] = (s32) x;
            cv[i].Position[1] = (s32) y;
            if (w <= 0) {
                allL = allR = allT = allB = false;
            } else {
                allL &= x < -w;
                allR &= x > w;
                allB &= y < -w;
                allT &= y > w;
            }
        }
        if (!fits || allL || allR || allT || allB) continue;
        int n = g.ClipPolygonForHook(cv, nverts);
        if (n > 0) addPolygon(g, s, cv, n, facingview, polytype);
    }
}

// A box test that missed the screen (Platinum culls its buildings, trees and signs with them): its corners mapped from
// the window's wider view onto clip space, so the test asks "in the window?" - the game then draws the model, the screen
// clips it away as before and the slices get it. x in [-(1 + 2L/256) w, (1 + 2R/256) w] becomes [-w, w]: x' = (256 x -
// (R - L) w) / (256 + L + R); y the same (clip-space y runs up: above is +y).
bool onBoxTest(GPU3D& g, Vertex* cube, void*)
{
    if (g.Viewport[0] != 0 || g.Viewport[4] != 256 || g.Viewport[3] != 0 || g.Viewport[5] != 192) return false;
    s64 l = wide.needL, r = wide.needR, t = wide.needT, b = wide.needB;
    if (!(l | r | t | b)) return false;
    // A little more each way: a model's box can be a pixel short of what it draws.
    l += 4; r += 4; t += 4; b += 4;
    for (int i = 0; i < 8; i++) {
        s64 x = cube[i].Position[0], y = cube[i].Position[1], w = cube[i].Position[3];
        cube[i].Position[0] = (s32) ((W3D * x - (r - l) * w) / (W3D + l + r));
        cube[i].Position[1] = (s32) ((H3D * y - (t - b) * w) / (H3D + t + b));
    }
    return true;
}

void onSwap(GPU3D& g, bool rendering, void*)
{
    for (Slice& s : wide.slices) {
        if (s.verts[0].empty()) continue;
        if (rendering) {
            u32 io = 0, it = s.numOpaque;
            for (u32 i = 0; i < s.numPolys; i++) {
                Polygon* poly = &s.polys[s.bank][i];
                if (poly->Translucent) s.render[it++] = poly;
                else s.render[io++] = poly;
            }
            std::stable_sort(s.render.begin(), s.render.begin() + ((g.FlushAttributes & 0x1) ? s.numOpaque : s.numPolys),
                [](Polygon* a, Polygon* b) { return a->SortKey < b->SortKey; });
            s.renderNum = s.numPolys;
            s.lastCount = (int) s.numPolys;
        }
        s.bank ^= 1;
        s.numVerts = s.numPolys = s.numOpaque = 0;
    }
}

inline u32 rgba(u32 r, u32 g, u32 b) { return 0xFF000000u | (b << 16) | (g << 8) | r; }

// The 3D engine's colour (6 bits a channel) through the top screen's master brightness, as RGBA.
inline u32 shade(u32 r6, u32 g6, u32 b6, u32 mb)
{
    u32 mode = mb >> 14, f = std::min<u32>(mb & 0x1F, 16);
    if (mode == 1) {
        r6 += ((63 - r6) * f) >> 4; g6 += ((63 - g6) * f) >> 4; b6 += ((63 - b6) * f) >> 4;
    } else if (mode == 2) {
        r6 -= (r6 * f + 0xF) >> 4; g6 -= (g6 * f + 0xF) >> 4; b6 -= (b6 * f + 0xF) >> 4;
    }
    return rgba((r6 << 2) | (r6 >> 4), (g6 << 2) | (g6 >> 4), (b6 << 2) | (b6 >> 4));
}

inline u32 shade(u32 c, u32 mb) { return shade(c & 0x3F, (c >> 8) & 0x3F, (c >> 16) & 0x3F, mb); }

inline u32 toRgba(u32 argb) { return 0xFF000000u | ((argb & 0xFF) << 16) | (argb & 0xFF00) | ((argb >> 16) & 0xFF); }

// Two RGBA colours within a texel's difference of each other: the slices sample textures a texel off here and there
// (a polygon cut at other planes interpolates along other edges), a text box or a sprite is far off.
inline bool near(u32 a, u32 b)
{
    for (int sh = 0; sh < 24; sh += 8) {
        int d = (int) ((a >> sh) & 0xFF) - (int) ((b >> sh) & 0xFF);
        if (d > 40 || d < -40) return false;
    }
    return true;
}

// Screen pixel (x, y) from slice (sx, sy)'s image: NONE where it wasn't drawn.
inline u32 at(const Image& im, int sx, int sy, int x, int y)
{
    if (!im.valid || x < im.x0 || x >= im.x1 || y < im.y0 || y >= im.y1) return NONE;
    return im.px[(y - sy * (H3D - OVERLAP)) * W3D + x - sx * (W3D - OVERLAP)];
}

inline u32 at(int sx, int sy, int x, int y) { return at(sliceAt(sx, sy).image, sx, sy, x, y); }

// A margin pixel's 3D colour (NONE: none drawn). The slices level with the screen are cut where the screen is (its
// columns 0 / 255 above and below it, its rows 0 / 191 beside it) and outlined there like the screen's own edges: those
// two pixels come from the corner slice beside them, which has them inside.
inline u32 marginPixel(int x, int y)
{
    int sx = x < 0 ? -1 : x >= W3D ? 1 : 0, sy = y < 0 ? -1 : y >= H3D ? 1 : 0;
    if (sx == 0) {
        int ax = x < SPARE ? -1 : x >= W3D - SPARE ? 1 : 0;
        if (ax) {
            u32 c = at(ax, sy, x, y);
            if (c != NONE) return c;
        }
    } else if (sy == 0) {
        int ay = y < SPARE ? -1 : y >= H3D - SPARE ? 1 : 0;
        if (ay) {
            u32 c = at(sx, ay, x, y);
            if (c != NONE) return c;
        }
    }
    return at(sx, sy, x, y);
}

// The rectangle slice (sx, sy) draws, in the screen's pixels: the margin the window needs on its side(s), SPARE more,
// and its overlap with the screen.
void sliceRect(int sx, int sy, int* x0, int* x1, int* y0, int* y1)
{
    *x0 = sx < 0 ? -(wide.needL + SPARE) : sx == 0 ? 0 : W3D - OVERLAP;
    *x1 = sx < 0 ? OVERLAP : sx == 0 ? W3D : W3D + wide.needR + SPARE;
    *y0 = sy < 0 ? -(wide.needT + SPARE) : sy == 0 ? 0 : H3D - OVERLAP;
    *y1 = sy < 0 ? OVERLAP : sy == 0 ? H3D : H3D + wide.needB + SPARE;
}

void drawSlice(Slice& s, Drawer& d)
{
    int x0, x1, y0, y1;
    sliceRect(s.sx, s.sy, &x0, &x1, &y0, &y1);
    int ox = s.sx * (W3D - OVERLAP), oy = s.sy * (H3D - OVERLAP);   // screen pixel = slice pixel + (ox, oy)
    SoftRenderer& r = *d.renderer;
    r.ColumnMin = std::clamp(x0 - ox, 0, W3D);
    r.ColumnMax = std::clamp(x1 - ox, 0, W3D);
    r.RowMin = std::clamp(y0 - oy, 0, H3D);
    r.RowMax = std::clamp(y1 - oy, 0, H3D);
    // Polygons wholly outside those cost their per-line set-up all the same: left out (the order kept).
    d.visible.clear();
    for (u32 i = 0; i < s.renderNum; i++) {
        Polygon* p = s.render[i];
        if (p->YBottom < r.RowMin || p->YTop >= r.RowMax) continue;
        s32 lo = 512, hi = -1;
        for (u32 v = 0; v < p->NumVertices; v++) {
            lo = std::min(lo, p->Vertices[v]->FinalPosition[0]);
            hi = std::max(hi, p->Vertices[v]->FinalPosition[0]);
        }
        if (hi >= r.ColumnMin && lo < r.ColumnMax) d.visible.push_back(p);
    }
    r.RenderPolygonList(wide.nds->GPU, d.visible.data(), (int) d.visible.size());
    for (int y = r.RowMin; y < r.RowMax; y++)
        memcpy(&s.image.px[y * W3D + r.ColumnMin], r.GetLine(y) + r.ColumnMin, (size_t) (r.ColumnMax - r.ColumnMin) * 4);
    s.image.x0 = r.ColumnMin + ox;
    s.image.x1 = r.ColumnMax + ox;
    s.image.y0 = r.RowMin + oy;
    s.image.y1 = r.RowMax + oy;
    s.image.valid = true;
}

void takeJobs(Drawer& d)
{
    Pool& p = wide.pool;
    for (int i; (i = p.next.fetch_add(1)) < (int) p.jobs.size();) drawSlice(*p.jobs[i], d);
}

void helperLoop(int id)
{
    Pool& p = wide.pool;
    u64 seen = 0;
    for (;;) {
        {
            std::unique_lock<std::mutex> l(p.lock);
            p.wake.wait(l, [&] { return p.quit || p.generation != seen; });
            if (p.quit) return;
            seen = p.generation;
        }
        takeJobs(wide.drawers[id]);
        {
            std::lock_guard<std::mutex> l(p.lock);
            p.busy--;
        }
        p.done.notify_one();
    }
}

// The slices in [jobs] drawn, largest first, on the caller and the helpers.
void drawSlices()
{
    Pool& p = wide.pool;
    if (p.jobs.empty()) return;
    auto area = [](const Slice* s) {
        int x0, x1, y0, y1;
        sliceRect(s->sx, s->sy, &x0, &x1, &y0, &y1);
        return (x1 - x0) * (y1 - y0);
    };
    std::stable_sort(p.jobs.begin(), p.jobs.end(), [&](Slice* a, Slice* b) { return area(a) > area(b); });
    p.next = 0;
    int helpers = std::min<int>((int) p.threads.size(), (int) p.jobs.size() - 1);
    if (helpers > 0) {
        {
            std::lock_guard<std::mutex> l(p.lock);
            p.busy = (int) p.threads.size();
            p.generation++;
        }
        p.wake.notify_all();
    }
    takeJobs(wide.drawers[0]);
    if (helpers > 0) {
        std::unique_lock<std::mutex> l(p.lock);
        p.done.wait(l, [&] { return p.busy == 0; });
    }
}

void stopPool()
{
    Pool& p = wide.pool;
    {
        std::lock_guard<std::mutex> l(p.lock);
        p.quit = true;
    }
    p.wake.notify_all();
    for (std::thread& t : p.threads) t.join();
    p.threads.clear();
    p.quit = false;
}

} // namespace

void pk_nds_wide_set(NDS* nds, bool on)
{
    if (nds == wide.nds && on == wide.on) return;
    if (wide.nds) {
        wide.nds->GPU.GPU3D.WideSubmitHook = nullptr;
        wide.nds->GPU.GPU3D.WideSwapHook = nullptr;
        wide.nds->GPU.GPU3D.WideBoxHook = nullptr;
    }
    wide.nds = nds;
    wide.on = nds && on;
    for (int i = 0; i < 9; i++) {
        wide.slices[i] = Slice();
        wide.slices[i].sx = i % 3 - 1;
        wide.slices[i].sy = i / 3 - 1;
    }
    wide.cx0 = wide.cx1 = wide.cy0 = wide.cy1 = 0;
    stopPool();
    if (!wide.on) {
        for (Drawer& d : wide.drawers) d = Drawer();
        wide.out.clear();
        wide.out.shrink_to_fit();
        return;
    }
    for (Drawer& d : wide.drawers) {
        d.renderer = std::make_unique<SoftRenderer>();
        d.renderer->Reset(nds->GPU);
    }
    for (int i = 1; i <= HELPERS; i++) wide.pool.threads.emplace_back(helperLoop, i);
    wide.out.assign((size_t) OW * OH, 0xFF000000u);
    nds->GPU.GPU3D.WideHookUser = &wide;
    nds->GPU.GPU3D.WideSubmitHook = onSubmit;
    nds->GPU.GPU3D.WideSwapHook = onSwap;
    nds->GPU.GPU3D.WideBoxHook = onBoxTest;
    pk_nds_wide_need(wide.needL, wide.needR, wide.needT, wide.needB);
}

void pk_nds_wide_need(int left, int right, int top, int bottom)
{
    wide.needL = std::clamp(left, 0, MX);
    wide.needR = std::clamp(right, 0, MX);
    wide.needT = std::clamp(top, 0, MY);
    wide.needB = std::clamp(bottom, 0, MY);
    if (!wide.on) return;
    for (Slice& s : wide.slices) {
        bool x = s.sx < 0 ? wide.needL > 0 : s.sx > 0 ? wide.needR > 0 : true;
        bool y = s.sy < 0 ? wide.needT > 0 : s.sy > 0 ? wide.needB > 0 : true;
        s.active = (s.sx || s.sy) && x && y;
        if (s.active && s.verts[0].empty()) {
            for (int b = 0; b < 2; b++) {
                s.verts[b].resize(MAX_VERTS);
                s.polys[b].resize(MAX_POLYS);
            }
            s.image.px.assign(W3D * H3D, 0);
            s.shown.px.assign(W3D * H3D, 0);
        }
    }
}

bool pk_nds_wide_frame(NDS* nds, const uint32_t* top, bool draw, bool margins, bool drawNext, bool battle)
{
    if (!wide.on || nds != wide.nds || !top) return false;
    // Only while the 3D engine (engine A) is the top screen, and the caller says it's the overworld.
    bool aOnTop = (nds->PowerControl9 & (1 << 15)) != 0;
    bool threeDOnTop = margins && aOnTop;
    // A battle: its background layer, BG3 - a text BG (mode 0-2), shown - drawn on past the screen.
    const GPU2D::Unit& unitA = nds->GPU.GPU2D_A;
    bool battleBg = battle && !threeDOnTop && aOnTop && (unitA.DispCnt & 7) <= 2 && (unitA.DispCnt & (1 << 11));
    int bgx = unitA.BGXPos[3], bgy = unitA.BGYPos[3];
    // BLDCNT on the background (BG3, or the backdrop where it's clear) - modes 2 / 3, brightness up / down - unless a
    // window keeps colour effects off outside it: +1 / -1, 0 none.
    int fade = 0;
    u32 evy = std::min<u32>(16, unitA.EVY);
    {
        u16 bc = unitA.BlendCnt;
        int fmode = (bc >> 6) & 3;
        bool windows = (unitA.DispCnt & 0xE000) != 0;
        bool outsideFx = !windows || (unitA.WinCnt[2] & 0x20);   // WINOUT's colour-effect bit
        if (battleBg && outsideFx && (bc & ((1 << 3) | (1 << 5))) && fmode >= 2) fade = fmode == 2 ? 1 : -1;
    }
    // A 2D layer over what the margins show (the 3D, or a battle's background) that's black at the screen's edge on a
    // row - a transition's wipe blocks, a battle intro's bands - blacks that row's margin out too: those layers aren't
    // pictured past the screen, but their black at its edge carries on. In a battle any colour does (the text box's dark
    // frame runs the screen's width), on the field any row the layer covers edge to edge: the row's margin takes the
    // screen's edge pixel.
    bool blackL[H3D] = {}, blackR[H3D] = {};
    if (aOnTop && (threeDOnTop || battleBg)) {
        int mode = (int) (unitA.DispCnt & 7);
        int base = battleBg ? 3 : 0;
        int basePrio = unitA.BGCnt[base] & 3;
        for (int bg = 1; bg <= 3; bg++) {
            if (bg == base || !(unitA.DispCnt & (1 << (8 + bg)))) continue;
            bool text = mode == 0 || (mode == 1 && bg < 3) || (mode == 2 && bg < 2);
            int prio = unitA.BGCnt[bg] & 3;
            if (!text || prio > basePrio || (prio == basePrio && bg > base)) continue;
            for (int y = 0; y < H3D; y++) {
                u16 cl = pk_nds_bg_pixel(nds, bg, unitA.BGXPos[bg], y + unitA.BGYPos[bg]);
                u16 cr = pk_nds_bg_pixel(nds, bg, W3D - 1 + unitA.BGXPos[bg], y + unitA.BGYPos[bg]);
                // A row the layer covers from edge to edge (HG/SS's "look at the touch screen" bar under the field
                // menu) carries on too; a text box stops short of the edges.
                bool across = cl && cr;
                for (int side = 0; side < 2; side++) {
                    u16 c = side ? cr : cl;
                    if (c && (battleBg || across || !(c & 0x7FFF))) (side ? blackR : blackL)[y] = true;
                }
            }
        }
    }
    // Sprites at the screen's sides (the HP boxes, flush with them): their rows carry the screen's edge column on
    // through the margin, so a box still reaches the picture's edge instead of stopping in mid-air.
    bool edgeObjL[H3D] = {}, edgeObjR[H3D] = {};
    // Only frames that were drawn (fast-forward skips the rest: their buffers are an older frame's).
    for (int side = 0; side < 2 && draw; side++) {
        u32 now[H3D];
        for (int y = 0; y < H3D; y++) now[y] = top[y * W3D + (side ? W3D - 1 : 0)];
        for (int y = 0; y < H3D; y++) {
            const u32* was = wide.edgeSeen[side];
            bool held = now[y] == was[y] || (y > 0 && now[y] == was[y - 1]) || (y + 1 < H3D && now[y] == was[y + 1]);
            u8& n = wide.edgeSteady[side][y];
            n = held ? (u8) std::min(255, n + 1) : 0;
        }
        std::memcpy(wide.edgeSeen[side], now, sizeof(now));
    }
    wide.objHeldNext.clear();
    if (draw && battleBg && (unitA.DispCnt & (1 << 12))) {
        static const int sizes[3][4][2] = {
            {{8, 8}, {16, 16}, {32, 32}, {64, 64}}, {{16, 8}, {32, 8}, {32, 16}, {64, 32}}, {{8, 16}, {8, 32}, {16, 32}, {32, 64}},
        };
        for (int i = 0; i < 128; i++) {
            const u8* o = &nds->GPU.OAM[i * 8];
            u16 a0 = (u16) (o[0] | o[1] << 8), a1 = (u16) (o[2] | o[3] << 8);
            // Its place across the screen (and whether it shows): a box bobbing a pixel up and down while the game waits
            // for a command still counts as still.
            u16 a2 = (u16) (o[4] | o[5] << 8);
            u64 key = (u64) (a0 & 0xC300) | (u64) (a1 & 0xF1FF) << 16 | (u64) a2 << 32;
            u8 held = 0;
            for (auto& [k, n] : wide.objHeld) if (k == key) { held = (u8) std::min(255, n + 1); break; }
            wide.objHeldNext.emplace_back(key, held);
            if (held < EDGE_STEADY) continue;
            if (!(a0 & 0x100) && (a0 & 0x200)) continue;   // hidden
            int shape = (a0 >> 14) & 3;
            if (shape == 3) continue;
            int w = sizes[shape][(a1 >> 14) & 3][0], h = sizes[shape][(a1 >> 14) & 3][1];
            if ((a0 & 0x300) == 0x300) { w *= 2; h *= 2; }   // double-size affine
            int x = a1 & 0x1FF, y = a0 & 0xFF;
            if (x >= 256) x -= 512;
            if (y >= 192) y -= 256;
            bool left = x <= 0 && x + w > 0, right = x < W3D && x + w >= W3D;
            for (int r = std::max(0, y); r < std::min(H3D, y + h); r++) {
                edgeObjL[r] |= left;
                edgeObjR[r] |= right;
            }
        }
    }
    if (draw) wide.objHeld.swap(wide.objHeldNext);
    if (draw) {
        u32 mb = nds->GPU.GPU2D_A.MasterBrightness;
        if (!(nds->GPU.GPU2D_A.DispCnt & 0x30000)) mb = 0;   // display off / VRAM modes: the 2D engine's call
        u16 bd = (u16) (nds->GPU.Palette[0] | (nds->GPU.Palette[1] << 8));
        u32 backdrop = shade((bd & 0x1F) << 1, ((bd >> 5) & 0x1F) << 1, ((bd >> 10) & 0x1F) << 1, mb);
        u32 off = threeDOnTop || battleBg ? backdrop : 0xFF000000u;
        // BG3's colour as the screen shows it: BGR555 widened to 6 bits (a 2D layer's c << 1, ScanlineFinalPass),
        // BLDCNT's brightness up / down on it (a move's darkening, a flash - no other layer is over it out here, so its
        // alpha blending has nothing to mix), then the master brightness.
        auto bgColor = [&](u16 c) {
            u32 r = (c & 0x1F) << 1, g = ((c >> 5) & 0x1F) << 1, b = ((c >> 10) & 0x1F) << 1;
            if (fade) {
                auto f = [&](u32 v) { return fade > 0 ? v + (63 - v) * evy / 16 : v - v * evy / 16; };
                r = f(r); g = f(g); b = f(b);
            }
            return shade(r, g, b, mb);
        };
        // Each row's colour carried past a box at the screen's side (0: none). A row whose edge band is one colour,
        // held EDGE_STEADY frames, carries its edge pixel; a row of the same box with a letter or a bar at the edge
        // takes the nearest such row's colour (the box's line or fill it's drawn over) - its own edge pixel would
        // streak the letter across the margin, and the background there cut the box into slats.
        u32 carry[2][H3D] = {};
        if (battleBg)
            for (int side = 0; side < 2; side++) {
                bool* objRows = side ? edgeObjR : edgeObjL;
                int ex = side ? W3D - 1 : 0;
                bool covered[H3D] = {}, sure[H3D] = {};
                bool* bgRows = side ? blackR : blackL;
                for (int y = 0; y < H3D; y++) {
                    // A layer over the background at the edge (the text box) has those rows: they end a box's run.
                    if (!objRows[y] || bgRows[y]) continue;
                    u16 under = pk_nds_bg_pixel(nds, 3, ex + bgx, y + bgy);
                    u32 shown = toRgba(top[y * W3D + ex]);
                    // Where the sprite leaves the background showing (a box's rounded corner), nothing to carry - the
                    // background's very colour: a box's light grey is near a daytime sky's.
                    u32 bgc = under ? bgColor(under) : backdrop;
                    bool same = true;
                    for (int sh = 0; sh < 24; sh += 8) same &= std::abs((int) ((shown >> sh) & 0xFF) - (int) ((bgc >> sh) & 0xFF)) <= 8;
                    if (same) continue;
                    covered[y] = true;
                    bool band = true;
                    for (int k = 1; k < EDGE_BAND && band; k++)
                        band = top[y * W3D + (side ? W3D - 1 - k : k)] == top[y * W3D + ex];
                    sure[y] = band && wide.edgeSteady[side][y] >= EDGE_STEADY;
                }
                for (int y = 0; y < H3D; y++) {
                    if (!covered[y]) continue;
                    int best = -1;
                    for (int d = 0; d < H3D && best < 0; d++) {
                        int up = y - d, down = y + d;
                        bool upIn = up >= 0, downIn = down < H3D;
                        // Within the box's run of rows only.
                        for (int k = y; upIn && k > up; k--) if (!covered[k - 1]) upIn = false;
                        for (int k = y; downIn && k < down; k++) if (!covered[k + 1]) downIn = false;
                        if (!upIn && !downIn) break;
                        if (upIn && sure[up]) best = up;
                        else if (downIn && sure[down]) best = down;
                    }
                    if (best >= 0) carry[side][y] = toRgba(top[best * W3D + ex]);
                }
            }
        wide.cx0 = -wide.needL;
        wide.cx1 = W3D + wide.needR;
        wide.cy0 = -wide.needT;
        wide.cy1 = H3D + wide.needB;
        for (int y = wide.cy0; y < wide.cy1; y++) {
            u32* row = &wide.out[(size_t) (y + MY) * OW + MX];   // row[x], x from -MX
            if (y >= 0 && y < H3D) {
                const u32* src = top + y * W3D;
                for (int x = 0; x < W3D; x++) row[x] = toRgba(src[x]);
            }
            auto margin = [&](int x) {
                if (y >= 0 && y < H3D && (x < 0 ? blackL[y] : x >= W3D && blackR[y])) {
                    // The screen's own edge pixels as shown (brightness and fades included), the last two repeated so
                    // a dithered band (the field menu's bar) keeps its pattern.
                    int ex = x < 0 ? ((x % 2) + 2) % 2 : W3D - 2 + (x - (W3D - 2)) % 2;
                    row[x] = toRgba(top[y * W3D + ex]);
                    return;
                }
                if (battleBg) {
                    if (y >= 0 && y < H3D && carry[x < 0 ? 0 : 1][y]) { row[x] = carry[x < 0 ? 0 : 1][y]; return; }
                    u16 c = y >= 0 && y < H3D ? pk_nds_bg_pixel(nds, 3, x + bgx, y + bgy) : 0;
                    // The background, else the backdrop (its palette's colour 0).
                    row[x] = c ? bgColor(c) : bgColor(bd);
                    return;
                }
                u32 c = threeDOnTop ? marginPixel(x, y) : NONE;
                row[x] = c != NONE && (c >> 24) ? shade(c, mb) : off;
            };
            for (int x = wide.cx0; x < 0; x++) margin(x);
            if (y < 0 || y >= H3D)
                for (int x = 0; x < W3D; x++) margin(x);
            for (int x = W3D; x < wide.cx1; x++) margin(x);
        }
        // The frame's outer rows and columns: the DS outlines polygons where the screen cuts them (edge marking), a
        // dark line once the picture goes on past them. Taken from the slice beside them instead - where the next one
        // in shows the slice's own pixel, so it's the 3D layer there and not a text box or a sprite over it.
        if (threeDOnTop) {
            // The edge-marking colours as they come out on screen (ScanlineFinalPass's 5 -> 6 bit conversion): a frame
            // edge pixel showing one of them exactly is the outline. Where the slice shows texture a texel off, the
            // "next one in" test below fails on a busy texture, and the outline stayed as a dark dot.
            u32 edgeColors[8];
            for (int i = 0; i < 8; i++) {
                u16 e = nds->GPU.GPU3D.RenderEdgeTable[i];
                u32 r = (e << 1) & 0x3E, g = (e >> 4) & 0x3E, b = (e >> 9) & 0x3E;
                edgeColors[i] = shade(r ? r + 1 : 0, g ? g + 1 : 0, b ? b + 1 : 0, mb);
            }
            bool edgeMarking = (nds->GPU.GPU3D.RenderDispCnt & (1 << 5)) != 0;
            auto fix = [&](int sx, int sy, int ex, int ey, int nx, int ny) {
                u32 cn = at(sx, sy, nx, ny), ce = at(sx, sy, ex, ey);
                if (cn == NONE || ce == NONE || !(cn >> 24) || !(ce >> 24)) return;
                u32 frame = toRgba(top[ey * W3D + ex]);
                bool outline = false;
                if (edgeMarking)
                    for (u32 c : edgeColors) outline |= c == frame;
                if (!outline && !near(shade(cn, mb), toRgba(top[ny * W3D + nx]))) return;
                wide.out[(size_t) (ey + MY) * OW + MX + ex] = shade(ce, mb);
            };
            for (int side = -1; side <= 1; side += 2) {
                int ex = side < 0 ? 0 : W3D - 1, ey = side < 0 ? 0 : H3D - 1;
                if (sliceAt(side, 0).image.valid)
                    for (int y = 0; y < H3D; y++) fix(side, 0, ex, y, ex - side, y);
                if (sliceAt(0, side).image.valid)
                    for (int x = 0; x < W3D; x++) fix(0, side, x, ey, x, ey - side);
            }
            // Its corner pixels, outlined from both sides: from the corner slice, checked one in diagonally.
            for (int sy = -1; sy <= 1; sy += 2)
                for (int sx = -1; sx <= 1; sx += 2) {
                    int ex = sx < 0 ? 0 : W3D - 1, ey = sy < 0 ? 0 : H3D - 1;
                    fix(sx, sy, ex, ey, ex - sx, ey - sy);
                }
        }
    }
    // The slices for the next frame: the lists swapped at this frame's VBlank are what the main renderer draws next.
    // Not for a frame no one will see: its slices would be drawn over before they're shown.
    wide.pool.jobs.clear();
    for (Slice& s : wide.slices) {
        if (s.verts[0].empty()) continue;
        std::swap(s.shown, s.image);   // the one just shown (pk_nds_wide_check); the other is drawn over
        s.image.valid = false;
        if (drawNext && s.active) wide.pool.jobs.push_back(&s);
    }
    drawSlices();
    return draw;
}

// Debugging (PK_WIDE_EDGES=1, nds_dump's widecheck): the composed frame's outer rows and columns against the slices'
// own pixels there - what's left far off after the outline fix.
static void edgeReport(NDS* nds)
{
    u32 mb = nds->GPU.GPU2D_A.MasterBrightness;
    if (!(nds->GPU.GPU2D_A.DispCnt & 0x30000)) mb = 0;
    int left = 0, right = 0, topRow = 0, bottomRow = 0, printed = 0;
    auto test = [&](int sx, int sy, int x, int y, int& count) {
        u32 c = at(sliceAt(sx, sy).shown, sx, sy, x, y);
        if (c == NONE || !(c >> 24)) return;
        u32 got = wide.out[(size_t) (y + MY) * OW + MX + x], want = shade(c, mb);
        if (near(got, want)) return;
        count++;
        if (printed++ < 10) fprintf(stderr, "edge (%d, %d): shown %06X slice %06X\n", x, y, got & 0xFFFFFF, want & 0xFFFFFF);
    };
    for (int y = 0; y < H3D; y++) { test(-1, 0, 0, y, left); test(1, 0, W3D - 1, y, right); }
    for (int x = 0; x < W3D; x++) { test(0, -1, x, 0, topRow); test(0, 1, x, H3D - 1, bottomRow); }
    fprintf(stderr, "edges off: left %d right %d top %d bottom %d\n", left, right, topRow, bottomRow);
}

void pk_nds_wide_check(NDS* nds, const uint32_t* top, int* compared, int* differ, int* far)
{
    if (getenv("PK_WIDE_EDGES")) edgeReport(nds);
    *compared = *differ = *far = 0;
    if (!wide.on) return;
    u32 mb = nds->GPU.GPU2D_A.MasterBrightness;
    if (!(nds->GPU.GPU2D_A.DispCnt & 0x30000)) mb = 0;
    auto check = [&](int sx, int sy, int x, int y) {
        u32 c = at(sliceAt(sx, sy).shown, sx, sy, x, y);
        if (c == NONE || !(c >> 24)) return;
        u32 want = shade(c, mb), got = toRgba(top[y * W3D + x]);
        (*compared)++;
        if (want != got) {
            bool isFar = !near(want, got);
            if (isFar) (*far)++;
            if (getenv("PK_WIDE_DEBUG") && isFar && *far < 40)
                fprintf(stderr, "slice (%d, %d) x %d y %d: slice %06X (raw %08X) frame %06X\n", sx, sy, x, y,
                    want & 0xFFFFFF, c, got & 0xFFFFFF);
            (*differ)++;
        }
    };
    // Past the screen's outline (its outer row / column) and the slice's (the overlap's innermost).
    for (int side = -1; side <= 1; side += 2) {
        for (int k = 1; k <= OVERLAP - 2; k++) {
            int x = side < 0 ? k : W3D - 1 - k, y = side < 0 ? k : H3D - 1 - k;
            for (int yy = 0; yy < H3D; yy++) check(side, 0, x, yy);
            for (int xx = 0; xx < W3D; xx++) check(0, side, xx, y);
        }
    }
}

void pk_nds_wide_view(uint32_t* out, int x0, int y0, int w, int h)
{
    for (int y = 0; y < h; y++) {
        int sy = y0 + y;
        u32* dst = out + (size_t) y * w;
        if (!wide.on || sy < wide.cy0 || sy >= wide.cy1) {
            std::fill(dst, dst + w, 0xFF000000u);
            continue;
        }
        const u32* src = &wide.out[(size_t) (sy + MY) * OW + MX];
        for (int x = 0; x < w; x++) {
            int sx = x0 + x;
            dst[x] = sx >= wide.cx0 && sx < wide.cx1 ? src[sx] : 0xFF000000u;
        }
    }
}

void pk_nds_wide_rect(int* x, int* y, int* w, int* h)
{
    *x = wide.cx0;
    *y = wide.cy0;
    *w = wide.cx1 - wide.cx0;
    *h = wide.cy1 - wide.cy0;
}

uint16_t pk_nds_bg_pixel(NDS* nds, int bg, int bx, int by)
{
    const GPU& gpu = nds->GPU;
    const GPU2D::Unit& u = gpu.GPU2D_A;
    u16 cnt = u.BGCnt[bg & 3];
    u32 disp = u.DispCnt;
    int size = cnt >> 14;
    int w = (size & 1) ? 512 : 256, h = (size & 2) ? 512 : 256;
    bx &= w - 1;
    by &= h - 1;
    u32 mapBase = 0x06000000u + ((disp >> 27) & 7) * 0x10000u + ((cnt >> 8) & 0x1F) * 0x800u;
    u32 charBase = 0x06000000u + ((disp >> 24) & 7) * 0x10000u + ((cnt >> 2) & 0xF) * 0x4000u;
    // The map is 32 x 32-tile blocks: right of the first, then (a 512-pixel-wide map) the two under them.
    int tx = bx >> 3, ty = by >> 3, block = 0;
    if (tx >= 32) { block += 1; tx -= 32; }
    if (ty >= 32) { block += w == 512 ? 2 : 1; ty -= 32; }
    u16 e = gpu.ReadVRAM_BG<u16>(mapBase + (u32) block * 0x800u + (u32) (ty * 32 + tx) * 2u);
    int px = bx & 7, py = by & 7;
    if (e & 0x400) px = 7 - px;
    if (e & 0x800) py = 7 - py;
    u32 tile = e & 0x3FF;
    u32 ci;
    if (cnt & 0x80) {
        ci = gpu.ReadVRAM_BG<u8>(charBase + tile * 64u + (u32) (py * 8 + px));
        if (!ci) return 0;
    } else {
        u8 b = gpu.ReadVRAM_BG<u8>(charBase + tile * 32u + (u32) (py * 4 + px / 2));
        ci = (px & 1) ? b >> 4 : b & 0xF;
        if (!ci) return 0;
        ci += (e >> 12) * 16u;
    }
    u16 c = (u16) (gpu.Palette[ci * 2] | (gpu.Palette[ci * 2 + 1] << 8));
    return (u16) (0x8000 | (c & 0x7FFF));
}

void pk_nds_wide_counts(int out[8])
{
    int n = 0;
    for (int i = 0; i < 9; i++)
        if (i != 4) out[n++] = wide.slices[i].lastCount;
}
