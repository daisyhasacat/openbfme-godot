// OpenBFME. GPL-3.0.
// ObjectModule and DrawableModule constructors (the runtime half of Common/Module.h).

#include "Common/Module.h"

#include "Common/Thing/Thing.h"
#include "Common/Xfer.h"

ObjectModule::ObjectModule(Thing *thing, const ModuleData *moduleData)
	: Module(moduleData)
	, m_object(thing ? thing->asObject() : nullptr)
{
}

DrawableModule::DrawableModule(Thing *thing, const ModuleData *moduleData)
	: Module(moduleData)
	, m_drawable(thing ? thing->asDrawable() : nullptr)
{
}

// ---- lane SAVE-4b: the module snapshot (see Common/Module.h) ----------------------------------------------------------------------------------------
const std::string &Module::getModuleTagName() const
{
	static const std::string none;
	return m_moduleData ? m_moduleData->getModuleTagName() : none;
}

void Module::xfer(Xfer &x)
{
	(void)x;
	throw XferException("the module class " + (m_className.empty() ? std::string(typeid(*this).name()) : m_className) + " (tag '" + getModuleTagName()
		+ "') has no xfer");
}

void Module::xferModule(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
}

void ObjectModule::xferObjectModule(Xfer &x)
{
	XferVersion v{ 1, 1 }; // RW 0x7082E2
	x.xferVersion(v);
	xferModule(x);
}
