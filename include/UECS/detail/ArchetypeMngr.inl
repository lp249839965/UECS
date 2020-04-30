#pragma once

#include "../CmptTag.h"

#include <UTemplate/Typelist.h>
#include <UTemplate/Func.h>

namespace Ubpa::detail::ArchetypeMngr_ {
	template<typename ArgList, typename OrderList, typename TaggedCmptList>
	struct GenJob;
}

namespace Ubpa {
	template<typename... Cmpts>
	inline Archetype* ArchetypeMngr::GetOrCreateArchetypeOf() {
		auto id = CmptIDSet(TypeList<Cmpts...>{});
		auto target = id2a.find(id);
		if (target == id2a.end()) {
			auto archetype = new Archetype(this, TypeList<Cmpts...>{});
			id2a[id] = archetype;
			ids.insert(id);
			for (auto& [queryHash, archetypes] : queryCache) {
				const auto& query = id2query.find(queryHash)->second;
				if (id.IsContain(query.cmptIDs) && id.IsNotContain(query.notCmptIDs))
					archetypes.insert(archetype);
			}
			return archetype;
		}
		else
			return target->second;
	}

	Archetype* ArchetypeMngr::GetArchetypeOf(const CmptIDSet& archetypeID) {
		auto target = id2a.find(archetypeID);
		assert(target != id2a.end());
		return target->second;
	}

	template<typename... Cmpts>
	const std::tuple<EntityBase*, Cmpts*...> ArchetypeMngr::CreateEntity() {
		auto entity = entityPool.Request();

		Archetype* archetype = GetOrCreateArchetypeOf<Cmpts...>();
		auto [idx, cmpts] = archetype->CreateEntity<Cmpts...>();

		entity->archetype = archetype;
		entity->idx = idx;
		ai2e[{archetype,idx}] = entity;

		using CmptList = TypeList<Cmpts...>;
		return { entity, std::get<Find_v<CmptList, Cmpts>>(cmpts)... };
	}

	template<typename NoneCmptList, typename CmptList>
	const std::set<Archetype*>& ArchetypeMngr::QueryArchetypes() {
		using SortedCmptList = QuickSort_t<CmptList, TypeID_Less>;
		using SortedNoneCmptList = QuickSort_t<NoneCmptList, TypeID_Less>;
		constexpr size_t queryHash = TypeID<TypeList<SortedNoneCmptList, SortedCmptList>>;
		auto target = queryCache.find(queryHash);
		if (target != queryCache.end())
			return target->second;

		std::set<Archetype*>& rst = queryCache[queryHash];
		id2query.emplace(queryHash, Query{ TypeListToIDVec(SortedCmptList{}), TypeListToIDVec(SortedNoneCmptList{}) });
		for (auto& [id, a] : id2a) {
			if (id.IsContain(CmptList{}) && id.IsNotContain(SortedNoneCmptList{}))
				rst.insert(a);
		}

		return rst;
	}

	template<typename... Cmpts>
	const std::tuple<Cmpts*...> ArchetypeMngr::EntityAttach(EntityBase* e) {
		assert(!e->archetype->id.IsContain<Cmpts...>());

		Archetype* srcArchetype = e->archetype;
		size_t srcIdx = e->idx;

		auto& srcID = srcArchetype->ID();
		auto dstID = srcID;
		dstID.Add<Cmpts...>();

		// get dstArchetype
		Archetype* dstArchetype;
		auto target = id2a.find(dstID);
		if (target == id2a.end()) {
			dstArchetype = Archetype::Add<Cmpts...>(srcArchetype);
			assert(dstID == dstArchetype->ID());
			id2a[dstID] = dstArchetype;
			ids.insert(dstID);
			for (auto& [queryHash, archetypes] : queryCache) {
				const auto& query = id2query.find(queryHash)->second;
				if (dstID.IsContain(query.cmptIDs) && dstID.IsNotContain(query.notCmptIDs))
					archetypes.insert(dstArchetype);
			}
		}
		else
			dstArchetype = target->second;

		// move src to dst
		size_t dstIdx = dstArchetype->RequestBuffer();
		
		(new(dstArchetype->At<Cmpts>(dstIdx))Cmpts, ...);
		for (auto cmptHash : srcID) {
			auto [srcCmpt, srcSize] = srcArchetype->At(cmptHash, srcIdx);
			auto [dstCmpt, dstSize] = dstArchetype->At(cmptHash, dstIdx);
			assert(srcSize == dstSize);
			CmptLifecycleMngr::Instance().MoveConstruct(cmptHash, dstCmpt, srcCmpt);
		}

		// erase
		auto srcMovedIdx = srcArchetype->Erase(srcIdx);
		if (srcMovedIdx != static_cast<size_t>(-1)) {
			auto srcMovedEntityTarget = ai2e.find({ srcArchetype, srcMovedIdx });
			auto srcMovedEntity = srcMovedEntityTarget->second;
			ai2e.erase(srcMovedEntityTarget);
			ai2e[{srcArchetype, srcIdx}] = srcMovedEntity;
			srcMovedEntity->idx = srcIdx;
		}

		ai2e.emplace(std::make_pair(std::make_tuple(dstArchetype, dstIdx), e));

		e->archetype = dstArchetype;
		e->idx = dstIdx;

		/*if (srcArchetype->Size() == 0 && srcArchetype->CmptNum() != 0) {
			ids.erase(srcArchetype->id);
			id2a.erase(srcArchetype->id);
			delete srcArchetype;
		}*/

		return { dstArchetype->At<Cmpts>(dstIdx)... };
	}

