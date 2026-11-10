/* SwitchRecomp - Copyright 2026 MZ Labs
   Licensed under the GNU Affero General Public License v3.0, see LICENSE.txt and NOTICE.txt.
   Forks and derived works have to keep the credit to SwitchRecomp by MZ Labs and the startup screen. */
/* Linux: the subset of Win32 the NVN layer (runtime/nvn) uses, over pthreads, mmap and POSIX files, so its files build
   unchanged. Only on the include path of the Linux build (tools/build_nvn.py --linux). */
#ifndef SWR_LINUX_WINDOWS_H
#define SWR_LINUX_WINDOWS_H
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <signal.h>
#include <wchar.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WINAPI
#define CALLBACK
#define APIENTRY
#define __stdcall
#define __declspec(x)
typedef uint32_t DWORD; typedef int32_t LONG; typedef uint32_t ULONG; typedef int BOOL; typedef uint8_t BYTE; typedef uint16_t WORD;
typedef int64_t LONGLONG; typedef uint64_t ULONGLONG; typedef uint64_t DWORD64; typedef uintptr_t ULONG_PTR; typedef uintptr_t SIZE_T;
typedef intptr_t LONG_PTR; typedef void* PVOID; typedef void* LPVOID; typedef const void* LPCVOID; typedef void* HANDLE; typedef void* HMODULE;
typedef void* HINSTANCE; typedef void* HWND; typedef char CHAR; typedef const char* LPCSTR; typedef char* LPSTR; typedef wchar_t WCHAR;
typedef const WCHAR* LPCWSTR; typedef DWORD* LPDWORD; typedef unsigned UINT; typedef long HRESULT;
#define TRUE 1
#define FALSE 0
#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
typedef union { struct { DWORD LowPart; LONG HighPart; }; LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { DWORD dwLowDateTime, dwHighDateTime; } FILETIME;
typedef struct { LONG left, top, right, bottom; } RECT;

/* locks and condition variables: a pthread mutex is an SRW lock taken exclusively */
typedef pthread_mutex_t SRWLOCK; typedef SRWLOCK* PSRWLOCK;
#define SRWLOCK_INIT PTHREAD_MUTEX_INITIALIZER
static inline void InitializeSRWLock(SRWLOCK* l){ pthread_mutex_init(l,NULL); }
static inline void AcquireSRWLockExclusive(SRWLOCK* l){ pthread_mutex_lock(l); }
static inline void ReleaseSRWLockExclusive(SRWLOCK* l){ pthread_mutex_unlock(l); }
static inline BOOL TryAcquireSRWLockExclusive(SRWLOCK* l){ return pthread_mutex_trylock(l)==0; }
static inline void AcquireSRWLockShared(SRWLOCK* l){ pthread_mutex_lock(l); }
static inline void ReleaseSRWLockShared(SRWLOCK* l){ pthread_mutex_unlock(l); }
typedef pthread_cond_t CONDITION_VARIABLE;
#define CONDITION_VARIABLE_INIT PTHREAD_COND_INITIALIZER
static inline void InitializeConditionVariable(CONDITION_VARIABLE* c){ pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a,CLOCK_MONOTONIC); pthread_cond_init(c,&a); }
static inline void WakeConditionVariable(CONDITION_VARIABLE* c){ pthread_cond_signal(c); }
static inline void WakeAllConditionVariable(CONDITION_VARIABLE* c){ pthread_cond_broadcast(c); }
/* statically initialized conditions use CLOCK_REALTIME: wait on that clock */
static inline BOOL SleepConditionVariableSRW(CONDITION_VARIABLE* c, SRWLOCK* l, DWORD ms, ULONG flags){ (void)flags;
  if(ms==INFINITE){ pthread_cond_wait(c,l); return TRUE; }
  struct timespec t; clock_gettime(CLOCK_REALTIME,&t); t.tv_sec+=ms/1000; t.tv_nsec+=(long)(ms%1000)*1000000L; if(t.tv_nsec>=1000000000L){ t.tv_sec++; t.tv_nsec-=1000000000L; }
  int r=pthread_cond_timedwait(c,l,&t); if(r==ETIMEDOUT){ errno=ETIMEDOUT; return FALSE; } return TRUE; }
