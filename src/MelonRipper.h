#ifndef MELONRIPPER_H
#define MELONRIPPER_H

#include <vector>

namespace melonDS
{
class NDS;
class Vertex;

class MelonRipper
{
public:
    explicit MelonRipper(melonDS::NDS& nds) noexcept : NDS(nds) {}
    void Reset() noexcept;

    void RequestRip(int count = 1) { RequestCount += count; }
    bool IsDumping() const { return DumpPolys; }

    // render commands submitted to back buffer
    void Polygon(Vertex verts[4], int nverts);
    void TexParam(u32);
    void TexPalette(u32);
    void PolygonAttr(u32);

    void NotifyFlush();   // front/back buffers swapped
    void NotifyRender();  // front buffer renderer

private:
    melonDS::NDS& NDS;

    void InitBackRip();
    void FinishFrontRip();
    void SaveFrontRipToFile() const;

    // Number of rips requested.
    int RequestCount = 0;

    // Whether render commands are being dumped into BackRip.
    //
    // It's possible for RequestCount > 0 but we aren't dumping.
    // If a request comes in the middle of a frame we want to
    // wait until the next frame starts before we start dumping,
    // so we don't wind up with a partial frame.
    bool DumpPolys = false;

    // The GPU3D has two poly buffers. While the game submits
    // polygon commands to the back buffer, the DS is rendering
    // from the front buffer. A flush command swaps the buffers.
    //
    // We also have two rips, corresponding to the front and
    // back buffers. Polys are recorded into BackRip when they
    // are submitted to the back buffer, BackRip is moved to
    // FrontRip when the buffers swap, and FrontRip is finalized
    // and written out when the front buffer is rendered.
    //
    // The reason we need to wait for the front buffer to be
    // rendered is we need to know the GPU/VRAM state *at the
    // time the polys are rendered*.
    std::vector<u8> BackRip, FrontRip;
};
}

#endif
