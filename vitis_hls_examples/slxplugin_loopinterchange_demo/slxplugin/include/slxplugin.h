#ifndef _SLX_PLUGIN_H__
#define _SLX_PLUGIN_H__
#if defined(__SYNTHESIS__)
#ifdef __cplusplus
extern "C" {
#endif
void _ssdm_SLXLoopInterchange(void);
#ifdef __cplusplus
}
#endif
#define _SLXLoopInterchange() _ssdm_SLXLoopInterchange()
#else
#define _SLXLoopInterchange()
#endif
#endif
