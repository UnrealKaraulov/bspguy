#include "Entity.h"
#include "util.h"
#include "Bsp.h"
#include <set>

void Entity::addKeyvalue(const std::string & key, const std::string & value, bool multisupport)
{
	if (!nullstrlen(key))
		return;

	int dup = 1;
	if (keyvalues.find(key) == keyvalues.end())
	{
		keyvalues[key] = value;
	}
	else if (multisupport)
	{
		while (true)
		{
			std::string newKey = key + "#" + std::to_string(dup);
			if (keyvalues.find(newKey) == keyvalues.end())
			{
				keyvalues[newKey] = value;
				break;
			}
			dup++;
		}
	}
	else
		keyvalues[key] = value;

	dup = 1;
	if (std::find(keyOrder.begin(), keyOrder.end(), key) == keyOrder.end())
	{
		keyOrder.push_back(key);
	}
	else if (multisupport)
	{
		while (true)
		{
			std::string newKey = key + "#" + std::to_string(dup);
			if (std::find(keyOrder.begin(), keyOrder.end(), newKey) == keyOrder.end())
			{
				keyOrder.push_back(newKey);
				break;
			}
			dup++;
		}
	}

	targetsCached = false;

	if (key == "origin")
		origin = parseVector(value);
	if (key == "classname")
		classname = value;
	if (key == "model")
		cachedModelIdx = -2;

	if (starts_with(key,"render"))
		updateRenderModes();
}

void Entity::setOrAddKeyvalue(const std::string& key, const std::string& value)
{
	if (!key.size())
		return;

	addKeyvalue(key, value);
}

void Entity::removeKeyvalue(const std::string& key)
{
	if (!key.size())
		return;

	if (key == "origin")
		origin = vec3();
	if (key == "classname")
		classname = "";
	if (key == "model")
		cachedModelIdx = -2;


	if (std::find(keyOrder.begin(), keyOrder.end(), key) != keyOrder.end())
		keyOrder.erase(std::find(keyOrder.begin(), keyOrder.end(), key));

	keyvalues.erase(key);

	targetsCached = false;

	if (starts_with(key,"render"))
		updateRenderModes();
}

bool Entity::renameKey(int idx, const std::string& newName)
{
	if (idx < 0 || idx >= (int)keyOrder.size() || newName.empty())
	{
		return false;
	}

	std::string keyName = keyOrder[idx];

	for (size_t i = 0; i < keyOrder.size(); i++)
	{
		if (keyOrder[i] == newName)
		{
			return false;
		}
	}

	if (starts_with(keyName,"render"))
		updateRenderModes();

	if (keyName == "origin" || newName == "origin")
	{
		if (keyName == "origin")
		{
			origin = vec3();
		}
		else
		{
			origin = parseVector(keyvalues[keyOrder[idx]]);
		}
	}

	keyvalues[newName] = keyvalues[keyOrder[idx]];
	keyvalues.erase(keyOrder[idx]);
	keyOrder[idx] = newName;

	if (keyName == "model" || newName == "model")
	{
		cachedModelIdx = -2;
	}
	targetsCached = false;


	if (keyName == "classname" && newName != "classname")
		classname = "";
	else if (newName == "classname" && keyName != "classname")
		classname = keyvalues["classname"];

	return true;
}

bool Entity::renameKey(const std::string& oldName, const std::string& newName)
{
	if (oldName.empty() || newName.empty())
	{
		return false;
	}

	int idx = -1;
	for (size_t i = 0; i < keyOrder.size(); i++)
	{
		if (keyOrder[i] == newName)
		{
			return false;
		}
		else if (keyOrder[i] == oldName)
		{
			idx = (int)i;
		}
	}

	if (idx == -1)
		return false;

	if (starts_with(oldName,"render") || starts_with(newName,"render"))
		updateRenderModes();

	if (oldName == "origin" || newName == "origin")
	{
		if (oldName == "origin")
		{
			origin = vec3();
		}
		else
		{
			origin = parseVector(keyvalues[keyOrder[idx]]);
		}
	}

	keyvalues[newName] = keyvalues[keyOrder[idx]];
	keyvalues.erase(keyOrder[idx]);
	keyOrder[idx] = newName;

	if (oldName == "model" || newName == "model")
	{
		cachedModelIdx = -2;
	}

	targetsCached = false;


	if (oldName == "classname" && newName != "classname")
		classname = "";
	else if (newName == "classname" && oldName != "classname")
		classname = keyvalues["classname"];


	return true;
}

