/*
 * BigGunTask.cpp
 *
 *  Created on: Jan 31, 2015
 *      Author: rlcevg
 */

#include "task/builder/BigGunTask.h"
#include "module/BuilderManager.h"
#include "CircuitAI.h"
#include "util/utils.h"

namespace circuit {

using namespace springai;

CBBigGunTask::CBBigGunTask(ITaskManager* mgr, Priority priority,
						   CCircuitDef* buildDef, const AIFloat3& position,
						   float cost, bool isShake, int timeout) :
		IBuilderTask(mgr, priority, buildDef, position, BuildType::BIG_GUN, cost, isShake, timeout)
{
}

CBBigGunTask::~CBBigGunTask()
{
	PRINT_DEBUG("Execute: %s\n", __PRETTY_FUNCTION__);
}

void CBBigGunTask::Finish()
{
	IBuilderTask::Finish();

	CCircuitAI* circuit = manager->GetCircuit();
	CBuilderManager* builderManager = circuit->GetBuilderManager();

	CCircuitDef* cdef = circuit->GetCircuitDef("armamd");
	builderManager->EnqueueTask(IBuilderTask::Priority::HIGH, cdef, buildPos, IBuilderTask::BuildType::BUNKER);
}

} // namespace circuit
