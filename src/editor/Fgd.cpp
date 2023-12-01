#include "lang.h"
#include "Fgd.h"
#include <set>

std::map<std::string, int> fgdKeyTypes{
	{"integer", FGD_KEY_INTEGER},
	{"choices", FGD_KEY_CHOICES},
	{"flags", FGD_KEY_FLAGS},
	{"color255", FGD_KEY_RGB},
	{"studio", FGD_KEY_STUDIO},
	{"sound", FGD_KEY_SOUND},
	{"sprite", FGD_KEY_SPRITE},
	{"target_source", FGD_KEY_TARGET_SRC},
	{"target_destination", FGD_KEY_TARGET_DST}
};

Fgd::Fgd(std::string _path)
{
	this->path = _path;
	this->name = stripExt(basename(path));
	this->lineNum = 0;
}

Fgd::~Fgd()
{
	for (int i = 0; i < classes.size(); i++)
	{
		delete classes[i];
	}
}

FgdClass* Fgd::getFgdClass(std::string cname)
{
	if (classMap.size() && cname.size() && classMap.find(cname) != classMap.end())
	{
		return classMap[cname];
	}
	return NULL;
}

void Fgd::merge(Fgd* other)
{
	if (path.empty() && other->path.size())
	{
		this->path = other->path;
		this->name = stripExt(basename(path));
		this->lineNum = 0;
	}

	for (auto it : other->classMap)
	{
		std::string className = it.first;
		FgdClass* fgdClass = it.second;

		if (classMap.find(className) != classMap.end())
		{
			logf(get_localized_string(LANG_0299),className,other->name);
			continue;
		}

		FgdClass* newClass = new FgdClass();
		*newClass = *fgdClass;

		classes.push_back(newClass);
		classMap[className] = newClass;
	}
}

bool Fgd::parse()
{
	if (!fileExists(path))
	{
		return false;
	}

	logf(get_localized_string(LANG_0300),path);

	std::ifstream in(path);

	lineNum = 0;

	FgdClass* fgdClass = new FgdClass();
	int bracketNestLevel = 0;

	line.clear();
	while (getline(in, line))
	{
		lineNum++;

		// strip comments
		size_t cpos = line.find("//");
		if (cpos != std::string::npos)
			line = line.substr(0, cpos);

		line = trimSpaces(line);

		if (line.empty())
			continue;

		if (line[0] == '@')
		{
			if (bracketNestLevel)
			{
				logf(get_localized_string(LANG_0301),lineNum,name);
			}

			parseClassHeader(*fgdClass);
		}

		if (line.find('[') != std::string::npos)
		{
			bracketNestLevel++;
		}
		if (line.find(']') != std::string::npos)
		{
			bracketNestLevel--;
			if (bracketNestLevel == 0)
			{
				classes.push_back(fgdClass);
				fgdClass = new FgdClass(); //memory leak
			}
		}

		if (line == "[" || line == "]")
		{
			continue;
		}

		if (bracketNestLevel == 1)
		{
			parseKeyvalue(*fgdClass);
		}

		if (bracketNestLevel == 2)
		{
			if (fgdClass->keyvalues.empty())
			{
				logf(get_localized_string(LANG_0302),lineNum,name);
				continue;
			}
			KeyvalueDef& lastKey = fgdClass->keyvalues[fgdClass->keyvalues.size() - 1];
			parseChoicesOrFlags(lastKey);
		}
	}

	processClassInheritance();
	createEntGroups();
	setSpawnflagNames();
	return true;
}

