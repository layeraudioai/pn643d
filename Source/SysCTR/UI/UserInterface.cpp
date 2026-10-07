#include <3ds.h>
#include <GL/picaGL.h>
#include <stdio.h>
#include <math.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "UserInterface.h"
#include "SysCTR/Input/CTRInput.h"

static stbtt_bakedchar charData[96];

static GLuint fontTex;
// picaGL texture memory can be reused by system applets; keep the baked atlas
// in linear app memory so it can be uploaded again when the app resumes.
static uint32_t fontBitmap[256 * 256];
static bool fontReloadPending = false;
static aptHookCookie uiAptHook;

static void UIAptHook(APT_HookType type, void *param)
{
	(void)param;
	if (type == APTHOOK_ONRESTORE)
		fontReloadPending = true;
}

static uint32_t _keysDown = 0;
static uint32_t _keysHeld = 0;
// Sample the touchscreen once per UI frame; DrawButton/DrawToggle are called
// many times while rendering one page. Minimal builds retain touch UI actions;
// only the optional settings pages are removed from their menus.
static touchPosition sTouchPosition = {};

static uint32_t GetStringWidth(const char* text)
{
	uint32_t stringWidth = 0;

	while (*text) 
	{
		if (*text >= 32 && *text < 128)
		{
			stbtt_bakedchar *b = charData + (*text-32);
			stringWidth += b->xadvance;

			if(*text == ' ')
				stringWidth += 3;
		}
		++text;
	}

	return stringWidth;
}

//Loads the font
void UI::Initialize()
{
	uint8_t* ttfBuffer = new uint8_t[1<<20];
	uint8_t* bitmap_u8 = new uint8_t[256*256];

	FILE *fontFile = fopen("romfs:/kenvector_future.ttf", "rb");

	if(fontFile == NULL)
		exit(0);

	fread(ttfBuffer, 1, 1<<20, fontFile);

	stbtt_BakeFontBitmap(ttfBuffer,0, 14.0, bitmap_u8, 256, 256, 32, 96, charData); // no guarantee this fits!

	for(uint32_t i =0; i < 256 * 256; i++)
	{
		fontBitmap[i] = (0x00ffffff) | (bitmap_u8[i] << 24);
	}

	glGenTextures(1, &fontTex);
	glBindTexture(GL_TEXTURE_2D, fontTex);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256,256, 0, GL_RGBA, GL_UNSIGNED_BYTE, fontBitmap);

	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	delete [] ttfBuffer;
	delete [] bitmap_u8;
	aptHook(&uiAptHook, UIAptHook, NULL);
}

void UI::RestoreRenderState()
{
	pglSelectScreen(GFX_BOTTOM, GFX_LEFT);

	if (fontReloadPending && fontTex != 0)
	{
		glBindTexture(GL_TEXTURE_2D, fontTex);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0,
			GL_RGBA, GL_UNSIGNED_BYTE, fontBitmap);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		fontReloadPending = false;
	}

	glClearColor(0.1f, 0.1f, 0.1f, 0.0f);

	glViewport(0,0,320,240);

	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glDisable(GL_CULL_FACE);
	glDisable(GL_ALPHA_TEST);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_SCISSOR_TEST);

	glEnable(GL_TEXTURE_2D);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glMatrixMode(GL_PROJECTION);
	// Always rebuild the UI projection from identity rather than multiplying
	// onto stale state left by a screen/graphics context transition.
	glLoadIdentity();
	glOrtho(0, 320, 240, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// libctru defines the compatibility macro `keysHeld` as `hidKeysHeld`.
	// Avoid that identifier here: the macro would rewrite the declaration and
	// shadow the hidKeysHeld() function on newer SDKs.
	const uint32_t heldKeyMask = hidKeysHeld();
	// Report only newly pressed keys. XOR also reports key releases, which can
	// replay a touch on whatever menu control is drawn after a modal dialog.
	_keysDown = heldKeyMask & ~_keysHeld;
	_keysHeld = heldKeyMask;
	hidTouchRead(&sTouchPosition);
}

void UI::ClearSecondScreen(unsigned screen)
{
	pglSelectScreen(screen, GFX_LEFT);
	glClear(GL_COLOR_BUFFER_BIT);
	pglSwapBuffers();
	pglSelectScreen(!screen, GFX_LEFT);
}

void UI::DrawHeader(const char *title)
{
	glDisable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
		glColor3f(0.1f, 0.6f, 0.5f);
		glVertex2f(0, 0);
		glVertex2f(320, 0);
		glVertex2f(0, 12);
		glVertex2f(320, 12);
	glEnd();

	glColor3f(0.9f, 0.9f, 0.9f);
	UI::DrawText(4, 10, title);
}