#define ERROR_TIMEOUT 1460

typedef struct { volatile int state; pthread_mutex_t m; } INIT_ONCE;
#define INIT_ONCE_STATIC_INIT {0, PTHREAD_MUTEX_INITIALIZER}
typedef INIT_ONCE* PINIT_ONCE;
typedef BOOL (*PINIT_ONCE_FN)(INIT_ONCE*, PVOID, PVOID*);
static inline BOOL InitOnceExecuteOnce(INIT_ONCE* o, PINIT_ONCE_FN fn, PVOID p, PVOID* ctx){
  if(__atomic_load_n(&o->state,__ATOMIC_ACQUIRE)) return TRUE;
  pthread_mutex_lock(&o->m); if(!o->state){ fn(o,p,ctx); __atomic_store_n(&o->state,1,__ATOMIC_RELEASE); } pthread_mutex_unlock(&o->m); return TRUE; }

/* interlocked */
static inline LONG InterlockedIncrement(volatile LONG* p){ return __atomic_add_fetch(p,1,__ATOMIC_SEQ_CST); }
static inline LONG InterlockedDecrement(volatile LONG* p){ return __atomic_sub_fetch(p,1,__ATOMIC_SEQ_CST); }
static inline LONG InterlockedExchange(volatile LONG* p, LONG v){ return __atomic_exchange_n(p,v,__ATOMIC_SEQ_CST); }
static inline LONG InterlockedCompareExchange(volatile LONG* p, LONG x, LONG c){ __atomic_compare_exchange_n(p,&c,x,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return c; }
static inline LONG InterlockedExchangeAdd(volatile LONG* p, LONG v){ return __atomic_fetch_add(p,v,__ATOMIC_SEQ_CST); }
#define MemoryBarrier() __atomic_thread_fence(__ATOMIC_SEQ_CST)

/* time */
static inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* f){ f->QuadPart=1000000000LL; return TRUE; }
static inline BOOL QueryPerformanceCounter(LARGE_INTEGER* c){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); c->QuadPart=(LONGLONG)t.tv_sec*1000000000LL+t.tv_nsec; return TRUE; }
static inline ULONGLONG GetTickCount64(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return (ULONGLONG)t.tv_sec*1000+t.tv_nsec/1000000; }
static inline DWORD GetTickCount(void){ return (DWORD)GetTickCount64(); }
static inline void GetSystemTimePreciseAsFileTime(FILETIME* f){ struct timespec t; clock_gettime(CLOCK_REALTIME,&t);
  uint64_t v=(uint64_t)t.tv_sec*10000000ull+t.tv_nsec/100+116444736000000000ull; f->dwLowDateTime=(DWORD)v; f->dwHighDateTime=(DWORD)(v>>32); }
#define GetSystemTimeAsFileTime GetSystemTimePreciseAsFileTime
static inline void Sleep(DWORD ms){ struct timespec t={ms/1000,(long)(ms%1000)*1000000L}; while(nanosleep(&t,&t) && errno==EINTR); }
static inline BOOL SwitchToThread(void){ sched_yield(); return TRUE; }
static inline UINT timeBeginPeriod(UINT p){ (void)p; return 0; }

