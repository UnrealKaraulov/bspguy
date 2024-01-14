#pragma once
#include "Keyvalue.h"
#include <map>

typedef std::map< std::string, std::string > hashmap;

class Entity
{
public:
	hashmap keyvalues;
	std::vector<std::string> keyOrder;

	int cachedModelIdx; // -2 = not cached
	std::vector<std::string> cachedTargets;
	bool targetsCached;
	bool hide;

	Entity()
	{
		cachedModelIdx = -2;
		targetsCached = false;
		rendermode = kRenderNormal;
		renderamt = 0;
		renderfx = kRenderFxNone;
		rendercolor = vec3(1.0f, 1.0f, 1.0f);
		origin = vec3(0.0f, 0.0f, 0.0f);
		originInited = false;
		targetsCached = false;
		hide = false;
	}

	Entity(const std::string& classname)
	{
		cachedModelIdx = -2;
		targetsCached = false;
		rendermode = kRenderNormal;
		renderamt = 0;
		renderfx = kRenderFxNone;
		rendercolor = vec3(1.0f, 1.0f, 1.0f);
		originInited = false;
		originInited = false;
		targetsCached = false;
		setOrAddKeyvalue("classname", classname);
	}

	~Entity(void)
	{
		cachedTargets.clear();
		keyOrder.clear();
		keyvalues.clear();
	}

	void addKeyvalue(const std::string key, const std::string value, bool multisupport = false);
	void removeKeyvalue(const std::string key);
	bool renameKey(int idx, const std::string& newName);
	bool renameKey(const std::string& oldName, const std::string& newName);
	void clearAllKeyvalues();
	void clearEmptyKeyvalues();

	void setOrAddKeyvalue(const std::string key, const std::string value);

	// returns -1 for invalid idx
	int getBspModelIdx();
	int getBspModelIdxForce();

	bool isBspModel();

	bool isWorldSpawn();

	vec3 getOrigin();

	bool hasKey(const std::string key);

	std::vector<std::string> getTargets();

	bool hasTarget(const std::string& checkTarget);

	void renameTargetnameValues(const std::string& oldTargetname, const std::string& newTargetname);

	void updateRenderModes();

	size_t getMemoryUsage(); // aproximate

	bool originInited = false;
	vec3 origin = vec3(0.0f, 0.0f, 0.0f);
	int rendermode = kRenderNormal;
	int renderamt = 0;
	int renderfx = kRenderFxNone;
	vec3 rendercolor = vec3(1.0f, 1.0f, 1.0f);
};