bool UI::DrawButton(float x, float y, float width, float height, const char *text)
{
	glDisable(GL_TEXTURE_2D);

	glBegin(GL_TRIANGLE_STRIP);
		glColor3f(0.15f, 0.5f, 0.75f);
		glVertex2f(x, y);
		glVertex2f(x+width, y);
		glVertex2f(x, y+height);
		glVertex2f(x+width, y+height);
	glEnd();

	glColor3f(0.9f, 0.9f, 0.9f);

	float tY = y + (height/2) + 6;
	float tX = x + (width/2) - (GetStringWidth(text) / 2);

	UI::DrawText(tX, tY, text);

	if (_keysDown & KEY_TOUCH)
	{
		if (sTouchPosition.px > x && sTouchPosition.px < (x + width) &&
			sTouchPosition.py > y && sTouchPosition.py < (y + height))
			return true;
	}

	return false;
}

bool UI::DrawToggle(float x, float y, float width, float height, const char *text, bool isToggled)
{
	glDisable(GL_TEXTURE_2D);

	if(isToggled)
		glColor3f(0.1f, 0.6f, 0.5f);
	else
		glColor3f(0.75f, 0.2f, 0.15f);

	glBegin(GL_TRIANGLE_STRIP);
		glVertex2f(x, y);
		glVertex2f(x+width, y);
		glVertex2f(x, y+height);
		glVertex2f(x+width, y+height);
	glEnd();

	glColor3f(0.9f, 0.9f, 0.9f);

	float tY = y + (height/2) + 6;
	float tX = x + (width/2) - (GetStringWidth(text) / 2);

	UI::DrawText(tX, tY, text);

	if (_keysDown & KEY_TOUCH)
	{
		if (sTouchPosition.px > x && sTouchPosition.px < (x + width) &&
			sTouchPosition.py > y && sTouchPosition.py < (y + height))
			return true;
	}

	return false;
}

void UI::DrawVirtualStick(float x, float y, bool active)
{
	// Fixed unit-circle points avoid 48 libm calls per frame on ARM11.
	static const float circle[24][2] = {
		{ 1.0000000f,  0.0000000f}, { 0.9659258f,  0.2588190f},
		{ 0.8660254f,  0.5000000f}, { 0.7071068f,  0.7071068f},
		{ 0.5000000f,  0.8660254f}, { 0.2588190f,  0.9659258f},
		{ 0.0000000f,  1.0000000f}, {-0.2588190f,  0.9659258f},
		{-0.5000000f,  0.8660254f}, {-0.7071068f,  0.7071068f},
		{-0.8660254f,  0.5000000f}, {-0.9659258f,  0.2588190f},
		{-1.0000000f,  0.0000000f}, {-0.9659258f, -0.2588190f},
		{-0.8660254f, -0.5000000f}, {-0.7071068f, -0.7071068f},
		{-0.5000000f, -0.8660254f}, {-0.2588190f, -0.9659258f},
		{ 0.0000000f, -1.0000000f}, { 0.2588190f, -0.9659258f},
		{ 0.5000000f, -0.8660254f}, { 0.7071068f, -0.7071068f},
		{ 0.8660254f, -0.5000000f}, { 0.9659258f, -0.2588190f}
	};
	const float radius = (float)CTR_TOUCH_STICK_RADIUS;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0.15f, 0.75f, 0.95f, active ? 0.9f : 0.45f);
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < 24; ++i)
		glVertex2f(x + circle[i][0] * radius, y + circle[i][1] * radius);
	glEnd();
	glBegin(GL_LINES);
		glVertex2f(x - 8.0f, y); glVertex2f(x + 8.0f, y);
		glVertex2f(x, y - 8.0f); glVertex2f(x, y + 8.0f);
	glEnd();
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void UI::DrawVirtualStickPreview(float x, float y, bool active)
{
	const float radius = (float)CTR_TOUCH_STICK_RADIUS * 0.5f;
	glDisable(GL_TEXTURE_2D);
	glColor4f(0.15f, 0.75f, 0.95f, active ? 0.9f : 0.45f);
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i < 16; ++i)
	{
		const float angle = (float)i * 6.28318530718f / 16.0f;
		glVertex2f(x + cosf(angle) * radius, y + sinf(angle) * radius);
	}
	glEnd();
	glBegin(GL_LINES);
		glVertex2f(x - 5.0f, y); glVertex2f(x + 5.0f, y);
		glVertex2f(x, y - 5.0f); glVertex2f(x, y + 5.0f);
	glEnd();
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
}

void UI::DrawText(float x, float y, const char *text)
{
	if (text == NULL || *text == '\0')
		return;

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, fontTex);

	// A menu label used to submit one PICA immediate-mode batch per glyph.
	// Emit all glyph quads as triangles in one batch instead. The two triangles
	// per glyph are independent, so no degenerate connector vertices are needed.
	glBegin(GL_TRIANGLES);
	while (*text)
	{
		if (*text >= 32 && *text < 128)
		{
			stbtt_aligned_quad q;
			stbtt_GetBakedQuad(charData, 256, 256, *text - 32, &x, &y, &q, 1);

			glTexCoord2f(q.s0, q.t0); glVertex2f(q.x0, q.y0);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
			glTexCoord2f(q.s0, q.t1); glVertex2f(q.x0, q.y1);
			glTexCoord2f(q.s1, q.t0); glVertex2f(q.x1, q.y0);
			glTexCoord2f(q.s1, q.t1); glVertex2f(q.x1, q.y1);
		}
		++text;
	}
	glEnd();
}