/*
 * AntiAirTask.cpp
 *
 *  Created on: Jan 6, 2016
 *      Author: rlcevg
 */

#include "task/fighter/AntiAirTask.h"
#include "task/TaskManager.h"
#include "map/ThreatMap.h"
#include "module/MilitaryManager.h"
#include "setup/SetupManager.h"
#include "terrain/TerrainManager.h"
#include "terrain/path/PathFinder.h"
#include "terrain/path/QueryPathSingle.h"
#include "terrain/path/QueryPathMulti.h"
#include "unit/action/MoveAction.h"
#include "unit/enemy/EnemyUnit.h"
#include "unit/CircuitUnit.h"
#include "CircuitAI.h"
#include "util/Utils.h"

#include "spring/SpringMap.h"

#include "AISCommands.h"

namespace circuit {

using namespace springai;

CAntiAirTask::CAntiAirTask(ITaskManager* mgr, float powerMod)
		: ISquadTask(mgr, FightType::AA, powerMod)
{
	CCircuitAI* circuit = manager->GetCircuit();
	float x = rand() % circuit->GetTerrainManager()->GetTerrainWidth();
	float z = rand() % circuit->GetTerrainManager()->GetTerrainHeight();
	position = AIFloat3(x, circuit->GetMap()->GetElevationAt(x, z), z);
}

CAntiAirTask::~CAntiAirTask()
{
}

bool CAntiAirTask::CanAssignTo(CCircuitUnit* unit) const
{
	if (!unit->GetCircuitDef()->IsRoleAA() ||
		(unit->GetCircuitDef() != leader->GetCircuitDef()))
	{
		return false;
	}
	const int frame = manager->GetCircuit()->GetLastFrame();
	if (leader->GetPos(frame).SqDistance2D(unit->GetPos(frame)) > SQUARE(1000.f)) {
		return false;
	}
	return true;
}

void CAntiAirTask::AssignTo(CCircuitUnit* unit)
{
	ISquadTask::AssignTo(unit);

	int squareSize = manager->GetCircuit()->GetPathfinder()->GetSquareSize();
	CMoveAction* travelAction = new CMoveAction(unit, squareSize);
	unit->PushTravelAct(travelAction);
	travelAction->StateWait();
}

void CAntiAirTask::RemoveAssignee(CCircuitUnit* unit)
{
	ISquadTask::RemoveAssignee(unit);
	if (leader == nullptr) {
		manager->AbortTask(this);
	}
}

void CAntiAirTask::Start(CCircuitUnit* unit)
{
	if ((State::REGROUP == state) || (State::ENGAGE == state)) {
		return;
	}
	if (!pPath->posPath.empty()) {
		unit->GetTravelAct()->SetPath(pPath);
		unit->GetTravelAct()->StateActivate();
	}
}

void CAntiAirTask::Update()
{
	++updCount;

	/*
	 * Check safety
	 */
	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();

	if (State::DISENGAGE == state) {
		if (updCount % 32 == 1) {
			const float maxDist = std::max<float>(lowestRange, circuit->GetPathfinder()->GetSquareSize());
			if (position.SqDistance2D(leader->GetPos(frame)) < SQUARE(maxDist)) {
				state = State::ROAM;
			} else {
				ProceedDisengage();
				return;
			}
		} else {
			return;
		}
	}

	/*
	 * Merge tasks if possible
	 */
	if (updCount % 32 == 1) {
		ISquadTask* task = GetMergeTask();
		if (task != nullptr) {
			task->Merge(this);
			units.clear();
			manager->AbortTask(this);
			return;
		}
	}

	/*
	 * Regroup if required
	 */
	bool wasRegroup = (State::REGROUP == state);
	bool mustRegroup = IsMustRegroup();
	if (State::REGROUP == state) {
		if (mustRegroup) {
			CCircuitAI* circuit = manager->GetCircuit();
			int frame = circuit->GetLastFrame() + FRAMES_PER_SEC * 60;
			for (CCircuitUnit* unit : units) {
				TRY_UNIT(circuit, unit,
					unit->GetUnit()->Fight(groupPos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, frame);
				)
				unit->GetTravelAct()->StateWait();
			}
		}
		return;
	}

	bool isExecute = (updCount % 4 == 2);
	if (!isExecute) {
		for (CCircuitUnit* unit : units) {
			isExecute |= unit->IsForceExecute(frame);
		}
		if (!isExecute) {
			if (wasRegroup && !pPath->posPath.empty()) {
				ActivePath();
			}
			return;
		}
	}

	/*
	 * Update target
	 */
	FindTarget();

	const AIFloat3& startPos = leader->GetPos(frame);
	state = State::ROAM;
	if (target != nullptr) {
		const float sqRange = SQUARE(lowestRange);
		if (position.SqDistance2D(startPos) < sqRange) {
			state = State::ENGAGE;
			Attack(frame);
			return;
		}
	}

	const auto it = pathQueries.find(leader);
	std::shared_ptr<IPathQuery> query = (it == pathQueries.end()) ? nullptr : it->second;
	if ((query != nullptr) && (query->GetState() != IPathQuery::State::READY)) {  // not ready
		return;
	}

	if (target == nullptr) {
		FallbackSafePos();
		return;
	}

	CPathFinder* pathfinder = circuit->GetPathfinder();
	query = pathfinder->CreatePathSingleQuery(
			leader, circuit->GetThreatMap(), frame,
			startPos, position, pathfinder->GetSquareSize());
	pathQueries[leader] = query;

	const CRefHolder thisHolder(this);
	pathfinder->RunQuery(query, [this, thisHolder](std::shared_ptr<IPathQuery> query) {
		if (this->IsQueryAlive(query)) {
			this->ApplyTargetPath(std::static_pointer_cast<CQueryPathSingle>(query));
		}
	});
}

void CAntiAirTask::OnUnitIdle(CCircuitUnit* unit)
{
	ISquadTask::OnUnitIdle(unit);
	if ((leader == nullptr) || (State::DISENGAGE == state)) {
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	const float maxDist = std::max<float>(lowestRange, circuit->GetPathfinder()->GetSquareSize());
	if (position.SqDistance2D(leader->GetPos(circuit->GetLastFrame())) < SQUARE(maxDist)) {
		CTerrainManager* terrainManager = circuit->GetTerrainManager();
		float x = rand() % terrainManager->GetTerrainWidth();
		float z = rand() % terrainManager->GetTerrainHeight();
		position = AIFloat3(x, circuit->GetMap()->GetElevationAt(x, z), z);
		position = terrainManager->GetMovePosition(leader->GetArea(), position);
	}

	if (units.find(unit) != units.end()) {
		Start(unit);  // NOTE: Not sure if it has effect
	}
}

void CAntiAirTask::OnUnitDamaged(CCircuitUnit* unit, CEnemyInfo* attacker)
{
	ISquadTask::OnUnitDamaged(unit, attacker);

	if ((leader == nullptr) || (State::DISENGAGE == state) ||
		((attacker != nullptr) && (attacker->GetCircuitDef() != nullptr) && attacker->GetCircuitDef()->IsAbleToFly()))
	{
		return;
	}
	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();
	static F3Vec ourPositions;  // NOTE: micro-opt
	AIFloat3 startPos = leader->GetPos(frame);
	circuit->GetMilitaryManager()->FillSafePos(leader, ourPositions);

	CPathFinder* pathfinder = circuit->GetPathfinder();
	pathfinder->SetMapData(leader, circuit->GetThreatMap(), circuit->GetLastFrame());
	pathfinder->FindBestPath(*pPath, startPos, DEFAULT_SLACK * 4, ourPositions);
	ourPositions.clear();

	if (!pPath->posPath.empty()) {
		position = pPath->posPath.back();
		ActivePath();
		state = State::DISENGAGE;
	} else {
		position = circuit->GetSetupManager()->GetBasePos();
		for (CCircuitUnit* unit : units) {
			TRY_UNIT(circuit, unit,
				unit->GetUnit()->Fight(position, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, frame + FRAMES_PER_SEC * 60);
			)
			unit->GetTravelAct()->StateWait();
		}
	}
}

void CAntiAirTask::FindTarget()
{
	CCircuitAI* circuit = manager->GetCircuit();
	CTerrainManager* terrainManager = circuit->GetTerrainManager();
	CThreatMap* threatMap = circuit->GetThreatMap();
	const AIFloat3& pos = leader->GetPos(circuit->GetLastFrame());
	STerrainMapArea* area = leader->GetArea();
	CCircuitDef* cdef = leader->GetCircuitDef();
	const int canTargetCat = cdef->GetTargetCategory();
	const int noChaseCat = cdef->GetNoChaseCategory();
	const float maxPower = attackPower * powerMod;

	CEnemyInfo* bestTarget = nullptr;
	float minSqDist = std::numeric_limits<float>::max();

	threatMap->SetThreatType(leader);
	const CCircuitAI::EnemyInfos& enemies = circuit->GetEnemyInfos();
	for (auto& kv : enemies) {
		CEnemyInfo* enemy = kv.second;
		if (enemy->IsHidden() ||
			(maxPower <= threatMap->GetThreatAt(enemy->GetPos()) - enemy->GetThreat()) ||
			!terrainManager->CanMoveToPos(area, enemy->GetPos()))
		{
			continue;
		}

		CCircuitDef* edef = enemy->GetCircuitDef();
		if (edef != nullptr) {
			if (((edef->GetCategory() & canTargetCat) == 0) || ((edef->GetCategory() & noChaseCat) != 0)) {
				continue;
			}
		}

		const float sqDist = pos.SqDistance2D(enemy->GetPos());
		if (minSqDist > sqDist) {
			minSqDist = sqDist;
			bestTarget = enemy;
		}
	}

	SetTarget(bestTarget);
	if (bestTarget != nullptr) {
		position = target->GetPos();
	}
	// Return: target, startPos=leader->pos, endPos=position
}

void CAntiAirTask::ProceedDisengage()
{
	const auto it = pathQueries.find(leader);
	std::shared_ptr<IPathQuery> query = (it == pathQueries.end()) ? nullptr : it->second;
	if ((query != nullptr) && (query->GetState() != IPathQuery::State::READY)) {  // not ready
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();
	const AIFloat3& startPos = leader->GetPos(frame);

	CPathFinder* pathfinder = circuit->GetPathfinder();
	query = pathfinder->CreatePathSingleQuery(
			leader, circuit->GetThreatMap(), frame,
			startPos, position, pathfinder->GetSquareSize());
	pathQueries[leader] = query;

	const CRefHolder thisHolder(this);
	pathfinder->RunQuery(query, [this, thisHolder](std::shared_ptr<IPathQuery> query) {
		if (this->IsQueryAlive(query)) {
			this->ApplyDisengagePath(std::static_pointer_cast<CQueryPathSingle>(query));
		}
	});
}

void CAntiAirTask::ApplyDisengagePath(std::shared_ptr<CQueryPathSingle> query)
{
	pPath = query->GetPathInfo();

	if (!pPath->path.empty()) {
		if (pPath->path.size() > 2) {
			ActivePath();
		}
		return;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();
	for (CCircuitUnit* unit : units) {
		TRY_UNIT(circuit, unit,
			unit->CmdMoveTo(position, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, frame + FRAMES_PER_SEC * 60);
		)
		unit->GetTravelAct()->StateWait();
	}
	state = State::ROAM;
}

void CAntiAirTask::ApplyTargetPath(std::shared_ptr<CQueryPathSingle> query)
{
	pPath = query->GetPathInfo();

	if (!pPath->posPath.empty()) {
		ActivePath();
		return;
	}

	Fallback();
}

void CAntiAirTask::FallbackSafePos()
{
	CCircuitAI* circuit = manager->GetCircuit();
	circuit->GetMilitaryManager()->FillSafePos(leader, urgentPositions);
	if (urgentPositions.empty()) {
		FallbackCommPos();
		return;
	}

	const int frame = circuit->GetLastFrame();
	const AIFloat3& startPos = leader->GetPos(frame);
	const float pathRange = DEFAULT_SLACK * 4;

	CPathFinder* pathfinder = circuit->GetPathfinder();
	std::shared_ptr<IPathQuery> query = pathfinder->CreatePathMultiQuery(
			leader, circuit->GetThreatMap(), frame,
			startPos, pathRange, urgentPositions);
	pathQueries[leader] = query;

	const CRefHolder thisHolder(this);
	pathfinder->RunQuery(query, [this, thisHolder](std::shared_ptr<IPathQuery> query) {
		if (this->IsQueryAlive(query)) {
			this->ApplySafePos(std::static_pointer_cast<CQueryPathMulti>(query));
		}
	});
}

void CAntiAirTask::ApplySafePos(std::shared_ptr<CQueryPathMulti> query)
{
	pPath = query->GetPathInfo();

	if (!pPath->posPath.empty()) {
		position = pPath->posPath.back();
		ActivePath();
		return;
	}

	FallbackCommPos();
}

void CAntiAirTask::FallbackCommPos()
{
	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();
	CCircuitUnit* commander = circuit->GetSetupManager()->GetCommander();
	if ((commander != nullptr) &&
		circuit->GetTerrainManager()->CanMoveToPos(leader->GetArea(), commander->GetPos(frame)))
	{
		// ApplyCommPos
		for (CCircuitUnit* unit : units) {
			unit->Guard(commander, frame + FRAMES_PER_SEC * 60);
			unit->GetTravelAct()->StateWait();
		}
		return;
	}

	Fallback();
}

void CAntiAirTask::Fallback()
{
	// should never happen
	CCircuitAI* circuit = manager->GetCircuit();
	const int frame = circuit->GetLastFrame();
	for (CCircuitUnit* unit : units) {
		TRY_UNIT(circuit, unit,
			unit->GetUnit()->Fight(position, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, frame + FRAMES_PER_SEC * 60);
		)
		unit->GetTravelAct()->StateWait();
	}
}

} // namespace circuit
