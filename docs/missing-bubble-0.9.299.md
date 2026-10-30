# Missing Back/Forward bubble investigation

The supplied `back buble not drawing log.txt` contains successful history
gestures and foreground commits. The user also reproduced the missing bubble in
populated folders; item count is not a supported explanation.

## What the capture establishes

Order asynchronous pipeline output by QPC, rather than log-output timestamps.
The retained 4.64-second QPC interval has 11,317 records and 1,116 explicitly
dropped records. Two complete gesture drawing sequences are retained. Both
continue submitting foreground changes during contact and install an 80 ms
compositor fade at release. A third release appears in immediate input logging,
outside those complete retained pipeline sequences.

`swipe.foreground` reports the effect group's logical opacity and successful
commit serial. It does **not** report raster opacity, geometry, occlusion, or
actual displayed pixels. Consequently these records do not establish that the
bubble was visible, and the missing visuals have not been reproduced locally.
There is no established root cause or visibility fix in this release.

## Checks performed

- Normal content and foreground siblings use the documented
  [AddVisual ordering](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-addvisual).
  All retained surface records have the normal content root, rather than an
  attached row/column transition scene.
- A standalone native GPU probe used the existing surface publication sequence
  and compared it with drawing through a target. Both yielded opaque red pixels
  in the actual DXGI update texture at DirectComposition's returned atlas
  offsets. The D3D debug layer reported no errors. This checks that publication
  method on this device; it does not prove Explorer's composed output.
- Existing touch pixel tests flatten foreground bitmaps themselves. Their
  results cannot validate the actual compositor tree or desktop scanout.

## Next capture

Turn on the mod's **Enable logging** setting and capture a gesture whose bubble is missing.
The existing bounded per-tab logging worker now also records:

- `swipe.host`: actual overlay screen rectangle, immediately preceding sibling
  HWND and its class, at genuine contact start.
- `swipe.geometry`, `swipe.pose`, `swipe.target`: shared drawing bounds, raster
  alpha/scale, contact Y, header, viewport/DPI/direction and root/visual IDs.
  Layer 0 is engaged; layer 1 is outgoing. Geometry and pose use the foreground
  revision as `a`. Samples occur once per revision, including skipped geometry.
- `composition.content`, `composition.drop`, `swipe.clear`: root changes and
  foreground retirement, to separate a draw problem from a lifecycle reset.
- `swipe.failure.*`, `surface.failure.*`: failing operation and HRESULT in `b`.
  Surface failures also include allocation dimensions and atlas offsets.

These diagnostics perform no GPU readback, compositor waits, Shell/provider
queries or additional timers. Disabling **Enable logging** disables the records
and gesture placement queries. No navigation, animation or
folder-content behavior has been changed.
