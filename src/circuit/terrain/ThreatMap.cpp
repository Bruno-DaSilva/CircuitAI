/*
 * ThreatMap.cpp
 *
 *  Created on: Aug 25, 2015
 *      Author: rlcevg
 *      Original implementation: https://github.com/spring/KAIK/blob/master/ThreatMap.cpp
 */

#include "terrain/ThreatMap.h"
#include "terrain/TerrainManager.h"
#include "unit/EnemyUnit.h"
#include "util/utils.h"

#include "OOAICallback.h"
#include "Mod.h"
#include "Map.h"

namespace circuit {

using namespace springai;

#define THREAT_DECAY	0.05f

CThreatMap::CThreatMap(CCircuitAI* circuit)
		: circuit(circuit)
//		, currMaxThreat(.0f)  // maximum threat (normalizer)
//		, currSumThreat(.0f)  // threat summed over all cells
//		, currAvgThreat(.0f)  // average threat over all cells
{
	areaData = circuit->GetTerrainManager()->GetAreaData();
	squareSize = circuit->GetTerrainManager()->GetConvertStoP();
	width = circuit->GetTerrainManager()->GetTerrainWidth() / squareSize;
	height = circuit->GetTerrainManager()->GetTerrainHeight() / squareSize;
	mapSize = width * height;

	rangeDefault = (DEFAULT_SLACK * 4) / squareSize;
	distCloak = (DEFAULT_SLACK * 3) / squareSize;

	airThreat.resize(mapSize, THREAT_BASE);
	surfThreat.resize(mapSize, THREAT_BASE);
	amphThreat.resize(mapSize, THREAT_BASE);
	cloakThreat.resize(mapSize, THREAT_BASE);
	threatArray = &surfThreat[0];

	Map* map = circuit->GetMap();
	int mapWidth = map->GetWidth();
	Mod* mod = circuit->GetCallback()->GetMod();
	int losMipLevel = mod->GetLosMipLevel();
	int radarMipLevel = mod->GetRadarMipLevel();
	delete mod;

//	radarMap = std::move(map->GetRadarMap());
	radarWidth = mapWidth >> radarMipLevel;
	sonarMap = std::move(map->GetSonarMap());
	radarResConv = SQUARE_SIZE << radarMipLevel;
	losMap = std::move(map->GetLosMap());
	losWidth = mapWidth >> losMipLevel;
	losResConv = SQUARE_SIZE << losMipLevel;
}

CThreatMap::~CThreatMap()
{
	PRINT_DEBUG("Execute: %s\n", __PRETTY_FUNCTION__);

#ifdef DEBUG_VIS
	for (const std::pair<Uint32, float*>& win : sdlWindows) {
		circuit->GetDebugDrawer()->DelSDLWindow(win.first);
		delete[] win.second;
	}
#endif
}

void CThreatMap::Update()
{
//	radarMap = std::move(circuit->GetMap()->GetRadarMap());
	sonarMap = std::move(circuit->GetMap()->GetSonarMap());
	losMap = std::move(circuit->GetMap()->GetLosMap());
	CTerrainManager* terrainManager = circuit->GetTerrainManager();
//	currMaxThreat = .0f;

	// account for moving units
	for (auto& kv : hostileUnits) {
		CEnemyUnit* e = kv.second;
		if (e->IsHidden()) {
			continue;
		}

		DelEnemyUnit(e);
//		if ((!e->IsInRadar() && IsInRadar(e->GetPos())) ||
//			(!e->IsInLOS() && IsInLOS(e->GetPos()))) {
		if (e->NotInRadarAndLOS() && IsInLOS(e->GetPos())) {
			e->SetHidden();
			continue;
		}
	}

	areaData = terrainManager->GetAreaData();

	for (auto& kv : hostileUnits) {
		CEnemyUnit* e = kv.second;
		if (e->IsHidden()) {
			continue;
		}

		if (e->IsInRadarOrLOS()) {
			AIFloat3 pos = e->GetUnit()->GetPos();
			terrainManager->CorrectPosition(pos);
			e->SetPos(pos);
//		} else {
//			e->DecayThreat(0.99f);  // decay 0.99^updateNum
		}
		if (e->IsInLOS()) {
			e->SetThreat(GetEnemyUnitThreat(e));
		}
		AddEnemyUnit(e);

//		currMaxThreat = std::max(currMaxThreat, e->GetThreat());
	}

	for (auto& kv : peaceUnits) {
		CEnemyUnit* e = kv.second;
		if (e->IsHidden()) {
			continue;
		}
		if (e->NotInRadarAndLOS() && IsInLOS(e->GetPos())) {
			DelDecloaker(e);
			e->SetHidden();
			continue;
		}
		if (e->IsInRadarOrLOS()) {
			AIFloat3 pos = e->GetUnit()->GetPos();
			terrainManager->CorrectPosition(pos);
			if (pos != e->GetPos()) {
				DelDecloaker(e);
				e->SetPos(pos);
				AddDecloaker(e);
			}
		}
	}

	// decay whole threatMap to compensate for precision errors
	for (int index = 0; index < mapSize; ++index) {
		airThreat[index]  = std::max<float>(airThreat[index]  - THREAT_DECAY, THREAT_BASE);
		surfThreat[index] = std::max<float>(surfThreat[index] - THREAT_DECAY, THREAT_BASE);
		amphThreat[index] = std::max<float>(amphThreat[index] - THREAT_DECAY, THREAT_BASE);
		// except for cloakThreat
	}
//	airMetal    = std::max(airMetal    - THREAT_DECAY, .0f);
//	staticMetal = std::max(staticMetal - THREAT_DECAY, .0f);
//	landMetal   = std::max(landMetal   - THREAT_DECAY, .0f);
//	waterMetal  = std::max(waterMetal  - THREAT_DECAY, .0f);

#ifdef DEBUG_VIS
	UpdateVis();
#endif
}

bool CThreatMap::EnemyEnterLOS(CEnemyUnit* enemy)
{
	// Possible cases:
	// (1) Unknown enemy that has been detected for the first time
	// (2) Unknown enemy that was only in radar enters LOS
	// (3) Known enemy that already was in LOS enters again

	enemy->SetInLOS();
	bool isKnown = enemy->IsKnown();

	if (enemy->GetDPS() < 0.1f) {
		if (enemy->GetThreat() > .0f) {  // (2)
			// threat prediction failed when enemy was unknown
			if (enemy->IsHidden()) {
				enemy->ClearHidden();
			} else {
				DelEnemyUnitAll(enemy);
			}
			hostileUnits.erase(enemy->GetId());
			peaceUnits[enemy->GetId()] = enemy;
			enemy->SetThreat(.0f);
			enemy->SetRange(CEnemyUnit::RangeType::MAX, 0);
			enemy->SetRange(CEnemyUnit::RangeType::AIR, 0);
			enemy->SetRange(CEnemyUnit::RangeType::LAND, 0);
			enemy->SetRange(CEnemyUnit::RangeType::WATER, 0);
			enemy->SetRange(CEnemyUnit::RangeType::CLOAK, GetCloakRange(enemy));
		} else if (peaceUnits.find(enemy->GetId()) == peaceUnits.end()) {
			peaceUnits[enemy->GetId()] = enemy;
			enemy->SetRange(CEnemyUnit::RangeType::CLOAK, GetCloakRange(enemy));
		} else if (enemy->IsHidden()) {
			enemy->ClearHidden();
		} else {
			DelDecloaker(enemy);
		}

		AIFloat3 pos = enemy->GetUnit()->GetPos();
		circuit->GetTerrainManager()->CorrectPosition(pos);
		enemy->SetPos(pos);
		enemy->SetKnown();

		AddDecloaker(enemy);
		return !isKnown;
	}

	if (hostileUnits.find(enemy->GetId()) == hostileUnits.end()) {
		hostileUnits[enemy->GetId()] = enemy;
	} else if (enemy->IsHidden()) {
		enemy->ClearHidden();
	} else if (enemy->IsKnown()) {
		DelEnemyUnit(enemy);
	} else {
		DelEnemyUnitAll(enemy);
	}

	AIFloat3 pos = enemy->GetUnit()->GetPos();
	circuit->GetTerrainManager()->CorrectPosition(pos);
	enemy->SetPos(pos);
	SetEnemyUnitRange(enemy);
	enemy->SetThreat(GetEnemyUnitThreat(enemy));
	enemy->SetKnown();

	AddEnemyUnit(enemy);
	return !isKnown;
}

void CThreatMap::EnemyLeaveLOS(CEnemyUnit* enemy)
{
	enemy->ClearInLOS();
}

void CThreatMap::EnemyEnterRadar(CEnemyUnit* enemy)
{
	// Possible cases:
	// (1) Unknown enemy wanders at radars
	// (2) Known enemy that once was in los wandering at radar
	// (3) EnemyEnterRadar invoked right after EnemyEnterLOS in area with no radar

	enemy->SetInRadar();

	if (enemy->IsInLOS()) {  // (3)
		return;
	}

	if (enemy->GetDPS() < 0.1f) {  // (2)
		if (enemy->IsHidden()) {
			enemy->ClearHidden();
		} else {
			DelDecloaker(enemy);
		}

		AIFloat3 pos = enemy->GetUnit()->GetPos();
		circuit->GetTerrainManager()->CorrectPosition(pos);
		enemy->SetPos(pos);

		AddDecloaker(enemy);
		return;
	}

	bool isNew = false;
	auto it = hostileUnits.find(enemy->GetId());
	if (it == hostileUnits.end()) {  // (1)
		std::tie(it, isNew) = hostileUnits.emplace(enemy->GetId(), enemy);
	} else if (enemy->IsHidden()) {
		enemy->ClearHidden();
	} else {
		DelEnemyUnit(enemy);
	}

	AIFloat3 pos = enemy->GetUnit()->GetPos();
	circuit->GetTerrainManager()->CorrectPosition(pos);
	enemy->SetPos(pos);
	if (isNew) {  // unknown enemy enters radar for the first time
		enemy->SetThreat(enemy->GetDPS());  // TODO: Randomize
		enemy->SetRange(CEnemyUnit::RangeType::MAX, rangeDefault);
		enemy->SetRange(CEnemyUnit::RangeType::AIR, rangeDefault);
		enemy->SetRange(CEnemyUnit::RangeType::LAND, rangeDefault);
		enemy->SetRange(CEnemyUnit::RangeType::WATER, rangeDefault);
		enemy->SetRange(CEnemyUnit::RangeType::CLOAK, distCloak);
	}

	AddEnemyUnit(enemy);
}

void CThreatMap::EnemyLeaveRadar(CEnemyUnit* enemy)
{
	enemy->ClearInRadar();
}

void CThreatMap::EnemyDamaged(CEnemyUnit* enemy)
{
	auto it = hostileUnits.find(enemy->GetId());
	if ((it == hostileUnits.end()) || !enemy->IsInLOS()) {
		return;
	}

	DelEnemyUnit(enemy);
	enemy->SetThreat(GetEnemyUnitThreat(enemy));
	AddEnemyUnit(enemy);
}

bool CThreatMap::EnemyDestroyed(CEnemyUnit* enemy)
{
	auto it = hostileUnits.find(enemy->GetId());
	if (it == hostileUnits.end()) {
		if (!enemy->IsHidden()) {
			DelDecloaker(enemy);
		}
		peaceUnits.erase(enemy->GetId());
		return enemy->IsKnown();
	}

	if (!enemy->IsHidden()) {
		DelEnemyUnit(enemy);
	}
	hostileUnits.erase(it);
	return enemy->IsKnown();
}

float CThreatMap::GetAllThreatAt(const AIFloat3& position) const
{
	const int z = (int)position.z / squareSize;
	const int x = (int)position.x / squareSize;
	const int index = z * width + x;
//	float air = airThreat[index] - THREAT_BASE;
	float land = surfThreat[index] - THREAT_BASE;
//	float water = amphThreat[index] - THREAT_BASE;
	return /*air + */land/* + water*/;
}

void CThreatMap::SetThreatType(CCircuitUnit* unit)
{
	assert(unit != nullptr);
	if (unit->GetCircuitDef()->IsAbleToFly()) {
		threatArray = &airThreat[0];
//	} else if (unit->GetPos(circuit->GetLastFrame()).y < -SQUARE_SIZE * 5) {
	} else if (unit->GetCircuitDef()->IsAmphibious()) {
		threatArray = &amphThreat[0];
	} else {
		threatArray = &surfThreat[0];
	}
}

float CThreatMap::GetThreatAt(const AIFloat3& position) const
{
	const int z = (int)position.z / squareSize;
	const int x = (int)position.x / squareSize;
	return threatArray[z * width + x] - THREAT_BASE;
}

float CThreatMap::GetThreatAt(CCircuitUnit* unit, const AIFloat3& position) const
{
	assert(unit != nullptr);
	const int z = (int)position.z / squareSize;
	const int x = (int)position.x / squareSize;
	if (unit->GetCircuitDef()->IsAbleToFly()) {
		return airThreat[z * width + x] - THREAT_BASE;
	}
//	if (unit->GetPos(circuit->GetLastFrame()).y < -SQUARE_SIZE * 5) {
	if (unit->GetCircuitDef()->IsAmphibious()) {
		return amphThreat[z * width + x] - THREAT_BASE;
	}
	return surfThreat[z * width + x] - THREAT_BASE;
}

float CThreatMap::GetUnitThreat(CCircuitUnit* unit) const
{
	return unit->GetDPS() * sqrtf(unit->GetUnit()->GetHealth() / 100.0f) * THREAT_MOD;  // / unit->GetUnit()->GetMaxHealth();
}

void CThreatMap::AddEnemyUnit(const CEnemyUnit* e)
{
	CCircuitDef* cdef = e->GetCircuitDef();
	if (cdef == nullptr) {
		AddEnemyUnitAll(e);
		return;
	}

	if (cdef->HasAntiAir()) {
		AddEnemyAir(e);
	}
	if (cdef->HasAntiLand() || cdef->HasAntiWater()) {
		AddEnemyAmph(e);
	}
	AddDecloaker(e);
}

void CThreatMap::DelEnemyUnit(const CEnemyUnit* e)
{
	CCircuitDef* cdef = e->GetCircuitDef();
	if (cdef == nullptr) {
		DelEnemyUnitAll(e);
		return;
	}

	if (cdef->HasAntiAir()) {
		DelEnemyAir(e);
	}
	if (cdef->HasAntiLand() || cdef->HasAntiWater()) {
		DelEnemyAmph(e);
	}
	DelDecloaker(e);
}

void CThreatMap::AddEnemyUnitAll(const CEnemyUnit* e)
{
	AddEnemyAir(e);
	AddEnemyAmph(e);
	AddDecloaker(e);
}

void CThreatMap::DelEnemyUnitAll(const CEnemyUnit* e)
{
	DelEnemyAir(e);
	DelEnemyAmph(e);
	DelDecloaker(e);
}

void CThreatMap::AddEnemyAir(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threat = e->GetThreat()/* - THREAT_DECAY*/;
	const int range = e->GetRange(CEnemyUnit::RangeType::AIR);
	const int rangeSq = SQUARE(range);

	const int beginX = std::max(int(posx - range + 1),      0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),      0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int rrx = rangeSq - SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			if (SQUARE(posz - z) > rrx) {
				continue;
			}

			const int index = z * width + x;
			airThreat[index] += threat;

//			currSumThreat += threat;
		}
	}