/* threads and events: a HANDLE is one of these */
typedef struct swr_wh { int kind; pthread_t th; int joined; pthread_mutex_t m; pthread_cond_t c; int manual, set; } swr_wh;
typedef unsigned (__stdcall *swr_thread_fn)(void*);
typedef struct { swr_thread_fn fn; void* arg; } swr_ts;
static inline void* swr_thread_tramp_(void* p){ swr_ts s=*(swr_ts*)p; free(p); return (void*)(uintptr_t)s.fn(s.arg); }
static inline uintptr_t _beginthreadex(void* sec, unsigned stack, swr_thread_fn fn, void* arg, unsigned flags, unsigned* tid){
  (void)sec; (void)flags; swr_wh* h=(swr_wh*)calloc(1,sizeof *h); h->kind=1; swr_ts* s=(swr_ts*)malloc(sizeof *s); s->fn=fn; s->arg=arg;
  pthread_attr_t a; pthread_attr_init(&a); if(stack) pthread_attr_setstacksize(&a,stack<65536? 65536 : stack);
  if(pthread_create(&h->th,&a,swr_thread_tramp_,s)){ pthread_attr_destroy(&a); free(s); free(h); return 0; } pthread_attr_destroy(&a);
  if(tid) *tid=0; return (uintptr_t)h; }
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID);
static inline HANDLE CreateThread(void* sec, SIZE_T stack, LPTHREAD_START_ROUTINE fn, LPVOID arg, DWORD flags, LPDWORD tid){
  return (HANDLE)_beginthreadex(sec,(unsigned)stack,(swr_thread_fn)(void*)fn,arg,flags,(unsigned*)tid); }
static inline DWORD GetCurrentThreadId(void){ return (DWORD)syscall(SYS_gettid); }
static inline HANDLE GetCurrentThread(void){ return (HANDLE)0; }
static inline HANDLE GetCurrentProcess(void){ return (HANDLE)0; }
#define THREAD_PRIORITY_LOWEST -2
#define THREAD_PRIORITY_BELOW_NORMAL -1
#define THREAD_PRIORITY_NORMAL 0
#define THREAD_PRIORITY_ABOVE_NORMAL 1
#define THREAD_PRIORITY_HIGHEST 2
#define THREAD_PRIORITY_TIME_CRITICAL 15
static inline BOOL SetThreadPriority(HANDLE h, int p){ (void)h; (void)p; return TRUE; }
static inline HRESULT SetThreadDescription(HANDLE h, const WCHAR* n){ (void)h; (void)n; return 0; }
static inline HANDLE CreateEventW(void* sec, BOOL manual, BOOL init, const void* name){ (void)sec; (void)name;
  swr_wh* h=(swr_wh*)calloc(1,sizeof *h); h->kind=2; h->manual=manual; h->set=init; pthread_mutex_init(&h->m,NULL);
  pthread_condattr_t a; pthread_condattr_init(&a); pthread_condattr_setclock(&a,CLOCK_MONOTONIC); pthread_cond_init(&h->c,&a); return h; }
#define CreateEventA CreateEventW
static inline BOOL SetEvent(HANDLE e){ swr_wh* h=(swr_wh*)e; pthread_mutex_lock(&h->m); h->set=1; pthread_cond_broadcast(&h->c); pthread_mutex_unlock(&h->m); return TRUE; }
static inline BOOL ResetEvent(HANDLE e){ swr_wh* h=(swr_wh*)e; pthread_mutex_lock(&h->m); h->set=0; pthread_mutex_unlock(&h->m); return TRUE; }
static inline DWORD WaitForSingleObject(HANDLE o, DWORD ms){ swr_wh* h=(swr_wh*)o; if(!h) return WAIT_FAILED;
  if(h->kind==1){ if(ms!=INFINITE){ /* polled join */ for(DWORD w=0;;w++){ if(pthread_tryjoin_np(h->th,NULL)==0){ h->joined=1; return WAIT_OBJECT_0; } if(w>=ms) return WAIT_TIMEOUT; Sleep(1); } }
    if(!h->joined){ pthread_join(h->th,NULL); h->joined=1; } return WAIT_OBJECT_0; }
  if(h->kind==4){ struct timespec t; int64_t d=(int64_t)(uint64_t)h->th; t.tv_sec=d/1000000000LL; t.tv_nsec=d%1000000000LL; while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&t,NULL)==EINTR); return WAIT_OBJECT_0; }
  pthread_mutex_lock(&h->m);
  if(ms==INFINITE) while(!h->set) pthread_cond_wait(&h->c,&h->m);
  else { struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); t.tv_sec+=ms/1000; t.tv_nsec+=(long)(ms%1000)*1000000L; if(t.tv_nsec>=1000000000L){ t.tv_sec++; t.tv_nsec-=1000000000L; }
    while(!h->set) if(pthread_cond_timedwait(&h->c,&h->m,&t)==ETIMEDOUT) break; }
  int ok=h->set; if(ok && !h->manual) h->set=0; pthread_mutex_unlock(&h->m); return ok? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
