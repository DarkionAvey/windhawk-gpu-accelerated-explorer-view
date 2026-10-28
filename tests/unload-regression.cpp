#include <windows.h>
#include <unknwn.h>
#include <iostream>
#include <stdexcept>
#include <uiautomationcore.h>

static HANDLE entered,released,returned,exitWorker,unloaded;
static void Check(bool good,const char* message) { if(!good) throw std::runtime_error(message); }
static void BlockNative() { SetEvent(entered); WaitForSingleObject(released,INFINITE); }
static LRESULT WINAPI Dispatch(const MSG*) { BlockNative(); return 0; }
static int WINAPI Accelerator(HWND,HACCEL,LPMSG) { BlockNative(); return 0; }
static BOOL WINAPI Show(HWND,int) { BlockNative(); return TRUE; }
static BOOL WINAPI Position(HWND,HWND,int,int,int,int,UINT) { BlockNative(); return TRUE; }
static HDWP WINAPI Deferred(HDWP,HWND,HWND,int,int,int,int,UINT) { BlockNative(); return nullptr; }
static HRESULT WINAPI Structure(IRawElementProviderSimple*,StructureChangeType,int*,int) { BlockNative(); return S_OK; }
using InvokeFn=void(*)(int);
static InvokeFn invoke;
static unsigned recursiveDepth=0,recursiveReturns=0;
static LRESULT WINAPI RecursiveDispatch(const MSG*) {
    if (++recursiveDepth<32) invoke(0); // Actual DLL hook reenters the actual DLL hook.
    --recursiveDepth;
    ++recursiveReturns;
    return 0;
}
static DWORD WINAPI SmallStackDispatch(void*) { invoke(0); return 0; }
static void(*unload)();
static DWORD WINAPI Worker(void* kind) {
    invoke(static_cast<int>(reinterpret_cast<INT_PTR>(kind)));
    SetEvent(returned);
    WaitForSingleObject(exitWorker,INFINITE); // Deliberately outlive the mod DLL.
    return 0;
}
static DWORD WINAPI Unloader(void*) { unload(); SetEvent(unloaded); return 0; }
template<typename T> static T Export(HMODULE module,const char* name) {
    auto address=GetProcAddress(module,name); Check(address!=nullptr,"missing lifecycle export");
    return reinterpret_cast<T>(address);
}
static HMODULE Load(const wchar_t* path) {
    HMODULE module=LoadLibraryW(path); Check(module!=nullptr,"test DLL load failed");
    unload=Export<void(*)()>(module,"Unload");
    Check(Export<BOOL(*)()>(module,"PrepareMessages")(),"test DLL message registration");
    return module;
}