//	currAvgThreat = currSumThreat / landThreat.size();
}

void CThreatMap::DelEnemyAir(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threat = e->GetThreat()/* + THREAT_DECAY*/;
	const int range = e->GetRange(CEnemyUnit::RangeType::AIR);
	const int rangeSq = SQUARE(range);

	// Threat circles are large and often have appendix, decrease it by 1 for micro-optimization
	const int beginX = std::max(int(posx - range + 1),      0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),      0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int rrx = rangeSq - SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			if (SQUARE(posz - z) > rrx) {
				continue;
			}

			const int index = z * width + x;
			// MicroPather cannot deal with negative costs
			// (which may arise due to floating-point drift)
			// nor with zero-cost nodes (see MP::SetMapData,
			// threat is not used as an additive overlay)
			airThreat[index] = std::max<float>(airThreat[index] - threat, THREAT_BASE);

//			currSumThreat -= threat;
		}
	}

//	currAvgThreat = currSumThreat / landThreat.size();
}

void CThreatMap::AddEnemyAmph(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threat = e->GetThreat()/* - THREAT_DECAY*/;
	const int rangeLand = e->GetRange(CEnemyUnit::RangeType::LAND);
	const int rangeLandSq = SQUARE(rangeLand);
	const int rangeWater = e->GetRange(CEnemyUnit::RangeType::WATER);
	const int rangeWaterSq = SQUARE(rangeWater);
	const int range = std::max(rangeLand, rangeWater);
	const std::vector<STerrainMapSector>& sector = areaData->sector;

	const int beginX = std::max(int(posx - range + 1),      0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),      0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int dxSq = SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			const int dzSq = SQUARE(posz - z);

			const int sum = dxSq + dzSq;
			const int index = z * width + x;
			bool isWaterThreat = (sum <= rangeWaterSq) && sector[index].isWater;
			if (isWaterThreat || ((sum <= rangeLandSq) && (sector[index].position.y >= -SQUARE_SIZE * 5)))
			{
				amphThreat[index] += threat;
			}
			if (isWaterThreat || (sum <= rangeLandSq)) {
				surfThreat[index] += threat;
			}
		}
	}
}

