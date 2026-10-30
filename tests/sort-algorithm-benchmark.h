#include <random>

// Algorithms see the same real Win32-enumerated rows. Copies, fixture creation
// and validation are outside the timing; sorting and permutation are included.
static void TestSortAlgorithms(const std::vector<Entry>& enumerated, bool extended) {
    LARGE_INTEGER frequency{}; QueryPerformanceFrequency(&frequency);
    struct Choice {const char* name; PROPERTYKEY key; SORTDIRECTION direction;};
    const Choice choices[]={
        {"name-asc",PKEY_ItemNameDisplay,SORT_ASCENDING},
        {"name-desc",PKEY_ItemNameDisplay,SORT_DESCENDING},
        {"modified",PKEY_DateModified,SORT_DESCENDING},
        {"size",PKEY_Size,SORT_DESCENDING},
        {"extension",PKEY_FileExtension,SORT_ASCENDING},
        {"extension-size",PKEY_FileExtension,SORT_DESCENDING},
    };
    std::cout<<"SORT_KERNEL_HEADER,items,sort,input,algorithm,run,sort_ms,comparisons\n";
    for (const auto& choice : choices) {
        SortSpec spec=DefaultSort(); spec.explicitOrder=true; spec.key=choice.key;
        spec.kind=ClassifySort(choice.key); spec.direction=choice.direction;
        if (SameKey(choice.key,PKEY_FileExtension) && choice.direction==SORT_DESCENDING)
            spec.secondary.push_back({PKEY_Size,SORT_ASCENDING});
        const auto less=[&](const Entry& a,const Entry& b) {return EntryPrecedes(a,b,spec);};
        auto expected=enumerated; std::sort(expected.begin(),expected.end(),less);
        const auto orderHash=DirectoryOrderHash(expected), metadataHash=DirectoryMetadataHash(expected);
        for (int input=0;input<5;++input) {
        auto source=input>=3 ? expected : enumerated;
        if (input==1 || input==4) std::reverse(source.begin(),source.end());
        if (input==2) {std::mt19937 random(1729); std::shuffle(source.begin(),source.end(),random);}
        for (int run=0;run<(extended ? 3 : 1);++run) for (int algorithm=0;algorithm<4;++algorithm) {
            auto rows=source; size_t comparisons=0;
            const auto compare=[&](const Entry& a,const Entry& b) {++comparisons; return less(a,b);};
            LARGE_INTEGER start{},end{}; QueryPerformanceCounter(&start);
            if (algorithm==0) std::sort(rows.begin(),rows.end(),compare);
            else if (algorithm==1) SortEntriesByIndex<EntrySortAlgorithm::Intro>(rows,compare);
            else if (algorithm==2) SortEntriesByIndex<EntrySortAlgorithm::Merge>(rows,compare);
            else SortEntriesByIndex(rows,compare);
            QueryPerformanceCounter(&end);
            Check(rows.size()==expected.size() && DirectoryOrderHash(rows)==orderHash &&
                DirectoryMetadataHash(rows)==metadataHash,"indirect sort changed order or row metadata");
            for (size_t i=0;i<rows.size();++i)
                Check(rows[i].path==expected[i].path && rows[i].name==expected[i].name &&
                    SameEntryMetadata(rows[i],expected[i]),"indirect permutation corrupted an item");
            std::cout<<"SORT_KERNEL,"<<rows.size()<<','<<choice.name<<','
                <<(input==0 ? "enumerated" : input==1 ? "reversed" : input==2 ? "shuffled" :
                    input==3 ? "sorted" : "reverse-sorted")<<','
                <<(algorithm==0 ? "direct-introsort" : algorithm==1 ? "index-introsort" :
                    algorithm==2 ? "index-mergesort" : "index-natural-merge")<<','
                <<run+1<<','<<1000.*(end.QuadPart-start.QuadPart)/frequency.QuadPart<<','<<comparisons<<'\n';
        }
        }
    }
    // Group comparisons have many genuinely equivalent keys. Their original
    // filesystem order must survive both descending runs and subsequent merges.
    auto grouped=enumerated, reference=enumerated;
    const auto folders=[](const Entry& a,const Entry& b) {return a.folder && !b.folder;};
    std::stable_sort(reference.begin(),reference.end(),folders);
    SortEntriesByIndex(grouped,folders);
    for (size_t i=0;i<grouped.size();++i)
        Check(grouped[i].path==reference[i].path,"natural merge lost equal-key stability");
    for (const auto count : {size_t(0),size_t(1),size_t(2),size_t(3),size_t(17)}) {
        auto small=std::vector<Entry>(enumerated.begin(),enumerated.begin()+std::min(count,enumerated.size()));
        auto expected=small; const auto compare=[](const Entry& a,const Entry& b) {return CompareEntryNames(a,b)<0;};
        std::sort(expected.begin(),expected.end(),compare); SortEntriesByIndex(small,compare);
        Check(DirectoryOrderHash(small)==DirectoryOrderHash(expected),"natural merge small or odd run");
    }
    std::cout<<"PASS: sort algorithm comparisons preserve real filesystem row identities and metadata\n";
}
