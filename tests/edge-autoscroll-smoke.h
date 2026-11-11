// Shared marquee/file-drag edge geometry and frame distance. Private windows,
// real DPI metrics and QPC; no synthetic input or desktop state.
static void TestEdgeAutoscrollZones(const std::wstring&) {
    for (bool grid : {false, true}) {
        PresentationFixture::Tab tab(L"edge zones", 120, false, grid);
        auto& state = tab.state;
        const RECT viewport = ContentViewport(state, true);
        const int band = std::min(Scale96(44, ViewDpi(state)),
            std::max(1, int((viewport.bottom - viewport.top) / 3)));
        Check(band > 2 && MaxScrollPx(state) > 0.f, "edge-zone fixture must scroll");
        const int x = (viewport.left + viewport.right) / 2;
        for (int distance : {1, band / 2, band - 1}) {
            const POINT point{x, viewport.bottom - distance};
            const auto marquee = GetMarqueeScrollEdge(state, point);
            const auto drag = ShellDragEdgeDepth(state, point);
            const float expected = static_cast<float>(band - distance) / band;
            Check(marquee.band == band && std::abs(EdgeScrollDepth(marquee) - expected) < .0001f &&
                std::abs(drag.y - expected) < .0001f,
                "marquee and file drag disagree on the vertical edge zone");
        }
        const POINT middle{x, (viewport.top + viewport.bottom) / 2};
        Check(GetMarqueeScrollEdge(state, middle).edge == 0 &&
            ShellDragEdgeDepth(state, middle).y == 0.f,
            "center of view incorrectly entered an edge zone");
        state.columnContentWidth = float(viewport.right - viewport.left + 200);
        state.horizontalScroll = 100.f;
        const POINT left{viewport.left + 1, middle.y};
        Check(ShellDragEdgeDepth(state, left).x < 0.f,
            "horizontal drag edge did not use the shared zone");
        state.rtlLayout = true;
        Check(ShellDragEdgeDepth(state, left).x > 0.f,
            "RTL horizontal edge did not reverse scroll direction");
    }
    std::cout << "PASS: marquee and file drag share fixed edge zones in list/grid and preserve RTL horizontal direction\n";
}

static void TestEdgeAutoscrollRate(const std::wstring&) {
    PresentationFixture::Tab list(L"edge rate list", 120, false, false);
    PresentationFixture::Tab grid(L"edge rate grid", 120, false, true);
    auto& a = list.state;
    auto& b = grid.state;
    Check(b.rowPx > a.rowPx && std::abs(EdgeScrollPitch(b) - a.rowPx) < .01f,
        "grid edge rate did not use the list-row pitch");
    const auto sample = [](ViewState& state, D2D1_POINT_2F depth) {
        AnimationClockFixture::Freeze clock;
        LARGE_INTEGER previous = AnimationClockFixture::instant;
        previous.QuadPart -= static_cast<LONGLONG>(state.qpcFrequency / 100.);
        const LARGE_INTEGER initial = previous;
        const auto delta = AdvanceEdgeScroll(state, previous, depth);
        const float elapsed = static_cast<float>(previous.QuadPart - initial.QuadPart) /
            static_cast<float>(state.qpcFrequency);
        const float reference = kEdgeScrollRowsPerSecond * EdgeScrollPitch(state) * elapsed;
        Check(std::abs(delta.y - reference * depth.y * std::abs(depth.y)) < .001f &&
            std::abs(delta.x - reference * depth.x * std::abs(depth.x)) < .001f,
            "shared edge frame disagreed with QPC distance and squared depth");
        return delta;
    };
    const auto listFull = sample(a, {0.f, 1.f});
    const auto gridFull = sample(b, {0.f, 1.f});
    Check(std::abs(listFull.y - gridFull.y) < .01f,
        "grid edge frame moved farther than list at full depth");
    const auto gridQuarter = sample(b, {.5f, -.5f});
    Check(gridQuarter.x > 0.f && gridQuarter.y < 0.f &&
        std::abs(gridQuarter.x + gridQuarter.y) < .01f,
        "horizontal and vertical drag axes diverged from the shared rate");
    std::cout << "PASS: list/grid marquee and file drag use the same QPC rate, depth curve and pixel pitch\n";
}