void Fgd::parseClassHeader(FgdClass& fgdClass)
{
	std::vector<std::string> headerParts = splitString(line, "=");

	if (headerParts.empty())
	{
		logf(get_localized_string(LANG_0303),lineNum,name);
		return;
	}

	// group parts enclosed in parens or quotes
	std::vector<std::string> typeParts = splitString(trimSpaces(headerParts[0]), " ");
	typeParts = groupParts(typeParts);

	std::string classType = toLowerCase(typeParts[0]);

	if (classType == "@baseclass")
	{
		fgdClass.classType = FGD_CLASS_BASE;
	}
	else if (classType == "@solidclass")
	{
		fgdClass.classType = FGD_CLASS_SOLID;
	}
	else if (classType == "@pointclass")
	{
		fgdClass.classType = FGD_CLASS_POINT;
	}
	else
	{
		logf(get_localized_string(LANG_0304),typeParts[0],name);
	}

	// parse constructors/properties
	for (int i = 1; i < typeParts.size(); i++)
	{
		std::string lpart = toLowerCase(typeParts[i]);

		if (lpart.starts_with("base("))
		{
			std::vector<std::string> baseClassList = splitString(getValueInParens(typeParts[i]), ",");
			for (int k = 0; k < baseClassList.size(); k++)
			{
				std::string baseClass = trimSpaces(baseClassList[k]);
				fgdClass.baseClasses.push_back(baseClass);
			}
		}
		else if (lpart.starts_with("size("))
		{
			std::vector<std::string> sizeList = splitString(getValueInParens(typeParts[i]), ",");

			if (sizeList.size() == 1)
			{
				vec3 size = parseVector(sizeList[0]);
				fgdClass.mins = size * -0.5f;
				fgdClass.maxs = size * 0.5f;
			}
			else if (sizeList.size() == 2)
			{
				fgdClass.mins = parseVector(sizeList[0]);
				fgdClass.maxs = parseVector(sizeList[1]);
			}
			else
			{
				logf(get_localized_string(LANG_0305),lineNum,name);
			}

			fgdClass.sizeSet = true;
		}
		else if (lpart.starts_with("color("))
		{
			std::vector<std::string> nums = splitString(getValueInParens(typeParts[i]), " ");

			if (nums.size() == 3)
			{
				fgdClass.color = {(unsigned char)atoi(nums[0].c_str()), (unsigned char)atoi(nums[1].c_str()), (unsigned char)atoi(nums[2].c_str())};
			}
			else
			{
				logf(get_localized_string(LANG_0306),lineNum,name);
			}
			fgdClass.colorSet = true;
		}
		else if (lpart.starts_with("studio("))
		{
			std::string mdlpath = getValueInParens(typeParts[i]);
			if (mdlpath.size())
			{
				fgdClass.model = mdlpath;
				fixupPath(fgdClass.model, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
			}
			fgdClass.isModel = true;
		}
		else if (lpart.starts_with("sequence("))
		{
			fgdClass.modelSequence = atoi(getValueInParens(typeParts[i]).c_str());
		}
		else if (lpart.starts_with("body("))
		{
			fgdClass.modelBody = atoi(getValueInParens(typeParts[i]).c_str());
		}
		else if (lpart.starts_with("iconsprite("))
		{
			fgdClass.iconSprite = getValueInParens(typeParts[i]);
			fixupPath(fgdClass.iconSprite, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
		}
		else if (lpart.starts_with("sprite("))
		{
			fgdClass.sprite = getValueInParens(typeParts[i]);
			fgdClass.isSprite = true;
		}
		else if (lpart.starts_with("decal("))
		{
			fgdClass.isDecal = true;
		}
		else if (lpart.starts_with("flags("))
		{
			std::vector<std::string> flagsList = splitString(getValueInParens(typeParts[i]), ",");
			for (int k = 0; k < flagsList.size(); k++)
			{
				std::string flag = trimSpaces(flagsList[k]);
				if (flag == "Angle")
					fgdClass.hasAngles = true; // force using angles/angle key ?
				else if (flag == "Path" || flag == "Light")
					;
				else
					logf(get_localized_string(LANG_0307),flag,lineNum,name);
			}
		}
		else if (typeParts[i].find('(') != std::string::npos)
		{
			std::string typeName = typeParts[i].substr(0, typeParts[i].find('('));
			logf(get_localized_string(LANG_0308),typeName,lineNum,name);
		}
	}

	if (headerParts.size() == 1)
	{
		logf(get_localized_string(LANG_1048),lineNum,name);
		return;
	}
	std::vector<std::string> nameParts = splitStringIgnoringQuotes(headerParts[1], ":");

	if (nameParts.size() >= 2)
	{
		fgdClass.description = getValueInQuotes(nameParts[1]);
	}
	if (nameParts.size() >= 1)
	{
		fgdClass.name = trimSpaces(nameParts[0]);
		// strips brackets if they're there
		fgdClass.name = fgdClass.name.substr(0, fgdClass.name.find(' '));
	}
}

void Fgd::parseKeyvalue(FgdClass& outClass)
{
	std::vector<std::string> keyParts = splitStringIgnoringQuotes(line, ":");

	KeyvalueDef def;

	def.name = keyParts[0].substr(0, keyParts[0].find('('));
	def.valueType = toLowerCase(getValueInParens(keyParts[0]));

	def.iType = FGD_KEY_STRING;
	if (fgdKeyTypes.find(def.valueType) != fgdKeyTypes.end())
	{
		def.iType = fgdKeyTypes[def.valueType];
	}

	if (keyParts.size() > 1)
		def.description = getValueInQuotes(keyParts[1]);
	else
	{
		def.description = def.name;

		// capitalize (infodecal)
		if ((def.description[0] > 96) && (def.description[0] < 123))
			def.description[0] = def.description[0] - 32;
	}

	if (keyParts.size() > 2)
	{
		if (keyParts[2].find('=') != std::string::npos)
		{ // choice
			def.defaultValue = trimSpaces(keyParts[2].substr(0, keyParts[2].find('=')));
		}
		else if (keyParts[2].find('\"') != std::string::npos)
		{ // std::string
			def.defaultValue = getValueInQuotes(keyParts[2]);
		}
		else
		{ // integer
			def.defaultValue = trimSpaces(keyParts[2]);
		}
	}

	outClass.keyvalues.push_back(def);

	//logf << "ADD KEY " << def.name << "(" << def.valueType << ") : " << def.description << " : " << def.defaultValue << endl;
}

void Fgd::parseChoicesOrFlags(KeyvalueDef& outKey)
{
	std::vector<std::string> keyParts = splitString(line, ":");

	KeyvalueChoice def;

	if (keyParts[0].find('\"') != std::string::npos)
	{
		def.svalue = getValueInQuotes(keyParts[0]);
		def.ivalue = 0;
		def.isInteger = false;
	}
	else
	{
		def.svalue = trimSpaces(keyParts[0]);
		def.ivalue = atoi(keyParts[0].c_str());
		def.isInteger = true;
	}

	if (keyParts.size() > 1)
		def.name = getValueInQuotes(keyParts[1]);

	outKey.choices.push_back(def);

	//logf << "ADD CHOICE LINE " << lineNum << " = " << def.svalue << " : " << def.name << endl;
}

std::vector<std::string> Fgd::groupParts(std::vector<std::string>& ungrouped)
{
	std::vector<std::string> grouped;

	for (int i = 0; i < ungrouped.size(); i++)
	{

		if (stringGroupStarts(ungrouped[i]))
		{
			std::string groupedPart = ungrouped[i];
			i++;
			for (; i < ungrouped.size(); i++)
			{
				groupedPart += " " + ungrouped[i];
				if (stringGroupEnds(ungrouped[i]))
				{
					break;
				}
			}
			grouped.push_back(groupedPart);
		}
		else
		{
			grouped.push_back(ungrouped[i]);
		}
	}

	return grouped;
}

bool Fgd::stringGroupStarts(const std::string& s)
{
	if (s.find('(') != std::string::npos)
	{
		return s.find(')') == std::string::npos;
	}

	size_t startStringPos = s.find('\"');
	if (startStringPos != std::string::npos)
	{
		size_t endStringPos = s.rfind('\"');
		return endStringPos == startStringPos || endStringPos == std::string::npos;
	}

	return false;
}

bool Fgd::stringGroupEnds(const std::string& s)
{
	return s.find(')') != std::string::npos || s.find('\"') != std::string::npos;
}

std::string Fgd::getValueInParens(std::string s)
{
	s = s.substr(s.find('(') + 1);
	s = s.substr(0, s.rfind(')'));
	return trimSpaces(s);
}

std::string Fgd::getValueInQuotes(std::string s)
{
	s = s.substr(s.find('\"') + 1);
	s = s.substr(0, s.rfind('\"'));
	return s;
}

void Fgd::processClassInheritance()
{
	for (int i = 0; i < classes.size(); i++)
	{
		classMap[classes[i]->name] = classes[i];
		//logf(get_localized_string(LANG_0309),classes[i]->name);
	}

	for (int i = 0; i < classes.size(); i++)
	{
		if (classes[i]->classType == FGD_CLASS_BASE)
			continue;

		std::vector<FgdClass*> allBaseClasses;
		classes[i]->getBaseClasses(this, allBaseClasses);

		if (!allBaseClasses.empty())
		{
			std::vector<KeyvalueDef> newKeyvalues;
			std::vector<KeyvalueChoice> newSpawnflags;
			std::set<std::string> addedKeys;
			std::set<std::string> addedSpawnflags;
			//logf << classes[i]->name << " INHERITS FROM: ";
			for (int k = (int)allBaseClasses.size() - 1; k >= 0; k--)
			{
				if (!classes[i]->colorSet && allBaseClasses[k]->colorSet)
				{
					classes[i]->color = allBaseClasses[k]->color;
				}
				if (!classes[i]->sizeSet && allBaseClasses[k]->sizeSet)
				{
					classes[i]->mins = allBaseClasses[k]->mins;
					classes[i]->maxs = allBaseClasses[k]->maxs;
				}
				auto tmpBaseClass = allBaseClasses[k];
				for (int c = 0; c < tmpBaseClass->keyvalues.size(); c++)
				{

					auto tmpBaseKeys = tmpBaseClass->keyvalues[c];
					if (addedKeys.find(tmpBaseKeys.name) == addedKeys.end())
					{
						newKeyvalues.push_back(tmpBaseClass->keyvalues[c]);
						addedKeys.insert(tmpBaseKeys.name);
					}
					if (tmpBaseKeys.iType == FGD_KEY_FLAGS)
					{
						for (int f = 0; f < tmpBaseKeys.choices.size(); f++)
						{
							KeyvalueChoice& spawnflagOption = tmpBaseKeys.choices[f];
							if (addedSpawnflags.find(spawnflagOption.svalue) == addedSpawnflags.end())
							{
								newSpawnflags.push_back(spawnflagOption);
								addedSpawnflags.insert(spawnflagOption.svalue);
							}
						}
					}
				}
				//logf << allBaseClasses[k]->name << " ";
			}

			for (int c = 0; c < classes[i]->keyvalues.size(); c++)
			{
				auto tmpBaseKeys = classes[i]->keyvalues[c];
				if (addedKeys.find(tmpBaseKeys.name) == addedKeys.end())
				{
					newKeyvalues.push_back(tmpBaseKeys);
					addedKeys.insert(tmpBaseKeys.name);
				}
				if (tmpBaseKeys.iType == FGD_KEY_FLAGS)
				{
					for (int f = 0; f < tmpBaseKeys.choices.size(); f++)
					{
						KeyvalueChoice& spawnflagOption = tmpBaseKeys.choices[f];
						if (addedSpawnflags.find(spawnflagOption.svalue) == addedSpawnflags.end())
						{
							newSpawnflags.push_back(spawnflagOption);
							addedSpawnflags.insert(spawnflagOption.svalue);
						}
					}
				}
			}

			std::vector<KeyvalueChoice> oldchoices;
			for (int c = 0; c < classes[i]->keyvalues.size(); c++)
			{
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS)
				{
					oldchoices = classes[i]->keyvalues[c].choices;
				}
			}


			classes[i]->keyvalues = std::move(newKeyvalues);

			for (int c = 0; c < classes[i]->keyvalues.size(); c++)
			{
				if (classes[i]->keyvalues[c].iType == FGD_KEY_FLAGS)
				{
					classes[i]->keyvalues[c].choices = newSpawnflags;

					for (auto& choise : classes[i]->keyvalues[c].choices)
					{
						for (auto choiseOld : oldchoices)
						{
							if (choise.ivalue == choiseOld.ivalue)
							{
								choise = choiseOld;
							}
						}
					}
				}
			}

			for (int c = 0; c < classes[i]->keyvalues.size(); c++)
			{
				if (classes[i]->keyvalues[c].iType == FGD_KEY_STUDIO)
				{
					if (classes[i]->keyvalues[c].name == "model")
					{
						if (!classes[i]->model.size())
						{
							classes[i]->model = classes[i]->keyvalues[c].defaultValue;
							fixupPath(classes[i]->model, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
						}
					}
				}
				if (classes[i]->keyvalues[c].iType == FGD_KEY_CHOICES)
				{
					if (classes[i]->keyvalues[c].name == "model")
					{
						if (!classes[i]->model.size())
						{
							classes[i]->model = classes[i]->keyvalues[c].defaultValue;
							fixupPath(classes[i]->model, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE, FIXUPPATH_SLASH::FIXUPPATH_SLASH_REMOVE);
						}
					}
					else if (classes[i]->keyvalues[c].name == "sequence")
					{
						if (classes[i]->modelSequence <= 0)
						{
							if (isNumeric(classes[i]->keyvalues[c].defaultValue))
							{
								classes[i]->modelSequence = atoi(classes[i]->keyvalues[c].defaultValue.c_str());
							}
						}
					}
					else if (classes[i]->keyvalues[c].name == "body")
					{
						if (classes[i]->modelBody <= 0)
						{
							if (isNumeric(classes[i]->keyvalues[c].defaultValue))
							{
								classes[i]->modelBody = atoi(classes[i]->keyvalues[c].defaultValue.c_str());
							}
						}
					}
					else if (classes[i]->keyvalues[c].name == "skin")
					{
						if (classes[i]->modelSkin <= 0)
						{
							if (isNumeric(classes[i]->keyvalues[c].defaultValue))
							{
								classes[i]->modelSkin = atoi(classes[i]->keyvalues[c].defaultValue.c_str());
							}
						}
					}
				}
			}

		}

	}
}

void FgdClass::getBaseClasses(Fgd* fgd, std::vector<FgdClass*>& inheritanceList)
{
	if (!baseClasses.empty())
	{
		for (int i = (int)baseClasses.size() - 1; i >= 0; i--)
		{
			if (fgd->classMap.find(baseClasses[i]) == fgd->classMap.end())
			{
				logf(get_localized_string(LANG_0310),baseClasses[i],name);
				continue;
			}
			inheritanceList.push_back(fgd->classMap[baseClasses[i]]);
			fgd->classMap[baseClasses[i]]->getBaseClasses(fgd, inheritanceList);
		}
	}
}

void Fgd::createEntGroups()
{
	std::set<std::string> addedPointGroups;
	std::set<std::string> addedSolidGroups;

	for (int i = 0; i < classes.size(); i++)
	{
		if (classes[i]->classType == FGD_CLASS_BASE || classes[i]->name == "worldspawn")
			continue;
		std::string cname = classes[i]->name;
		std::string groupName = cname.substr(0, cname.find('_'));

		bool isPointEnt = classes[i]->classType == FGD_CLASS_POINT;

		std::set<std::string> & targetSet = isPointEnt ? addedPointGroups : addedSolidGroups;
		std::vector<FgdGroup> & targetGroup = isPointEnt ? pointEntGroups : solidEntGroups;

		if (targetSet.find(groupName) == targetSet.end())
		{
			FgdGroup newGroup;
			newGroup.groupName = groupName;

			targetGroup.push_back(newGroup);
			targetSet.insert(groupName);
		}

		for (int k = 0; k < targetGroup.size(); k++)
		{
			if (targetGroup[k].groupName == groupName)
			{
				targetGroup[k].classes.push_back(classes[i]);
				break;
			}
		}
	}

	FgdGroup otherPointEnts;
	otherPointEnts.groupName = "other";
	for (int i = 0; i < pointEntGroups.size(); i++)
	{
		if (pointEntGroups[i].classes.size() == 1)
		{
			otherPointEnts.classes.push_back(pointEntGroups[i].classes[0]);
			pointEntGroups.erase(pointEntGroups.begin() + i);
			i--;
		}
	}
	pointEntGroups.push_back(otherPointEnts);

	FgdGroup otherSolidEnts;
	otherSolidEnts.groupName = "other";
	for (int i = 0; i < solidEntGroups.size(); i++)
	{
		if (solidEntGroups[i].classes.size() == 1)
		{
			otherSolidEnts.classes.push_back(solidEntGroups[i].classes[0]);
			solidEntGroups.erase(solidEntGroups.begin() + i);
			i--;
		}
	}
	solidEntGroups.push_back(otherSolidEnts);
}

void Fgd::setSpawnflagNames()
{
	for (int i = 0; i < classes.size(); i++)
	{
		if (classes[i]->classType == FGD_CLASS_BASE)
			continue;

		for (int k = 0; k < classes[i]->keyvalues.size(); k++)
		{
			if (classes[i]->keyvalues[k].name == "spawnflags")
			{
				for (int c = 0; c < classes[i]->keyvalues[k].choices.size(); c++)
				{
					KeyvalueChoice& choice = classes[i]->keyvalues[k].choices[c];

					if (!choice.isInteger)
					{
						logf(get_localized_string(LANG_0311),choice.svalue,name);
						continue;
					}

					int val = choice.ivalue;
					int bit = 0;
					while (val >>= 1)
					{
						bit++;
					}

					if (bit > 31)
					{
						logf(get_localized_string(LANG_0312),choice.svalue,name);
					}
					else
					{
						classes[i]->spawnFlagNames[bit] = choice.name;

						bool flgnameexists = false;

						for (auto& s : existsFlagNames)
						{
							if (s == choice.name)
								flgnameexists = true;
						}

						if (!flgnameexists)
						{
							existsFlagNames.push_back(choice.name);
							existsFlagNamesBits.push_back(bit);
						}
					}
				}
			}
		}
	}
}

std::vector<std::string> existsFlagNames;
std::vector<int> existsFlagNamesBits;