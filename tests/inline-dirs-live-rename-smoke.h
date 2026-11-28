// Rename inside expanded folders, against the real Shell, with the same
// outcomes as renaming the open folder's own items: one item through its own
// folder; a set (one folder or several, as in Explorer's search results)
// numbered " (1)", " (2)" in row order, each item in its own folder, keeping
// its selection and keyboard position; failures reported, nothing renamed in
// the wrong folder. Private fixture only; focus and dialogs are substituted.
namespace InlineLive {
// The rows' selection, by path relative to the root.
inline std::vector<std::wstring> SelectedRelative(Live& live) {
    std::vector<std::wstring> selected;
    for(size_t i=0;i<live.state.entries.size() && i<live.state.selectedFlags.size();++i)
        if(live.state.selectedFlags[i]) selected.push_back(live.state.entries[i].path.substr(live.root.size()+1));
    return selected;
}
inline void RenameSet(Live& live,std::initializer_list<const wchar_t*> items,const wchar_t* text) {
    live.SelectOnly(items);
    BeginRename(live.state,live.Row(*items.begin()),false,items.size()>1);
    Check(live.state.rename.editor!=nullptr,"no rename editor for the selection inside expanded folders");
    Check(SetWindowTextW(live.state.rename.editor,text),"rename text");
    FinishRename(live.state,true);
}
}

static void TestInlineDirsLiveRename(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-rename");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"alpha\\sub"); live.Open(L"gamma");
    // One item two levels deep: its own folder's Shell rename.
    RenameSet(live,{L"alpha\\sub\\deep.txt"},L"renamed.txt");
    // SetNameOf has completed when FinishRename returns.
    Check(std::filesystem::exists(live.Path(L"alpha\\sub\\renamed.txt")),
        "the item inside the expanded folder was not renamed on disk");
    Check(!std::filesystem::exists(live.Path(L"alpha\\sub\\deep.txt")) && !std::filesystem::exists(live.Path(L"renamed.txt")) &&
        !std::filesystem::exists(live.Path(L"alpha\\renamed.txt")),"the rename landed in the wrong folder");
    live.Flush({L"alpha\\sub"});
    live.Settled([&] {return live.Row(L"alpha\\sub\\renamed.txt")>=0 && live.Row(L"alpha\\sub\\deep.txt")<0;},
        "the renamed row did not follow");
    Check(s.selected==live.Row(L"alpha\\sub\\renamed.txt"),"the renamed item lost its selection");
    live.CheckConsistent("after child rename");
    // Middle-click target: the child folder's own menu.
    live.SelectOnly({L"alpha\\sub"});
    ComPtr<IContextMenu> menu;
    Check(InlineSelectionMenu(s,menu),"child folder menu");
    if(!HasVerb(MenuVerbs(menu.Get()),L"opennewtab"))
        std::cout<<"NOTE: this Windows build's folder menu has no opennewtab verb"<<std::endl;
    std::cout<<"PASS: renaming one item inside an expanded folder uses that folder and keeps the selection\n";
}

