#pragma once
#include "util.h"
#include "Wad.h"
#include "Entity.h"

enum FGD_CLASS_TYPES : int
{
	FGD_CLASS_BASE = 0,
	FGD_CLASS_SOLID,
	FGD_CLASS_POINT
};

enum FGD_KEY_TYPES : int
{
	FGD_KEY_INTEGER = 0,
	FGD_KEY_STRING,
	FGD_KEY_CHOICES,
	FGD_KEY_FLAGS,
	FGD_KEY_RGB,
	FGD_KEY_STUDIO,
	FGD_KEY_SOUND,
	FGD_KEY_SPRITE,
	FGD_KEY_TARGET_SRC,
	FGD_KEY_TARGET_DST
};

// for both "choice" and "flags" keyvalue types
struct KeyvalueChoice
{
	std::string name;
	std::string svalue;
	std::string sdefvalue;
	int ivalue;
	bool isInteger;
	std::string fullDescription;
};

struct KeyvalueDef
{
	std::string name;
	std::string valueType;
	int iType;
	std::string shortDescription;
	std::string fullDescription;
	std::string defaultValue;
	std::vector<KeyvalueChoice> choices;
};

class Fgd;

struct FgdClass
{
	int classType;
	std::string name;
	std::string description;
	std::vector<KeyvalueDef> keyvalues;
	std::vector<std::string> baseClasses;
	std::string spawnFlagNames[32];
	std::string spawnFlagDescriptions[32];
	std::string model;
	std::string sprite;
	bool isModel;
	bool isSprite;
	bool isDecal;
	bool hasAngles;
	int modelSequence;
	int modelSkin;
	int modelBody;
	vec3 mins;
	vec3 maxs;
	COLOR3 color;
	vec3 offset;
	hashmap otherTypes; // unrecognized types

	// if false, then need to get props from the base class
	bool colorSet;
	bool sizeSet;

	FgdClass()
	{
		classType = FGD_CLASS_POINT;
		name = "";
		model = "";
		isSprite = false;
		isModel = false;
		isDecal = false;
		colorSet = false;
		sizeSet = false;
		hasAngles = false;
		// default to the purple cube
		mins = vec3(-8, -8, -8);
		maxs = vec3(8, 8, 8);
		color = { 220, 0, 220 };
		offset = vec3();
		modelSkin = modelBody = modelSequence = 0;
	}

	// get parent classes from youngest to oldest, in right-to-left order
	// reversing the std::vector changes order to oldest to youngest, left-to-right order
	void getBaseClasses(Fgd* fgd, std::vector<FgdClass*>& inheritanceList);
};

struct FgdGroup
{
	std::vector<FgdClass*> classes;
	std::string groupName;
};

class Fgd
{
public:
	std::string path;
	std::string name;
	std::vector<FgdClass*> classes;

	std::vector<FgdGroup> pointEntGroups;
	std::vector<FgdGroup> solidEntGroups;

	std::vector<std::string> existsFlagNames;
	std::vector<int> existsFlagNamesBits;


	Fgd(std::string path);
	Fgd() = default;
	~Fgd();

	bool parse();
	void merge(Fgd* other);

	FgdClass* getFgdClass(const std::string& cname);
	FgdClass* getFgdClass(const std::string& cname, int type);

private:
	int lineNum = 0;
	std::string line; // current line being parsed

	void parseClassHeader(FgdClass& fgdClass);
	void parseKeyvalue(FgdClass& outClass);
	void parseChoicesOrFlags(KeyvalueDef& outKey);

	void processClassInheritance();

	void createEntGroups();
	void setSpawnflagNames();
};
