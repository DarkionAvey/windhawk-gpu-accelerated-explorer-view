// The fixture-release helper must see this process's own handles, or every
// cleanup assertion that relies on it would pass vacuously.
static void TestPrivateFixtureRelease(const std::wstring& fixture) {
    const auto root = std::filesystem::path(fixture) / L"release probe";
    Check(std::filesystem::create_directories(root / L"nested"), "release probe directories");
    HANDLE directory = CreateFileW((root / L"nested").c_str(), FILE_LIST_DIRECTORY,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    HANDLE file = CreateFileW((root / L"held.txt").c_str(), GENERIC_WRITE, FILE_SHARE_READ,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(directory != INVALID_HANDLE_VALUE && file != INVALID_HANDLE_VALUE, "release probe handles");
    // A sibling whose name extends the root must not count as inside it.
    const auto sibling = std::filesystem::path(fixture) / L"release probe sibling";
    Check(std::filesystem::create_directory(sibling), "release probe sibling");
    HANDLE outside = CreateFileW(sibling.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    Check(outside != INVALID_HANDLE_VALUE, "release probe sibling handle");
    Check(ProcessHandlesUnder(root).size() == 2, "fixture release did not see this process's directory and file handles");
    Check(!ReleasePrivateFixture(root, 50), "fixture release accepted a directory this process still holds");
    CloseHandle(file); CloseHandle(directory);
    Check(ProcessHandlesUnder(root).empty(), "fixture release counted a closed or sibling handle");
    Check(ReleasePrivateFixture(root) && !std::filesystem::exists(root), "fixture release did not delete a released directory");
    CloseHandle(outside);
    Check(ReleasePrivateFixture(sibling), "fixture release sibling cleanup");
    std::cout << "PASS: private fixture release sees this process's own handles and ignores siblings\n";
}