	template<typename... Cmpts>
	void ArchetypeMngr::EntityDetach(EntityBase* e) {
		assert(e->archetype->id.IsContain<Cmpts...>());

		Archetype* srcArchetype = e->archetype;
		size_t srcIdx = e->idx;

		auto& srcID = srcArchetype->ID();
		auto dstID = srcID;
		dstID.Remove<Cmpts...>();

		// get dstArchetype
		Archetype* dstArchetype;
		auto target = id2a.find(dstID);
		if (target == id2a.end()) {
			dstArchetype = Archetype::Remove<Cmpts...>(srcArchetype);
			assert(dstID == dstArchetype->ID());
			id2a[dstID] = dstArchetype;
			ids.insert(dstID);
			for (auto& [queryHash, archetypes] : queryCache) {
				const auto& query = id2query.find(queryHash)->second;
				if (dstID.IsContain(query.cmptIDs) && dstID.IsNotContain(query.notCmptIDs))
					archetypes.insert(dstArchetype);
			}
		}
		else
			dstArchetype = target->second;

		// move src to dst
		size_t dstIdx = dstArchetype->RequestBuffer();
		for (auto cmptHash : srcID) {
			auto [srcCmpt, srcSize] = srcArchetype->At(cmptHash, srcIdx);
			if (dstID.IsContain(cmptHash)) {
				auto [dstCmpt, dstSize] = dstArchetype->At(cmptHash, dstIdx);
				assert(srcSize == dstSize);
				CmptLifecycleMngr::Instance().MoveConstruct(cmptHash, dstCmpt, srcCmpt);
			}
		}

		// erase
		auto srcMovedIdx = srcArchetype->Erase(srcIdx);
		if (srcMovedIdx != static_cast<size_t>(-1)) {
			auto srcMovedEntityTarget = ai2e.find({ srcArchetype, srcMovedIdx });
			auto srcMovedEntity = srcMovedEntityTarget->second;
			ai2e.erase(srcMovedEntityTarget);
			ai2e[{srcArchetype, srcIdx}] = srcMovedEntity;
			srcMovedEntity->idx = srcIdx;
		}

		ai2e[{dstArchetype, dstIdx}] = e;

		e->archetype = dstArchetype;
		e->idx = dstIdx;

		/*if (srcArchetype->Size() == 0) {
			ids.erase(srcArchetype->id);
			id2a.erase(srcArchetype->id);
			delete srcArchetype;
		}*/
	}

	template<typename Sys>
	void ArchetypeMngr::GenJob(Job* job, Sys&& sys) {
		using ArgList = FuncTraits_ArgList<std::decay_t<Sys>>;
		using TaggedCmptList = CmptTag::GetTimePointList_t<ArgList>;
		using OtherArgList = CmptTag::RemoveTimePoint_t<ArgList>;
		return detail::ArchetypeMngr_::GenJob<ArgList, TaggedCmptList, OtherArgList>::run(job, this, std::forward<Sys>(sys));
	}

	template<typename... Cmpts>
	std::vector<size_t> ArchetypeMngr::TypeListToIDVec(TypeList<Cmpts...>) {
		return { TypeID<Cmpts>... };
	}
}

namespace Ubpa::detail::ArchetypeMngr_ {
	template<typename... Args, typename... TagedCmpts, typename... OtherArgs>
	struct GenJob<TypeList<Args...>, TypeList<TagedCmpts...>, TypeList<OtherArgs...>> {
		static_assert(sizeof...(TagedCmpts) > 0);
		using CmptList = TypeList<CmptTag::RemoveTag_t<TagedCmpts>...>;
		using NoneCmptList = CmptTag::GetAllNoneList_t<TypeList<Args...>>;
		static_assert(IsSet_v<CmptList>, "Componnents must be different");
		template<typename Sys>
		static void run(Job* job, ArchetypeMngr* mngr, Sys&& s) {
			assert(job->empty());
			for (Archetype* archetype : mngr->QueryArchetypes<NoneCmptList, CmptList>()) {
				auto cmptsTupleVec = archetype->Locate<CmptTag::RemoveTag_t<TagedCmpts>...>();
				size_t num = archetype->Size();
				size_t chunkNum = archetype->ChunkNum();
				size_t chunkCapacity = archetype->ChunkCapacity();

				for (size_t i = 0; i < chunkNum; i++) {
					size_t J = std::min(chunkCapacity, num - (i * chunkCapacity));
					job->emplace([s, cmptsTuple=std::move(cmptsTupleVec[i]), J]() {
						for (size_t j = 0; j < J; j++) {
							s(std::get<Args>(
								std::make_tuple(
									static_cast<TagedCmpts>((std::get<Find_v<CmptList, CmptTag::RemoveTag_t<TagedCmpts>>>(cmptsTuple) + j))...,
									OtherArgs{}...
								))...);
						}
					});
				}
			}
		}
	};
}
