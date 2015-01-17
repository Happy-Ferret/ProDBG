#if 0

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <math.h>
#include <assert.h>
#include <stb_truetype.h>
#include "core/file.h"
#include "ui_render.h"

#include "scintilla/include/Platform.h"

//#ifdef SCI_NAMESPACE
//namespace Scintilla
//{
//#endif
//
//

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

inline uint32_t MakeRGBA(uint32_t r, uint32_t g, uint32_t b, uint32_t a=0xFF)
{
	return a<<24|b<<16|g<<8|r;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ColourDesired Platform::Chrome()
{
    return MakeRGBA(0xe0, 0xe0, 0xe0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ColourDesired Platform::ChromeHighlight()
{
    return MakeRGBA(0xff, 0xff, 0xff);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const char* Platform::DefaultFont()
{
    return "Lucida Console";
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int Platform::DefaultFontSize()
{
    return 10;
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

unsigned int Platform::DoubleClickTime()
{
    return 500;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Platform::MouseButtonBounce()
{
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if defined(__clang__)
__attribute__((noreturn)) void Platform::Assert(const char* error, const char* filename, int line)
#else
void Platform::Assert(const char* error, const char* filename, int line)
#endif
{
    printf("Assertion [%s] failed at %s %d\n", error, filename, line);
    assert(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SetClipboardTextUTF8(const char* text, size_t len, int additionalFormat)
{
    (void)text;
    (void)len;
    (void)additionalFormat;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int GetClipboardTextUTF8(char* text, size_t len)
{
    (void)text;
    (void)len;

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SurfaceImpl : public Surface
{
public:
    SurfaceImpl();
    virtual ~SurfaceImpl();

    bool InitBgfx();

	void Init(WindowID wid);
	void Init(SurfaceID sid, WindowID wid);
	void InitPixMap(int width, int height, Surface *surface_, WindowID wid);

	bool Initialised();

    void Release();
    void PenColour(ColourDesired fore);
    int LogPixelsY();
    int DeviceHeightFont(int points);
    void MoveTo(int x_, int y_);
    void LineTo(int x_, int y_);
    void Polygon(Point* pts, int npts, ColourDesired fore, ColourDesired back);
    void RectangleDraw(PRectangle rc, ColourDesired fore, ColourDesired back);
    void FillRectangle(PRectangle rc, ColourDesired back);
    void FillRectangle(PRectangle rc, Surface& surfacePattern);
    void RoundedRectangle(PRectangle rc, ColourDesired fore, ColourDesired back);
    void AlphaRectangle(PRectangle rc, int cornerSize, ColourDesired fill, int alphaFill, ColourDesired outline, int alphaOutline, int flags);
    void Ellipse(PRectangle rc, ColourDesired fore, ColourDesired back);

    //void DrawPixmap(PRectangle rc, Point from, Pixmap pixmap);
    void DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char* pixelsImage);

    void DrawTextBase(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired fore);
    void DrawTextNoClip(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired fore, ColourDesired back);
    void DrawTextClipped(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired  fore, ColourDesired back);
    void DrawTextTransparent(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired fore);
    void MeasureWidths(Font& font_, const char* s, int len, float* positions);
    float WidthText(Font& font_, const char* s, int len);
    float WidthChar(Font& font_, char ch);
    float Ascent(Font& font_);
    float Descent(Font& font_);
    float InternalLeading(Font& font_);
    float ExternalLeading(Font& font_);
    float Height(Font& font_);
    float AverageCharWidth(Font& font_);

    void SetClip(PRectangle rc);
    void FlushCachedState();

private:

    ColourDesired m_penColour;
    float m_x;
    float m_y;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct stbtt_Font
{
    stbtt_fontinfo fontinfo;
    stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs
	bgfx::TextureHandle ftex;
    float scale;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Font::Font() : fid(0)
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Font::~Font()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font::Create(const FontParameters& fp)
{
	stbtt_Font* newFont = new stbtt_Font;

	size_t len;

	// TODO: Remove hard-coded value

    unsigned char* bmp = new unsigned char[512*512];
	void* data = File_loadToMemory(fp.faceName, &len, 0);

	stbtt_BakeFontBitmap((unsigned char*)data, 0, fp.size, bmp, 512, 512, 32, 96, newFont->cdata); // no guarantee this fits!

    const bgfx::Memory* mem = bgfx::alloc(512 * 512);
    memcpy(mem->data, bmp, 512 * 512); 

    newFont->ftex = bgfx::createTexture2D(512, 512, 1, bgfx::TextureFormat::R8, BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_POINT, mem);

	stbtt_InitFont(&newFont->fontinfo, (unsigned char*)data, 0);

	newFont->scale = stbtt_ScaleForPixelHeight(&newFont->fontinfo, fp.size);

    delete [] bmp;

    fid = newFont;
}

/*

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct pixmap_t
{
	bgfx::TextureHandle tex;
    float scalex, scaley;
    bool initialised;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Pixmap CreatePixmap()
{
    Pixmap pm = new pixmap_t;
    pm->scalex = 0;
    pm->scaley = 0;
    pm->initialised = false;

    return pm;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool IsPixmapInitialised(Pixmap pixmap)
{
    return pixmap->initialised;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void DestroyPixmap(Pixmap pixmap)
{
    glDeleteTextures(1, &pixmap->tex);
    delete pixmap;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void UpdatePixmap(Pixmap pixmap, int w, int h, int* data)
{
	const size_t byteSize = w * h * sizeof(uint32_t);

	if (!pixmap->initilised)
    	pixmap->tex = bgfx::createTexture2D((uint16_t)w, (uint16_t)h, 1, bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_MIN_POINT | BGFX_TEXTURE_MAG_PONT, 0);

    const bgfx::Memory* mem = bgfx::alloc(byteSize);
    memcpy(mem->data, data, byteSize); 

	pixmap->initialised = true;
	pixmap->scalex = 1.0f / (float)w;
	pixmap->scaley = 1.0f / (float)h;

	bgfx::updateTexture2D(pixmap->tex, 0, 0, 0, (uint16_t)w, (uint16_t)h, mem);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawPixmap(PRectangle rc, Point offset, Pixmap pixmap)
{
	bgfx::TransientVertexBuffer tvb;

    float w = (rc.right - rc.left) * pixmap->scalex, h = (rc.bottom - rc.top) * pixmap->scaley;
    float u1 = offset.x * pixmap->scalex, v1 = offset.y * pixmap->scaley, u2 = u1 + w, v2 = v1 + h;

    // TODO: Use program that doesn't set color 

	UIRender_allocPosTexColorTb(&tvb, 6);

	PosTexColorVertex* vb = (PosTexColorVertex*)tbv.data;

	// First triangle

	vb[0].x = rc.left;
	vb[0].y = rc.top;
	vb[0].u = u1;
	vb[0].v = v1;
	vb[0].color = 0xffffffff;

	vb[1].x = rc.right;
	vb[1].y = rc.top;
	vb[1].u = u2;
	vb[1].v = v1;
	vb[1].color = 0xffffffff;

	vb[2].x = rc.right;
	vb[2].y = rc.bottom;
	vb[2].u = u2;
	vb[2].v = v2;
	vb[2].color = 0xffffffff;

	// Second triangle

	vb[3].x = rc.left;
	vb[3].y = rc.top;
	vb[3].u = u1;
	vb[3].v = v1;
	vb[3].color = 0xffffffff;

	vb[4].x = rc.right;
	vb[4].y = rc.bottom;
	vb[4].u = u2;
	vb[4].v = v2;
	vb[4].color = 0xffffffff;

	vb[5].x = rc.left;
	vb[5].y = rc.bottom;
	vb[5].u = u1;
	vb[5].v = v2;
	vb[5].color = 0xffffffff;

	UIRender_posTexColor(&tvb, 0, 6, pixmap->tex);
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool SurfaceImpl::Initialised()
{
	return true;
}	

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::Init(WindowID wid)
{
	(void)wid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::Init(SurfaceID sid, WindowID wid)
{
	(void)wid;
	(void)sid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::InitPixMap(int width, int height, Surface *surface_, WindowID wid)
{
	(void)width;
	(void)height;
	(void)surface_;
	(void)wid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawRGBAImage(PRectangle rc, int width, int height, const unsigned char* pixelsImage)
{
	(void)rc;
	(void)width;
	(void)height;
	(void)pixelsImage;

    assert(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::FillRectangle(PRectangle rc, ColourDesired b)
{
	bgfx::TransientVertexBuffer tvb;

	const uint32_t back = (uint32_t)b.AsLong();

	UIRender_allocPosColorTb(&tvb, 6);

	PosColorVertex* vb = (PosColorVertex*)tvb.data;

	// First triangle

	vb[0].x = rc.left;
	vb[0].y = rc.top;
	vb[0].color = back;

	vb[1].x = rc.right;
	vb[1].y = rc.top;
	vb[1].color = back;

	vb[2].x = rc.right;
	vb[2].y = rc.bottom;
	vb[2].color = back;

	// Second triangle

	vb[3].x = rc.left;
	vb[3].y = rc.top;
	vb[3].color = back;

	vb[4].x = rc.right;
	vb[4].y = rc.bottom;
	vb[4].color = back;

	vb[5].x = rc.left;
	vb[5].y = rc.bottom;
	vb[5].color = back;

	UIRender_posColor(&tvb, 0, 6);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::FillRectangle(PRectangle, Surface&)
{
    assert(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::RoundedRectangle(PRectangle, ColourDesired, ColourDesired)
{
    assert(false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::AlphaRectangle(
    PRectangle rc, int cornerSize, ColourDesired fill, int alphaFill,
    ColourDesired outline, int alphaOutline, int flags)
{
    unsigned int back = (uint32_t)((fill.AsLong() & 0xffffff) | ((alphaFill & 0xff) << 24));

    (void)cornerSize;
    (void)outline;
    (void)alphaOutline;
    (void)flags;

	FillRectangle(rc, ColourDesired(back));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::Ellipse(PRectangle, ColourDesired, ColourDesired)
{
    assert(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Font::Release()
{
    if (fid)
    {
        free(((stbtt_Font*)fid)->fontinfo.data);
        delete (stbtt_Font*)fid;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawTextBase(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired f)
{
	uint32_t realLength = 0;
    float x = rc.left;
	float y = ybase;

	uint32_t fore = (uint32_t)f.AsLong();

	bgfx::TransientVertexBuffer tvb;

    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();

	UIRender_allocPosColorTb(&tvb, (uint32_t)(len * 2)); // * 2 as triangles

	PosTexColorVertex* vb = (PosTexColorVertex*)tvb.data;

    while (len--)
    {
        if (*s >= 32 && *s < 127)
        {
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(realFont->cdata, 512, 512, *s - 32, &x, &y, &q, 1);
			
			// First triangle

			vb[0].x = q.x0; 
			vb[0].y = q.y0; 
			vb[0].u = q.s0; 
			vb[0].v = q.s0; 
			vb[0].color = fore;

			vb[1].x = q.x1; 
			vb[1].y = q.y0; 
			vb[1].u = q.s1; 
			vb[1].v = q.s0; 
			vb[1].color = fore;

			vb[2].x = q.x1; 
			vb[2].y = q.y1; 
			vb[2].u = q.s1; 
			vb[2].v = q.s1; 
			vb[2].color = fore;

			// Second triangle
			
			vb[3].x = q.x0; 
			vb[3].y = q.y0; 
			vb[3].u = q.s0; 
			vb[3].v = q.s0; 
			vb[3].color = fore;

			vb[4].x = q.x1; 
			vb[4].y = q.y0; 
			vb[4].u = q.s1; 
			vb[4].v = q.s0; 
			vb[4].color = fore;

			vb[5].x = q.x0; 
			vb[5].y = q.y1; 
			vb[5].u = q.s0; 
			vb[5].v = q.s1; 
			vb[5].color = fore;

			vb += 6;
			realLength++;
        }

        ++s;
    }

	UIRender_posTexColor(&tvb, 0, realLength, realFont->ftex);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawTextNoClip(PRectangle rc, Font& font_, float ybase, const char* s, int len,
                                 ColourDesired fore, ColourDesired /*back*/)
{
    DrawTextBase(rc, font_, ybase, s, len, fore);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawTextClipped(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired fore, ColourDesired /*back*/)
{
    DrawTextBase(rc, font_, ybase, s, len, fore);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::DrawTextTransparent(PRectangle rc, Font& font_, float ybase, const char* s, int len, ColourDesired fore)
{
    DrawTextBase(rc, font_, ybase, s, len, fore);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::MeasureWidths(Font& font_, const char* s, int len, float* positions)
{
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();

    //TODO: implement proper UTF-8 handling

    float position = 0;
    while (len--)
    {
        int advance, leftBearing;

        stbtt_GetCodepointHMetrics(&realFont->fontinfo, *s++, &advance, &leftBearing);

        position += advance;//TODO: +Kerning
        *positions++ = position * realFont->scale;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::WidthText(Font& font_, const char* s, int len)
{
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();

    //TODO: implement proper UTF-8 handling

    float position = 0;
    while (len--)
    {
        int advance, leftBearing;
        stbtt_GetCodepointHMetrics(&realFont->fontinfo, *s++, &advance, &leftBearing);
        position += advance * realFont->scale;//TODO: +Kerning
    }

    return position;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::WidthChar(Font& font_, char ch)
{
    int advance, leftBearing;
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();
    stbtt_GetCodepointHMetrics(&realFont->fontinfo, ch, &advance, &leftBearing);

    return advance * realFont->scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::Ascent(Font& font_)
{
    int ascent, descent, lineGap;
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();
    stbtt_GetFontVMetrics(&realFont->fontinfo, &ascent, &descent, &lineGap);

    return ascent * realFont->scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::Descent(Font& font_)
{
    int ascent, descent, lineGap;
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();
    stbtt_GetFontVMetrics(&realFont->fontinfo, &ascent, &descent, &lineGap);

    return -descent * realFont->scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::InternalLeading(Font&)
{
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::ExternalLeading(Font& font_)
{
    stbtt_Font* realFont = (stbtt_Font*)font_.GetID();
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&realFont->fontinfo, &ascent, &descent, &lineGap);
    return lineGap * realFont->scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::Height(Font& font_)
{
    return Ascent(font_) + Descent(font_);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float SurfaceImpl::AverageCharWidth(Font& font_)
{
    return WidthChar(font_, 'n');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::SetClip(PRectangle rc)
{
	bgfx::setScissor((uint16_t)rc.left, (uint16_t)rc.right, (uint16_t)rc.top, (uint16_t)rc.bottom);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void SurfaceImpl::FlushCachedState()
{
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Surface* Surface::Allocate(int technology)
{
	(void)technology;
    return new SurfaceImpl;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#endif
