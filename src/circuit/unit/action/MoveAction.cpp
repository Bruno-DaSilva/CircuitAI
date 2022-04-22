/*
 * MoveAction.cpp
 *
 *  Created on: Jan 13, 2015
 *      Author: rlcevg
 */

#include "unit/action/MoveAction.h"
#include "unit/CircuitUnit.h"
#include "CircuitAI.h"
#include "util/Utils.h"

#include "AISCommands.h"

namespace circuit {

using namespace springai;

CMoveAction::CMoveAction(CCircuitUnit* owner, int squareSize, float speed)
		: ITravelAction(owner, Type::MOVE, squareSize, speed)
{
}

CMoveAction::CMoveAction(CCircuitUnit* owner, const std::shared_ptr<CPathInfo>& pPath,
		int squareSize, float speed)
		: ITravelAction(owner, Type::MOVE, pPath, squareSize, speed)
{
}

CMoveAction::~CMoveAction()
{
}

void CMoveAction::Update(CCircuitAI* circuit)
{
	if (lastFrame + FRAMES_PER_SEC > circuit->GetLastFrame()) {
		return;
	}
	lastFrame = circuit->GetLastFrame();
	CCircuitUnit* unit = static_cast<CCircuitUnit*>(ownerList);

	float stepSpeed;
	int pathMaxIndex = CalcSpeedStep(stepSpeed);
	if (pathMaxIndex < 0) {
		return;
	}
	int step = pathIterator;

	TRY_UNIT(circuit, unit,
		const AIFloat3& pos = pPath->posPath[step];
		unit->CmdMoveTo(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, lastFrame + FRAMES_PER_SEC * 60);
		unit->CmdWantedSpeed(stepSpeed);

		constexpr short options = UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY;
		for (int i = 2; (step < pathMaxIndex) && (i < 3); ++i) {
			step = std::min(step + increment, pathMaxIndex);
			const AIFloat3& pos = pPath->posPath[step];
			unit->CmdMoveTo(pos, options, lastFrame + FRAMES_PER_SEC * 60 * i);
		}
	)
}

} // namespace circuit
