/*
 * MoveAction.h
 *
 *  Created on: Jan 13, 2015
 *      Author: rlcevg
 */

#ifndef SRC_CIRCUIT_UNIT_ACTION_MOVEACTION_H_
#define SRC_CIRCUIT_UNIT_ACTION_MOVEACTION_H_

#include "unit/action/TravelAction.h"

namespace circuit {

class CMoveAction: public ITravelAction {
public:
	CMoveAction(CCircuitUnit* owner, int squareSize, float speed = NO_SPEED_LIMIT);
	CMoveAction(CCircuitUnit* owner, const std::shared_ptr<CPathInfo>& pPath,
			int squareSize, float speed = NO_SPEED_LIMIT);
	virtual ~CMoveAction();

	virtual void Update(CCircuitAI* circuit) override;
};

} // namespace circuit

#endif // SRC_CIRCUIT_UNIT_ACTION_MOVEACTION_H_