void CThreatMap::DelEnemyAmph(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threat = e->GetThreat()/* + THREAT_DECAY*/;
	const int rangeLand = e->GetRange(CEnemyUnit::RangeType::LAND);
	const int rangeLandSq = SQUARE(rangeLand);
	const int rangeWater = e->GetRange(CEnemyUnit::RangeType::WATER);
	const int rangeWaterSq = SQUARE(rangeWater);
	const int range = std::max(rangeLand, rangeWater);
	const std::vector<STerrainMapSector>& sector = areaData->sector;

	const int beginX = std::max(int(posx - range + 1),      0);
	const int endX   = std::min(int(posx + range    ),  width);
	const int beginZ = std::max(int(posz - range + 1),      0);
	const int endZ   = std::min(int(posz + range    ), height);

	for (int x = beginX; x < endX; ++x) {
		const int dxSq = SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			const int dzSq = SQUARE(posz - z);

			const int sum = dxSq + dzSq;
			const int index = z * width + x;
			bool isWaterThreat = (sum <= rangeWaterSq) && sector[index].isWater;
			if (isWaterThreat || ((sum <= rangeLandSq) && (sector[index].position.y >= -SQUARE_SIZE * 5)))
			{
				amphThreat[index] = std::max<float>(amphThreat[index] - threat, THREAT_BASE);
			}
			if (isWaterThreat || (sum <= rangeLandSq)) {
				surfThreat[index] = std::max<float>(surfThreat[index] - threat, THREAT_BASE);
			}
		}
	}
}

