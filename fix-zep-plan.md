# Zeppelin panel: wrong index — player used where the game uses team

## Context

QA tested the new `ZeppelinPanel=on` feature (README "Zeppelin Capture Panel", `Alt`+`z`) in
multiplayer and reported that only some players see the `1/2` counts and the capture
countdown:

- **1v1**: both sides see everything. Works.
- **2v2**: one tester (Rommel) sees the rows, his teammate sees nothing at all — no count, no
  timer, no swatches.
- QA's own hypotheses: "only the host", "only the first two to join", "players 3 to 12 and
  teams 3 and 4 are broken", "only one player per team gets the display".

The last two are close to the truth. The panel reads the game's zeppelin state with the wrong
index: it uses the **local player index (0–11)** where the game indexes by **team (0–3)**.

This plan records the evidence, the exact fix, and how to verify it without a Windows build
machine.

## How the analysis was done

No build or run happened — macOS host, no MinGW/Wine toolchain. The reasoning is static
analysis of the real game code:

- The HS_2 `Game_Dll.dll` on disk is Petite-packed (`.petite` section), so its code cannot be
  read statically. Its unpacked image is byte-identical to **Sudden Strike 2 v2.2**
  (`GameVersion::SS_2`, `.text` sha256 `0d86386c…`) — which is already how the repo treats
  them: `GroupPanelTraits<HS_2> : GroupPanelTraits<SS_2>`.
- All four `ZeppelinTraits<HS_2>` signatures match that SS_2 binary at their exact RVAs, so
  the code the reader was built against is the code disassembled below.
- Disassembly: Capstone over the `.text` section, addresses quoted as RVAs.

## Root cause