int wmain(int argc,wchar_t** argv) {
    if(argc==3 && wcscmp(argv[2],L"--process-exit")==0) {
        HMODULE module=Load(argv[1]);
        if(!Export<BOOL(*)()>(module,"BlockedPoolAtProcessExit")()) return 3;
        ExitProcess(0); // Loader teardown, deliberately without Wh_ModUninit.
    }
    if(argc!=2) return 2;
    entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    released=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    returned=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    exitWorker=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    unloaded=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    try {
        {
            wchar_t executable[MAX_PATH]{}; Check(GetModuleFileNameW(nullptr,executable,MAX_PATH)!=0,"exit fixture executable");
            std::wstring command=L"\""+std::wstring(executable)+L"\" \""+argv[1]+L"\" --process-exit";
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
            Check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),
                "process-exit child creation");
            const DWORD wait=WaitForSingleObject(process.hProcess,10000);
            DWORD code=1;
            if(wait==WAIT_OBJECT_0) GetExitCodeProcess(process.hProcess,&code);
            else {TerminateProcess(process.hProcess,1); WaitForSingleObject(process.hProcess,5000);}
            CloseHandle(process.hThread); CloseHandle(process.hProcess);
            Check(wait==WAIT_OBJECT_0 && code==0,"loader teardown joined an abandoned pool callback at process exit");
            std::cout<<"PASS: graceful process exit with a blocked pool callback and registry-only worker ownership, without explicit mod cleanup\n";
        }
        {
            HMODULE module=Load(argv[1]);
            HWND frame=Export<HWND(*)()>(module,"RetainedFrameAfterHookUpgrade")();
            Check(frame!=nullptr,"upgraded hook owner retained its frame subclass");
            Export<void(*)()>(module,"Before")(); unload();
            Check(FreeLibrary(module),"retained frame DLL release");
            MEMORY_BASIC_INFORMATION memory{};
            Check(VirtualQuery(module,&memory,sizeof(memory)) && memory.AllocationBase!=module,"frame test DLL remained mapped");
            SendMessageW(frame,WM_NULL,0,0); Check(DestroyWindow(frame),"retained frame after DLL unmapping");
            std::cout<<"PASS: frame messages/destruction after upgraded-hook owner release and verified DLL unmapping\n";
        }
        {
            HMODULE nested=Load(argv[1]);
            Export<void(*)(int,FARPROC)>(nested,"Prepare")(0,reinterpret_cast<FARPROC>(RecursiveDispatch));
            invoke=Export<InvokeFn>(nested,"Invoke");
            HANDLE small=CreateThread(nullptr,256*1024,SmallStackDispatch,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
            Check(small && WaitForSingleObject(small,5000)==WAIT_OBJECT_0 && recursiveReturns==32,
                "nested native dispatch exhausted a 256 KiB thread stack");
            CloseHandle(small);
            Export<void(*)()>(nested,"Before")(); unload();
            Check(FreeLibrary(nested)!=FALSE,"nested dispatch DLL release");
            std::cout<<"PASS: 32 nested actual DLL dispatch hooks return on a 256 KiB stack\n";
        }
        const FARPROC originals[]{reinterpret_cast<FARPROC>(Dispatch),reinterpret_cast<FARPROC>(Accelerator),
            reinterpret_cast<FARPROC>(Show),reinterpret_cast<FARPROC>(Show),
            reinterpret_cast<FARPROC>(Position),reinterpret_cast<FARPROC>(Deferred),reinterpret_cast<FARPROC>(Structure)};
        // Repeat actual LoadLibrary/FreeLibrary, keeping callback threads alive
        // past unloading to exercise both return-address and TLS-dtor lifetime.
        for(int round=0;round<3;++round) for(int kind=0;kind<7;++kind) {
            for(HANDLE event : {entered,released,returned,exitWorker,unloaded}) ResetEvent(event);
            HMODULE module=Load(argv[1]);
            Export<void(*)(int,FARPROC)>(module,"Prepare")(kind,originals[kind]);
            invoke=Export<InvokeFn>(module,"Invoke");
            HANDLE worker=CreateThread(nullptr,0,Worker,reinterpret_cast<void*>(static_cast<INT_PTR>(kind)),0,nullptr);
            Check(worker && WaitForSingleObject(entered,5000)==WAIT_OBJECT_0,"native hook did not enter");
            Export<void(*)()>(module,"Before")(); // Must NOT wait before detours are removed.
            HANDLE unloader=CreateThread(nullptr,0,Unloader,nullptr,0,nullptr);
            Check(unloader!=nullptr,"unloader thread creation");
            const bool held=WaitForSingleObject(unloaded,100)==WAIT_TIMEOUT;
            SetEvent(released);
            Check(WaitForSingleObject(returned,5000)==WAIT_OBJECT_0 &&
                WaitForSingleObject(unloader,5000)==WAIT_OBJECT_0,"unload did not finish after native return");
            if(!held) {
                // Safe negative check against historical sources: finish the
                // thread while its TLS destructor still has a mapped DLL.
                SetEvent(exitWorker); WaitForSingleObject(worker,5000);
                FreeLibrary(module); CloseHandle(worker); CloseHandle(unloader);
                throw std::runtime_error("unload completed while a native hook retained a DLL return address");
            }
            Check(FreeLibrary(module)!=FALSE,"test DLL did not unload");
            MEMORY_BASIC_INFORMATION memory{};
            Check(VirtualQuery(module,&memory,sizeof(memory)) && memory.AllocationBase!=module,
                "DLL remained mapped: thread-exit test would not establish unload safety");
            SetEvent(exitWorker);
            Check(WaitForSingleObject(worker,5000)==WAIT_OBJECT_0,"thread did not exit after DLL unload");
            CloseHandle(worker); CloseHandle(unloader);
        }
        std::cout<<"PASS: all seven API hooks drain after removal, 21 real DLL unloads before callback-thread exit, no delayed TLS destructor\n";
        {
            HMODULE rename=Load(argv[1]);
            HWND editor=Export<HWND(*)()>(rename,"RetainedRenameEditor")();
            Check(editor!=nullptr,"retained rename editor kept its subclass or freed font");
            HWND frame=GetParent(editor);
            Export<void(*)()>(rename,"Before")(); unload();
            Check(FreeLibrary(rename)!=FALSE,"rename DLL release");
            MEMORY_BASIC_INFORMATION memory{};
            Check(VirtualQuery(rename,&memory,sizeof(memory)) && memory.AllocationBase!=rename,
                "rename test DLL remained mapped");
            // These messages and NCDESTROY run after verified DLL unmapping.
            Check(SetWindowTextW(editor,L"after unload"),"detached EDIT lost its native procedure");
            wchar_t text[32]{};
            Check(GetWindowTextW(editor,text,32)==12 && wcscmp(text,L"after unload")==0,
                "detached EDIT cannot process native text messages");
            Check(DestroyWindow(frame)!=FALSE && !IsWindow(editor),"detached EDIT destruction after unload");
            std::cout<<"PASS: surviving rename EDIT has no mod subclass, uses a stock font and handles text/destruction after verified DLL unmapping\n";
        }
        {
            HMODULE barrier=Load(argv[1]);
            Check(Export<BOOL(*)()>(barrier,"NativeBarrierRetirement")(),"native creation barrier did not suppress/retire with its owning overlay");
            Export<void(*)()>(barrier,"Before")();
            unload();
            Check(FreeLibrary(barrier)!=FALSE,"native barrier DLL release");
            MEMORY_BASIC_INFORMATION memory{};
            if(VirtualQuery(barrier,&memory,sizeof(memory)) && memory.AllocationBase==barrier) {
                wchar_t file[MAX_PATH]{}; GetModuleFileNameW(barrier,file,MAX_PATH);
                std::wcerr<<L"Native barrier retained image="<<file<<L" type="<<memory.Type<<L" state="<<memory.State<<L'\n';
            }
            Check(VirtualQuery(barrier,&memory,sizeof(memory)) && memory.AllocationBase!=barrier,
                "synchronous native barrier retained its DLL after teardown");
            std::cout<<"PASS: actual disable retires a native touch recognizer/hold deadline, restores native visibility/geometry and permits verified DLL unmapping\n";
        }
        HMODULE module=Load(argv[1]);
        Check(Export<BOOL(*)()>(module,"FailedInitialization")(),"module-owned class conflict or initialization unwind failed");
        std::cout<<"PASS: module-owned overlay class rejects conflicts, coexists with the EXE class and unwinds with its event/factory after failed hooks\n";
        Check(Export<BOOL(*)()>(module,"NestedRetirement")(),"nested destruction freed live overlay state/resources");
        std::cout<<"PASS: foreign threads cannot borrow ViewState; reentrant destruction detaches immediately and retires state/resources after the outer callback\n";
        IUnknown* sink=Export<IUnknown*(*)()>(module,"RetainSink")();
        ResetEvent(unloaded);
        Export<void(*)()>(module,"Before")();
        HANDLE unloader=CreateThread(nullptr,0,Unloader,nullptr,0,nullptr);
        Check(WaitForSingleObject(unloaded,100)==WAIT_TIMEOUT,"unload ignored an externally retained COM callback object");
        sink->Release();
        Check(WaitForSingleObject(unloader,5000)==WAIT_OBJECT_0,"COM reference release did not allow unload");
        CloseHandle(unloader); FreeLibrary(module);
        std::cout<<"PASS: an external COM reference keeps callback vtables mapped until its final Release\n";
        for(HANDLE event : {entered,released,returned,exitWorker,unloaded}) CloseHandle(event);
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"FAIL: "<<error.what()<<'\n';
        // Do not unwind/free a DLL still executing on a test thread.
        ExitProcess(1);
    }
}