void CThreatMap::AddDecloaker(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threatCloak = 8.0f;
	const int rangeCloak = e->GetRange(CEnemyUnit::RangeType::CLOAK);
	const int rangeCloakSq = SQUARE(rangeCloak);

	// Decloak ranges are small, full range shouldn't hit performance
	const int beginX = std::max(int(posx - rangeCloak    ),      0);
	const int endX   = std::min(int(posx + rangeCloak + 1),  width);
	const int beginZ = std::max(int(posz - rangeCloak    ),      0);
	const int endZ   = std::min(int(posz + rangeCloak + 1), height);

	for (int x = beginX; x < endX; ++x) {
		const int rrx = rangeCloakSq - SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			if (SQUARE(posz - z) > rrx) {
				continue;
			}
			const int index = z * width + x;
			cloakThreat[index] += threatCloak;
		}
	}
}

void CThreatMap::DelDecloaker(const CEnemyUnit* e)
{
	const int posx = (int)e->GetPos().x / squareSize;
	const int posz = (int)e->GetPos().z / squareSize;

	const float threatCloak = 8.0f;
	const int rangeCloak = e->GetRange(CEnemyUnit::RangeType::CLOAK);
	const int rangeCloakSq = SQUARE(rangeCloak);

	// Decloak ranges are small, full range shouldn't hit performance
	const int beginX = std::max(int(posx - rangeCloak    ),      0);
	const int endX   = std::min(int(posx + rangeCloak + 1),  width);
	const int beginZ = std::max(int(posz - rangeCloak    ),      0);
	const int endZ   = std::min(int(posz + rangeCloak + 1), height);

	for (int x = beginX; x < endX; ++x) {
		const int rrx = rangeCloakSq - SQUARE(posx - x);
		for (int z = beginZ; z < endZ; ++z) {
			if (SQUARE(posz - z) > rrx) {
				continue;
			}
			const int index = z * width + x;
			cloakThreat[index] = std::max<float>(cloakThreat[index] - threatCloak, THREAT_BASE);
		}
	}
}

