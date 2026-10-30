// Actual tab-owned production pipeline over private Win32 filesystem fixtures.
// Creation and correctness checks are outside the timed interval. OS caches
// remain warm; these numbers do not measure displayed Explorer frame rates.
#include "sort-algorithm-benchmark.h"

static void TestMixedDirectoryIndex(const std::wstring& fixture, const std::vector<Entry>& nested) {
    std::vector<Entry> rows(nested.begin(),nested.begin()+std::min(size_t(3),nested.size()));
    WIN32_FIND_DATAW data{};
    HANDLE find=FindFirstFileExW(JoinPath(fixture,L"*").c_str(),FindExInfoBasic,&data,FindExSearchNameMatch,nullptr,0);
    Check(find!=INVALID_HANDLE_VALUE,"mixed-directory identity fixture");
    do {
        if (data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) continue;
        Entry entry; entry.name=data.cFileName; entry.path=JoinPath(fixture,entry.name);
        entry.attributes=data.dwFileAttributes; entry.created=data.ftCreationTime;
        entry.accessed=data.ftLastAccessTime; entry.lastWrite=data.ftLastWriteTime;
        entry.byteSize=(ULONGLONG(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
        rows.push_back(std::move(entry)); break;
    } while (FindNextFileW(find,&data));
    FindClose(find);
    Check(rows.size()>3,"mixed-directory fixture has no parent file");
    const auto index=BuildDirectorySnapshotIndex(rows,[] {return true;});
    Check(index.orderHash==DirectoryOrderHash(rows) && index.metadataHash==DirectoryMetadataHash(rows),
        "shared prefix hash changed a mixed-directory fingerprint");
    for (const auto& [hash,row] : index.paths)
        Check(hash==SelectionPathHash(rows[row].path),"shared prefix hash failed its mixed-path fallback");
}

static void TestPreprocessingBenchmarks(const std::wstring& fixture, bool extended=false) {
    const bool tracing=g_pipelineTracing.exchange(true);
    struct Restore {bool trace; ~Restore() {g_pipelineTracing=trace;}} restore{tracing};
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    std::cout<<"PREPROCESS_HEADER,items,sort,run,bind_ms,enumerate_ms,sort_ms,baseline_ms,snapshot_ms,ready_ms\n";
    for (const int count : extended ? std::vector<int>{1000,10000,50000} : std::vector<int>{1000,10000}) {
        const auto root=std::filesystem::path(fixture)/(L"preprocessing-"+std::to_wstring(count));
        Check(std::filesystem::create_directory(root),"preprocessing private directory");
        const wchar_t* extensions[]={L".txt",L".cpp",L".bin",L".log"};
        for (int i=0;i<count;++i) {
            const int id=(i*7919)%count; // Coprime to these sizes; creation order differs from name order.
            const auto path=root/((id%3 ? L"Item-" : L"café-")+std::to_wstring(id)+extensions[id%4]);
            if (id%32==0) {Check(CreateDirectoryW(path.c_str(),nullptr)!=FALSE,"preprocessing fixture folder"); continue;}
            HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE|FILE_WRITE_ATTRIBUTES,0,nullptr,CREATE_NEW,0,nullptr);
            Check(file!=INVALID_HANDLE_VALUE,"preprocessing fixture file");
            LARGE_INTEGER length{}; length.QuadPart=(id*31)%1024;
            const ULONGLONG value=135000000000000000ULL+(id%997)*10000000ULL;
            const FILETIME stamp{DWORD(value),DWORD(value>>32)};
            Check(SetFilePointerEx(file,length,nullptr,FILE_BEGIN) && SetEndOfFile(file) &&
                SetFileTime(file,&stamp,&stamp,&stamp),"preprocessing fixture metadata");
            CloseHandle(file);
        }
        ViewState state; state.hwnd=MessageWindow(); state.background->hwnd=state.hwnd;
        state.folderPath=root.wstring(); state.rowPx=32;
        auto& work=*state.background;
        work.pipeline=std::make_unique<PipelineTrace>(); work.pipelinePublished=work.pipeline.get();
        struct Choice {const char* name; PROPERTYKEY key; SORTDIRECTION direction; bool sorted;};
        const Choice choices[]={
            {"unsorted",PKEY_ItemNameDisplay,SORT_ASCENDING,false},
            {"name-asc",PKEY_ItemNameDisplay,SORT_ASCENDING,true},
            {"name-desc",PKEY_ItemNameDisplay,SORT_DESCENDING,true},
            {"modified",PKEY_DateModified,SORT_DESCENDING,true},
            {"size",PKEY_Size,SORT_DESCENDING,true},
            {"extension",PKEY_FileExtension,SORT_ASCENDING,true},
            {"type",PKEY_ItemTypeText,SORT_ASCENDING,true},
        };
        for (const auto& choice : choices) {
            std::vector<double> totals;
            for (int run=0;run<(extended ? 5 : 3);++run) {
                state.sort=DefaultSort(); state.sort.key=choice.key;
                state.sort.kind=ClassifySort(choice.key); state.sort.direction=choice.direction;
                state.sort.explicitOrder=choice.sorted;
                std::array<PipelineRecord,128> records;
                while (work.pipeline->Drain(records.data(),records.size())) {}
                LARGE_INTEGER start{}; QueryPerformanceCounter(&start);
                QueueDirectoryRefresh(state,true);
                auto result=WaitForComplete(work,state.directoryToken);
                std::map<std::wstring,double> stages;
                double ready=-1;
                size_t n=0;
                while ((n=work.pipeline->Drain(records.data(),records.size())))
                    for (size_t i=0;i<n;++i) {
                        const auto& event=records[i];
                        if (event.a==state.directoryToken && event.phase==L'E') stages[event.operation]+=event.v[0];
                        if (wcscmp(event.operation,L"worker.publish")==0 &&
                            event.a==DirectoryResult::Complete && event.b==state.directoryToken)
                            ready=1000.*(event.qpc-start.QuadPart)/frequency.QuadPart;
                    }
                Check(ready>=0 && stages.contains(L"directory.bind") && stages.contains(L"directory.enumerate") &&
                    stages.contains(L"directory.sort") && stages.contains(L"directory.baseline") &&
                    stages.contains(L"directory.snapshot"),"preprocessing trace lost a required timing record");
                Check(result.error==ERROR_SUCCESS && result.entries.size()==size_t(count) &&
                    result.pathIndex.size()==size_t(count),"large-directory preprocessing lost identities");
                Check(result.orderHash==DirectoryOrderHash(result.entries) &&
                    result.metadataHash==DirectoryMetadataHash(result.entries),"preprocessing reused stale fingerprints");
                Check(std::is_sorted(result.pathIndex.begin(),result.pathIndex.end()),"preprocessing identity index order");
                std::vector<bool> indexed(count);
                for (const auto& [hash,row] : result.pathIndex) {
                    Check(row>=0 && row<count && !indexed[row] &&
                        hash==SelectionPathHash(result.entries[row].path),"preprocessing identity index mismatch");
                    indexed[row]=true;
                }
                if (choice.sorted) Check(std::is_sorted(result.entries.begin(),result.entries.end(),
                    [&](const Entry& a,const Entry& b) {return EntryPrecedes(a,b,state.sort);}),
                    "large-directory preprocessing returned the wrong order");
                else {
                    WIN32_FIND_DATAW data{}; const auto pattern=JoinPath(state.folderPath,L"*");
                    HANDLE find=FindFirstFileExW(pattern.c_str(),FindExInfoBasic,&data,FindExSearchNameMatch,nullptr,0);
                    Check(find!=INVALID_HANDLE_VALUE,"benchmark order reference"); size_t row=0;
                    do {
                        if (!wcscmp(data.cFileName,L".") || !wcscmp(data.cFileName,L"..")) continue;
                        Check(row<result.entries.size() && result.entries[row++].name==data.cFileName,
                            "opt-out changed Win32 enumeration order");
                    } while (FindNextFileW(find,&data));
                    FindClose(find); Check(row==result.entries.size(),"benchmark order reference count");
                }
                totals.push_back(ready);
                std::cout<<"PREPROCESS,"<<count<<','<<choice.name<<','<<run+1<<','
                    <<stages[L"directory.bind"]<<','<<stages[L"directory.enumerate"]<<','
                    <<stages[L"directory.sort"]<<','<<stages[L"directory.baseline"]<<','
                    <<stages[L"directory.snapshot"]<<','<<ready<<'\n';
            }
            std::sort(totals.begin(),totals.end());
            std::cout<<"PREPROCESS_SUMMARY,"<<count<<','<<choice.name<<",median_ms,"<<totals[totals.size()/2]
                <<",max_ms,"<<totals.back()<<'\n';
        }
        // Keep algorithm experiments outside the sequence of pipeline samples.
        // Obtain their input through another real unsorted worker enumeration.
        if (extended ? count==50000 : count==1000) {
            state.sort=DefaultSort(); state.sort.explicitOrder=false;
            QueueDirectoryRefresh(state,true);
            const auto rows=WaitForComplete(work,state.directoryToken);
            TestMixedDirectoryIndex(fixture,rows.entries);
            TestSortAlgorithms(rows.entries,extended);
        }
        Stop(state.background); DestroyWindow(state.hwnd);
        Check(std::filesystem::weakly_canonical(root).parent_path()==std::filesystem::weakly_canonical(fixture),
            "preprocessing cleanup escaped its private fixture");
        Check(Eventually([&] {
            std::error_code error; std::filesystem::remove_all(root,error); return !error;
        }),"preprocessing fixture cleanup did not settle");
    }
    std::cout<<"PASS: private large-directory preprocessing correctness and phase benchmarks (warm component timings)\n";
}
