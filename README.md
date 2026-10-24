# AliasEventDeadlockFix

SKSE plugin for Skyrim AE (1.6.x, Address Library) that removes one cause of a hard freeze:
the screen goes black, the HUD stays up, the game stops responding, CPU drops to zero.

## The freeze

Two engine job threads lock each other on the same actor, usually the player:

| thread | holds | wants | doing |
|---|---|---|---|
| A | the actor's `ExtraDataList` lock (write) | the actor's `ExtraAliasInstanceArray` lock (write) | a quest filling an alias with that actor (ids 12046 -> 12718) |
| B | the actor's `ExtraAliasInstanceArray` lock (read) | the actor's `ExtraDataList` lock (read) | `SkyrimVM::RelayEvent` delivering an engine event (a magic effect applying, a hit, ...) to the actor's alias scripts (ids 12050 -> 12720 -> 54030) |

`Main::Update` waits for the job lists to finish (id 35409, the JobListManager worker-ack
wait) and never returns. The main thread is parked in `WaitForSingleObjectEx(INFINITE)`, both
workers sleep in `BSReadWriteLock` spins, the GPU idles, the HUD is still drawn.

Seen on 1.6.1170 on 2026-10-24: a dialogue mod's quest started on the player (alias fill) in
the same frame a `Cum Breath Reduce` effect from another mod applied to the player (event relay).

This is a different cycle from the ProcessLists spinlock inversions that
[GoAhead-at/FreezeLogger skyrim-freeze-fix](https://github.com/GoAhead-at/FreezeLogger) and
[GarrixWong/skyrim-freeze-fix](https://github.com/GarrixWong/skyrim-freeze-fix) patch, although
it ends in the same main-thread wait those projects watch.

## The fix

`ExtraAliasInstanceArray::ForEach` (id 12720) is replaced. The original holds the array's
read lock while it calls the visitor for every alias; the visitor is what needs the
extra-data lock. The replacement copies the alias pointers under the lock, releases it, then
visits. Thread B no longer holds one lock while asking for the other, so the inversion cannot
form, whichever mod fills aliases.

Alias objects belong to their quest, so a snapshot pointer stays valid even if the alias
instance is removed from the actor between the copy and the visit. The only behavioural
difference is that an alias cleared in that tiny window may still receive the event once.

The plugin verifies the function's first bytes (`41 56 48 83 EC 30`) before patching and logs
what it did to `SKSE\AliasEventDeadlockFix.log`. On a mismatch, or on SE/VR, it installs nothing.

## Build

Requires xmake 3.x and Visual Studio 2022 (v143, C++23). The CommonLibSSE-NG checkout is
shared with the AudioUtil repository next to this one (`includes("../AudioUtil/lib/commonlibsse-ng")`);
turn it into a submodule before publishing this repository standalone.

```
xmake f -m release -y
xmake
```

The DLL lands in `dist\SKSE\Plugins\`. With `XSE_TES5_MODS_PATH` set and
`xmake f --copy_to_mod=y`, `dist\` is also copied into `<mods>\AliasEventDeadlockFix`.

## Verifying in game

- `SKSE\AliasEventDeadlockFix.log` must say the function was replaced.
- Engine events must still reach alias scripts: with Naked Defeat installed, equipping an item
  still logs `NAKED DEFEAT playeraliasquest: OnObjectEquipped` in the Papyrus log (that
  handler lives on a player alias and is delivered through exactly this path).
