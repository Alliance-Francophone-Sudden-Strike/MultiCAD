## HS2Engine

HS2Engine (`../HS2Engine`) is a hybrid Game_Dll for Hidden Stroke 2: its `Game_Dll.dll`
loads the original (`[HS2Engine] OriginalDll=`) and replaces functions of it after MultiCAD
has patched it. MultiCAD skips the host (`GameModules::IsHs2EngineHost`) and patches the
original as the HS_2 profile. After changing anything HS_2 uses (the SS_2 v2.2 game hooks,
patches and relocations, the group or zeppelin panel signatures, the redirected imports),
run `../HS2Engine/tests/check_multicad.py`: it fails when an HS2Engine replacement would
bypass one of them.

<!-- CODEGRAPH_START -->
## CodeGraph

In repositories indexed by CodeGraph (a `.codegraph/` directory exists at the repo root), reach for it BEFORE grep/find or reading files when you need to understand or locate code:

- **MCP tool** (when available): `codegraph_explore` answers most code questions in one call — the relevant symbols' verbatim source plus the call paths between them, including dynamic-dispatch hops grep can't follow. Name a file or symbol in the query to read its current line-numbered source. If it's listed but deferred, load it by name via tool search.
- **Shell** (always works): `codegraph explore "<symbol names or question>"` prints the same output.

If there is no `.codegraph/` directory, skip CodeGraph entirely — indexing is the user's decision.
<!-- CODEGRAPH_END -->
