void AiOpenStrategy(const CCircuitDef@ facDef, const AIFloat3& in pos)
{
}

/*
 * struct ResourceInfo {
 *   const float current;
 *   const float storage;
 *   const float pull;
 *   const float income;
 * }
 */
void AiUpdateEconomy()
{
	const ResourceInfo@ metal = aiEconomyMgr.metal;
	const ResourceInfo@ energy = aiEconomyMgr.energy;
	aiEconomyMgr.isMetalEmpty = metal.current < metal.storage * 0.2f;
	aiEconomyMgr.isMetalFull = metal.current > metal.storage * 0.8f;
	aiEconomyMgr.isEnergyEmpty = energy.current < energy.storage * 0.2f;
//	aiEconomyMgr.isEnergyStalling = aiMin(metal.income - metal.pull, .0f)/* * 0.98f*/ > aiMin(energy.income - energy.pull, .0f);
	const float percent = (ai.lastFrame < 3 * 60 * 30) ? 0.2f : 0.6f;
	aiEconomyMgr.isEnergyStalling = aiEconomyMgr.isEnergyEmpty || ((energy.income < energy.pull) && (energy.current < energy.storage * percent));
}