static inline BOOL CloseHandle(HANDLE o){ swr_wh* h=(swr_wh*)o; if(!h || o==INVALID_HANDLE_VALUE) return FALSE;
  if(h->kind==1){ if(!h->joined) pthread_detach(h->th); free(h); }
  else if(h->kind==2){ pthread_mutex_destroy(&h->m); pthread_cond_destroy(&h->c); free(h); }
  else if(h->kind==3){ close((int)(intptr_t)h->th); free(h); }
  else if(h->kind==4) free(h);
  return TRUE; }
static inline void ExitProcess(UINT c){ _exit((int)c); }

/* fiber-local storage: pthread keys */
typedef void (*PFLS_CALLBACK_FUNCTION)(PVOID);
static inline DWORD FlsAlloc(PFLS_CALLBACK_FUNCTION f){ pthread_key_t k; if(pthread_key_create(&k,f)) return 0xFFFFFFFFu; return (DWORD)k; }
static inline PVOID FlsGetValue(DWORD k){ return pthread_getspecific((pthread_key_t)k); }
static inline BOOL FlsSetValue(DWORD k, PVOID v){ return pthread_setspecific((pthread_key_t)k,v)==0; }
#define TlsAlloc() FlsAlloc(NULL)
#define TlsGetValue FlsGetValue
#define TlsSetValue FlsSetValue

/* virtual memory */
#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000
#define MEM_FREE 0x10000
#define MEM_PRIVATE 0x20000
#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_GUARD 0x100
typedef struct { PVOID BaseAddress, AllocationBase; DWORD AllocationProtect; SIZE_T RegionSize; DWORD State, Protect, Type; } MEMORY_BASIC_INFORMATION;
static inline int swr_prot_(DWORD p){ return p==PAGE_NOACCESS? PROT_NONE : p==PAGE_READONLY? PROT_READ : PROT_READ|PROT_WRITE; }
static inline LPVOID VirtualAlloc(LPVOID a, SIZE_T n, DWORD type, DWORD prot){
  if(a && !(type&MEM_RESERVE)){ /* commit inside a reservation */ return mprotect(a,n,swr_prot_(prot))? NULL : a; }
  int pr= (type&MEM_COMMIT)? swr_prot_(prot) : PROT_NONE;
  void* p=mmap(a,n,pr,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE|(a? MAP_FIXED_NOREPLACE : 0),-1,0); return p==MAP_FAILED? NULL : p; }
static inline BOOL VirtualFree(LPVOID a, SIZE_T n, DWORD type){
  if(type==MEM_RELEASE){ /* the whole reservation: the caller passes 0, so find it from /proc/self/maps */
    if(!n){ FILE* f=fopen("/proc/self/maps","r"); char l[512]; uintptr_t lo,hi; while(f && fgets(l,sizeof l,f)) if(sscanf(l,"%lx-%lx",&lo,&hi)==2 && lo==(uintptr_t)a){ n=hi-lo; break; } if(f) fclose(f); }
    return n && munmap(a,n)==0; }
  return mprotect(a,n,PROT_NONE)==0 && madvise(a,n,MADV_DONTNEED)==0; }