void CThreatMap::SetEnemyUnitRange(CEnemyUnit* e) const
{
	CCircuitDef* cdef = e->GetCircuitDef();
	assert(cdef != nullptr);

	const int slack = DEFAULT_SLACK * (cdef->IsMobile() ? 4 : 2);
	int range;
	int maxRange;

	range = cdef->HasAntiAir() ? ((int)cdef->GetMaxRange(CCircuitDef::RangeType::AIR) + slack) / squareSize : 0;
	e->SetRange(CEnemyUnit::RangeType::AIR, range);
	maxRange = range;

	range = cdef->HasAntiLand() ? ((int)cdef->GetMaxRange(CCircuitDef::RangeType::LAND) + slack) / squareSize : 0;
	e->SetRange(CEnemyUnit::RangeType::LAND, range);
	maxRange = std::max(maxRange, range);

	range = cdef->HasAntiWater() ? ((int)cdef->GetMaxRange(CCircuitDef::RangeType::WATER) + slack) / squareSize : 0;
	e->SetRange(CEnemyUnit::RangeType::WATER, range);
	maxRange = std::max(maxRange, range);

	e->SetRange(CEnemyUnit::RangeType::MAX, maxRange);
	e->SetRange(CEnemyUnit::RangeType::CLOAK, GetCloakRange(e));
}

