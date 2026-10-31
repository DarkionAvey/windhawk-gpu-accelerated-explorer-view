# Header sorting validation: 0.9.305

The internal `g_animateItemInteractions` switch defaults to false. Header
sorts tag their intended descriptor before the owner-STA Shell setter, retain
that intent across replacement refreshes and consume it at snapshot commit.
Native/menu commands and navigation clear the intent. The shared commit skips
row/group movement for that interaction while preserving metadata and selection
reconciliation. The switch is not part of the user settings schema yet.

Private real ExplorerBrowser tests cover header reversals, grouped headers,
native order conformance, replacement worker generations, enabling the switch
and native commands superseding a pending header. The real native-menu suite
also asserts that menu reorders animate. These are offscreen/private-desktop
component and Shell integration checks, not live Explorer presentation timings.

Two full gate attempts stopped at other integration checks:

- A This PC capacity/metadata assertion failed once. The unchanged retained
  binary passed six focused `--navigation` reruns. Individual values were not
  captured in the first failure, so no cause is established. Added diagnostic
  output for the capacity value, pending requests, capture state and epochs.
- The popup comparison found an additional disabled "Edit with Notepad++"
  command in one of two real native popup samples. The unchanged binary passed
  the focused `--native-context-menu` rerun. No popup comparison was relaxed.

The header and native-command animation checks passed in both attempts.
Failed gates left the published 0.9.304 source and archive retention untouched.

The third full `build.bat -SourceFile 0.9.305.cpp` gate passed, including both
checks above, repository regressions, the full C++ suite and DLL unload. It
finalized 0.9.305 through the normal publication and retention workflow.