static inline BOOL VirtualProtect(LPVOID a, SIZE_T n, DWORD prot, DWORD* old){ if(old) *old=PAGE_READWRITE; return mprotect((void*)((uintptr_t)a&~(uintptr_t)4095),n+((uintptr_t)a&4095),swr_prot_(prot))==0; }
/* the mapping (and its protection) that holds a, from /proc/self/maps */
static inline SIZE_T VirtualQuery(LPCVOID a, MEMORY_BASIC_INFORMATION* mi, SIZE_T sz){ (void)sz; memset(mi,0,sizeof *mi);
  FILE* f=fopen("/proc/self/maps","r"); if(!f) return 0; char l[512]; uintptr_t lo,hi,x=(uintptr_t)a; char pr[8]; SIZE_T r=0;
  while(fgets(l,sizeof l,f)) if(sscanf(l,"%lx-%lx %7s",&lo,&hi,pr)==3 && x>=lo && x<hi){
    mi->BaseAddress=(PVOID)(x&~(uintptr_t)4095); mi->AllocationBase=(PVOID)lo; mi->RegionSize=hi-(x&~(uintptr_t)4095); mi->Type=MEM_PRIVATE;
    int rd=pr[0]=='r', wr=pr[1]=='w'; mi->State= rd? MEM_COMMIT : MEM_RESERVE; mi->Protect= wr? PAGE_READWRITE : rd? PAGE_READONLY : PAGE_NOACCESS; r=sizeof *mi; break; }
  fclose(f); if(!r){ mi->State=MEM_FREE; mi->BaseAddress=(PVOID)(x&~(uintptr_t)4095); mi->RegionSize=4096; r=sizeof *mi; } return r; }
static inline BOOL IsBadReadPtr(const void* p, uintptr_t n){ MEMORY_BASIC_INFORMATION mi; (void)n; return !VirtualQuery(p,&mi,sizeof mi) || mi.State!=MEM_COMMIT; }
static inline void* _aligned_malloc(size_t n, size_t al){ void* p=NULL; return posix_memalign(&p,al<sizeof(void*)? sizeof(void*) : al,n)? NULL : p; }
static inline void _aligned_free(void* p){ free(p); }

/* modules */
static inline HMODULE GetModuleHandleA(const char* n){ return n? dlopen(n,RTLD_NOW|RTLD_NOLOAD) : dlopen(NULL,RTLD_NOW); }
#define GetModuleHandleW(n) GetModuleHandleA(NULL)
static inline HMODULE LoadLibraryA(const char* n){ return dlopen(n,RTLD_NOW|RTLD_LOCAL); }
static inline void* GetProcAddress(HMODULE m, const char* s){ return m? dlsym(m,s) : NULL; }
static inline BOOL FreeLibrary(HMODULE m){ return m && dlclose(m)==0; }

/* files: a HANDLE of kind 3 holds the descriptor */
typedef struct { ULONG_PTR Internal, InternalHigh; union { struct { DWORD Offset, OffsetHigh; }; PVOID Pointer; }; HANDLE hEvent; } OVERLAPPED;
#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define FILE_SHARE_DELETE 4
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_ATTRIBUTE_NORMAL 0x80
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_BEGIN 0
#define FILE_END 2
static inline HANDLE CreateFileA(const char* p0, DWORD acc, DWORD share, void* sec, DWORD disp, DWORD fl, HANDLE t){ (void)share; (void)sec; (void)fl; (void)t;
  char p[4096]; { size_t i=0; for(;p0[i] && i<4095;i++) p[i]= p0[i]=='\\'? '/' : p0[i]; p[i]=0; }
  int o= (acc&GENERIC_WRITE)? ((acc&GENERIC_READ)? O_RDWR : O_WRONLY) : O_RDONLY;
  if(disp==CREATE_ALWAYS) o|=O_CREAT|O_TRUNC; else if(disp==OPEN_ALWAYS) o|=O_CREAT; else if(disp==CREATE_NEW) o|=O_CREAT|O_EXCL; else if(disp==TRUNCATE_EXISTING) o|=O_TRUNC;
  int fd=open(p,o|O_CLOEXEC,0644); if(fd<0) return INVALID_HANDLE_VALUE; swr_wh* h=(swr_wh*)calloc(1,sizeof *h); h->kind=3; h->th=(pthread_t)fd; return h; }