int CThreatMap::GetCloakRange(const CEnemyUnit* e) const
{
	assert(e->GetCircuitDef() != nullptr);
	const int sizeX = e->GetCircuitDef()->GetUnitDef()->GetXSize() * (SQUARE_SIZE / 2);
	const int sizeZ = e->GetCircuitDef()->GetUnitDef()->GetZSize() * (SQUARE_SIZE / 2);
	return (int)sqrtf(sizeX * sizeX + sizeZ * sizeZ) / squareSize + distCloak;
}

float CThreatMap::GetEnemyUnitThreat(CEnemyUnit* enemy) const
{
	if (enemy->GetRange() > 2000 / squareSize) {
		return THREAT_BASE;  // or 0
	}
	if (enemy->GetUnit()->IsBeingBuilt()) {
		return THREAT_BASE;
	}
	const float health = enemy->GetUnit()->GetHealth();
	if (health <= .0f) {
		return .0f;
	}
	const float dps = std::min(enemy->GetDPS(), 2000.0f);
	const float dpsMod = sqrtf(health / 100.0f) * THREAT_MOD;  // / enemy->GetUnit()->GetMaxHealth();
	return dps * dpsMod;
}

bool CThreatMap::IsInLOS(const AIFloat3& pos) const
{
	// res = 1 << Mod->GetLosMipLevel();
	// the value for the full resolution position (x, z) is at index ((z * width + x) / res)
	// the last value, bottom right, is at index (width/res * height/res - 1)

	if (pos.y < -SQUARE_SIZE * 5) {  // Mod->GetRequireSonarUnderWater() = true
		const int x = (int)pos.x / radarResConv;
		const int z = (int)pos.z / radarResConv;
		if (sonarMap[z * radarWidth + x] <= 0) {
			return false;
		}
	}
	// convert from world coordinates to losmap coordinates
	const int x = (int)pos.x / losResConv;
	const int z = (int)pos.z / losResConv;
	return losMap[z * losWidth + x] > 0;
}

