/*
 * ReclaimTask.cpp
 *
 *  Created on: Sep 4, 2016
 *      Author: rlcevg
 */

#include "task/common/ReclaimTask.h"
#include "task/TaskManager.h"
#include "terrain/TerrainManager.h"
#include "unit/action/DGunAction.h"
#include "CircuitAI.h"
#include "util/Utils.h"

#include "AISCommands.h"

namespace circuit {

using namespace springai;

IReclaimTask::IReclaimTask(ITaskManager* mgr, Priority priority, Type type,
						   const AIFloat3& position,
						   float cost, int timeout, float radius, bool isMetal)
		: IBuilderTask(mgr, priority, nullptr, position, type, BuildType::RECLAIM, cost, 0.f, timeout)
		, radius(radius)
		, isMetal(isMetal)
{
}

IReclaimTask::IReclaimTask(ITaskManager* mgr, Priority priority, Type type,
						   CCircuitUnit* target,
						   int timeout)
		: IBuilderTask(mgr, priority, nullptr, -RgtVector, type, BuildType::RECLAIM, 1000.0f, 0.f, timeout)
		, radius(0.f)
		, isMetal(false)
{
	SetTarget(target);
}

IReclaimTask::~IReclaimTask()
{
}

bool IReclaimTask::CanAssignTo(CCircuitUnit* unit) const
{
	return unit->GetCircuitDef()->IsAbleToReclaim() && (cost > buildPower * MAX_BUILD_SEC);
}

void IReclaimTask::RemoveAssignee(CCircuitUnit* unit)
{
	IBuilderTask::RemoveAssignee(unit);
	if (units.empty()) {
		manager->AbortTask(this);
	}
}

void IReclaimTask::Finish()
{
}

void IReclaimTask::Cancel()
{
}

void IReclaimTask::Execute(CCircuitUnit* unit)
{
	CCircuitAI* circuit = manager->GetCircuit();
	TRY_UNIT(circuit, unit,
		unit->CmdPriority(ClampPriority());
	)

	const int frame = circuit->GetLastFrame();
	if (target != nullptr) {
		TRY_UNIT(circuit, unit,
			unit->GetUnit()->ReclaimUnit(target->GetUnit(), UNIT_CMD_OPTION, frame + FRAMES_PER_SEC * 60);
		)
		return;
	}

	AIFloat3 pos;
	float reclRadius;
	if ((radius == .0f) || !utils::is_valid(position)) {
		CTerrainManager* terrainMgr = circuit->GetTerrainManager();
		float width = terrainMgr->GetTerrainWidth() / 2;
		float height = terrainMgr->GetTerrainHeight() / 2;
		pos = AIFloat3(width, 0, height);
		reclRadius = sqrtf(width * width + height * height);
	} else {
		pos = position;
		reclRadius = radius;
	}
	TRY_UNIT(circuit, unit,
		unit->GetUnit()->ReclaimInArea(pos, reclRadius, UNIT_CMD_OPTION, frame + FRAMES_PER_SEC * 60);
	)
}

void IReclaimTask::OnUnitIdle(CCircuitUnit* unit)
{
	manager->AbortTask(this);
}

void IReclaimTask::SetTarget(CCircuitUnit* unit)
{
	target = unit;
	buildPos = (unit != nullptr) ? unit->GetPos(manager->GetCircuit()->GetLastFrame()) : AIFloat3(-RgtVector);
}

} // namespace circuit