static inline HANDLE CreateFileW(const wchar_t* w, DWORD acc, DWORD share, void* sec, DWORD disp, DWORD fl, HANDLE t){ char p[4096]; size_t i=0; for(;w[i] && i<4095;i++) p[i]= w[i]==L'\\'? '/' : (char)w[i]; p[i]=0; return CreateFileA(p,acc,share,sec,disp,fl,t); }
static inline int swr_fd_(HANDLE h){ return (int)(intptr_t)((swr_wh*)h)->th; }
static inline BOOL ReadFile(HANDLE h, void* b, DWORD n, DWORD* got, OVERLAPPED* ov){
  ssize_t r= ov? pread(swr_fd_(h),b,n,(off_t)(((uint64_t)ov->OffsetHigh<<32)|ov->Offset)) : read(swr_fd_(h),b,n); if(r<0){ if(got) *got=0; return FALSE; } if(got) *got=(DWORD)r; return TRUE; }
static inline BOOL WriteFile(HANDLE h, const void* b, DWORD n, DWORD* put, OVERLAPPED* ov){
  ssize_t r= ov? pwrite(swr_fd_(h),b,n,(off_t)(((uint64_t)ov->OffsetHigh<<32)|ov->Offset)) : write(swr_fd_(h),b,n); if(r<0){ if(put) *put=0; return FALSE; } if(put) *put=(DWORD)r; return TRUE; }
