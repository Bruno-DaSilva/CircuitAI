/*
 * ReclaimTask.cpp
 *
 *  Created on: Jan 31, 2015
 *      Author: rlcevg
 */

#include "task/builder/ReclaimTask.h"
#include "task/TaskManager.h"
#include "map/ThreatMap.h"
#include "module/EconomyManager.h"
#include "terrain/TerrainManager.h"
#include "unit/action/TravelAction.h"
#include "CircuitAI.h"
#include "util/Utils.h"

#include "spring/SpringCallback.h"

#include "AISCommands.h"
#include "Feature.h"
#include "FeatureDef.h"

namespace circuit {

using namespace springai;

CBReclaimTask::CBReclaimTask(ITaskManager* mgr, Priority priority,
							 const AIFloat3& position,
							 float cost, int timeout, float radius, bool isMetal)
		: IReclaimTask(mgr, priority, Type::BUILDER, position, cost, timeout, radius, isMetal)
{
}

CBReclaimTask::CBReclaimTask(ITaskManager* mgr, Priority priority,
							 CCircuitUnit* target,
							 int timeout)
		: IReclaimTask(mgr, priority, Type::BUILDER, target, timeout)
{
}

CBReclaimTask::~CBReclaimTask()
{
}

void CBReclaimTask::AssignTo(CCircuitUnit* unit)
{
	IBuilderTask::AssignTo(unit);

	lastTouched = manager->GetCircuit()->GetLastFrame();
}

bool CBReclaimTask::Reevaluate(CCircuitUnit* unit)
{
	if (!isMetal) {
		return true;
	}

	CCircuitAI* circuit = manager->GetCircuit();
	if (circuit->GetEconomyManager()->IsMetalFull()) {
		manager->AbortTask(this);
		return false;
	} else if (!units.empty()) {
		/*
		 * Update reclaim position
		 */
		const int frame = circuit->GetLastFrame();
		const AIFloat3& pos = unit->GetPos(frame);
		auto enemies = circuit->GetCallback()->GetEnemyUnitsIn(pos, 500.0f);
		if (!enemies.empty()) {
			for (Unit* enemy : enemies) {
				if ((enemy != nullptr) && enemy->IsBeingBuilt()) {
					TRY_UNIT(circuit, unit,
						unit->GetUnit()->ReclaimUnit(enemy, UNIT_CMD_OPTION, frame + FRAMES_PER_SEC * 60);
					)
					utils::free_clear(enemies);
					return false;
				}
			}
			utils::free_clear(enemies);
		}

		auto features = circuit->GetCallback()->GetFeaturesIn(pos, 500.0f);
		if (!features.empty()) {
			CTerrainManager* terrainMgr = circuit->GetTerrainManager();
			circuit->GetThreatMap()->SetThreatType(unit);
			float minSqDist = std::numeric_limits<float>::max();
			Resource* metalRes = circuit->GetEconomyManager()->GetMetalRes();
			for (Feature* feature : features) {
				AIFloat3 featPos = feature->GetPosition();
				CTerrainManager::CorrectPosition(featPos);  // Impulsed flying feature
				if (!terrainMgr->CanReachAtSafe(unit, featPos, unit->GetCircuitDef()->GetBuildDistance())) {
					continue;
				}
				FeatureDef* featDef = feature->GetDef();
				if (!featDef->IsReclaimable()) {
					delete featDef;
					continue;
				}
				float reclaimValue = featDef->GetContainedResource(metalRes)/* * feature->GetReclaimLeft()*/;
				delete featDef;
				if (reclaimValue < 1.0f) {
					continue;
				}
				float sqDist = pos.SqDistance2D(featPos);
				if (sqDist < minSqDist) {
					position = featPos;
					minSqDist = sqDist;
				}
			}
			utils::free_clear(features);
		}
	}

	return true;
}

} // namespace circuit
