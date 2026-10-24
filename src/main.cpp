// AliasEventDeadlockFix
//
// Fixes a hard freeze (black screen, HUD still drawn, zero CPU) caused by a lock-order
// inversion between two engine job threads working on the same actor:
//
//   thread A  TESObjectREFR alias instance add (id 12046 -> 12718):
//             holds  ExtraDataList::_lock           for WRITE
//             wants  ExtraAliasInstanceArray::lock  for WRITE
//   thread B  SkyrimVM::RelayEvent -> ExtraDataList::ForEachAliasInstance (id 12050)
//             -> ExtraAliasInstanceArray::ForEach (id 12720):
//             holds  ExtraAliasInstanceArray::lock  for READ while it calls the VM relay
//             visitor per alias, and the visitor (id 54030 -> handle policy) needs
//             ExtraDataList::_lock                  for READ
//
// Main::Update then waits forever in the JobListManager worker-ack wait (id 35409) because
// the job lists never drain. Seen live on 1.6.1170 (2026-10-24): a quest filling its Player
// alias while a magic effect applied to the player.
//
// The fix replaces id 12720. The original walks the array under its read lock and calls the
// visitor for each entry inside the lock. The replacement snapshots the alias pointers under
// the lock, releases it, then visits. The relay path therefore never holds the alias array
// lock while it needs the extra-data lock, so the cycle cannot form, whoever fills aliases.
// BGSBaseAlias objects are owned by their quest (form data), so a snapshot pointer stays valid
// even if the instance is removed from the actor between the snapshot and the visit; the
// worst case is one event delivered to an alias that was just cleared, instead of a freeze.

using namespace SKSE;
using namespace SKSE::log;

namespace
{
	// The visitor interface id 12720 drives: slot 0 is the virtual destructor, slot 1 is
	// Visit(alias), called as `mov rcx,visitor; mov rdx,alias; call [vtbl+8]`, result
	// compared to 1 (BSContainer::ForEachResult::kContinue).
	struct AliasVisitor
	{
		virtual ~AliasVisitor() = default;
		virtual std::uint32_t Visit(const RE::BGSBaseAlias* a_alias) = 0;
	};

	// ExtraAliasInstanceArray layout (CommonLib static_asserts it): aliases BSTArray at
	// +0x10 (data +0x10, size +0x20), BSReadWriteLock at +0x28. Both confirmed on the live
	// 1.6.1170 image and in the 1.7.104 disassembly.
	void ForEachAliasReplacement(RE::ExtraAliasInstanceArray* a_self, AliasVisitor* a_visitor)
	{
		if (!a_self || !a_visitor) {
			return;
		}

		std::vector<const RE::BGSBaseAlias*> snapshot;
		{
			RE::BSReadLockGuard guard(a_self->lock);
			snapshot.reserve(a_self->aliases.size());
			for (const auto* entry : a_self->aliases) {
				if (entry && entry->alias) {
					snapshot.push_back(entry->alias);
				}
			}
		}
		// lock released: the visitor may take the actor's extra-data lock freely now
		for (const auto* alias : snapshot) {
			if (a_visitor->Visit(alias) != static_cast<std::uint32_t>(RE::BSContainer::ForEachResult::kContinue)) {
				break;
			}
		}
	}

	bool InstallHook()
	{
		if (!REL::Module::IsAE()) {
			log::warn("not an AE runtime ({}); the id below is AE-only, nothing installed", REL::Module::get().version().string());
			return false;
		}

		// ExtraAliasInstanceArray::ForEach (AE Address Library id 12720). Entry on 1.6.1170
		// and 1.7.104: push r14 / sub rsp,0x30 = 41 56 48 83 EC 30. Refuse to patch anything
		// else: an unexpected prologue means a different build or another hook got there first.
		REL::Relocation<std::uintptr_t> target{ REL::ID(12720) };
		static constexpr std::array<std::uint8_t, 6> kExpected{ 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30 };
		std::array<std::uint8_t, 6> actual{};
		std::memcpy(actual.data(), reinterpret_cast<const void*>(target.address()), actual.size());
		if (actual != kExpected) {
			log::error("id 12720 at {:#x} does not start with the expected prologue ({:02X} {:02X} {:02X} {:02X} {:02X} {:02X}); nothing installed",
				target.address(), actual[0], actual[1], actual[2], actual[3], actual[4], actual[5]);
			return false;
		}

		AllocTrampoline(64);
		auto& trampoline = GetTrampoline();
		trampoline.write_branch<6>(target.address(), &ForEachAliasReplacement);
		log::info("ExtraAliasInstanceArray::ForEach (id 12720) at SkyrimSE.exe+{:X} replaced: aliases are now visited outside the array lock",
			target.address() - REL::Module::get().base());
		return true;
	}
}

SKSEPluginLoad(const LoadInterface* a_skse)
{
	// Init() also sets up <SKSE logs>\AliasEventDeadlockFix.log and prints the plugin
	// name and version (CommonLibSSE-NG v7 does that unless told not to).
	Init(a_skse);
	log::info("runtime {}", REL::Module::get().version().string());

	InstallHook();
	return true;
}