static inline BOOL FlushFileBuffers(HANDLE h){ return fsync(swr_fd_(h))==0; }
static inline BOOL GetFileSizeEx(HANDLE h, LARGE_INTEGER* s){ struct stat st; if(fstat(swr_fd_(h),&st)) return FALSE; s->QuadPart=st.st_size; return TRUE; }
static inline BOOL SetFilePointerEx(HANDLE h, LARGE_INTEGER d, LARGE_INTEGER* np, DWORD how){ off_t r=lseek(swr_fd_(h),d.QuadPart,how==FILE_END? SEEK_END : how==1? SEEK_CUR : SEEK_SET); if(np) np->QuadPart=r; return r>=0; }
static inline BOOL SetEndOfFile(HANDLE h){ off_t p=lseek(swr_fd_(h),0,SEEK_CUR); return ftruncate(swr_fd_(h),p)==0; }
static inline BOOL CreateDirectoryA(const char* p0, void* sec){ (void)sec; char p[4096]; size_t i=0; for(;p0[i] && i<4095;i++) p[i]= p0[i]=='\\'? '/' : p0[i]; p[i]=0; return mkdir(p,0755)==0; }
static inline BOOL DeleteFileA(const char* p){ return unlink(p)==0; }
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
static inline BOOL MoveFileExA(const char* a, const char* b, DWORD f){ (void)f; return rename(a,b)==0; }
static inline DWORD GetLastError(void){ return (DWORD)errno; }
static inline void SetLastError(DWORD e){ errno=(int)e; }
static inline void OutputDebugStringA(const char* s){ (void)s; }
/* named pipes (the debug console of vk_ctl.c): not on Linux */
#define PIPE_ACCESS_DUPLEX 3
#define PIPE_TYPE_BYTE 0
#define PIPE_READMODE_BYTE 0
#define PIPE_WAIT 0
static inline HANDLE CreateNamedPipeA(const char* n, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, void* g){ (void)n;(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g; return INVALID_HANDLE_VALUE; }
static inline BOOL ConnectNamedPipe(HANDLE h, void* o){ (void)h; (void)o; return FALSE; }
static inline BOOL DisconnectNamedPipe(HANDLE h){ (void)h; return FALSE; }

/* wide paths: a wchar_t holds one byte of the UTF-8 path, so the round trip gives the same bytes back */
#define CP_UTF8 65001
static inline int MultiByteToWideChar(UINT cp, DWORD f, const char* s, int n, wchar_t* d, int dn){ (void)cp; (void)f;
  int len= n<0? (int)strlen(s)+1 : n; if(!d || !dn) return len; int k=0; for(;k<len && k<dn;k++) d[k]=(wchar_t)(unsigned char)s[k]; return k; }
static inline int WideCharToMultiByte(UINT cp, DWORD f, const wchar_t* s, int n, char* d, int dn, const char* def, BOOL* used){ (void)cp; (void)f; (void)def; (void)used;
  int len= n<0? (int)wcslen(s)+1 : n; if(!d || !dn) return len; int k=0; for(;k<len && k<dn;k++) d[k]=(char)s[k]; return k; }
static inline void swr_w2a_(const wchar_t* w, char* o, size_t n){ size_t i=0; for(;w[i] && i<n-1;i++) o[i]= w[i]==L'\\'? '/' : (char)w[i]; o[i]=0; }
#define INVALID_FILE_ATTRIBUTES 0xFFFFFFFFu
#define FILE_ATTRIBUTE_DIRECTORY 0x10
static inline DWORD GetFileAttributesA(const char* p){ struct stat st; if(stat(p,&st)) return INVALID_FILE_ATTRIBUTES; return S_ISDIR(st.st_mode)? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL; }
static inline DWORD GetFileAttributesW(const wchar_t* w){ char p[4096]; swr_w2a_(w,p,sizeof p); return GetFileAttributesA(p); }
static inline BOOL CreateDirectoryW(const wchar_t* w, void* sec){ char p[4096]; swr_w2a_(w,p,sizeof p); return CreateDirectoryA(p,sec); }
static inline FILE* _wfopen(const wchar_t* w, const wchar_t* m){ char p[4096], mm[16]; swr_w2a_(w,p,sizeof p); swr_w2a_(m,mm,sizeof mm); return fopen(p,mm); }
static inline BOOL DeleteFileW(const wchar_t* w){ char p[4096]; swr_w2a_(w,p,sizeof p); return DeleteFileA(p); }
static inline BOOL MoveFileExW(const wchar_t* a, const wchar_t* b, DWORD f){ char p[4096], q[4096]; swr_w2a_(a,p,sizeof p); swr_w2a_(b,q,sizeof q); return MoveFileExA(p,q,f); }
#define LoadLibraryW(w) ((HMODULE)NULL)
typedef struct { DWORD nLength; void* lpSecurityDescriptor; BOOL bInheritHandle; } SECURITY_ATTRIBUTES;

typedef uint64_t ULONG64; typedef uint8_t UCHAR; typedef UCHAR* PUCHAR; typedef wchar_t* PWSTR;
#define MAX_PATH 260
#define ERROR_PIPE_CONNECTED 535
static inline BOOL QueryThreadCycleTime(HANDLE h, ULONG64* c){ (void)h; struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t); *c=(ULONG64)t.tv_sec*1000000000ull+t.tv_nsec; return TRUE; }
typedef struct { WORD wYear, wMonth, wDayOfWeek, wDay, wHour, wMinute, wSecond, wMilliseconds; } SYSTEMTIME;
static inline void GetLocalTime(SYSTEMTIME* s){ struct timespec t; clock_gettime(CLOCK_REALTIME,&t); struct tm m; time_t tt=t.tv_sec; localtime_r(&tt,&m);
  s->wYear=m.tm_year+1900; s->wMonth=m.tm_mon+1; s->wDayOfWeek=m.tm_wday; s->wDay=m.tm_mday; s->wHour=m.tm_hour; s->wMinute=m.tm_min; s->wSecond=m.tm_sec; s->wMilliseconds=t.tv_nsec/1000000; }
