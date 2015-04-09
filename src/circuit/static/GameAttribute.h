/*
 * GameAttribute.h
 *
 *  Created on: Aug 12, 2014
 *      Author: rlcevg
 */

#ifndef SRC_CIRCUIT_STATIC_GAMEATTRIBUTE_H_
#define SRC_CIRCUIT_STATIC_GAMEATTRIBUTE_H_

#include "static/SetupData.h"
#include "static/MetalData.h"
#include "static/TerrainData.h"

#include <unordered_set>

namespace springai {
	class Pathing;
}

namespace circuit {

class CCircuitAI;

class CGameAttribute {
public:
	CGameAttribute();
	virtual ~CGameAttribute();

	void SetGameEnd(bool value);
	bool IsGameEnd();
	void RegisterAI(CCircuitAI* circuit);
	void UnregisterAI(CCircuitAI* circuit);

	const std::unordered_set<CCircuitAI*>& GetCircuits() const;
	CSetupData& GetSetupData();
	CMetalData& GetMetalData();
	CTerrainData& GetTerrainData();

private:
	bool gameEnd;
	std::unordered_set<CCircuitAI*> circuits;
	CSetupData setupData;
	CMetalData metalData;
	CTerrainData terrainData;
};

} // namespace circuit

#endif // SRC_CIRCUIT_STATIC_GAMEATTRIBUTE_H_