void Entity::clearAllKeyvalues()
{
	keyOrder.clear();
	keyvalues.clear();
	cachedModelIdx = -2;
	targetsCached = false;
}

void Entity::clearEmptyKeyvalues()
{
	std::vector<std::string> newKeyOrder;
	for (size_t i = 0; i < keyOrder.size(); i++)
	{
		if (!keyvalues[keyOrder[i]].empty())
		{
			newKeyOrder.push_back(keyOrder[i]);
		}
	}
	keyOrder = std::move(newKeyOrder);
}

bool Entity::hasKey(const std::string& key)
{
	if (keyvalues.empty() || keyOrder.empty()) {
		return false; 
	}
	return keyvalues.find(key) != keyvalues.end() && std::find(keyOrder.begin(), keyOrder.end(), key) != keyOrder.end();
}

int Entity::getBspModelIdx()
{
	if (cachedModelIdx != -2)
	{
		return cachedModelIdx;
	}

	if (hasKey("classname") && keyvalues["classname"] == "worldspawn")
	{
		cachedModelIdx = 0;
		return 0;
	}

	if (!hasKey("model"))
	{
		cachedModelIdx = -1;
		return -1;
	}

	std::string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*')
	{
		cachedModelIdx = -1;
		return -1;
	}

	std::string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr))
	{
		cachedModelIdx = -1;
		return -1;
	}
	cachedModelIdx = str_to_int(modelIdxStr);
	return cachedModelIdx;
}

int Entity::getBspModelIdxForce()
{
	if (hasKey("classname") && keyvalues["classname"] == "worldspawn")
	{
		return 0;
	}

	if (!hasKey("model"))
	{
		return -1;
	}

	std::string model = keyvalues["model"];
	if (model.size() <= 1 || model[0] != '*')
	{
		return -1;
	}

	std::string modelIdxStr = model.substr(1);
	if (!isNumeric(modelIdxStr))
	{
		return -1;
	}
	return str_to_int(modelIdxStr);
}

bool Entity::isBspModel()
{
	return getBspModelIdx() >= 0;
}

bool Entity::isWorldSpawn()
{
	return getBspModelIdx() == 0;
}

