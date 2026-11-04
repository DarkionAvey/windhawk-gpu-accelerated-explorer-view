// Preserved pre-optimization matcher for differential checks and CPU benchmarks.
namespace TypeSearchBaseline {
size_t TypeSearchCharacterLength(std::wstring_view text,size_t at) {
    return IS_HIGH_SURROGATE(text[at]) && at+1<text.size() && IS_LOW_SURROGATE(text[at+1]) ? 2:1;
}

UINT32 TypeSearchPrefixLength(const std::wstring& name,const std::wstring& prefix) {
    if(prefix.empty() || IS_HIGH_SURROGATE(prefix.back())) return 0; // Wait for the second WM_CHAR of a pair.
    if(prefix.size()<=name.size() && CompareStringOrdinal(name.data(),int(prefix.size()),prefix.data(),int(prefix.size()),TRUE)==CSTR_EQUAL)
        return UINT32(prefix.size());
    const auto separator=[](std::wstring_view text,size_t at) {
        const auto length=TypeSearchCharacterLength(text,at);
        if(g_systemRelativeTime.UnicodeReady()) {
            const int32_t codePoint=length==2 ?
                0x10000+((int32_t(text[at])-0xD800)<<10)+(int32_t(text[at+1])-0xDC00) : int32_t(text[at]);
            return g_systemRelativeTime.Separator(codePoint);
        }
        WORD basic[2]{},extended[2]{};
        GetStringTypeW(CT_CTYPE1,text.data()+at,int(length),basic);
        GetStringTypeW(CT_CTYPE3,text.data()+at,int(length),extended);
        return (basic[0]&(C1_SPACE|C1_PUNCT))!=0 || (extended[0]&C3_SYMBOL)!=0;
    };
    size_t n=0,p=0;
    bool matched=false;
    // Compare letter/number runs without constructing folded copies for
    // every row. Return the original filename extent for visible highlights.
    while(p<prefix.size()) {
        if(separator(prefix,p)) {p+=TypeSearchCharacterLength(prefix,p); continue;}
        while(n<name.size() && separator(name,n)) n+=TypeSearchCharacterLength(name,n);
        size_t ne=n,pe=p;
        while(ne<name.size() && !separator(name,ne)) ne+=TypeSearchCharacterLength(name,ne);
        while(pe<prefix.size() && !separator(prefix,pe)) pe+=TypeSearchCharacterLength(prefix,pe);
        const auto length=std::min(ne-n,pe-p);
        if(!length || CompareStringOrdinal(name.data()+n,int(length),prefix.data()+p,int(length),TRUE)!=CSTR_EQUAL) return 0;
        n+=length; p+=length;
        matched=true;
    }
    return matched ? UINT32(n) : 0;
}
}
