/*
 * EconomyScript.h
 *
 *  Created on: Apr 19, 2019
 *      Author: rlcevg
 */

#ifndef SRC_CIRCUIT_SCRIPT_ECONOMYSCRIPT_H_
#define SRC_CIRCUIT_SCRIPT_ECONOMYSCRIPT_H_

#include "script/ModuleScript.h"

class asIScriptFunction;

namespace springai {
	class AIFloat3;
}

namespace circuit {

class CEconomyManager;
class CCircuitDef;

class CEconomyScript: public IModuleScript {
public:
	CEconomyScript(CScriptManager* scr, CEconomyManager* mgr);
	virtual ~CEconomyScript();

	void Init() override;

public:
	void OpenStrategy(const CCircuitDef* facDef, const springai::AIFloat3& pos);

private:
	struct SScriptInfo {
		asIScriptFunction* openStrategy = nullptr;
	} info;
};

} // namespace circuit

#endif // SRC_CIRCUIT_SCRIPT_ECONOMYSCRIPT_H_