// A set inside one expanded folder, then a set spanning folders (and the open
// folder's own row), renamed together. The renamed rows keep their identity,
// selection and keyboard position at once, before the Shell's notifications
// re-list the folders (Down then moves one row, not past the renamed set).
static void TestInlineDirsLiveRenameSets(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-rename-sets");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma");
    // One folder; the typed extension equal to the items' own is not doubled.
    RenameSet(live,{L"alpha\\a.txt",L"alpha\\b.txt"},L"set.txt");
    const auto renamedNow=[&](std::initializer_list<const wchar_t*> rows) {
        for(const auto* row:rows) if(live.Row(row)<0) return false;
        return true;
    };
    Check(renamedNow({L"alpha\\set (1).txt",L"alpha\\set (2).txt"}),
        "the renamed rows did not take their new names immediately");
    Check(SelectedRelative(live)==std::vector<std::wstring>{L"alpha\\set (1).txt",L"alpha\\set (2).txt"},
        "the renamed set lost its selection");
    Check(std::filesystem::exists(live.Path(L"alpha\\set (1).txt")) && std::filesystem::exists(live.Path(L"alpha\\set (2).txt")) &&
        !std::filesystem::exists(live.Path(L"alpha\\a.txt")) && !std::filesystem::exists(live.Path(L"alpha\\b.txt")),
        "the set was not renamed on disk with Explorer's numbering");
    live.Settle("re-list after the set rename");
    Check(SelectedRelative(live)==std::vector<std::wstring>{L"alpha\\set (1).txt",L"alpha\\set (2).txt"},
        "the re-list dropped the renamed set's selection");
    Check(s.selected>=0 && s.selectedFlags[size_t(s.selected)],"the renamed set lost its focus");
    Check(KeyboardNavigationTarget(s,VK_DOWN)==s.selected+1,"Down after the set rename does not move one row");
    Check(s.restoredKeyboardRow<0 && s.vacantKeyboardRow<0 && s.postDeletionFocusPath.empty(),
        "the set rename left a deletion caret behind");
    live.CheckConsistent("after a set rename in one folder");
    // Several folders and the open folder's own row, as in search results:
    // each item renamed in its own folder, numbered in row order.
    // The mod's selection decisions are kept, to name what changed it if the
    // selection is lost (printed only then).
    std::vector<std::wstring> selectionLog;
    SelectionLogFixture::Session logSession(selectionLog);
    const bool previousAll=std::exchange(SelectionLogFixture::allSelection,true);
    const bool previousPerf=g_perf.exchange(true);
    struct RestoreLog { bool all,perf; ~RestoreLog() { SelectionLogFixture::allSelection=all; g_perf=perf; } }
        restoreLog{previousAll,previousPerf};
    RenameSet(live,{L"alpha\\set (2).txt",L"gamma\\c.txt",L"zeta.txt"},L"mix");
    const auto selectedAtCommit=SelectedRelative(live);
    Check(renamedNow({L"alpha\\mix (1).txt",L"gamma\\mix (2).txt",L"mix (3).txt"}),
        "the cross-folder rows did not take their new names immediately");
    for(const auto* path:{L"alpha\\mix (1).txt",L"gamma\\mix (2).txt",L"mix (3).txt"})
        Check(std::filesystem::exists(live.Path(path)),"an item of the cross-folder set was not renamed in its own folder");
    Check(std::filesystem::exists(live.Path(L"alpha\\set (1).txt")),"an unselected item was renamed");
    live.Settle("re-list after the cross-folder rename");
    if(SelectedRelative(live)!=std::vector<std::wstring>{L"alpha\\mix (1).txt",L"gamma\\mix (2).txt",L"mix (3).txt"}) {
        std::cout<<"RENAME selected right after the commit:"<<std::endl;
        for(const auto& p:selectedAtCommit) std::cout<<"  "<<Regression::Narrow(p)<<std::endl;
        std::cout<<"RENAME selection log ("<<selectionLog.size()<<" lines):"<<std::endl;
        for(const auto& line:selectionLog) std::cout<<"  "<<Regression::Narrow(line)<<std::endl;
        std::cout<<"RENAME selected after re-list:"<<std::endl;
        for(const auto& p:SelectedRelative(live)) std::cout<<"  "<<Regression::Narrow(p)<<std::endl;
        std::cout<<"RENAME native selection ("<<s.nativeSelectionPaths.size()<<"):"<<std::endl;
        for(const auto& p:s.nativeSelectionPaths) std::cout<<"  "<<Regression::Narrow(p)<<std::endl;
        std::cout<<"RENAME rows:"<<std::endl;
        for(const auto& e:s.entries) std::cout<<"  "<<e.treeDepth<<" "<<Regression::Narrow(e.path)<<std::endl;
    }
    Check(SelectedRelative(live)==std::vector<std::wstring>{L"alpha\\mix (1).txt",L"gamma\\mix (2).txt",L"mix (3).txt"},
        "the cross-folder set lost its selection");
    live.CheckConsistent("after a cross-folder set rename");
    std::cout<<"PASS: sets inside expanded folders, in one folder or several, rename as Explorer numbers them and keep their selection\n";
}

