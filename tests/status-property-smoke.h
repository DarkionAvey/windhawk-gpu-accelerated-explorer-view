// Exercise the system serializer and registered property schema; no provider,
// user cloud folder, input messages, clipboard or global settings are modified.
static void TestStatusProperty(const std::wstring& fixture) {
    const auto key=[](PCWSTR name) {
        PROPERTYKEY result{};
        Check(SUCCEEDED(PSGetPropertyKeyFromName(name,&result)),"registered status property");
        return result;
    };
    Check(SameKey(key(L"System.PropList.StatusIcons"),kPropListStatusIcons) &&
        SameKey(key(L"System.ItemCustomState.Values"),kItemCustomStateValues) &&
        SameKey(key(L"System.ItemCustomState.IconReferences"),kItemCustomStateIcons),
        "status property constants disagree with Windows");
    const auto stateKey=key(L"System.StorageProviderState");
    const auto customKey=key(L"System.StorageProviderCustomStates");
    const auto uiKey=key(L"System.StorageProviderUIStatus");
    const auto memory=[] {
        ComPtr<IPropertyStore> result;
        Check(SUCCEEDED(PSCreateMemoryPropertyStore(IID_PPV_ARGS(&result))),"status store");
        return result;
    };
    const auto set=[](IPropertyStore* store,const PROPERTYKEY& property,PROPVARIANT& value) {
        Check(SUCCEEDED(store->SetValue(property,value)),"set status property");
        PropVariantClear(&value);
    };
    const auto serialize=[](IPropertyStore* store,bool vector) {
        ComPtr<IPersistSerializedPropStorage> persist;
        Check(SUCCEEDED(store->QueryInterface(IID_PPV_ARGS(&persist))),"status serializer");
        SERIALIZEDPROPSTORAGE* data=nullptr; DWORD size=0;
        Check(SUCCEEDED(persist->GetPropertyStorage(&data,&size)),"serialize status");
        PROPVARIANT result{};
        if(vector) Check(SUCCEEDED(InitPropVariantFromBuffer(data,size,&result)),"status vector");
        else {
            result.vt=VT_BLOB; result.blob.cbSize=size;
            result.blob.pBlobData=static_cast<BYTE*>(CoTaskMemAlloc(size));
            Check(result.blob.pBlobData!=nullptr,"status blob allocation");
            memcpy(result.blob.pBlobData,data,size);
        }
        CoTaskMemFree(data);
        return result;
    };
    ComPtr<IPropertyDescription> description;
    ComPtr<IPropertyEnumTypeList> types;
    Check(SUCCEEDED(PSGetPropertyDescription(stateKey,IID_PPV_ARGS(&description))) &&
        SUCCEEDED(description->GetEnumTypeList(IID_PPV_ARGS(&types))),"state schema");
    UINT count=0; Check(SUCCEEDED(types->GetCount(&count)),"state schema count");
    PROPVARIANT state{}; std::wstring label,image;
    for(UINT i=0;i<count;++i) {
        ComPtr<IPropertyEnumType2> type;
        if(FAILED(types->GetAt(i,IID_PPV_ARGS(&type)))) continue;
        PWSTR reference=nullptr;
        if(SUCCEEDED(type->GetImageReference(&reference)) && reference && *reference &&
            SUCCEEDED(type->GetValue(&state))) {
            PWSTR formatted=nullptr;
            if(SUCCEEDED(PSFormatForDisplayAlloc(stateKey,state,PDFF_DEFAULT,&formatted)) && formatted)
                label=formatted;
            CoTaskMemFree(formatted);
            image=reference;
        }
        CoTaskMemFree(reference);
        if(!label.empty() && !image.empty()) break;
        PropVariantClear(&state);
    }
    Check(!label.empty() && !image.empty(),"Windows state with label and icon");
    std::vector<std::wstring> scalarIcons;
    Check(FormatShellDetail(nullptr,stateKey,&state,nullptr,&scalarIcons)==label &&
        scalarIcons==std::vector<std::wstring>{image},"scalar schema status icon");
    StatusIconCache cache;
    auto pixels=DecodeStatusIcons({image},nullptr,cache);
    Check(pixels && pixels->size()==1 && pixels->front().Ready(),"decode schema icon");
    Check(DecodeStatusIcons({image},nullptr,cache)==pixels,"worker cache reuse");
    Check(!DecodeStatusIcons({image,L""},nullptr,cache),"missing icon hid a state");
    const auto missing=(std::filesystem::path(fixture)/L"missing-status.dll").wstring();
    Check(!DecodeStatusIcons({image,missing+L",-1"},nullptr,cache),"partial decode hid a state");
    for(bool outerVector:{false,true}) for(bool innerVector:{false,true}) {
        auto store=memory(); auto custom=memory(); PROPVARIANT value{};
        InitPropVariantFromString(L"prop:System.StorageProviderState;System.StorageProviderCustomStates",&value);
        set(store.Get(),kPropListStatusIcons,value);
        Check(SUCCEEDED(store->SetValue(stateKey,state)),"set schema state");
        PCWSTR labels[]={L"Shared"}, images[]={image.c_str()};
        InitPropVariantFromStringVector(labels,1,&value); set(custom.Get(),kItemCustomStateValues,value);
        InitPropVariantFromStringVector(images,1,&value); set(custom.Get(),kItemCustomStateIcons,value);
        value=serialize(custom.Get(),innerVector); set(store.Get(),customKey,value);
        value=serialize(store.Get(),outerVector);
        std::vector<std::wstring> locations; bool resolved=false;
        const auto text=FormatShellDetail(nullptr,uiKey,&value,&resolved,&locations);
        Check(resolved && text==label+L", Shared" && locations==std::vector<std::wstring>{image,image},
            "serialized status text/order/icons");
        Check(DecodeStatusIcons(locations,nullptr,cache)!=nullptr,"decode multi-state row");
        PropVariantClear(&value);
        // Unequal vector lengths preserve every label and force text fallback.
        InitPropVariantFromStringVector(images,0,&value); set(custom.Get(),kItemCustomStateIcons,value);
        value=serialize(custom.Get(),innerVector); set(store.Get(),customKey,value);
        value=serialize(store.Get(),outerVector);
        Check(FormatShellDetail(nullptr,uiKey,&value,&resolved,&locations)==text &&
            !DecodeStatusIcons(locations,nullptr,cache),"mismatched custom-state fallback");
        PropVariantClear(&value);
    }
    PropVariantClear(&state);
    auto empty=memory(); PROPVARIANT value{};
    InitPropVariantFromString(L"prop:System.StorageProviderState",&value);
    set(empty.Get(),kPropListStatusIcons,value);
    value=serialize(empty.Get(),false);
    std::wstring text=L"stale"; std::vector<std::wstring> locations{image};
    Check(FormatStatusPropertyBlob(value,text,&locations) && text.empty() && locations.empty(),
        "empty status failed to clear previous state");
    const ULONG fullSize=value.blob.cbSize;
    value.blob.cbSize=1;
    Check(!FormatStatusPropertyBlob(value,text,&locations),"truncated store accepted");
    value.blob.cbSize=fullSize; PropVariantClear(&value);
    value.vt=VT_BLOB; value.blob.cbSize=32; value.blob.pBlobData=nullptr;
    Check(!FormatStatusPropertyBlob(value,text,&locations),"null blob accepted");
    bool resolved=true;
    Check(FormatShellDetail(nullptr,uiKey,&value,&resolved,&locations).empty() && !resolved && locations.empty(),
        "invalid binary buffer reached the display formatter");
    value={}; value.vt=VT_UI4; value.ulVal=42;
    Check(!FormatStatusPropertyBlob(value,text,&locations),"scalar treated as a store");
    ColumnCell a{}, b{}; a.text=b.text=L"same"; a.icons=pixels;
    Check(!(a==b),"icon-only change ignored by cell equality");
    for(float size:{16.f,24.f,32.f}) {
        const float expected=size*float(pixels->front().width)/float(pixels->front().height);
        Check(fabsf(StatusIconsWidth(*pixels,100.f,size)-expected)<.001f,"status DPI width");
    }
    std::cout<<"PASS: Windows status serialization/schema icons, both binary formats, custom-state order, malformed data and complete fallback\n";
}
