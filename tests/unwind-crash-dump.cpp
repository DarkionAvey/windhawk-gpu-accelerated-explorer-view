// Read-only offline x64 minidump unwinder. Loaded modules are parsed as data;
// no crashed-process attach, DLL execution, injection, or process writes.
#include <windows.h>
#include <dbghelp.h>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

struct ReadFileMap {
    HANDLE file=INVALID_HANDLE_VALUE,mapping=nullptr;
    const unsigned char* data=nullptr;
    size_t size=0;
    explicit ReadFileMap(const wchar_t* path) {
        file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
        LARGE_INTEGER length{};
        if(file!=INVALID_HANDLE_VALUE && GetFileSizeEx(file,&length)) {
            size=static_cast<size_t>(length.QuadPart);
            mapping=CreateFileMappingW(file,nullptr,PAGE_READONLY,0,0,nullptr);
            if(mapping) data=static_cast<const unsigned char*>(MapViewOfFile(mapping,FILE_MAP_READ,0,0,0));
        }
    }
    ~ReadFileMap() { if(data) UnmapViewOfFile(data); if(mapping) CloseHandle(mapping); if(file!=INVALID_HANDLE_VALUE) CloseHandle(file); }
};
struct DumpRange { DWORD64 base,size; const unsigned char* data; };
struct DumpModule {
    DWORD64 base=0; DWORD size=0,stamp=0;
    std::wstring path;
    std::unique_ptr<ReadFileMap> image;
    bool tried=false;
};
static std::vector<DumpRange> ranges;
static std::vector<DumpModule> modules;
static const IMAGE_NT_HEADERS64* ModuleHeaders(DumpModule& module) {
    if(!module.tried) { module.tried=true; module.image=std::make_unique<ReadFileMap>(module.path.c_str()); }
    auto* image=module.image.get();
    if(!image->data || image->size<sizeof(IMAGE_DOS_HEADER)) return nullptr;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image->data);
    if(dos->e_lfanew<0 || static_cast<size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>image->size) return nullptr;
    auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image->data+dos->e_lfanew);
    return nt->Signature==IMAGE_NT_SIGNATURE && nt->FileHeader.TimeDateStamp==module.stamp ? nt : nullptr;
}
static PVOID CALLBACK FunctionTable(HANDLE,DWORD64 address) {
    for(auto& module : modules) if(address>=module.base && address-module.base<module.size) {
        const auto* nt=ModuleHeaders(module); if(!nt) return nullptr;
        const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if(!directory.VirtualAddress) return nullptr;
        auto* table=static_cast<RUNTIME_FUNCTION*>(ImageRvaToVa(const_cast<IMAGE_NT_HEADERS64*>(nt),
            const_cast<unsigned char*>(module.image->data),directory.VirtualAddress,nullptr));
        if(!table) return nullptr;
        size_t first=0,last=directory.Size/sizeof(*table);
        const auto rva=address-module.base;
        while(first<last) {
            const auto middle=(first+last)/2;
            if(rva<table[middle].BeginAddress) last=middle;
            else if(rva>=table[middle].EndAddress) first=middle+1;
            else return table+middle;
        }
    }
    return nullptr;
}
static BOOL CALLBACK ReadDump(HANDLE,DWORD64 address,PVOID output,DWORD size,LPDWORD read) {
    *read=0;
    for(const auto& range : ranges) if(address>=range.base && address-range.base<range.size) {
        const DWORD n=static_cast<DWORD>(std::min<DWORD64>(size,range.size-(address-range.base)));
        memcpy(output,range.data+(address-range.base),n); *read=n; return n==size;
    }
    for(auto& module : modules) if(address>=module.base && address-module.base<module.size) {
        if(!module.tried) { module.tried=true; module.image=std::make_unique<ReadFileMap>(module.path.c_str()); }
        auto* image=module.image.get();
        if(!image->data || image->size<sizeof(IMAGE_DOS_HEADER)) return FALSE;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image->data);
        if(dos->e_lfanew<0 || static_cast<size_t>(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>image->size) return FALSE;
        auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(image->data+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp!=module.stamp) return FALSE;
        const DWORD rva=static_cast<DWORD>(address-module.base);
        const auto* bytes=static_cast<const unsigned char*>(ImageRvaToVa(const_cast<IMAGE_NT_HEADERS64*>(nt),
            const_cast<unsigned char*>(image->data),rva,nullptr));
        if(!bytes || bytes<image->data || static_cast<size_t>(bytes-image->data)>image->size ||
            size>image->size-static_cast<size_t>(bytes-image->data)) return FALSE;
        memcpy(output,bytes,size); *read=size; return TRUE;
    }
    return FALSE;
}
static void Address(HANDLE process,DWORD64 address) {
    IMAGEHLP_MODULEW64 module{}; module.SizeOfStruct=sizeof(module);
    SymGetModuleInfoW64(process,address,&module);
    alignas(SYMBOL_INFOW) unsigned char storage[sizeof(SYMBOL_INFOW)+2048*sizeof(wchar_t)]{};
    auto* symbol=reinterpret_cast<SYMBOL_INFOW*>(storage);
    symbol->SizeOfStruct=sizeof(*symbol); symbol->MaxNameLen=2048;
    DWORD64 displacement=0;
    if(SymFromAddrW(process,address,&displacement,symbol))
        wprintf(L"0x%llx %ls!%ls+0x%llx [rva=0x%llx]",address,module.ModuleName,symbol->Name,displacement,address-module.BaseOfImage);
    else wprintf(L"0x%llx %ls+0x%llx",address,module.ModuleName,address-module.BaseOfImage);
}
int wmain(int argc,wchar_t** argv) {
    if(argc<2) { fwprintf(stderr,L"Usage: unwind-crash-dump dump-path [symbol-path]\n"); return 2; }
    ReadFileMap dump(argv[1]);
    if(!dump.data) { fprintf(stderr,"Dump open failed: %lu\n",GetLastError()); return 1; }
    const auto stream=[&](MINIDUMP_STREAM_TYPE type)->const void* {
        PMINIDUMP_DIRECTORY directory=nullptr; PVOID data=nullptr; ULONG bytes=0;
        return MiniDumpReadDumpStream(const_cast<unsigned char*>(dump.data),type,&directory,&data,&bytes) ? data : nullptr;
    };
    auto* exception=static_cast<const MINIDUMP_EXCEPTION_STREAM*>(stream(ExceptionStream));
    auto* inventory=static_cast<const MINIDUMP_MODULE_LIST*>(stream(ModuleListStream));
    if(!exception || !inventory || exception->ThreadContext.DataSize<sizeof(CONTEXT)) return 1;
    if(auto* memory=static_cast<const MINIDUMP_MEMORY_LIST*>(stream(MemoryListStream)))
        for(ULONG i=0;i<memory->NumberOfMemoryRanges;++i) {
            const auto& r=memory->MemoryRanges[i]; ranges.push_back({r.StartOfMemoryRange,r.Memory.DataSize,dump.data+r.Memory.Rva});
        }
    if(auto* memory=static_cast<const MINIDUMP_MEMORY64_LIST*>(stream(Memory64ListStream))) {
        DWORD64 offset=memory->BaseRva;
        for(ULONG64 i=0;i<memory->NumberOfMemoryRanges;++i) {
            const auto& r=memory->MemoryRanges[i]; ranges.push_back({r.StartOfMemoryRange,r.DataSize,dump.data+offset}); offset+=r.DataSize;
        }
    }
    if(auto* threads=static_cast<const MINIDUMP_THREAD_LIST*>(stream(ThreadListStream)))
        for(ULONG i=0;i<threads->NumberOfThreads;++i) {
            const auto& r=threads->Threads[i].Stack;
            ranges.push_back({r.StartOfMemoryRange,r.Memory.DataSize,dump.data+r.Memory.Rva});
        }
    HANDLE process=GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS|SYMOPT_FAIL_CRITICAL_ERRORS|SYMOPT_NO_PROMPTS);
    if(!SymInitializeW(process,argc>2 ? argv[2] : nullptr,FALSE)) return 1;
    for(ULONG i=0;i<inventory->NumberOfModules;++i) {
        const auto& source=inventory->Modules[i];
        auto* name=reinterpret_cast<const MINIDUMP_STRING*>(dump.data+source.ModuleNameRva);
        std::wstring path(name->Buffer,name->Length/sizeof(wchar_t));
        SymLoadModuleExW(process,nullptr,path.c_str(),nullptr,source.BaseOfImage,source.SizeOfImage,nullptr,0);
        modules.push_back({source.BaseOfImage,source.SizeOfImage,source.TimeDateStamp,std::move(path)});
    }
    CONTEXT context{}; memcpy(&context,dump.data+exception->ThreadContext.Rva,sizeof(context));
    printf("Exception 0x%08x thread=%u; unwind uses captured memory and matching loaded-module files.\n",
        exception->ExceptionRecord.ExceptionCode,exception->ThreadId);
    if(argc>3 && wcscmp(argv[3],L"--stack-probe")==0) {
        // MinGW's assembly stack probe has no unwind record. Recover its two
        // saved registers only after verifying the actual CALL/probe bytes.
        DWORD64 saved[3]{}; DWORD read=0;
        unsigned char call[5]{},probe[2]{};
        if(!ReadDump(process,context.Rsp,saved,sizeof(saved),&read) ||
            !ReadDump(process,saved[2]-5,call,sizeof(call),&read) || call[0]!=0xe8) return 1;
        LONG delta=0; memcpy(&delta,call+1,sizeof(delta));
        const DWORD64 target=saved[2]+delta;
        if(context.Rip<target || context.Rip>=target+64 ||
            !ReadDump(process,target,probe,sizeof(probe),&read) || probe[0]!=0x51 || probe[1]!=0x50) return 1;
        printf("Verified assembly stack probe: saved allocation=%llu bytes; caller=0x%llx\n",saved[0],saved[2]);
        context.Rax=saved[0]; context.Rcx=saved[1]; context.Rsp+=sizeof(saved); context.Rip=saved[2];
    }
    STACKFRAME64 frame{};
    frame.AddrPC={context.Rip,0,AddrModeFlat}; frame.AddrFrame={context.Rbp,0,AddrModeFlat};
    frame.AddrStack={context.Rsp,0,AddrModeFlat};
    DWORD64 last=0,lastSp=0;
    for(unsigned depth=0;depth<512 && frame.AddrPC.Offset;++depth) {
        printf("%3u sp=0x%llx ",depth,frame.AddrStack.Offset); Address(process,frame.AddrPC.Offset); puts("");
        last=frame.AddrPC.Offset; lastSp=frame.AddrStack.Offset;
        if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,nullptr,&frame,&context,ReadDump,
            FunctionTable,SymGetModuleBase64,nullptr)) break;
        if(frame.AddrPC.Offset==last && frame.AddrStack.Offset==lastSp && depth) break;
    }
    SymCleanup(process); return 0;
}
