#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "vshader_shbin.h"
#include "clear_shbin.h"
static aptHookCookie _hookCookie;
const void *pglCachedVertexShader=NULL,*pglCachedClearShader=NULL; size_t pglCachedVertexShaderSize=0,pglCachedClearShaderSize=0;
static void _AptEventHook(APT_HookType type,void*param){(void)param;if(type==APTHOOK_ONSUSPEND)_queueWaitAndClear();else if(type==APTHOOK_ONRESTORE){GX_BindQueue(&pglState->gxQueue);gxCmdQueueRun(&pglState->gxQueue);_picaRenderBuffer(pglState->colorBuffer[pglState->activeEye],pglState->depthBuffer[pglState->activeEye]);_picaAttribBuffersLocation((void*)__ctru_linear_heap);for(int i=1;i<6;i++)_picaTextureEnvSet(i,&pglState->texenv[PGL_TEXENV_DUMMY]);shaderProgramUse(&pglState->basicShader);pglState->changes=0xFFFFFFFF;}}
void pglInit(){static int initialized=0;if(initialized)return;initialized=1;pglState=malloc(sizeof(picaGLState));memset(pglState,0,sizeof(picaGLState));_stateInitialize();_stateDefault();aptHook(&_hookCookie,_AptEventHook,NULL);}
void pglExit(){aptUnhook(&_hookCookie);_queueWaitAndClear();GX_BindQueue(NULL);free((void*)pglCachedVertexShader);free((void*)pglCachedClearShader);pglCachedVertexShader=NULL;pglCachedClearShader=NULL;}
static void _pglTransfer(uint32_t*out,uint8_t fmt,int eye){if(pglState->display==GFX_TOP)GX_DisplayTransfer((u32*)pglState->colorBuffer[eye],GX_BUFFER_DIM(240,400),out,GX_BUFFER_DIM(240,400),GX_TRANSFER_OUT_FORMAT(fmt));else GX_DisplayTransfer((u32*)pglState->colorBuffer[0]+240*80,GX_BUFFER_DIM(240,320),out,GX_BUFFER_DIM(240,320),GX_TRANSFER_OUT_FORMAT(fmt));}
void pglSwapBuffers(){glFlush();uint8_t fmt=gfxGetScreenFormat(pglState->display);bool stereo=pglState->display==GFX_TOP&&gfxIs3D()&&pglState->stereoEnabled;if(stereo){uint32_t*l=(uint32_t*)gfxGetFramebuffer(GFX_TOP,GFX_LEFT,NULL,NULL);uint32_t*r=(uint32_t*)gfxGetFramebuffer(GFX_TOP,GFX_RIGHT,NULL,NULL);_pglTransfer(l,fmt,0);_pglTransfer(r,fmt,1);memset(pglState->colorBuffer[1],0,400*240*4);memset(pglState->depthBuffer[1],0xFF,400*240*4);}else{uint32_t*out=(uint32_t*)gfxGetFramebuffer(pglState->display,pglState->display_side,NULL,NULL);_pglTransfer(out,fmt,0);}_queueRun(false);gfxScreenSwapBuffers(pglState->display,stereo);}
void pglSelectScreen(unsigned display,unsigned side){pglState->display=display;pglState->display_side=side;}
void pglSetShaderCache(const void*v,size_t vs,const void*c,size_t cs){free((void*)pglCachedVertexShader);free((void*)pglCachedClearShader);pglCachedVertexShader=NULL;pglCachedClearShader=NULL;pglCachedVertexShaderSize=pglCachedClearShaderSize=0;if(v&&vs){void*b=malloc(vs);if(b){memcpy(b,v,vs);pglCachedVertexShader=b;pglCachedVertexShaderSize=vs;}}if(c&&cs){void*b=malloc(cs);if(b){memcpy(b,c,cs);pglCachedClearShader=b;pglCachedClearShaderSize=cs;}}}
void pglGetShaderCache(const void**v,size_t*vs,const void**c,size_t*cs){if(v)*v=pglCachedVertexShader?pglCachedVertexShader:vshader_shbin;if(vs)*vs=pglCachedVertexShader?pglCachedVertexShaderSize:vshader_shbin_size;if(c)*c=pglCachedClearShader?pglCachedClearShader:clear_shbin;if(cs)*cs=pglCachedClearShader?pglCachedClearShaderSize:clear_shbin_size;}
