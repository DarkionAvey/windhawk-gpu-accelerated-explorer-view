// Standalone CPU benchmark: compile only extracted matcher/ICU bindings, never
// the mod or a regression executable. No files, clipboard, focus or input IO.
#include "type-search-source.h"
#include "type-search-baseline.h"
#include <array>
#include <iomanip>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using Matcher=UINT32(*)(const std::wstring&,const std::wstring&);
static void Require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static void Equivalent(const std::wstring& name,const std::wstring& query) {
    Require(TypeSearchBaseline::TypeSearchPrefixLength(name,query)==
        TypeSearchCandidate::TypeSearchPrefixLength(name,query),"matching/highlight extent changed");
}
static void CheckEquivalence() {
    const std::vector<std::wstring> pieces={L"",L"a",L"A",L"z",L"9",L"-",L" ",L".",L"_",L"\t",
        L"\u00E9",L"\u0301",L"\u00DF",L"\u0130",L"\u6587",L"\u0627",L"\u2605",L"\uFE0F",L"\u200D",
        L"\U0001F600",L"\U00010400",L"\U00010428",L"\U0001D7D8",std::wstring(1,wchar_t(0xD83D)),
        std::wstring(1,wchar_t(0xDE00)),std::wstring(L"\0x",2)};
    // Deterministic differential corpus, including invalid UTF-16 and embedded
    // nulls, plus queries derived from names to exercise successful paths.
    std::mt19937 random(0x53454152);
    for(size_t i=0;i<100000;++i) {
        std::wstring name,query;
        for(size_t j=0,n=random()%20;j<n;++j) name+=pieces[random()%pieces.size()];
        if(i%3==0) query=name.substr(0,name.empty() ? 0:random()%(name.size()+1));
        else if(i%3==1) {
            for(size_t j=0;j<name.size();++j) {
                query+=name[j];
                if(random()%4==0) query+=L" -";
            }
        } else for(size_t j=0,n=random()%12;j<n;++j) query+=pieces[random()%pieces.size()];
        Equivalent(name,query);
    }
    for(const auto& name:pieces) for(const auto& query:pieces) Equivalent(name,query);
    if(g_systemRelativeTime.UnicodeReady()) {
        Require(TypeSearchCandidate::TypeSearchPrefixLength(L"file\U0001F600name.txt",L"filename")==10,
            "independent emoji highlight expectation failed");
        Require(TypeSearchCandidate::TypeSearchPrefixLength(L"Program files",L"Program fi")==10,
            "independent whitespace extent expectation failed");
        Require(TypeSearchCandidate::TypeSearchPrefixLength(L"\U00010400-file.txt",L"file")==0,
            "supplementary letter was discarded");
    }
    std::cout<<"CHECK,"<<(g_systemRelativeTime.UnicodeReady() ? "icu":"windows-fallback")
        <<",100676 matching-and-extent comparisons passed\n";
}
struct Workload {const char* label; std::vector<std::wstring> names,queries;};
static uint64_t Scan(const Workload& work,Matcher matcher) {
    uint64_t result=0;
    for(const auto& query:work.queries) for(const auto& name:work.names) result+=matcher(name,query);
    return result;
}
static volatile uint64_t witness=0;
static double Timed(const Workload& work,Matcher matcher,double frequency) {
    LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
    const auto result=Scan(work,matcher);
    QueryPerformanceCounter(&end); witness=result;
    return 1000.*double(end.QuadPart-begin.QuadPart)/frequency/double(work.queries.size());
}
static void Benchmark(const Workload& work,double frequency,const char* mode) {
    for(const auto& query:work.queries) for(const auto& name:work.names) Equivalent(name,query);
    Require(Scan(work,TypeSearchBaseline::TypeSearchPrefixLength)==
        Scan(work,TypeSearchCandidate::TypeSearchPrefixLength),"workload extent checksum changed");
    std::array<double,7> before{},after{};
    for(size_t i=0;i<before.size();++i) {
        if(i%2) after[i]=Timed(work,TypeSearchCandidate::TypeSearchPrefixLength,frequency);
        before[i]=Timed(work,TypeSearchBaseline::TypeSearchPrefixLength,frequency);
        if(!(i%2)) after[i]=Timed(work,TypeSearchCandidate::TypeSearchPrefixLength,frequency);
        std::cout<<"SAMPLE,"<<mode<<','<<work.label<<','<<work.names.size()<<','<<i+1<<','
            <<before[i]<<','<<after[i]<<'\n'<<std::flush;
    }
    std::sort(before.begin(),before.end()); std::sort(after.begin(),after.end());
    std::cout<<"MEDIAN,"<<mode<<','<<work.label<<','<<work.names.size()<<",7,"
        <<before[3]<<','<<after[3]<<','<<before[3]/after[3]<<'\n'<<std::flush;
}
static int ReferenceFind(const std::vector<std::wstring>& names,const std::wstring& query,int start,
                         const std::vector<bool>& visible) {
    for(size_t offset=1;offset<=names.size();++offset) {
        const size_t row=(size_t(start+1)+offset-1)%names.size();
        if(visible[row] && TypeSearchCandidate::TypeSearchPrefixLength(names[row],query)) return int(row);
    }
    return -1;
}
static void CheckCandidateIndex() {
    std::mt19937 random(0x53554253);
    const std::vector<std::wstring> queries={L"S",L"Sy",L"Sys",L"S y",L"S-y-s",L"Sy",L"S",L"",L"S",
        L"SS",L"S",L"---",L"-S",L"\U00010400",L"\U00010400f",L"S\U0001F600y",L"S ",
        L"S"+std::wstring(1,wchar_t(0xD83D)),L"S\U0001F600",L"S",L"S"};
    TypeSearchCandidates cache;
    for(size_t round=0;round<1000;++round) {
        std::vector<std::wstring> names(130);
        std::vector<bool> visible(names.size(),true);
        for(auto& name:names) {
            name=std::wstring(1,wchar_t(L'A'+random()%26))+L"Archive";
            if(random()%3==0) name=L"-\U0001F600"+name;
        }
        names[63]=L"System"; names[64]=L"S-y-stem"; names[129]=L"\U00010400file";
        int selected=-1;
        unsigned long long order=round;
        for(size_t step=0;step<queries.size();++step) {
            const auto& query=queries[step];
            if(query.empty()) {cache.Reset(); continue;}
            if(step==5) {names[0]=L"System renamed"; cache.Reset();}
            if(step==10) {std::reverse(names.begin(),names.end()); ++order;}
            if(step==15) {names.push_back(L"System added"); visible.push_back(true); ++order;}
            for(size_t row=0;row<visible.size();++row) visible[row]=(row+step)%7!=0;
            const int start=query.size()==1 ? selected:-1;
            auto* candidates=TypeSearchCandidate::PrepareTypeSearchCandidates(cache,query,names.data(),names.size(),round,0,order);
            const int actual=TypeSearchCandidate::FindTypeSearchCandidate(query,start,names.size(),candidates,
                [&](size_t row)->const std::wstring& {return names[row];},[&](int row) {return visible[size_t(row)];});
            Require(actual==ReferenceFind(names,query,start,visible),"first-letter subset changed selection/wraparound");
            if(actual>=0) selected=actual;
        }
        cache.Reset();
        Require(cache.initial.empty() && cache.possible.empty(),"empty search did not cancel candidates");
    }
    std::cout<<"CHECK,index,"<<(g_systemRelativeTime.UnicodeReady() ? "icu":"windows-fallback")
        <<",20000 query/visibility/rename/reorder/addition comparisons passed\n";
}
struct ChainResult {uint64_t checksum=0,rows=0;};
static ChainResult SearchChain(const Workload& work,bool indexed,bool printRows=false) {
    TypeSearchCandidates cache;
    ChainResult result;
    int selected=-1;
    for(const auto& query:work.queries) {
        const int start=query.size()==1 ? selected:-1;
        auto* candidates=indexed ? TypeSearchCandidate::PrepareTypeSearchCandidates(cache,query,work.names.data(),work.names.size(),1,0,1):nullptr;
        const auto before=result.rows;
        const int found=TypeSearchCandidate::FindTypeSearchCandidate(query,start,work.names.size(),candidates,
            [&](size_t row)->const std::wstring& {++result.rows; return work.names[row];},[](int) {return true;});
        if(found>=0) selected=found;
        result.checksum=result.checksum*131+uint64_t(found+1);
        if(printRows) std::cout<<"KEY_ROWS,"<<work.label<<','<<query.size()<<','<<indexed<<','<<result.rows-before<<'\n';
    }
    return result;
}
static void BenchmarkChain(const Workload& work,double frequency,const char* mode) {
    const auto baseline=SearchChain(work,false,true),indexed=SearchChain(work,true,true);
    Require(baseline.checksum==indexed.checksum,"incremental search sequence changed selected items");
    const size_t repeat=std::string_view(work.label)=="early-hit" ? 1000:3;
    const auto time=[&](bool useIndex) {
        LARGE_INTEGER begin{},end{}; QueryPerformanceCounter(&begin);
        uint64_t sum=0;
        for(size_t run=0;run<repeat;++run) sum+=SearchChain(work,useIndex).checksum;
        QueryPerformanceCounter(&end); witness=sum;
        return 1000.*double(end.QuadPart-begin.QuadPart)/frequency/double(repeat);
    };
    std::array<double,7> before{},after{};
    for(size_t i=0;i<before.size();++i) {
        if(i%2) after[i]=time(true);
        before[i]=time(false);
        if(!(i%2)) after[i]=time(true);
    }
    std::sort(before.begin(),before.end()); std::sort(after.begin(),after.end());
    std::cout<<"CHAIN,"<<mode<<','<<work.label<<','<<work.names.size()<<",7,"<<before[3]<<','<<after[3]
        <<','<<before[3]/after[3]<<','<<baseline.rows<<','<<indexed.rows<<'\n'<<std::flush;
}
int wmain(int argc,wchar_t** argv) {
    try {
        const size_t rows=argc>1 ? std::stoul(argv[1]):20000;
        Require(rows>0 && rows<=1000000,"rows must be 1..1000000");
        const bool incrementalOnly=argc>2 && !wcscmp(argv[2],L"--incremental-only");
        LARGE_INTEGER frequency{}; Require(QueryPerformanceFrequency(&frequency),"QPC unavailable");
        std::vector<Workload> workloads={
            {"literal-prefix",{}, {L"Project",L"project",L"PROJECT",L"Proj"}},
            {"ordinary-flexible",{}, {L"projectfiles",L"project files dr",L"zz",L"projectfZ"}},
            {"long-word",{}, {L"z",L"abbbq",L"abbb-b",L"a bbb q"}},
            {"emoji-flexible",{}, {L"filename",L"file name",L"fileother",L"zz"}}};
        for(size_t i=0;i<rows;++i) {
            const auto id=std::to_wstring(i);
            workloads[0].names.push_back(L"Project-files-draft-"+id+L".txt");
            workloads[1].names.push_back(L"Project-files-draft-"+id+L".txt");
            workloads[2].names.push_back(L"a"+std::wstring(220,L'b')+id+L".txt");
            workloads[3].names.push_back(L"file\U0001F468\u200D\U0001F469name"+std::wstring(180,L'x')+id+L".txt");
        }
        std::vector<Workload> chains={
            {"mixed-late",{}, {L"S",L"Sy",L"Sys",L"Syst",L"Syste",L"System"}},
            {"all-S-late",{}, {L"S",L"Sy",L"Sys",L"Syst",L"Syste",L"System"}},
            {"early-hit",{}, {L"S",L"Sy",L"Sys",L"Syst",L"Syste",L"System"}}};
        for(size_t i=0;i<rows;++i) {
            const auto id=std::to_wstring(i);
            chains[0].names.push_back(std::wstring(1,wchar_t(L'A'+i%26))+L"Archive"+id);
            chains[1].names.push_back(L"SArchive"+id);
            chains[2].names.push_back(L"System-file"+id);
        }
        chains[0].names.back()=chains[1].names.back()=L"S-y-s-t-e-m.txt";
        std::cout<<std::fixed<<std::setprecision(6);
        std::cout<<"kind,mode,workload,rows,sample,before_ms_per_query_full_scan,after_ms_per_query_full_scan,speedup\n";
        for(bool icu:{false,true}) {
            if(icu) {
                g_systemRelativeTime.Initialize();
                Require(g_systemRelativeTime.UnicodeReady(),"ICU classification unavailable");
            }
            CheckEquivalence();
            CheckCandidateIndex();
            const char* mode=icu ? "icu":"windows-fallback";
            if(!incrementalOnly) for(const auto& work:workloads) Benchmark(work,double(frequency.QuadPart),mode);
            for(const auto& work:chains) BenchmarkChain(work,double(frequency.QuadPart),mode);
        }
        g_systemRelativeTime.Reset();
        return 0;
    } catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<'\n'; return 1;}
}
