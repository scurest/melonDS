#include <chrono>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "GPU3D.h"
#include "NDS.h"
#include "MelonRipper.h"

namespace melonDS
{
using VecU8 = std::vector<u8>;

static void WriteMagic(VecU8& rip)
{
    const char magic[24] = "melon ripper v2";
    rip.insert(rip.begin(), &magic[0], &magic[sizeof(magic)]);
}

static void WriteOpcode(VecU8& rip, const char* op)
{
    rip.insert(rip.end(), op, op + 4);
}

static void WriteU16(VecU8& rip, u16 x)
{
    rip.push_back(x & 0xFF);
    rip.push_back(x >> 8);
}

static void WriteU32(VecU8& rip, u32 x)
{
    rip.push_back((x >> 0) & 0xFF);
    rip.push_back((x >> 8) & 0xFF);
    rip.push_back((x >> 16) & 0xFF);
    rip.push_back((x >> 24) & 0xFF);
}

static void WritePolygon(VecU8& rip, Vertex verts[4], int nverts)
{
    WriteOpcode(rip, nverts == 3 ? "TRI " : "QUAD");

    for (int i = 0; i != nverts; ++i)
    {
        const Vertex& v = verts[i];
        WriteU32(rip, v.WorldPosition[0]);
        WriteU32(rip, v.WorldPosition[1]);
        WriteU32(rip, v.WorldPosition[2]);
        WriteU32(rip, v.Color[0]);
        WriteU32(rip, v.Color[1]);
        WriteU32(rip, v.Color[2]);
        WriteU16(rip, v.TexCoords[0]);
        WriteU16(rip, v.TexCoords[1]);
    }
}

static void WriteTexParam(VecU8& rip, u32 tex_param)
{
    WriteOpcode(rip, "TPRM");
    WriteU32(rip, tex_param);
}

static void WriteTexPalette(VecU8& rip, u32 tex_pal)
{
    WriteOpcode(rip, "TPLT");
    WriteU32(rip, tex_pal);
}

static void WritePolygonAttr(VecU8& rip, u32 attr)
{
    WriteOpcode(rip, "PATR");
    WriteU32(rip, attr);
}

static void WriteVRAM(VecU8& rip, const melonDS::GPU& GPU)
{
    WriteOpcode(rip, "VRAM");

    for (int i = 0; i != 4; ++i)
        WriteU32(rip, GPU.VRAMMap_Texture[i]);

    for (int i = 0; i != 8; ++i)
        WriteU32(rip, GPU.VRAMMap_TexPal[i]);

    auto DumpBank = [&](const u8* bank, size_t nkb) {
        rip.insert(rip.end(), bank, bank + nkb * 1024);
    };
    DumpBank(GPU.VRAM_A, 128);
    DumpBank(GPU.VRAM_B, 128);
    DumpBank(GPU.VRAM_C, 128);
    DumpBank(GPU.VRAM_D, 128);
    DumpBank(GPU.VRAM_E, 64);
    DumpBank(GPU.VRAM_F, 16);
    DumpBank(GPU.VRAM_G, 16);
}

static void WriteDispCnt(VecU8& rip, u32 disp_cnt)
{
    WriteOpcode(rip, "DISP");
    WriteU32(rip, disp_cnt);
}

static void WriteToonTable(VecU8& rip, u16 toon_table[32])
{
    WriteOpcode(rip, "TOON");
    for (int i = 0; i != 32; ++i)
        WriteU16(rip, toon_table[i]);
}

void MelonRipper::Reset() noexcept
{
    RequestCount = 0;
    DumpPolys = false;
    BackRip.clear();
    FrontRip.clear();
}

void MelonRipper::Polygon(Vertex verts[4], int nverts)
{
    WritePolygon(BackRip, verts, nverts);
}

void MelonRipper::TexParam(u32 tex_param)
{
    WriteTexParam(BackRip, tex_param);
}

void MelonRipper::TexPalette(u32 tex_pal)
{
    WriteTexPalette(BackRip, tex_pal);
}

void MelonRipper::PolygonAttr(u32 attr)
{
    WritePolygonAttr(BackRip, attr);
}

void MelonRipper::NotifyFlush()
{
    if (DumpPolys)
    {
        // Move BackRip to FrontRip and consider one request
        // finished.
        std::swap(BackRip, FrontRip);
        BackRip.clear();
        RequestCount--;
    }

    DumpPolys = false;

    if (RequestCount > 0)
    {
        InitBackRip();
        DumpPolys = true;
    }
}

void MelonRipper::NotifyRender()
{
    if (!FrontRip.empty())
    {
        FinishFrontRip();
        SaveFrontRipToFile();
        FrontRip.clear();
    }
}

void MelonRipper::InitBackRip()
{
    BackRip.clear();
    BackRip.reserve(1 * 1024 * 1024);  // 1 MB
    WriteMagic(BackRip);
}

void MelonRipper::FinishFrontRip()
{
    WriteVRAM(FrontRip, NDS.GPU);
    WriteDispCnt(FrontRip, NDS.GPU.GPU3D.RenderDispCnt);
    WriteToonTable(FrontRip, NDS.GPU.GPU3D.RenderToonTable);
}

static char ConvertToFilenameChar(char c)
{
    if (('0' <= c && c <= '9') || ('a' <= c && c <= 'z'))
        return c;
    if ('A' <= c && c <= 'Z')
        return c - 'A' + 'a';
    return 0;
}

static void GetGameTitleForFilename(char title[13], const melonDS::NDS& NDS)
{
    title[0] = 0;

    const auto cart = NDS.NDSCartSlot.GetCart();
    if (cart)
    {
        const auto& hdr = cart->GetHeader();
        int j = 0;
        for (int i = 0; i != 12; i++)
        {
            char c = hdr.GameTitle[i];
            c = ConvertToFilenameChar(c);
            if (c) title[j++] = c;
        }
        title[j] = 0;
    }

    // Default name if empty for some reason
    if (title[0] == 0)
        strcpy(title, "melonrip");
}

static void GetTimeWithMilliseconds(time_t& t, int& millis)
{
    namespace chrono = std::chrono;

    auto now = chrono::system_clock::now();
    t = chrono::system_clock::to_time_t(now);

    auto d = now.time_since_epoch();
    auto secs = chrono::duration_cast<chrono::seconds>(d);
    millis = chrono::duration_cast<chrono::milliseconds>(d - secs).count();
}

static void GetDumpFileName(char* filename, size_t len, const melonDS::NDS& NDS)
{
    time_t t;
    int millis;
    GetTimeWithMilliseconds(t, millis);

    char datetime[32];
    struct tm *tm = localtime(&t);
    strftime(datetime, sizeof(datetime), "%Y-%m-%d-%H-%M-%S", tm);

    char title[13];
    GetGameTitleForFilename(title, NDS);

    snprintf(filename, len, "%s-%s-%03d.dump", title, datetime, millis);
}

void MelonRipper::SaveFrontRipToFile() const
{
    char filename[64];
    GetDumpFileName(filename, sizeof(filename), NDS);

    bool ok = false;
    FILE* fp = fopen(filename, "wb+");
    if (fp)
    {
        if (fwrite(FrontRip.data(), FrontRip.size(), 1, fp) == 1)
            ok = true;
        fclose(fp);
    }

    if (ok)
        printf("MelonRipper: ripped %s\n", filename);
    else
        printf("MelonRipper: error writing %s\n", filename);
}
}
