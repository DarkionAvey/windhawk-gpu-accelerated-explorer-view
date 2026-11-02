// Real native UI on a non-input desktop. A child process owns every Shell,
// graphics and thread-pool thread; its exit precedes desktop handle cleanup.
#pragma once
namespace PrivateDesktopFixture {
inline void Run(const wchar_t* purpose,const std::wstring& arguments,DWORD timeout) {
    const std::wstring name=L"gpu-overlay-"+std::wstring(purpose)+L"-"+std::to_wstring(GetCurrentProcessId());
    const HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    Check(desktop!=nullptr,"private desktop creation");
    wchar_t station[256]{}; DWORD length=0;
    Check(GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&length),
        "private window station");
    std::wstring destination=std::wstring(station)+L"\\"+name;
    std::wstring module(32768,L'\0');
    module.resize(GetModuleFileNameW(nullptr,module.data(),DWORD(module.size())));
    std::wstring command=L"\""+module+L"\" "+arguments;
    SECURITY_ATTRIBUTES attributes{sizeof(attributes),nullptr,TRUE};
    HANDLE read=nullptr,write=nullptr;
    Check(CreatePipe(&read,&write,&attributes,65536) && SetHandleInformation(read,HANDLE_FLAG_INHERIT,0),
        "private desktop diagnostics pipe");
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,
        &attributes,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW startup{sizeof(startup)}; startup.lpDesktop=destination.data();
    startup.dwFlags=STARTF_USESTDHANDLES; startup.hStdOutput=startup.hStdError=write; startup.hStdInput=input;
    PROCESS_INFORMATION child{};
    const BOOL created=CreateProcessW(module.c_str(),command.data(),nullptr,nullptr,TRUE,
        CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
    CloseHandle(write); if(input!=INVALID_HANDLE_VALUE) CloseHandle(input);
    if(!created) {CloseHandle(read); CloseDesktop(desktop); Check(false,"private desktop child creation");}
    const DWORD waited=WaitForSingleObject(child.hProcess,timeout);
    DWORD result=DWORD(-1);
    if(waited==WAIT_OBJECT_0) GetExitCodeProcess(child.hProcess,&result);
    else {
        TerminateProcess(child.hProcess,125); // Isolated test process, never Explorer/provider threads.
        WaitForSingleObject(child.hProcess,INFINITE);
    }
    DWORD available=0,received=0; char buffer[4096];
    while(PeekNamedPipe(read,nullptr,0,nullptr,&available,nullptr) && available &&
        ReadFile(read,buffer,std::min<DWORD>(available,sizeof(buffer)),&received,nullptr) && received)
        std::cout.write(buffer,received);
    CloseHandle(read); CloseHandle(child.hThread); CloseHandle(child.hProcess);
    const BOOL closed=CloseDesktop(desktop);
    if(waited!=WAIT_OBJECT_0 || result)
        std::wcout<<L"Private desktop="<<purpose<<L" wait="<<waited<<L" exit="<<result<<std::endl;
    Check(waited==WAIT_OBJECT_0 && result==0,"native UI regression failed on the isolated desktop");
    Check(closed,"private desktop cleanup after child exit");
}
}
