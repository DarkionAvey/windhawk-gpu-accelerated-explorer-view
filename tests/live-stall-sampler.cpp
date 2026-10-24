// Manual read-only diagnostic. Copy a bounded UI stack while briefly paused,
// resume immediately, then unwind/resolve only after all samples are collected.
// No injection, breakpoints, process writes, provider cancellation or focus API.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

struct Snapshot { CONTEXT context{}; DWORD64 base=0; std::vector<unsigned char> bytes; };
static const Snapshot* reading=nullptr;
static BOOL CALLBACK ReadSnapshot(HANDLE process,DWORD64 address,PVOID output,DWORD size,LPDWORD read) {
    if(reading && address>=reading->base && address-reading->base<=reading->bytes.size() &&
        size<=reading->bytes.size()-(address-reading->base)) {
        memcpy(output,reading->bytes.data()+address-reading->base,size); *read=size; return TRUE;
    }
    SIZE_T actual=0;
    const BOOL ok=ReadProcessMemory(process,reinterpret_cast<LPCVOID>(address),output,size,&actual);
    *read=static_cast<DWORD>(actual); return ok;
}
int wmain(int argc,wchar_t** argv) {
    if(argc<3) { fwprintf(stderr,L"Usage: live-stall-sampler PID TID [symbol-path] [sample-count]\n"); return 2; }
    const DWORD pid=wcstoul(argv[1],nullptr,10),tid=wcstoul(argv[2],nullptr,10);
    HANDLE process=OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ,FALSE,pid);
    HANDLE thread=OpenThread(THREAD_GET_CONTEXT|THREAD_SUSPEND_RESUME|THREAD_QUERY_INFORMATION,FALSE,tid);
    if(!process || !thread || GetProcessIdOfThread(thread)!=pid) {
        fprintf(stderr,"Cannot read requested process/thread: %lu\n",GetLastError()); return 1;
    }
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    if(!SymInitializeW(process,argc>3 ? argv[3] : nullptr,TRUE)) {
        fprintf(stderr,"SymInitialize failed: %lu\n",GetLastError()); return 1;
    }
    const unsigned count=argc>4 ? std::clamp<unsigned>(wcstoul(argv[4],nullptr,10),1,3000) : 300;
    std::vector<Snapshot> snapshots; snapshots.reserve(count);
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    double pauseMax=0,pauseTotal=0;
    printf("Capturing %u bounded UI stack snapshots; symbols are resolved after resume.\n",count); fflush(stdout);
    for(unsigned sample=0;sample<count;++sample) {
        Snapshot snapshot; snapshot.bytes.resize(128*1024);
        snapshot.context.ContextFlags=CONTEXT_FULL;
        LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
        if(SuspendThread(thread)==DWORD(-1)) break;
        bool ok=GetThreadContext(thread,&snapshot.context)!=FALSE;
        SIZE_T actual=0;
        MEMORY_BASIC_INFORMATION region{};
        if(ok) {
            snapshot.base=snapshot.context.Rsp;
            ok=VirtualQueryEx(process,reinterpret_cast<LPCVOID>(snapshot.base),&region,sizeof(region))!=0;
            if(ok) {
                const auto available=reinterpret_cast<DWORD64>(region.BaseAddress)+region.RegionSize-snapshot.base;
                ok=ReadProcessMemory(process,reinterpret_cast<LPCVOID>(snapshot.base),snapshot.bytes.data(),
                    std::min<SIZE_T>(snapshot.bytes.size(),available),&actual)!=FALSE;
            }
        }
        // No allocation, symbol lookup, disk/network access or unwind while paused.
        const DWORD resumed=ResumeThread(thread);
        QueryPerformanceCounter(&end);
        const double pause=1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart;
        pauseTotal+=pause; pauseMax=std::max(pauseMax,pause);
        if(resumed==DWORD(-1)) { fprintf(stderr,"Resume failed: %lu\n",GetLastError()); return 1; }
        if(ok) { snapshot.bytes.resize(actual); snapshots.push_back(std::move(snapshot)); }
        Sleep(10);
    }
    printf("Collected %zu samples; total pause %.3f ms; maximum pause %.3f ms. Unwinding offline.\n",
        snapshots.size(),pauseTotal,pauseMax); fflush(stdout);
    std::map<std::vector<DWORD64>,unsigned> stacks;
    for(auto& snapshot : snapshots) {
        reading=&snapshot;
        CONTEXT context=snapshot.context;
        STACKFRAME64 frame{};
        frame.AddrPC={context.Rip,0,AddrModeFlat};
        frame.AddrFrame={context.Rbp,0,AddrModeFlat};
        frame.AddrStack={context.Rsp,0,AddrModeFlat};
        std::vector<DWORD64> stack{context.Rip};
        for(unsigned depth=0;depth<40;++depth) {
            if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,thread,&frame,&context,ReadSnapshot,
                SymFunctionTableAccess64,SymGetModuleBase64,nullptr) || !frame.AddrPC.Offset) break;
            if(stack.back()==frame.AddrPC.Offset) { if(depth) break; }
            else stack.push_back(frame.AddrPC.Offset);
        }
        ++stacks[stack];
    }
    reading=nullptr;
    std::vector<std::pair<std::vector<DWORD64>,unsigned>> ranked(stacks.begin(),stacks.end());
    std::sort(ranked.begin(),ranked.end(),[](const auto& a,const auto& b){return a.second>b.second;});
    std::map<std::wstring,unsigned> inclusive;
    std::vector<size_t> show;
    for(size_t i=0;i<ranked.size();++i) {
        bool native=false;
        std::vector<std::wstring> seen;
        for(const auto address : ranked[i].first) {
            IMAGEHLP_MODULEW64 module{}; module.SizeOfStruct=sizeof(module);
            if(!SymGetModuleInfoW64(process,address,&module)) continue;
            const std::wstring name=module.ModuleName;
            native|=_wcsicmp(name.c_str(),L"DUI70")==0 ||
                _wcsicmp(name.c_str(),L"COMCTL32")==0 ||
                _wcsicmp(name.c_str(),L"windows.storage")==0;
            if(std::find(seen.begin(),seen.end(),name)==seen.end()) {
                inclusive[name]+=ranked[i].second; seen.push_back(name);
            }
        }
        if(i<14 || native) show.push_back(i);
    }
    puts("Inclusive module sample counts:");
    for(const auto& [module,count] : inclusive) wprintf(L"  %ls %u\n",module.c_str(),count);
    for(const auto i : show) {
        printf("\nSTACK %zu samples=%u\n",i,ranked[i].second);
        for(const auto address : ranked[i].first) {
            IMAGEHLP_MODULEW64 module{}; module.SizeOfStruct=sizeof(module);
            SymGetModuleInfoW64(process,address,&module);
            alignas(SYMBOL_INFOW) unsigned char storage[sizeof(SYMBOL_INFOW)+2048*sizeof(wchar_t)]{};
            auto* symbol=reinterpret_cast<SYMBOL_INFOW*>(storage);
            symbol->SizeOfStruct=sizeof(*symbol); symbol->MaxNameLen=2048;
            DWORD64 displacement=0;
            if(SymFromAddrW(process,address,&displacement,symbol))
                wprintf(L"  %ls!%ls+0x%llx\n",module.ModuleName,symbol->Name,displacement);
            else wprintf(L"  %ls+0x%llx [0x%llx]\n",module.ModuleName,address-module.BaseOfImage,address);
        }
        fflush(stdout);
    }
    SymCleanup(process); CloseHandle(thread); CloseHandle(process); return snapshots.empty() ? 1 : 0;
}
