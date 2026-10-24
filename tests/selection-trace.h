// Read-only inspection of the running overlay using this selected source's
// layout. No DLL injection, process writes, suspension or input automation.
#include <sstream>
template<class Flags>
static bool ReadSelectionFlag(HANDLE process,ULONG_PTR address,int total,int index,BYTE& value) {
    const auto read=[&](ULONG_PTR source,auto& target) {
        SIZE_T got=0; return ReadProcessMemory(process,reinterpret_cast<void*>(source),&target,sizeof(target),&got) && got==sizeof(target);
    };
    struct Range {ULONG_PTR first=0,last=0,capacity=0;};
    if constexpr(requires {Flags::PageLength; Flags::StoreSizeOffset(); Flags::StorePagesOffset();}) {
        ULONG_PTR root=0; size_t count=0; Range pages;
        if(!read(address,root) || !root || !read(root+Flags::StoreSizeOffset(),count) || count!=size_t(total) ||
            !read(root+Flags::StorePagesOffset(),pages) || pages.last<pages.first) return false;
        if(index<0 || index>=total) return true;
        const size_t page=size_t(index)/Flags::PageLength;
        if((page+1)*sizeof(std::shared_ptr<void>)>pages.last-pages.first) return false;
        ULONG_PTR bytes=0;
        return read(pages.first+page*sizeof(std::shared_ptr<void>),bytes) && bytes && read(bytes+size_t(index)%Flags::PageLength,value);
    } else {
        Range flags;
        if(!read(address,flags) || flags.last<flags.first || flags.last-flags.first!=size_t(total)) return false;
        return index<0 || index>=total || read(flags.first+index,value);
    }
}
static int TraceLiveSelection(unsigned seconds) {
    struct Target { HWND hwnd; HANDLE process; DWORD pid; std::wstring last; };
    std::vector<Target> targets;
    EnumWindows(+[](HWND frame,LPARAM data)->BOOL {
        if(!IsExplorerFrame(frame)) return TRUE;
        EnumChildWindows(frame,+[](HWND hwnd,LPARAM data)->BOOL {
            wchar_t cls[128]{};
            if(!GetClassNameW(hwnd,cls,ARRAYSIZE(cls)) || wcscmp(cls,kOverlayClass)) return TRUE;
            DWORD pid=0; GetWindowThreadProcessId(hwnd,&pid);
            HANDLE process=OpenProcess(PROCESS_VM_READ|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
            if(process) reinterpret_cast<std::vector<Target>*>(data)->push_back({hwnd,process,pid,{}});
            return TRUE;
        },data);
        return TRUE;
    },reinterpret_cast<LPARAM>(&targets));
    if(targets.empty()) { std::cout<<"No readable Explorer overlay windows found.\n"; return 1; }
    ViewState layout;
    const auto offset=[&](auto member) { return reinterpret_cast<const BYTE*>(&(layout.*member))-reinterpret_cast<const BYTE*>(&layout); };
    struct Range { ULONG_PTR first=0,last=0,capacity=0; };
    static_assert(sizeof(Range)==sizeof(layout.entries));
    const auto start=GetTickCount64();
    std::cout<<"READ_ONLY_SELECTION_TRACE source="<<GPU_OVERLAY_SOURCE<<" windows="<<targets.size()<<" seconds="<<seconds<<std::endl;
    do {
        for(auto& target : targets) {
            if(!IsWindow(target.hwnd) || !IsWindowVisible(target.hwnd)) continue;
            const auto base=static_cast<ULONG_PTR>(GetWindowLongPtrW(target.hwnd,GWLP_USERDATA));
            if(!base) continue;
            const auto read=[&](auto member,auto& value) {
                SIZE_T got=0; return ReadProcessMemory(target.process,reinterpret_cast<void*>(base+offset(member)),
                    &value,sizeof(value),&got) && got==sizeof(value);
            };
            HWND self=nullptr,frame=nullptr,native=nullptr,signal=nullptr;
            unsigned long long token=0,after=0,pending=0;
            int total=0,index=-1,anchor=-1,vacant=-1,restored=-1,observed=-1;
            bool loaded=false,preview=false,dirty=false,import=false,unsafe=false,committing=false,directory=false,marquee=false;
            ULONGLONG optimistic=0;
            Range entries;
            if(!read(&ViewState::hwnd,self) || self!=target.hwnd || !read(&ViewState::frameHwnd,frame) ||
                frame!=GetAncestor(target.hwnd,GA_ROOT) || !read(&ViewState::nativeHwnd,native) || !IsWindow(native) ||
                !read(&ViewState::token,token) || !read(&ViewState::total,total) || total<0 || total>2000000 ||
                !read(&ViewState::entries,entries) || entries.last<entries.first ||
                entries.last-entries.first!=static_cast<size_t>(total)*sizeof(Entry)) continue;
            if(!read(&ViewState::selected,index) || !read(&ViewState::anchor,anchor) ||
                !read(&ViewState::vacantKeyboardRow,vacant) || !read(&ViewState::restoredKeyboardRow,restored) ||
                !read(&ViewState::nativeSelectionObservedCount,observed) || !read(&ViewState::loaded,loaded) ||
                !read(&ViewState::namesPreview,preview) || !read(&ViewState::nativeSelectionDirty,dirty) ||
                !read(&ViewState::nativeSelectionImportPending,import) || !read(&ViewState::nativeSelectionUnsafe,unsafe) ||
                !read(&ViewState::nativeSelectionCommitting,committing) || !read(&ViewState::directoryPending,directory) ||
                !read(&ViewState::pendingNativeSelection,pending) || !read(&ViewState::optimisticStartedAt,optimistic) ||
                !read(&ViewState::marqueeActive,marquee) || !read(&ViewState::nativeSelectionSignalView,signal) ||
                !read(&ViewState::token,after) || after!=token) continue;
            BYTE selected=0;
            if(!ReadSelectionFlag<decltype(layout.selectedFlags)>(target.process,base+offset(&ViewState::selectedFlags),total,index,selected)) continue;
            GUITHREADINFO gui{sizeof(gui)};
            GetGUIThreadInfo(GetWindowThreadProcessId(target.hwnd,nullptr),&gui);
            const HWND foreground=GetForegroundWindow(); wchar_t cls[96]{};
            GetClassNameW(foreground,cls,ARRAYSIZE(cls));
            std::wostringstream line;
            line<<L"pid="<<target.pid<<L" gpu="<<target.hwnd<<L" frame="<<frame<<L" native="<<native
                <<L" token="<<token<<L" rows="<<total<<L" index="<<index<<L" flag="<<unsigned(selected)<<L" anchor="<<anchor
                <<L" vacant="<<vacant<<L" restored="<<restored<<L" observed="<<observed
                <<L" loaded/preview="<<loaded<<L"/"<<preview<<L" dirty/import/unsafe="<<dirty<<L"/"<<import<<L"/"<<unsafe
                <<L" committing/pending="<<committing<<L"/"<<pending<<L" names="<<directory<<L" optimistic="<<(optimistic!=0)
                <<L" marquee="<<marquee<<L" signal="<<signal<<L" enabled="<<IsWindowEnabled(frame)
                <<L" focus="<<gui.hwndFocus<<L" foreground="<<foreground<<L"("<<cls<<L")";
            const auto current=line.str();
            if(current!=target.last) { std::wcout<<L"+"<<(GetTickCount64()-start)<<L"ms "<<current<<std::endl; target.last=current; }
        }
        Sleep(50);
    } while(GetTickCount64()-start<seconds*1000ULL);
    size_t seen=0; for(auto& target : targets) { seen+=!target.last.empty(); CloseHandle(target.process); }
    std::cout<<"TRACE_COMPLETE readable_matching_layouts="<<seen<<std::endl;
    return seen ? 0 : 2;
}