std::vector<std::string> potential_tergetname_keys = {
	// common target-related keys
	"targetname",
	"target",
	"killtarget",
	"master",
	"netname",
	"message", // not always an entity, but unlikely a .wav file or something will match an entity name

	// monster_* and monster spawners
	"TriggerTarget",
	"path_name",
	"guard_ent",
	"trigger_target",
	"xenmaker",
	"squadname",

	// OpenClosable
	"fireonopening",
	"fireonclosing",
	"fireonopened",
	"fireonclosed",

	// breakables
	"fireonbreak",

	// Trackchange
	"train",
	"toptrack",
	"bottomtrack",

	// scripted sequences
	"m_iszEntity",
	"entity",
	//"listener", // TODO: what is this?

	// BaseCharger
	"TriggerOnEmpty",
	"TriggerOnRecharged",

	// Beams
	"LightningStart",
	"LightningEnd",
	"LaserTarget",
	"laserentity",

	// func_rot_button
	"changetarget",

	// game_zone_player
	"intarget",
	"outtarget",

	// info_bigmomma
	"reachtarget",
	"presequence",

	// info_monster_goal
	"enemy",

	// path_condition_controller
	"conditions_reference",
	"starttrigger",
	// TODO: support lists of targetnames
	//"pathcondition_list",
	//"waypoint_list",
	//"m_szASConditionsName", // TODO: what is this?

	// path_waypoint
	"alternate_target",
	"trigger_on_arrival",
	"trigger_after_arrival",
	"wait_master",
	"trigger_on_departure",
	"overflow_waypoint",
	"stop_trigger",

	// path_track
	"altpath",

	// trigger_camera + trigger_cameratarget
	"moveto",
	// TODO: parameters are not always entities(?)
	"mouse_param_0_0",
	"mouse_param_0_1",
	"mouse_param_1_0",
	"mouse_param_1_1",
	"mouse_param_2_0",
	"mouse_param_2_1",
	"m_iszOverridePlayerTargetname",
	"m_iszTargetWhenPlayerStartsUsing",
	"m_iszTargetWhenPlayerStopsUsing",
	"m_iszTurnedOffTarget",
	"max_player_target",
	"mouse_target_0_0",
	"mouse_target_0_1",
	"mouse_target_1_0",
	"mouse_target_1_1",
	"mouse_target_2_0",
	"mouse_target_2_1",

	// trigger_changelevel
	"changetarget",

	// trigger_changetarget
	"m_iszNewTarget",

	// trigger_condition
	"m_iszSourceName",

	// trigger_createentity
	"m_iszCrtEntChildName",
	"m_iszTriggerAfter", // commented out in FGD for some reason? Think I've used it before.

	// trigger_endsection
	"section", // TODO: what is this?

	// trigger_entity_iterator
	"name_filter",
	"trigger_after_run",

	// trigger_load/save
	"m_iszTrigger",

	// BaseRandom
	"target1",
	"target2",
	"target3",
	"target4",
	"target5",
	"target6",
	"target7",
	"target8",
	"target9",
	"target10",
	"target11",
	"target12",
	"target13",
	"target14",
	"target15",
	"target16",

	// trigger_setorigin
	"copypointer",

	"noise",

	// weapon_displacer
	"m_iszTeleportDestination",

	// item_inventory
	"item_name",
	"item_group",
	"filter_targetnames",
	"item_name_moved",
	"item_name_not_moved",
	"target_on_collect",
	"target_on_collect_team",
	"target_on_collect_other",
	"target_cant_collect",
	"target_cant_collect_team",
	"target_cant_collect_other",
	"target_on_drop",
	"target_on_drop_team",
	"target_on_drop_other",
	"target_cant_drop",
	"target_cant_drop_team",
	"target_cant_drop_other",
	"target_on_activate",
	"target_on_activate_team",
	"target_on_activate_other",
	"target_cant_activate",
	"target_cant_activate_team",
	"target_cant_activate_other",
	"target_on_use",
	"target_on_use_team",
	"target_on_use_other",
	"target_on_wearing_out",
	"target_on_wearing_out_team",
	"target_on_wearing_out_other",
	"target_on_return",
	"target_on_return_team",
	"target_on_return_other",
	"target_on_materialise",
	"target_on_destroy",

	// inventory rules
	"item_name_required",
	"item_group_required",
	"item_name_canthave",
	"item_group_canthave",
	"pass_drop_item_name",
	"pass_drop_item_group",
	"pass_return_item_name",
	"pass_return_item_group",
	"pass_destroy_item_name",
	"pass_destroy_item_group"
};

// This needs to be kept in sync with the FGD

std::vector<std::string> Entity::getTargets()
{
	if (targetsCached)
	{
		return cachedTargets;
	}

	std::vector<std::string> targets;

	for (size_t i = 1; i < potential_tergetname_keys.size(); i++)
	{ 
		// skip targetname
		auto & key = potential_tergetname_keys[i];
		if (hasKey(key))
		{
			targets.push_back(keyvalues[key]);
		}
	}

	if (keyvalues["classname"] == "multi_manager")
	{
		// multi_manager is a special case where the targets are in the key names
		for (size_t i = 0; i < keyOrder.size(); i++)
		{
			std::string tname = keyOrder[i];
			size_t hashPos = tname.find('#');
			// std::string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != std::string::npos)
			{
				tname = tname.substr(0, hashPos);
			}
			targets.push_back(tname);
		}
	}

	cachedTargets.clear();
	cachedTargets.reserve(targets.size());
	for (size_t i = 0; i < targets.size(); i++)
	{
		cachedTargets.push_back(targets[i]);
	}
	targetsCached = true;

	return targets;
}

bool Entity::hasTarget(const std::string& checkTarget)
{
	std::vector<std::string> targets = getTargets();
	for (size_t i = 0; i < targets.size(); i++)
	{
		if (targets[i] == checkTarget)
		{
			return true;
		}
	}

	return false;
}