// A set whose item vanished before the commit: nothing is renamed, and the
// failure is posted as Windows' own rename message, as for the open folder's
// own sets (TestRenameFailureNotice).
static void TestInlineDirsLiveRenameFailure(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-rename-failure");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha");
    live.SelectOnly({L"alpha\\a.txt",L"alpha\\b.txt"});
    BeginRename(s,live.Row(L"alpha\\a.txt"),false,true);
    Check(s.rename.editor!=nullptr,"no rename editor for the set");
    Check(SetWindowTextW(s.rename.editor,L"gone"),"rename text");
    // Removed by another program, before the Shell reported it.
    Check(DeleteFileW(live.Path(L"alpha\\b.txt").c_str()),"remove a selected item");
    FinishRename(s,true);
    MSG notice{};
    Check(PeekMessageW(&notice,s.hwnd,kMsgRenameFailureNotice,kMsgRenameFailureNotice,PM_REMOVE),
        "the failed set rename was not reported");
    Check(!s.renameFailureName.empty(),"the failure report has no item");
    Check(std::filesystem::exists(live.Path(L"alpha\\a.txt")) && !std::filesystem::exists(live.Path(L"alpha\\gone (1).txt")),
        "part of a failed set was renamed");
    std::cout<<"PASS: a set inside an expanded folder that cannot be renamed is reported and left unchanged\n";
}

// The Shell removes the item being renamed (another window deletes it): the
// field closes and the row leaves, as for the open folder's own items
// (TestRenameEditedItemRemoved). Removing an unrelated item, here in another
// expanded folder, leaves the field open.
static void TestInlineDirsLiveRenameEditedItemRemoved(const std::wstring& fixture) {
    using namespace InlineLive;
    const auto root=MakeTree(fixture,L"live-rename-removed");
    Live live(root,4);
    auto& s=live.state;
    live.Open(L"alpha"); live.Open(L"gamma");
    live.SelectOnly({L"alpha\\a.txt"});
    BeginRename(s,live.Row(L"alpha\\a.txt"));
    Check(s.rename.editor!=nullptr,"no rename editor");
    const HWND editor=s.rename.editor;
    Check(DeleteFileW(live.Path(L"gamma\\c.txt").c_str()),"remove an unrelated item");
    Notify(SHCNE_DELETE,live.Path(L"gamma\\c.txt")); // Flushed: delivered before it returns.
    live.Settle("the unrelated removal's re-list");
    // As for the open folder's own items: the change is held while the field
    // is open, and shown once it closes.
    Check(s.rename.editor==editor && IsWindow(editor) && s.rename.path==live.Path(L"alpha\\a.txt"),
        "removing an unrelated item in another expanded folder closed the rename field");
    Check(s.inlineTreeRegraftDeferred,"the unrelated change was neither shown nor held for the field");
    CancelRename(s);
    live.Settled([&] {return live.Row(L"gamma\\c.txt")<0 && !s.inlineTreeRegraftDeferred;},
        "the unrelated deleted item's row stayed listed after the field closed");
    // The edited item itself removed: the field closes at once.
    live.SelectOnly({L"alpha\\a.txt"});
    BeginRename(s,live.Row(L"alpha\\a.txt"));
    Check(s.rename.editor!=nullptr,"no rename editor after the field closed");
    Check(DeleteFileW(live.Path(L"alpha\\a.txt").c_str()),"remove the edited item");
    Notify(SHCNE_DELETE,live.Path(L"alpha\\a.txt"));
    live.Settled([&] {return !s.rename.editor && live.Row(L"alpha\\a.txt")<0;},
        "the rename field stayed open for an item the Shell removed");
    live.CheckConsistent("after the edited item was removed");
    std::cout<<"PASS: removing the edited item inside an expanded folder closes the field; unrelated removals do not\n";
}
