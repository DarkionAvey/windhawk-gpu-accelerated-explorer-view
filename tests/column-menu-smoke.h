// Exercise the real Explorer column provider and the mod's column-menu
// transaction. Only the modal menu choice is substituted; no input message
// or Shell column response is manufactured.
static void TestExplorerColumnMenu(const std::wstring& fixture) {
    const auto root=std::filesystem::path(fixture)/L"column-menu";
    std::filesystem::create_directory(root);
    HANDLE file=CreateFileW((root/L"item.txt").c_str(),GENERIC_WRITE,FILE_SHARE_READ,
        nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"column menu private file"); CloseHandle(file);
    ProviderSelectionFixture::Browser host;
    host.Navigate(root.wstring(),1);
    auto& state=host.state;
    state.columnsView=true;
    Check(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(g_writeFactory.ReleaseAndGetAddressOf()))) &&
        EnsureTarget(state),"column menu graphics target");
    Check(state.columnManager && RequestColumnCatalog(state),"column menu Shell catalog request");
    Check(Eventually([&] {host.Pump(); ReceiveDirectoryResults(state); return state.columnCatalogReady;}),
        "column menu catalog did not arrive");
    const auto nativeColumns=[&] {
        UINT count=0;
        Check(SUCCEEDED(state.columnManager->GetColumnCount(CM_ENUM_VISIBLE,&count)) && count,
            "column menu native visible count");
        std::vector<PROPERTYKEY> keys(count);
        Check(SUCCEEDED(state.columnManager->GetColumns(CM_ENUM_VISIBLE,keys.data(),count)),
            "column menu native visible keys");
        return keys;
    };
    const auto before=nativeColumns();
    const auto candidate=std::find_if(state.columnMenuChoices.begin(),state.columnMenuChoices.end(),
        [&](const auto& choice) {
            return !choice.second.empty() && ClassifySort(choice.first)!=SortKind::Name &&
                std::none_of(before.begin(),before.end(),[&](const PROPERTYKEY& key) {
                    return SameKey(key,choice.first);
                });
        });
    Check(candidate!=state.columnMenuChoices.end(),"private Shell folder has no optional column");
    const PROPERTYKEY selected=candidate->first;
    const std::wstring title=candidate->second;
    const UINT expectedCommand=static_cast<UINT>(std::distance(state.columnMenuChoices.begin(),candidate)+1);
    const auto contains=[&](const std::vector<PROPERTYKEY>& keys) {
        return std::any_of(keys.begin(),keys.end(),[&](const PROPERTYKEY& key) {
            return SameKey(key,selected);
        });
    };
    bool expectChecked=false;
    const auto choose=[&](HMENU menu) -> UINT {
        const auto search=[&](auto&& self,HMENU page) -> UINT {
            const int count=GetMenuItemCount(page);
            for(int i=0;i<count;++i) {
                wchar_t label[512]{};
                MENUITEMINFOW item{sizeof(item)};
                item.fMask=MIIM_ID|MIIM_STRING|MIIM_STATE|MIIM_SUBMENU;
                item.dwTypeData=label; item.cch=ARRAYSIZE(label);
                Check(GetMenuItemInfoW(page,i,TRUE,&item),"column menu choice inspection");
                if(item.hSubMenu) {
                    if(const UINT nested=self(self,item.hSubMenu)) return nested;
                } else if(item.wID==expectedCommand) {
                    Check(title==label,"column menu command did not display the Shell catalog label");
                    Check(!(item.fState&MFS_DISABLED) &&
                        bool(item.fState&MFS_CHECKED)==expectChecked && item.wID,
                        "Shell column menu check state differed from native columns");
                    return item.wID;
                }
            }
            return 0;
        };
        const UINT command=search(search,menu);
        Check(command,"chosen Shell column was absent from the menu");
        return command;
    };
    const auto invoke=[&] {
        MenuFixture::Session menu(state.hwnd);
        MenuFixture::choose=choose;
        ExplorerColumnMenu(state,{300,400});
        Check(MenuFixture::calls==1,"column menu did not present its choices");
    };
    invoke();
    const auto after=nativeColumns();
    Check(after.size()==before.size()+1 && contains(after) &&
        std::equal(before.begin(),before.end(),after.begin(),SameKey),
        "column menu choice did not reach Explorer's visible columns");
    Check(state.columns.size()==after.size() &&
        std::equal(after.begin(),after.end(),state.columns.begin(),[](const PROPERTYKEY& key,const ExplorerColumn& column) {
            return SameKey(key,column.key);
        }),"column menu choice did not update the GPU header immediately");
    expectChecked=true;
    invoke();
    const auto restored=nativeColumns();
    Check(restored.size()==before.size() && !contains(restored) &&
        std::equal(before.begin(),before.end(),restored.begin(),SameKey) &&
        state.columns.size()==restored.size() &&
        std::equal(restored.begin(),restored.end(),state.columns.begin(),[](const PROPERTYKEY& key,const ExplorerColumn& column) {
            return SameKey(key,column.key);
        }),"column menu removal did not restore native and GPU columns");
    std::cout<<"PASS: real Shell catalog column choice adds and removes native and GPU headers together\n";
}
