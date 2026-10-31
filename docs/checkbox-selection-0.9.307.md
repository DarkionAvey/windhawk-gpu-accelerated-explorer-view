# Explorer selection checkboxes

Read `SSF_AUTOCHECKSELECT` with the existing owner-thread click policy and
refresh it on Shell settings broadcasts. The renderer receives this policy
and the last actual pointing-device mode in its immutable presentation model.
List geometry reserves a DPI-scaled control band independently of visibility.
The same geometry feeds painting, rename fields, sizing and mirrored hit tests.
Visibility follows hover, touch and actual selection immediately. Checked and
unchecked boxes share their geometry and normal native-foreground border.

The tab's presentation worker assesses the immutable COW selection snapshot
once when selection changes, and compares the current item count. It publishes
the checked/unchecked/hidden result with the snapshot's identity and folder
generation. The UI accepts matching results without scanning items. Header
actions awaiting a result complete asynchronously; changed selection, membership
or navigation cancels an obsolete action. Empty folders hide the header control.
Item checkmarks read the same Shell-synchronized flags used by selection highlights.
This includes native-view imports, Ctrl toggles and provisional/committed marquees.
Touch grid hits use a 40 epx target clipped to the actual cell and its leading
half; the trailing half retains single-click/double-tap activation. Mouse hits
retain the visual checkbox bounds. Visual geometry does not change input modes.

A private Windows 11 ExplorerBrowser probe rejected standard
`OLECMDID_SELECTALL` (`OLECMDERR_E_NOTSUPPORTED`), null Shell-item selection and
folder index -1. Its queried `IShellFolderView::Select(SFVS_SELECT_ALLITEMS)`
succeeded and native `ItemCount(SVGIO_SELECTION)` reported all fixture items.
Use that native bulk operation, with a queried OLE command target as an alternate
for providers exposing that interface instead. Import native selection through
the existing generation-checked path. Never infer success from the GPU flags.
See Microsoft's [Select API](https://learn.microsoft.com/en-us/windows/win32/api/shlobj_core/nf-shlobj_core-ishellfolderview-select).

`--checkboxes` uses a private ExplorerBrowser, Windows' calibrated touch
recognizer and GPU readback. These offscreen component checks do not establish
live Explorer device behavior or displayed frame rates.
