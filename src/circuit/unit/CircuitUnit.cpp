/*
 * CircuitUnit.cpp
 *
 *  Created on: Sep 2, 2014
 *      Author: rlcevg
 */

#include "unit/CircuitUnit.h"
#include "unit/UnitManager.h"
#include "unit/action/DGunAction.h"
#include "unit/action/TravelAction.h"
#include "unit/enemy/EnemyUnit.h"
#include "setup/SetupManager.h"
#include "CircuitAI.h"
#include "util/Utils.h"
#ifdef DEBUG_VIS
#include "task/UnitTask.h"
#include "Command.h"
#endif

#include "AISCommands.h"
#include "Weapon.h"
#include "WrappWeaponMount.h"

namespace circuit {

using namespace springai;

CCircuitUnit::CCircuitUnit(Id unitId, Unit* unit, CCircuitDef* cdef)
		: CAllyUnit(unitId, unit, cdef)
		, taskFrame(-1)
		, manager(nullptr)
		, area(nullptr)
		, dgunAct(nullptr)
		, travelAct(nullptr)
//		, damagedFrame(-1)
		, moveFails(0)
		, failFrame(-1)
		, execFrame(-1)
		, isDead(false)
		, isDisarmed(false)
		, disarmFrame(-1)
		, isWeaponReady(true)
		, ammoFrame(-1)
		, isMorphing(false)
		, target(nullptr)
		, targetTile(-1)
{
	WeaponMount* wpMnt;
	if (cdef->IsRoleComm()) {
		dgun = nullptr;
		for (int num = 1; num < 3; ++num) {
			std::string str = utils::int_to_string(num, "comm_weapon_manual_%i");
			if (unit->GetRulesParamFloat(str.c_str(), -1) <= 0.f) {
				continue;
			}
			str = utils::int_to_string(num, "comm_weapon_num_%i");
			int mntId = CWeaponDef::WeaponIdFromLua(int(unit->GetRulesParamFloat(str.c_str(), -1)));
			if (mntId < 0) {
				continue;
			}
			wpMnt = WrappWeaponMount::GetInstance(unit->GetSkirmishAIId(), cdef->GetId(), mntId);
			if (wpMnt == nullptr) {
				continue;
			}
			dgun = unit->GetWeapon(wpMnt);
			delete wpMnt;
			break;
		}
	} else {
		wpMnt = cdef->GetDGunMount();
		dgun = (wpMnt == nullptr) ? nullptr : unit->GetWeapon(wpMnt);
	}
	wpMnt = cdef->GetWeaponMount();
	weapon = (wpMnt == nullptr) ? nullptr : unit->GetWeapon(wpMnt);
	wpMnt = cdef->GetShieldMount();
	shield = (wpMnt == nullptr) ? nullptr : unit->GetWeapon(wpMnt);
}

CCircuitUnit::~CCircuitUnit()
{
	delete dgun;
	delete weapon;
	delete shield;
}

void CCircuitUnit::SetTask(IUnitTask* task)
{
	this->task = task;
	SetTaskFrame(manager->GetCircuit()->GetLastFrame());
}

void CCircuitUnit::ClearAct()
{
	CActionList::Clear();
	dgunAct = nullptr;
	travelAct = nullptr;
}

void CCircuitUnit::PushDGunAct(CDGunAction* action)
{
	PushBack(action);
	dgunAct = action;
}

void CCircuitUnit::PushTravelAct(ITravelAction* action)
{
	PushBack(action);
	travelAct = action;
}

bool CCircuitUnit::IsMoveFailed(int frame)
{
	if (frame - failFrame >= FRAMES_PER_SEC * 3) {
		moveFails = 0;
	}
	failFrame = frame;
	return ++moveFails > TASK_RETRIES * 2;
}

bool CCircuitUnit::IsForceExecute(int frame)
{
	if (execFrame > 0) {
		if (execFrame <= frame) {
			execFrame = -1;
			return true;
		}
	}
	return false;
}

void CCircuitUnit::ManualFire(CEnemyInfo* target, int timeOut)
{
	TRY_UNIT(manager->GetCircuit(), this,
		if (circuitDef->HasDGun()) {
			if (target->GetUnit()->IsCloaked()) {  // los-cheat related
				unit->DGunPosition(target->GetPos(), UNIT_COMMAND_OPTION_ALT_KEY | UNIT_COMMAND_OPTION_CONTROL_KEY, timeOut);
			} else {
				unit->DGun(target->GetUnit(), UNIT_COMMAND_OPTION_ALT_KEY | UNIT_COMMAND_OPTION_CONTROL_KEY, timeOut);
			}
		} else {
			unit->MoveTo(target->GetPos() + target->GetVel() * FRAMES_PER_SEC * 2, UNIT_COMMAND_OPTION_ALT_KEY, timeOut);
			unit->ExecuteCustomCommand(CMD_ONECLICK_WEAPON, {}, UNIT_COMMAND_OPTION_SHIFT_KEY, timeOut);
		}
	)
}

bool CCircuitUnit::IsDisarmed(int frame)
{
	if (disarmFrame != frame) {
		disarmFrame = frame;
		isDisarmed = unit->GetRulesParamFloat("disarmed", 0) > 0.f;
	}
	return isDisarmed;
}

bool CCircuitUnit::IsWeaponReady(int frame)
{
	if (ammoFrame != frame) {
		ammoFrame = frame;
		if (circuitDef->IsPlane()) {
			isWeaponReady = unit->GetRulesParamFloat("noammo", 0) < 1.f;
		} else {
			isWeaponReady = (weapon == nullptr) ? false : weapon->GetReloadFrame() <= frame;
		}
	}
	return isWeaponReady;
}

bool CCircuitUnit::IsDGunReady(int frame)
{
	return dgun->GetReloadFrame() <= frame;
}

bool CCircuitUnit::IsShieldCharged(float percent)
{
	return shield->GetShieldPower() > circuitDef->GetMaxShield() * percent;
}

bool CCircuitUnit::IsJumpReady()
{
	return circuitDef->IsAbleToJump() && !(unit->GetRulesParamFloat("jumpReload", 1) < 1.f);
}

bool CCircuitUnit::IsJumping()
{
	return unit->GetRulesParamFloat("is_jumping", 0) > 0.f;
}

float CCircuitUnit::GetDamage()
{
	float dmg = circuitDef->GetPwrDamage();
	if (dmg < 1e-3f) {
		return 0.f;
	}
	if (unit->IsParalyzed() || IsDisarmed(manager->GetCircuit()->GetLastFrame())) {
		return 0.01f;
	}
	// TODO: Mind the slow down: dps * WeaponDef->GetReload / Weapon->GetReloadTime;
	return dmg;
}

float CCircuitUnit::GetShieldPower()
{
	if (shield != nullptr) {
		return shield->GetShieldPower();
	}
	return 0.f;
}

float CCircuitUnit::GetBuildSpeed()
{
	return circuitDef->GetBuildSpeed() * unit->GetRulesParamFloat("buildpower_mult", 1.f);
}

float CCircuitUnit::GetDGunRange()
{
	return dgun->GetRange() * unit->GetRulesParamFloat("comm_range_mult", 1.f);
}

float CCircuitUnit::GetHealthPercent()
{
	return unit->GetHealth() / unit->GetMaxHealth() - unit->GetCaptureProgress() * 16.f;
}

void CCircuitUnit::Attack(CEnemyInfo* enemy, int timeout)
{
	target = enemy;
	TRY_UNIT(manager->GetCircuit(), this,
		const AIFloat3& pos = enemy->GetPos();
		if (circuitDef->IsAttrMelee()) {
			if (IsJumpReady()) {
				unit->ExecuteCustomCommand(CMD_JUMP, {pos.x, pos.y, pos.z}, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
				unit->Attack(enemy->GetUnit(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
			} else {
				unit->MoveTo(enemy->GetPos(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
				unit->Attack(enemy->GetUnit(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
			}
		} else {
			unit->Attack(enemy->GetUnit(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
		}
		unit->Fight(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);  // los-cheat related
		unit->ExecuteCustomCommand(CMD_WANTED_SPEED, {NO_SPEED_LIMIT});
//		unit->ExecuteCustomCommand(CMD_UNIT_SET_TARGET, {(float)target->GetId()});
	)
}

void CCircuitUnit::Attack(const AIFloat3& position, int timeout)
{
	const AIFloat3& pos = utils::get_radial_pos(position, SQUARE_SIZE * 8);
	TRY_UNIT(manager->GetCircuit(), this,
		if (circuitDef->IsAttrMelee()) {
			if (IsJumpReady()) {
				unit->ExecuteCustomCommand(CMD_JUMP, {pos.x, pos.y, pos.z}, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
				unit->Fight(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
			} else {
				unit->MoveTo(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
			}
		} else {
			unit->Fight(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
		}
		unit->ExecuteCustomCommand(CMD_WANTED_SPEED, {NO_SPEED_LIMIT});
	)
}

void CCircuitUnit::Attack(const AIFloat3& position, CEnemyInfo* enemy, int timeout)
{
	const AIFloat3& pos = utils::get_radial_pos(position, SQUARE_SIZE * 8);
	TRY_UNIT(manager->GetCircuit(), this,
		if (circuitDef->IsAttrMelee()) {
			if (IsJumpReady()) {
				unit->ExecuteCustomCommand(CMD_JUMP, {pos.x, pos.y, pos.z}, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
				unit->Fight(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
			} else {
				unit->MoveTo(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
			}
		} else {
			unit->Fight(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
		}
		unit->Attack(enemy->GetUnit(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
		unit->Fight(position, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);  // los-cheat related
		unit->ExecuteCustomCommand(CMD_WANTED_SPEED, {NO_SPEED_LIMIT});
//		unit->ExecuteCustomCommand(CMD_UNIT_SET_TARGET, {(float)target->GetId()});
	)
}

void CCircuitUnit::Attack(const AIFloat3& position, CEnemyInfo* enemy, int tile, int timeout)
{
	target = enemy;
	targetTile = tile;
	Attack(position, enemy, timeout);
}

void CCircuitUnit::Guard(CCircuitUnit* target, int timeout)
{
	TRY_UNIT(manager->GetCircuit(), this,
		unit->ExecuteCustomCommand(CMD_ORBIT, {(float)target->GetId(), 300.0f}, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
//		unit->Guard(target->GetUnit(), UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
//		unit->ExecuteCustomCommand(CMD_WANTED_SPEED, {NO_SPEED_LIMIT});
	)
}

void CCircuitUnit::Gather(const AIFloat3& groupPos, int timeout)
{
	const AIFloat3& pos = utils::get_radial_pos(groupPos, SQUARE_SIZE * 8);
	TRY_UNIT(manager->GetCircuit(), this,
		unit->MoveTo(groupPos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY, timeout);
		unit->ExecuteCustomCommand(CMD_WANTED_SPEED, {NO_SPEED_LIMIT});
		unit->PatrolTo(pos, UNIT_COMMAND_OPTION_RIGHT_MOUSE_KEY | UNIT_COMMAND_OPTION_SHIFT_KEY, timeout);
	)
}

void CCircuitUnit::Morph()
{
	isMorphing = true;
	TRY_UNIT(manager->GetCircuit(), this,
		unit->ExecuteCustomCommand(CMD_MORPH, {});
		unit->ExecuteCustomCommand(CMD_MISC_PRIORITY, {1.0f});
	)
}

void CCircuitUnit::StopMorph()
{
	isMorphing = false;
	TRY_UNIT(manager->GetCircuit(), this,
		unit->ExecuteCustomCommand(CMD_MORPH_STOP, {});
		unit->ExecuteCustomCommand(CMD_MISC_PRIORITY, {1.0f});
	)
}

bool CCircuitUnit::IsUpgradable()
{
	unsigned level = unit->GetRulesParamFloat("comm_level", 0.f);
	assert(manager != nullptr);
	return manager->GetCircuit()->GetSetupManager()->HasModules(circuitDef, level);
}

void CCircuitUnit::Upgrade()
{
	isMorphing = true;
	/*
	 * @see
	 * gui_chili_commander_upgrade.lua
	 * unit_morph.lua
	 * unit_commander_upgrade.lua
	 * dynamic_comm_defs.lua
	 *
	 * Level = params[1]
	 * Chassis = params[2]
	 * AlreadyCount = params[3]
	 * NewCount = params[4]
	 * OwnedModules = params[5..N]
	 * NewModules = params[N+1..M]
	 */

	float level = unit->GetRulesParamFloat("comm_level", 0.f);
	float chassis = unit->GetRulesParamFloat("comm_chassis", 0.f);
	float alreadyCount = unit->GetRulesParamFloat("comm_module_count", 0.f);

	assert(manager != nullptr);
	const std::vector<float>& newModules = manager->GetCircuit()->GetSetupManager()->GetModules(circuitDef, level);

	std::vector<float> upgrade;
	upgrade.push_back(level);
	upgrade.push_back(chassis);
	upgrade.push_back(alreadyCount);
	upgrade.push_back(newModules.size());

	for (int i = 1; i <= alreadyCount; ++i) {
		std::string modId = utils::int_to_string(i, "comm_module_%i");
		float value = unit->GetRulesParamFloat(modId.c_str(), -1.f);
		if (value != -1.f) {
			upgrade.push_back(value);
		}
	}

	upgrade.insert(upgrade.end(), newModules.begin(), newModules.end());

	TRY_UNIT(manager->GetCircuit(), this,
		unit->ExecuteCustomCommand(CMD_MORPH_UPGRADE_INTERNAL, upgrade);
		unit->ExecuteCustomCommand(CMD_MISC_PRIORITY, {1.0f});
	)
}

void CCircuitUnit::StopUpgrade()
{
	isMorphing = false;
	TRY_UNIT(manager->GetCircuit(), this,
		unit->ExecuteCustomCommand(CMD_UPGRADE_STOP, {});
		unit->ExecuteCustomCommand(CMD_MISC_PRIORITY, {1.0f});
	)
}

#ifdef DEBUG_VIS
void CCircuitUnit::Log()
{
	if (task != nullptr) {
		task->Log();
	}
	CCircuitAI* circuit = manager->GetCircuit();
	auto commands = unit->GetCurrentCommands();
	for (const springai::Command* c : commands) {
		circuit->LOG("command: %i", c->GetCommandId());
	}
	utils::free_clear(commands);
}
#endif

} // namespace circuit