//bool CThreatMap::IsInRadar(const AIFloat3& pos) const
//{
//	// the value for the full resolution position (x, z) is at index ((z * width + x) / 8)
//	// the last value, bottom right, is at index (width/8 * height/8 - 1)
//
//	// convert from world coordinates to radarmap coordinates
//	const int x = (int)pos.x / radarResConv;
//	const int z = (int)pos.z / radarResConv;
//	return ((pos.y < -SQUARE_SIZE * 5) ? sonarMap : radarMap)[z * radarWidth + x] > 0;
//}

#ifdef DEBUG_VIS
void CThreatMap::UpdateVis()
{
	if (sdlWindows.empty()/* || (currMaxThreat < .1f)*/) {
		return;
	}

	Uint32 sdlWindowId;
	float* dbgMap;
	std::tie(sdlWindowId, dbgMap) = sdlWindows[0];
	for (unsigned i = 0; i < airThreat.size(); ++i) {
		dbgMap[i] = std::min<float>((airThreat[i] - THREAT_BASE) / 40.0f /*currMaxThreat*/, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap);

	std::tie(sdlWindowId, dbgMap) = sdlWindows[1];
	for (unsigned i = 0; i < surfThreat.size(); ++i) {
		dbgMap[i] = std::min<float>((surfThreat[i] - THREAT_BASE) / 40.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap);

	std::tie(sdlWindowId, dbgMap) = sdlWindows[2];
	for (unsigned i = 0; i < amphThreat.size(); ++i) {
		dbgMap[i] = std::min<float>((amphThreat[i] - THREAT_BASE) / 40.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap);

	std::tie(sdlWindowId, dbgMap) = sdlWindows[3];
	for (unsigned i = 0; i < cloakThreat.size(); ++i) {
		dbgMap[i] = std::min<float>((cloakThreat[i] - THREAT_BASE) / 8.0f, 1.0f);
	}
	circuit->GetDebugDrawer()->DrawMap(sdlWindowId, dbgMap);
}

void CThreatMap::ToggleVis()
{
	if (sdlWindows.empty()) {
		// ~threat
		std::pair<Uint32, float*> win;
		std::string label;

		win.second = new float [airThreat.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: AIR Threat Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [surfThreat.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: SURFACE Threat Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [amphThreat.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: AMPHIBIOUS Threat Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		win.second = new float [cloakThreat.size()];
		label = utils::int_to_string(circuit->GetSkirmishAIId(), "Circuit AI [%i] :: CLOAK Threat Map");
		win.first = circuit->GetDebugDrawer()->AddSDLWindow(width, height, label.c_str());
		sdlWindows.push_back(win);

		UpdateVis();
	} else {
		for (const std::pair<Uint32, float*>& win : sdlWindows) {
			circuit->GetDebugDrawer()->DelSDLWindow(win.first);
			delete[] win.second;
		}
		sdlWindows.clear();
	}
}
#endif

} // namespace circuit
