# AliasEventDeadlockFix

SKSE plugin for Skyrim Special Edition / Anniversary Edition 1.6.x (Address Library) that
removes one cause of a hard freeze: the world goes black, the HUD stays up, input stops,
CPU use drops to zero.

## The freeze

Two engine job threads lock each other on the same actor, usually the player:

| thread | holds | wants | doing |
|---|---|---|---|
| A | the actor's `ExtraDataList` lock (write) | the actor's `ExtraAliasInstanceArray` lock (write) | a quest filling an alias with that actor (Address Library ids 12046 -> 12718) |
| B | the actor's `ExtraAliasInstanceArray` lock (read) | the actor's `ExtraDataList` lock (read) | `SkyrimVM::RelayEvent` delivering an engine event (a magic effect applying, a hit, an equip, ...) to the actor's alias scripts (ids 12050 -> 12720 -> 54030) |

`Main::Update` waits for the job lists to finish (id 35409, the JobListManager worker-ack
wait) and never returns. The main thread sits in `WaitForSingleObjectEx(INFINITE)`, both
workers sleep in `BSReadWriteLock` spins, the GPU idles, the HUD keeps drawing.

Reproduced from a live process on 1.6.1170: a quest started on the player (alias fill) in the
same frame a magic effect from another mod applied to the player (event relay). Any two mods
can trigger it; nothing is wrong with either of them.

## The fix

`ExtraAliasInstanceArray::ForEach` (id 12720) is replaced. The original holds the array's read
lock while it calls the visitor for every alias, and the visitor is what needs the extra-data
lock. The replacement copies the alias pointers under the lock, releases it, then visits.
Thread B no longer holds one lock while asking for the other, so the inversion cannot form,
whichever mod fills aliases.

Alias objects belong to their quest, so a copied pointer stays valid even if the alias
instance is removed from the actor between the copy and the visit. The only behavioural
difference is that an alias cleared in that tiny window may still receive the event once.

The plugin verifies the function's first bytes (`41 56 48 83 EC 30`) before patching and logs
what it did to `SKSE\AliasEventDeadlockFix.log`. On a mismatch, or on SE 1.5.97 / VR, it
installs nothing.

## Compatibility with other freeze fixes

Fully compatible with all of the following; each patches different code for a different lock
cycle, so run them together. Every one of them, this plugin included, checks the bytes it is
about to patch and stands down if another plugin got there first, and no two of them share a
patch site.

- [WorkerSpinLockFix](https://github.com/GoAhead-at/FreezeLogger/tree/main/skyrim-freeze-fix)
  (GoAhead-at/FreezeLogger): defers `ProcessLists::Add/RemoveFromTempChangeList` while its
  static `LockA` spinlock is held (ids 19796, 41343, 41344; call sites in 36991 and 19799) and
  wraps the job-wait and worker-ack sites (SE ids 34554 / 34557, `WaitForJobTask`) with
  watchdogs. Its "Site-A" wait is the same main-thread wait this freeze ends in, so its
  detector logs this class too; its deferral layer does not prevent it.
- [skyrim-freeze-fix](https://github.com/GarrixWong/skyrim-freeze-fix) (GarrixWong): patches
  `LockForWrite` / `UnlockForWrite` call sites in the ProcessLists cell-load path for the
  static ProcessLists locks (`stru_142F3B8E8` / `F0` on SE).
- [Combat Group Deadlock Fix](https://www.nexusmods.com/skyrimspecialedition/mods/194877)
  ([source](https://github.com/kezc7/CombatGroupDeadlockFix)): replaces the combat group's
  "is this actor a target" check (id 44706) with the same check under a time-bounded read lock
  on the combat group's `BSReadWriteLock`. Different function, different locks.
- [Interrupt Package Deadlock Fix](https://www.nexusmods.com/skyrimspecialedition/mods/194878)
  ([source](https://github.com/kezc7/InterruptPackageDeadlockFix)): redirects the two
  `Actor::EndInterruptPackage` (id 37474) call sites inside AI pass id 41331 to the main thread,
  because that pass holds the player's `ExtraDataList` lock for read and the cancel needs it
  for write. That is the same lock object as thread A's above, but a single-thread
  read-to-write cycle, and this plugin does not touch id 41331 or 37474.

## Build

Requires xmake 3.x and Visual Studio 2022 (v143, C++23). Put a checkout of
[CommonLibSSE-NG (alandtse fork)](https://github.com/alandtse/CommonLibSSE-NG), v7.1.0 or
later, at `lib/commonlibsse-ng` (clone it there, or add it as a submodule); the folder is not
part of this repository.

```
xmake f -m release -y
xmake
```

The DLL lands in `dist\SKSE\Plugins\`. CommonLibSSE-NG's plugin rule also installs it to
`%XSE_TES5_MODS_PATH%\AliasEventDeadlockFix` when that variable is set.

## Verifying in game

- `SKSE\AliasEventDeadlockFix.log` must say the function was replaced.
- Engine events must still reach alias scripts: any quest alias script that handles
  `OnObjectEquipped`, `OnHit` or `OnMagicEffectApply` on the player keeps firing, and the
  Papyrus log shows no new errors.

## License

GPLv3, like the CommonLibSSE-NG it builds on.