`ZeppelinReader::scan()` ([ZeppelinReader.h:83-135](src/patcher/ZeppelinReader.h#L83-L135))
reads the global at RVA `0x106F05C` and uses that value directly as the index into three
game arrays:

```cpp
const int player = *playerSlot;
if (player < 0 || player >= kPlayers)          // kPlayers == 4
    return;
...
read<uint32_t>(object + at.heldZeppelins + sizeof(uint32_t) * player);   // held[player]
if ((owner & (1u << player)) == 0) continue;                            // owner bit player
read<int>(record + at.recordProgress + sizeof(int) * player);           // progress[player]
```

`0x106F05C` is the **local player index**, range 0–11. All three arrays are indexed by
**team**, range 0–3. The two only coincide by accident.

### Evidence

**`0x106F05C` is a player index, not a team index.** At RVA `0xB57A2` the game walks all
player records with `[esp+0x20]` as the player counter and compares it to this global to
decide whether a capture message is addressed to "you". At `0xB53D2` a unit's owner byte
`[unit+0x1C]` is fed through the same conversion this global gets.

**Players are 0–11, teams are 0–3.** At `0xB535C` a unit's owner byte is rejected with
`cmp byte [ebp+0x1c], 0xC / jae` — 12 players. The player record table is walked at `0xB57F5`
with `add eax, 0xB5 / cmp eax, 0x87C / jl` — 12 records of 181 bytes (181 × 12 = 0x87C). Both
team loops that touch the zeppelin object are bounded by 4 (`0xB5450`, `0xB5821`, `0xB5202`).

**The game converts player → team before touching the arrays.** At `0xB53CE`:

```asm
mov  al, byte ptr [ebp + 0x1c]                  ; unit's player index
lea  ecx, [eax + eax*4]                         ; ×5
lea  ecx, [ecx + ecx*8]                         ; ×45
mov  dl, byte ptr [eax + ecx*4 + 0x10892fd4]    ; team = playerTeam[181 * player]
lea  eax, [esi + edx*4 + 0x340]                 ; &held[team]     <-- 0x340 indexed by TEAM
```

The same `×181 + 0x892FD4` conversion is applied to `0x106F05C` itself at `0xB5263`,
`0xB52E8`, `0xB5559` and `0xB5A41`.

**The three arrays are team arrays.** From the constructor at `0xB4730`, `object+0x340` is
exactly four dwords (`[ecx]`, `[ecx+4]`, `[ecx+8]`, `[ecx+0xc]`), a separate 12-dword array
starts at `0x350`, and two 32-dword arrays at `0x380`/`0x400`. The capture loop at `0xB54D4`
iterates `t = 0..3` and uses `t` for `owner & (1<<t)`, `held[t]` and
`record.progress[0x14C + 4*t]`, then writes `record.captor = t` at `+0x148`. `recordProgress`
has room for exactly 4 entries inside the 0x160 stride — it was never a 12-slot array.

### Why each QA observation follows

| Situation                          | Player idx | Team | Reader reads                   | Result                                                      |
| ---------------------------------- | ---------- | ---- | ------------------------------ | ----------------------------------------------------------- |
| 1v1, both sides                    | 0, 1       | 0, 1 | team 0, team 1                 | correct by coincidence                                      |
| 2v2, player index 2 or 3 on team 0 | 2, 3       | 0    | team 2 / team 3 slots (unused) | **`owner` bit never set → zero rows → panel never appears** |
| 2v2, player index 1 on team 1      | 1          | 1    | team 1                         | correct by coincidence                                      |
| 2v2, player index 1 on team 0      | 1          | 0    | team 1                         | **shows the enemy team's counts and countdown**             |
| Any player at index 4–11           | ≥ 4        | any  | —                              | `player >= kPlayers` → early return, panel never appears    |

So it is not "the host", not "the first two to join", and not "one player per team" as a rule —
it is **every player whose lobby slot index happens to equal their team index sees correct
data; everyone else sees nothing, or someone else's data.** In a 1v1 that is always everyone,
which is why the 1v1 test looked fine.

The silently-wrong case (right index, wrong team) is worse than the blank case and is not in
QA's report yet: a player could be shown the enemy's capture countdown. Worth testing for
explicitly.

## Fix

Convert player → team once, at the top of `scan()`, then index everything by team.

### 1. `src/patcher/ZeppelinTraits.h`

Add two fields to `ZeppelinAddresses`, next to `localPlayer`:

```cpp
uintptr_t localPlayer;
uintptr_t playerTeam;     // RVA of player 0's team byte
uintptr_t playerStride;   // bytes between two player records
```

HS_2 values: `playerTeam = 0x892FD4`, `playerStride = 0xB5`.

Extend `signatures[2]` (RVA `0xB53A7`) by three bytes so the team table is pinned, not just
assumed — the pattern currently stops at `8a9488d4`, one byte into the displacement:

```
...8a9488d42f????
```

`d4 2f` are the low 16 bits of `0x10892FD4`; Windows rebases modules on 64 KB boundaries, so
those two bytes survive relocation while `89 10` do not and stay masked. The `8d0c80 8d0cc9`
pair already in this pattern pins the ×181 stride.

### 2. `src/patcher/ZeppelinReader.h`

Rename `kPlayers` to `kTeams` (it always meant teams), add `kMaxPlayers = 12`, and insert the
lookup in [`scan()`](src/patcher/ZeppelinReader.h#L87):

```cpp
const int player = *playerSlot;
if (player < 0 || player >= kMaxPlayers)
    return;

const auto* const teamSlot =
    globals_->getPtr<uint8_t>(at.playerTeam) + at.playerStride * static_cast<size_t>(player);
if (!MemoryProbe::IsReadable(teamSlot, 1))
    return;

const int team = *teamSlot;
if (team >= kTeams)          // uint8_t: 0xFF for an unassigned slot
    return;
```

Then replace `player` with `team` in the three indexing sites: `heldZeppelins + 4 * team`,
`owner & (1u << team)`, `recordProgress + 4 * team`.

Resolve the table base through `globals_->getPtr` exactly like `modeFlag`/`objectPtr`/
`localPlayer` — one `GameGlobals` cache entry, and `MemoryRelocator` gaps stay handled.

### 3. `tests/zeppelin_panel_test.cpp`

Extend the existing HS_2 address block (around
[line 299](tests/zeppelin_panel_test.cpp#L299)) with the invariants the fix depends on:

```cpp
assert(hs2->playerStride == 0xB5);
assert(hs2->heldZeppelins + sizeof(uint32_t) * 4 <= hs2->records);   // 4 team slots
assert(hs2->recordProgress + sizeof(int) * 4 <= hs2->recordStride);  // already present
assert(hs2->signatures[2].pattern.find("8a9488d42f") != std::string_view::npos);
```

The existing mutation loop already flips every non-`??` byte of each pattern, so the three new
signature bytes get covered for free.

### Not doing

- No refactor of `ZeppelinReader` to make `scan()` host-testable. It pulls in `windows.h`; a
  seam for one bug is not worth it. If the team lookup ever grows a second rule, revisit.
- No new signature entry. `signatures` is a fixed `std::array<…, 4>` and extending the
  existing pattern pins the same thing for three bytes.

## Verification

**On macOS, right now** — the panel test is a pure host test, no Windows deps:

```sh
c++ -std=c++20 -Wall -Wextra -pedantic -Isrc/core -Isrc/patcher \
    tests/zeppelin_panel_test.cpp -o /tmp/zep_test && /tmp/zep_test
```

(verified: it compiles and passes on this machine today). `make -C mingw zeppelin-panel-test`
is the same thing once a toolchain is around.

**On a Windows box**: `make -C mingw test` for the full suite plus the Wine `LoadLibrary`
smoke test.

**In game (HS_2, `ZeppelinPanel=on`, map with zeppelin groups)** — the matrix above is the test
plan, and the middle two rows are the ones that were broken:

1. 2v2, the DLL on **all four** players (the earlier 2v2 only had it on two). Every player must
   see identical rows to their teammate, and rows that differ from the enemy team's.
2. Specifically put a DLL user in lobby slot 3 or 4 on the **first** team — that is the case
   that showed nothing before.
3. Specifically put a DLL user in lobby slot 2 on a team whose index is not 1 — that is the
   case that showed the _enemy's_ countdown before.
4. A 5th+ player (index ≥ 4), any team: the panel must now appear.
5. Re-check the fine-grained behaviour QA asked about, per player: a group drops off the list
   when your team completes it (`owner &= ~(1<<team)` at `0xB55A9`), the countdown freezes and
   alternates with the count when a hold breaks, and the count comes back when an enemy
   capture resets the group.

## Follow-up (out of scope)

The zeppelin code in the SS_2 build is byte-identical to HS_2's, and the traits are already
shared that way for the group panel. Enabling the panel for `GameVersion::SS_2` is a one-line
addition to `TryGetZeppelinAddresses` plus a README edit — but it should be its own change,
after this fix is confirmed in game.