#define GetSystemTime GetLocalTime

typedef uint16_t USHORT;
#include <execinfo.h>
static inline USHORT RtlCaptureStackBackTrace(ULONG skip, ULONG n, void** bt, ULONG* hash){ (void)hash; void* t[64]; int k=backtrace(t,(int)(skip+n<64? skip+n : 64)); int m=0; for(int i=(int)skip;i<k;i++) bt[m++]=t[i]; return (USHORT)m; }
static inline DWORD GetModuleFileNameW(HMODULE m, wchar_t* out, DWORD n){ (void)m; char p[4096]; ssize_t k=readlink("/proc/self/exe",p,sizeof p-1); if(k<0) k=0; p[k]=0; DWORD i=0; for(;p[i] && i<n-1;i++) out[i]=(wchar_t)(unsigned char)p[i]; out[i]=0; return i; }
static inline DWORD GetModuleFileNameA(HMODULE m, char* out, DWORD n){ (void)m; ssize_t k=readlink("/proc/self/exe",out,n-1); if(k<0) k=0; out[k]=0; return (DWORD)k; }
/* waitable timers: a HANDLE of kind 4 holds the due time */
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#define TIMER_ALL_ACCESS 0x1F0003
static inline HANDLE CreateWaitableTimerExW(void* sec, const void* name, DWORD f, DWORD acc){ (void)sec; (void)name; (void)f; (void)acc; swr_wh* h=(swr_wh*)calloc(1,sizeof *h); h->kind=4; return h; }
static inline BOOL SetWaitableTimer(HANDLE t, const LARGE_INTEGER* due, LONG per, void* fn, void* arg, BOOL resume){ (void)per; (void)fn; (void)arg; (void)resume;
  swr_wh* h=(swr_wh*)t; struct timespec n; clock_gettime(CLOCK_MONOTONIC,&n); int64_t now=(int64_t)n.tv_sec*1000000000LL+n.tv_nsec;
  int64_t d= due->QuadPart<0? now+(-due->QuadPart)*100 : now; h->set=0; h->th=(pthread_t)(uint64_t)d; return TRUE; }

#define _snwprintf swprintf
#define YieldProcessor() __builtin_ia32_pause()
static inline BOOL SetEnvironmentVariableA(const char* n, const char* v){ return (v? setenv(n,v,1) : unsetenv(n))==0; }

/* CRT bits */
static inline int localtime_s(struct tm* o, const time_t* t){ return localtime_r(t,o)? 0 : 1; }
static inline int gmtime_s(struct tm* o, const time_t* t){ return gmtime_r(t,o)? 0 : 1; }
#define _mkgmtime timegm
#define _lock_file flockfile
#define _unlock_file funlockfile
static inline int _putenv_s(const char* n, const char* v){ return *v? setenv(n,v,1) : unsetenv(n); }
static inline int _putenv(const char* s){ return putenv(strdup(s)); }

/* exceptions: nvn_host.c installs a SIGSEGV handler on Linux, these only let the shared code compile */
#define EXCEPTION_ACCESS_VIOLATION 0xC0000005u
#define EXCEPTION_CONTINUE_EXECUTION (-1)
#define EXCEPTION_CONTINUE_SEARCH 0
#define EXCEPTION_EXECUTE_HANDLER 1
typedef struct { DWORD ExceptionCode; DWORD NumberParameters; ULONG_PTR ExceptionInformation[15]; } EXCEPTION_RECORD;
typedef struct { DWORD64 Rbp, Rip, Rsp; } CONTEXT;
typedef struct { EXCEPTION_RECORD* ExceptionRecord; CONTEXT* ContextRecord; } EXCEPTION_POINTERS;

#ifdef __cplusplus
}
#endif
#endif
