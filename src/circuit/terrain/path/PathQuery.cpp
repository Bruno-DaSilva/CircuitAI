/*
 * PathQuery.cpp
 *
 *  Created on: Apr 22, 2020
 *      Author: rlcevg
 */

#include "terrain/path/PathQuery.h"

namespace circuit {

IPathQuery::IPathQuery(const CPathFinder& pathfinder, int id, Type type)
		: pathfinder(pathfinder)
		, id(id)
		, type(type)
		, state(State::NONE)
		, canMoveArray(nullptr)
		, threatArray(nullptr)
		, unit(nullptr)
{
}

IPathQuery::~IPathQuery()
{
}

void IPathQuery::Init(const bool* canMoveArray, const float* threatArray,
		NSMicroPather::CostFunc moveFun, NSMicroPather::CostFunc moveThreatFun,
		CCircuitUnit* unit)
{
	this->canMoveArray = canMoveArray;
	this->threatArray = threatArray;
	this->moveFun = moveFun;
	this->moveThreatFun = moveThreatFun;
	this->unit = unit;  // optional
}

} // namespace circuit
