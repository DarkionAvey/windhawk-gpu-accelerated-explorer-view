template<class State = ViewState>
static void TestPropertyPoolIsolation(const std::wstring& directory) {
    State probe;
    if constexpr (requires { probe.background->detailProviders; }) {
        using Background = typename decltype(probe.background)::element_type;
        using Job = typename std::remove_reference_t<decltype(probe.background->capacityJobs)>::value_type;
        using Result = typename std::remove_reference_t<decltype(probe.background->directoryResults)>::value_type;
        PropertyFixture::Session fixture;
        Check(PropertyFixture::gate != nullptr, "property provider gate");
        State details, capacities, other;
        for (State* state : {&details, &capacities, &other}) {
            state->hwnd = IconTestWindow(); state->background->hwnd = state->hwnd;
            state->inScope = state->detailsViewActive = state->loaded = true;
            state->rowPx = 34.f; state->folderPath = directory;
            state->directoryToken = 771;
            state->background->directoryGeneration = state->directoryToken;
        }
        details.columnsView = true;
        ExplorerColumn column; column.key = PKEY_Author; details.columns.push_back(column);
        for (int i = 0; i < 8; ++i) {
            Entry entry;
            entry.path = std::wstring(L"wh-property-test:") + (i ? L"fast" : L"slow") + std::to_wstring(i);
            entry.name = entry.path;
            details.entries.push_back(std::move(entry));
        }
        details.total = static_cast<int>(details.entries.size());
        QueueVisibleDetails(details);
        Check(Eventually([&] {
            ReceiveDirectoryResults(details);
            return PropertyFixture::blocked.load() == 1 &&
                std::all_of(details.entries.begin()+1, details.entries.end(), [](const auto& entry) { return entry.detailsReady; });
        }), "one slow metadata row blocked the remaining batch");
        Check(details.entries[0].detailsPending && !details.entries[0].detailsReady,
              "slow metadata fixture did not stay in flight");
        auto queueCapacities = [&](State& state, std::vector<std::wstring> paths) {
            Check(EnsureProviderWorkers(state, Background::Lane::Capacity, false), "private capacity pool");
            Job job; job.hwnd = state.hwnd; job.path = kThisPcLocation;
            job.token = state.capacityToken = state.directoryToken;
            state.background->capacityGeneration = state.capacityToken;
            job.thisPcItems = std::move(paths);
            job.capacityRemaining = std::make_shared<std::atomic<size_t>>(job.thisPcItems.size());
            {
                ExclusiveSrwLock lock(state.background->directoryLock);
                state.background->capacityJobs.push_back(std::move(job));
            }
            state.directoryPending = true;
            Check(EnsureProviderWorkers(state, Background::Lane::Capacity), "capacity dispatch");
        };
        // This tab's metadata callback is still blocked; capacity must progress.
        queueCapacities(details, {L"wh-property-test:fast-capacity"});
        Check(Eventually([&] {
            ExclusiveSrwLock lock(details.background->directoryLock);
            return std::any_of(details.background->directoryResults.begin(), details.background->directoryResults.end(),
                [](const auto& result) { return result.kind == DirectoryResult::DriveSpace && result.driveSpaceComplete; });
        }), "metadata occupied the same tab's capacity pool");
        std::vector<Entry> retired(10000);
        RetireCachedEntries(details, retired);
        WatchDirectory(details);
        Check(Eventually([&] {
            ExclusiveSrwLock lock(details.background->directoryLock);
            return details.background->retiredEntries.empty() &&
                std::any_of(details.background->directoryResults.begin(), details.background->directoryResults.end(),
                    [](const auto& result) { return result.kind == DirectoryResult::Watch; });
        }), "blocked metadata delayed reclamation or watch setup");
        capacities.thisPcGpu = true; capacities.folderPath = kThisPcLocation;
        queueCapacities(capacities, {L"wh-property-test:slow-capacity", L"wh-property-test:fast-capacity"});
        Check(Eventually([&] {
            ReceiveDirectoryResults(capacities);
            return PropertyFixture::blocked.load() == 2 && capacities.thisPcCapacities.size() == 1;
        }), "one slow volume withheld the other volume's partial result");
        Check(capacities.directoryPending && capacities.thisPcCapacities[0].root == L"wh-property-test:fast-capacity",
              "partial capacities ended the request prematurely");
        queueCapacities(other, {L"wh-property-test:fast-other-tab"});
        Check(Eventually([&] {
            ExclusiveSrwLock lock(other.background->directoryLock);
            return !other.background->directoryResults.empty();
        }), "blocked volume occupied another tab's capacity workers");
        Check(details.background->detailPool.handle != details.background->capacityPool.handle &&
              capacities.background->capacityPool.handle != other.background->capacityPool.handle,
              "provider families or tabs share a Windows pool");
        SetEvent(PropertyFixture::gate);
        Check(Eventually([&] {
            ReceiveDirectoryResults(details); ReceiveDirectoryResults(capacities);
            return details.entries[0].detailsReady && capacities.thisPcCapacities.size() == 2 && !capacities.directoryPending;
        }), "released providers did not publish final results");
        // Concurrent callbacks may publish their final marker before an earlier
        // partial. A late partial must never make the completed request pending.
        Result late; late.kind = DirectoryResult::DriveSpace;
        late.token = capacities.capacityToken; late.driveSpaceComplete = false;
        late.driveSpace.push_back(capacities.thisPcCapacities[0]);
        PublishDirectoryResult(*capacities.background, std::move(late));
        ReceiveDirectoryResults(capacities);
        Check(!capacities.directoryPending, "late partial reversed capacity completion");
        std::vector<std::weak_ptr<TabBackground>> lifetimes;
        for (State* state : {&details, &capacities, &other}) {
            lifetimes.push_back(state->background); Stop(state->background);
        }
        Check(Eventually([&] {
            return std::all_of(lifetimes.begin(), lifetimes.end(), [](const auto& weak) { return weak.expired(); });
        }), "property pools did not retire");
        for (State* state : {&details, &capacities, &other}) DestroyWindow(state->hwnd);
        CloseHandle(PropertyFixture::gate); PropertyFixture::gate = nullptr;
        std::cout << "PASS: parallel metadata rows, streamed capacities, independent watch/reclamation/tabs, out-of-order completion and private pool retirement\n";
    }
}
