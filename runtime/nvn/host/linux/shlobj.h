/* Linux: SHGetKnownFolderPath(LocalAppData) is $XDG_CACHE_HOME or ~/.cache */
#pragma once
#include <windows.h>
#define S_OK 0
typedef int KNOWNFOLDERID;
static const KNOWNFOLDERID FOLDERID_LocalAppData = 1;
static inline HRESULT SHGetKnownFolderPath(const KNOWNFOLDERID* id, DWORD f, HANDLE t, wchar_t** out){ (void)id; (void)f; (void)t;
  char p[2048]; const char* x=getenv("XDG_CACHE_HOME"); if(x && *x) snprintf(p,sizeof p,"%s",x); else snprintf(p,sizeof p,"%s/.cache",getenv("HOME")? getenv("HOME") : ".");
  size_t n=strlen(p); wchar_t* w=(wchar_t*)malloc((n+1)*sizeof *w); for(size_t i=0;i<=n;i++) w[i]=(wchar_t)(unsigned char)p[i]; *out=w; return S_OK; }
#ifdef __cplusplus
static inline HRESULT SHGetKnownFolderPath(const KNOWNFOLDERID& id, DWORD f, HANDLE t, wchar_t** out){ return SHGetKnownFolderPath(&id,f,t,out); }
#endif
static inline void CoTaskMemFree(void* p){ free(p); }
/* _snwprintf: windows.h */
