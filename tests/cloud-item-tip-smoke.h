// Small ABI declarations for dynamically loaded Windows Cloud Files APIs.
// Mirrors the documented CF_SYNC_REGISTRATION/CF_SYNC_POLICIES and
// CF_PLACEHOLDER_CREATE_INFO layouts; no Windows SDK installation is needed.
// https://learn.microsoft.com/windows/win32/api/cfapi/
namespace CloudTipFixture {
struct Registration {
    ULONG size; LPCWSTR provider,version; const void* rootIdentity; DWORD rootIdentityLength;
    const void* fileIdentity; DWORD fileIdentityLength; GUID providerId;
};
struct Policies {ULONG size; USHORT hydration,hydrationModifier,population,populationModifier;
    DWORD inSync,hardLink,management;};
struct Metadata {FILE_BASIC_INFO basic; LARGE_INTEGER fileSize;};
struct Placeholder {LPCWSTR name; Metadata metadata; const void* identity; DWORD identityLength,flags;
    HRESULT result; LONGLONG usn;};
struct Info {LARGE_INTEGER disk,validated,modified,properties; DWORD pin,inSync;
    LARGE_INTEGER fileId,rootId; ULONG identityLength; BYTE identity[1];};
}
static void TestCloudItemTip(const std::wstring& fixture) {
    using namespace CloudTipFixture;
    const auto root=std::filesystem::path(fixture)/L"cloud-tip";
    std::filesystem::create_directories(root);
    HMODULE module=LoadLibraryW(L"cldapi.dll"); Check(module!=nullptr,"Windows Cloud Files API prerequisite");
    const auto reg=reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR,const Registration*,const Policies*,DWORD)>(GetProcAddress(module,"CfRegisterSyncRoot"));
    const auto unreg=reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR)>(GetProcAddress(module,"CfUnregisterSyncRoot"));
    const auto create=reinterpret_cast<HRESULT(WINAPI*)(LPCWSTR,Placeholder*,DWORD,DWORD,DWORD*)>(GetProcAddress(module,"CfCreatePlaceholders"));
    const auto info=reinterpret_cast<HRESULT(WINAPI*)(HANDLE,DWORD,void*,DWORD,DWORD*)>(GetProcAddress(module,"CfGetPlaceholderInfo"));
    Check(reg && unreg && create && info,"Cloud Files API exports");
    Registration registration{sizeof(Registration),L"GPU tooltip private regression",L"1"};
    Policies policies{sizeof(Policies)}; policies.hydration=0; policies.population=2;
    const HRESULT registered=reg(root.c_str(),&registration,&policies,2|4);
    std::cout<<"Cloud Files registration hr=0x"<<std::hex<<unsigned(registered)<<std::dec<<'\n';
    struct Cleanup {HMODULE module; std::filesystem::path root; decltype(unreg) unregisterRoot; bool registered;
        ~Cleanup() {PropertyFixture::observedPath=nullptr; if(registered) unregisterRoot(root.c_str()); FreeLibrary(module);}} cleanup{module,root,unreg,SUCCEEDED(registered)};
    Check(SUCCEEDED(registered),"private Cloud Files sync-root registration prerequisite (see HRESULT)");
    Placeholder placeholder{}; placeholder.name=L"dehydrated.jpg";
    placeholder.metadata.basic.FileAttributes=FILE_ATTRIBUTE_NORMAL;
    placeholder.metadata.fileSize.QuadPart=65536;
    const BYTE identity=1; placeholder.identity=&identity; placeholder.identityLength=1; placeholder.flags=2;
    DWORD processed=0; const HRESULT made=create(root.c_str(),&placeholder,1,1,&processed);
    std::cout<<"Cloud Files placeholder hr=0x"<<std::hex<<unsigned(made)<<" item=0x"<<unsigned(placeholder.result)<<std::dec<<'\n';
    Check(SUCCEEDED(made) && processed==1 && SUCCEEDED(placeholder.result),"private dehydrated placeholder creation");
    const auto path=(root/placeholder.name).wstring();
    const auto onDisk=[&] {
        HANDLE file=CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        Check(file!=INVALID_HANDLE_VALUE,"placeholder attribute handle");
        std::array<BYTE,sizeof(Info)+4096> bytes{}; DWORD returned=0;
        const auto hr=info(file,1,bytes.data(),DWORD(bytes.size()),&returned); CloseHandle(file);
        Check(SUCCEEDED(hr),"placeholder hydration state");
        Info value{}; memcpy(&value,bytes.data(),sizeof(value)); return value.disk.QuadPart;
    };
    const DWORD attributes=GetFileAttributesW(path.c_str());
    Check(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS) && onDisk()==0,
        "fixture must be a real dehydrated recall-on-access file");
    // Probe Windows' documented offline-store behavior too. Never request OPENSLOWITEM.
    ComPtr<IPropertyStore> handler;
    const auto opened=SHGetPropertyStoreFromParsingName(path.c_str(),nullptr,GPS_HANDLERPROPERTIESONLY,IID_PPV_ARGS(&handler));
    if(SUCCEEDED(opened)) {
        DWORD count=0; handler->GetCount(&count);
        for(DWORD i=0;i<count;++i) {
            PROPERTYKEY key{}; PROPVARIANT value{};
            if(SUCCEEDED(handler->GetAt(i,&key))) handler->GetValue(key,&value);
            PropVariantClear(&value);
        }
    }
    handler.Reset();
    Check(onDisk()==0,"handler-only property-store probe hydrated the private placeholder");
    PropertyFixture::observedPath=path.c_str(); PropertyFixture::observedCalls=PropertyFixture::observedSlowCalls=0;
    ShellSizeBinding sizeBinding; std::map<std::wstring,std::wstring> typeNames;
    for(DWORD snapshot:{attributes,DWORD(FILE_ATTRIBUTE_NORMAL)}) {
        // The second hover models a listing captured before dehydration. The
        // actual Windows attributes must protect it even with stale job data.
        ItemTipJob job; job.path=path; job.attributes=snapshot; job.sizeKnown=true; job.size=65536;
        job.detailed=true; job.rowLimit=100;
        ResolveItemTip(job,sizeBinding,typeNames,[]{return true;});
        Check(!job.rows.empty() && std::any_of(job.rows.begin(),job.rows.end(),[](const auto& row){return SameKey(row.key,PKEY_Size);}),
            "dehydrated tooltip lost captured file metadata");
    }
    Check(PropertyFixture::observedSlowCalls==0 && onDisk()==0 &&
        (GetFileAttributesW(path.c_str())&FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS),"hover opened a slow store or hydrated the placeholder");
    PropertyFixture::observedPath=nullptr;
    Check(SUCCEEDED(unreg(root.c_str())),"private Cloud Files sync-root cleanup");
    cleanup.registered=false;
    std::cout<<"PASS: real dehydrated JPEG remains at zero on-disk data after handler probe and guarded tooltip resolution\n";
}