void Entity::renameTargetnameValues(const std::string& oldTargetname, const std::string& newTargetname)
{
	for (size_t i = 0; i < potential_tergetname_keys.size(); i++)
	{
		auto & key = potential_tergetname_keys[i];
		if (keyvalues.find(key) != keyvalues.end() && keyvalues[key] == oldTargetname)
		{
			keyvalues[key] = newTargetname;
		}
	}

	if (keyvalues["classname"] == "multi_manager")
	{
		// multi_manager is a special case where the targets are in the key names
		for (size_t i = 0; i < keyOrder.size(); i++)
		{
			std::string tname = keyOrder[i];
			size_t hashPos = tname.find('#');
			std::string suffix;

			// duplicate targetnames have a #X suffix to differentiate them
			if (hashPos != std::string::npos)
			{
				tname = keyOrder[i].substr(0, hashPos);
				suffix = keyOrder[i].substr(hashPos);
			}

			if (tname == oldTargetname)
			{
				std::string newKey = newTargetname + suffix;
				keyvalues[newKey] = keyvalues[keyOrder[i]];
				keyOrder[i] = std::move(newKey);
			}
		}
	}
}

size_t Entity::getMemoryUsage()
{
	size_t size = sizeof(Entity);

	for (size_t i = 0; i < cachedTargets.size(); i++)
	{
		size += cachedTargets[i].size();
	}
	for (size_t i = 0; i < keyOrder.size(); i++)
	{
		size += keyOrder[i].size();
	}
	for (const auto& entry : keyvalues)
	{
		size += entry.first.size() + entry.second.size();
	}

	return size;
}

vec3 Entity::getHullOrigin(Bsp* map) {
	vec3 ori = origin;
	int modelIdx = getBspModelIdx();

	if (modelIdx != -1) {
		vec3 mins, maxs;
		map->get_model_vertex_bounds(modelIdx, mins, maxs);
		ori += (maxs + mins) * 0.5f;
	}

	return ori;
}

void Entity::updateRenderModes()
{
	rendermode = kRenderNormal;
	if (hasKey("rendermode"))
	{
		rendermode = str_to_int(keyvalues["rendermode"]);
	}
	renderamt = 0;
	if (hasKey("renderamt"))
	{
		renderamt = str_to_int(keyvalues["renderamt"]);
	}
	renderfx = kRenderFxNone;
	if (hasKey("renderfx"))
	{
		renderfx = str_to_int(keyvalues["renderfx"]);
	}
	rendercolor = vec3(1.0, 1.0, 1.0);
	if (hasKey("rendercolor"))
	{
		vec3 color = parseVector(keyvalues["rendercolor"]);
		rendercolor = vec3(color[0] / 255.f, color[1] / 255.f, color[2] / 255.f);
	}
}

bool Entity::isEverVisible() {
	std::string cname = keyvalues["classname"];
	std::string tname = hasKey("targetname") ? keyvalues["targetname"] : "";

	static std::set<std::string> invisibleEnts = {
		"env_bubbles",
		"func_clip",
		"func_friction",
		"func_ladder",
		"func_monsterclip",
		"func_mortar_field",
		"func_op4mortarcontroller",
		"func_tankcontrols",
		"func_traincontrols",
		"trigger_autosave",
		"trigger_cameratarget",
		"trigger_cdaudio",
		"trigger_changelevel",
		"trigger_counter",
		"trigger_endsection",
		"trigger_gravity",
		"trigger_hurt",
		"trigger_monsterjump",
		"trigger_multiple",
		"trigger_once",
		"trigger_push",
		"trigger_teleport",
		"trigger_transition",
		"game_zone_player",
		"info_hullshape",
		"player_respawn_zone",
	};

	if (invisibleEnts.count(cname)) {
		return false;
	}

	if (!tname.length() && hasKey("rendermode") && atoi(keyvalues["rendermode"].c_str()) != 0) {
		if (!hasKey("renderamt") || atoi(keyvalues["renderamt"].c_str()) == 0) {
			// starts invisible and likely nothing will change that because it has no targetname
			return false;
		}
	}

	return true;
}

std::string Entity::serialize() 
{
	std::stringstream ent_data;

	ent_data << "{\n";

	for (int k = 0; k < keyOrder.size(); k++) {
		std::string key = keyOrder[k];
		ent_data << "\"" << key << "\" \"" << keyvalues[key] << "\"\n";
	}

	ent_data << "}\n";

	return ent_data.str();
}

