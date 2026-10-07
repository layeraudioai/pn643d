#include "stdafx.h"
#include "Graphics/GraphicsContext.h"
#include "SysCTR/Utility/CTRHeadTracking.h"
#include "SysCTR/Graphics/CTRVertexBuffers.h"
#include "SysCTR/Graphics/CTRRenderConfig.h"

#include <3ds.h>
#include <GL/picaGL.h>

#include "Config/ConfigOptions.h"
#include "Core/ROM.h"
#include "Debug/DBGConsole.h"
#include "Debug/Dump.h"
#include "Graphics/ColourValue.h"
#include "Graphics/PngUtil.h"
#include "Utility/IO.h"
#include "Utility/Preferences.h"
#include "Utility/Profiler.h"
#include "Utility/VolatileMem.h"

#include "SysCTR/UI/InGameMenu.h"

extern void HandleEndOfFrame();

#define SCR_WIDTH CTR_GAME_VIEW_WIDTH
#define SCR_HEIGHT CTR_GAME_VIEW_HEIGHT

#define RATIO_4_3 0
#define RATIO_5_3 1

uint8_t aspectRatio = RATIO_5_3;

u32 CTRGetRenderWidth()
{
#if defined(DAEDALUS_MINIMAL_EMULATOR)
	return CTR_GAME_VIEW_WIDTH;
#else
	return gGlobalPreferences.CTRRenderWidth;
#endif
}

u32 CTRGetRenderHeight()
{
#if defined(DAEDALUS_MINIMAL_EMULATOR)
	return CTR_GAME_VIEW_HEIGHT;
#else
	return gGlobalPreferences.CTRRenderHeight;
#endif
}

uint32_t  gVertexCount = 0;
float    *gVertexBuffer;
uint32_t *gColorBuffer;
float    *gTexCoordBuffer;
float    *gVertexBufferPtr;
uint32_t *gColorBufferPtr;
float    *gTexCoordBufferPtr;

class IGraphicsContext : public CGraphicsContext
{
public:
	IGraphicsContext();
	virtual ~IGraphicsContext();

	bool				Initialise();
	bool				IsInitialised() const { return mInitialised; }

	void				SwitchToChosenDisplay();
	void				SwitchToLcdDisplay();
	void				StoreSaveScreenData();

	void				ClearAllSurfaces();

	void				ClearToBlack();
	void				ClearZBuffer();
	void				ClearColBuffer(const c32 &colour);
	void				ClearColBufferAndDepth(const c32 &colour);

	void				BeginFrame();
	void				EndFrame();
	void				UpdateFrame(bool wait_for_vbl);
	void				GetScreenSize(u32 * width, u32 * height) const;

	void				SetDebugScreenTarget( ETargetSurface buffer );

	void				ViewportType(u32 *d_width, u32 *d_height) const;
	void				DumpScreenShot();
	void				DumpNextScreen()			{ mDumpNextScreen = 2; }

private:
	void				SaveScreenshot(const char* filename, s32 x, s32 y, u32 width, u32 height);

private:
	bool				mInitialised;

	u32					mDumpNextScreen;
};

//*************************************************************************************
//
//*************************************************************************************
template<> bool CSingleton< CGraphicsContext >::Create()
{
#ifdef DAEDALUS_ENABLE_ASSERTS
	DAEDALUS_ASSERT_Q(mpInstance == nullptr);
#endif
	mpInstance = new IGraphicsContext();
	if (!mpInstance->Initialise())
	{
		delete mpInstance;
		mpInstance = nullptr;
		return false;
	}
	return true;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

IGraphicsContext::IGraphicsContext()
	:	mInitialised(false)
	,	mDumpNextScreen(false)
{	
	gVertexBufferPtr = (float*)linearAlloc(CTR_VERTEX_BUFFER_CAPACITY * 3 * sizeof(float));
	gColorBufferPtr = (uint32_t*)linearAlloc(CTR_VERTEX_BUFFER_CAPACITY * sizeof(uint32_t));
	gTexCoordBufferPtr = (float*)linearAlloc(CTR_VERTEX_BUFFER_CAPACITY * 2 * sizeof(float));

	gVertexBuffer = gVertexBufferPtr;
	gColorBuffer = gColorBufferPtr;
	gTexCoordBuffer = gTexCoordBufferPtr;
}

IGraphicsContext::~IGraphicsContext()
{
	if (gVertexBufferPtr) linearFree(gVertexBufferPtr);
	if (gColorBufferPtr) linearFree(gColorBufferPtr);
	if (gTexCoordBufferPtr) linearFree(gTexCoordBufferPtr);
	gVertexBufferPtr = nullptr;
	gColorBufferPtr = nullptr;
	gTexCoordBufferPtr = nullptr;
	gVertexBuffer = nullptr;
	gColorBuffer = nullptr;
	gTexCoordBuffer = nullptr;
}

bool IGraphicsContext::Initialise()
{
	if (!gVertexBufferPtr || !gColorBufferPtr || !gTexCoordBufferPtr)
		return false;

	mInitialised = true;

	pglSelectScreen(GFX_TOP, GFX_LEFT);
	ClearAllSurfaces();

	return true;
}

void IGraphicsContext::ClearAllSurfaces()
{
	ClearToBlack();
	pglSwapBuffers();
	ClearToBlack();
	pglSwapBuffers();
}

void IGraphicsContext::ClearToBlack()
{
	glDepthMask(GL_TRUE);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth( 1.0f );
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void IGraphicsContext::ClearZBuffer()
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClear( GL_DEPTH_BUFFER_BIT );
}

void IGraphicsContext::ClearColBuffer(const c32 & colour)
{
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT );
}

void IGraphicsContext::ClearColBufferAndDepth(const c32 & colour)
{
	glDepthMask(GL_TRUE);
	glClearDepth( 1.0f );
	glClearColor( colour.GetRf(), colour.GetGf(), colour.GetBf(), colour.GetAf() );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
}

void IGraphicsContext::BeginFrame()
{
	CTRHeadTracking::Update();
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	glVertexPointer(3, GL_FLOAT, 0, gVertexBufferPtr);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, gColorBufferPtr);
	glTexCoordPointer(2, GL_FLOAT, 0, gTexCoordBufferPtr);
}

void IGraphicsContext::EndFrame()
{
	HandleEndOfFrame();
}

void IGraphicsContext::UpdateFrame(bool wait_for_vbl)
{
	pglSwapBuffers();
	UI::DrawInGameMenu();
	//gfxSwapBuffersGpu();

	ClearToBlack();

	gVertexBuffer = gVertexBufferPtr;
	gColorBuffer = gColorBufferPtr;
	gTexCoordBuffer = gTexCoordBufferPtr;
	gVertexCount = 0;
}

void IGraphicsContext::SetDebugScreenTarget(ETargetSurface buffer)
{

}

void IGraphicsContext::ViewportType(u32 *d_width, u32 *d_height) const
{
	*d_width = CTRGetRenderWidth();
	*d_height = CTRGetRenderHeight();
}

void IGraphicsContext::SaveScreenshot(const char* filename, s32 x, s32 y, u32 width, u32 height)
{
}

void IGraphicsContext::DumpScreenShot()
{
}

void IGraphicsContext::StoreSaveScreenData()
{
}

void IGraphicsContext::GetScreenSize(u32 * p_width, u32 * p_height) const
{
	*p_width = CTRGetRenderWidth();
	*p_height = CTRGetRenderHeight();
}
