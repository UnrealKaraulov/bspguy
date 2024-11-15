#pragma once

#include "util.h"
#include "Bsp.h"
#include "Entity.h"
#include "Settings.h"
#include "Renderer.h"

// Undoable actions following the Command Pattern
class Command
{
public:
	std::string desc;
	int mapIdx;
	bool allowedDuringLoad = false;

	Command(std::string _desc, int _mapIdx) : desc(std::move(_desc))
	{
		this->mapIdx = _mapIdx;
	}

	virtual void execute() = 0;
	virtual void undo() = 0;
	virtual size_t memoryUsage() = 0;
	virtual ~Command() = default;

	BspRenderer* getBspRenderer();
	Bsp* getBsp();
};


class EditEntityCommand : public Command
{
public:
	int entIdx;
	Entity oldEntData;
	Entity newEntData;


	EditEntityCommand(std::string desc, int entIdx, Entity _oldEntData, Entity _newEntData);

	~EditEntityCommand()
	{
	}

	void execute() override;
	void undo() override;
	Entity* getEnt();
	void refresh();
	size_t memoryUsage() override;
};


class DeleteEntityCommand : public Command
{
public:
	int entIdx;
	Entity* entData;

	DeleteEntityCommand(std::string desc, int entIdx);
	~DeleteEntityCommand();

	void execute() override;
	void undo() override;
	void refresh();
	size_t memoryUsage() override;
};


class CreateEntityCommand : public Command
{
public:
	Entity* entData;

	CreateEntityCommand(std::string desc, int mapIdx, Entity* entData);
	~CreateEntityCommand();

	void execute() override;
	void undo() override;
	size_t memoryUsage() override;
};



class CreateEntityFromTextCommand : public Command {
public:
	std::string textData;
	size_t createdEnts;

	CreateEntityFromTextCommand(std::string desc, int mapIdx, std::string textData);
	~CreateEntityFromTextCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};


class DuplicateBspModelCommand : public Command
{
public:
	int oldModelIdx;
	int newModelIdx; // TODO: could break redos if this is ever not deterministic
	int entIdx;
	LumpState oldLumps{};
	DuplicateBspModelCommand(std::string desc, int entIdx);
	~DuplicateBspModelCommand();

	void execute() override;
	void undo() override;
	size_t memoryUsage() override;
};


class CreateBspModelCommand : public Command
{
public:
	Entity* entData;
	LumpState oldLumps{};
	float mdl_size;
	bool empty = false;

	CreateBspModelCommand(std::string desc, int mapIdx, Entity* entData, float size, bool empty);
	~CreateBspModelCommand();

	void execute() override;
	void undo() override;
	size_t memoryUsage() override;

private:
	int getDefaultTextureIdx();
	int addDefaultTexture();
};


class EditBspModelCommand : public Command
{
public:
	int modelIdx;
	int entIdx;
	unsigned int targetLumps;
	vec3 oldOrigin;
	vec3 newOrigin;
	LumpState oldLumps{};
	LumpState newLumps{};

	EditBspModelCommand(std::string desc, int entIdx, LumpState oldLumps, LumpState newLumps, vec3 oldOrigin, unsigned int targetLumps);
	~EditBspModelCommand();

	void execute() override;
	void undo() override;
	void refresh();
	size_t memoryUsage() override;
};


class CleanMapCommand : public Command
{
public:
	LumpState oldLumps{};

	CleanMapCommand(std::string desc, int mapIdx, LumpState oldLumps);
	~CleanMapCommand();

	void execute() override;
	void undo() override;
	void refresh();
	size_t memoryUsage() override;
};


class OptimizeMapCommand : public Command
{
public:
	LumpState oldLumps{};

	OptimizeMapCommand(std::string desc, int mapIdx, LumpState oldLumps);
	~OptimizeMapCommand();

	void execute() override;
	void undo() override;
	void refresh();
	size_t memoryUsage() override;
};


class DeleteBoxedDataCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	vec3 mins, maxs;

	DeleteBoxedDataCommand(std::string desc, int mapIdx, vec3 mins, vec3 maxs, LumpState oldLumps);
	~DeleteBoxedDataCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};

class DeleteOobDataCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	int clipFlags;

	DeleteOobDataCommand(std::string desc, int mapIdx, int clipFlags, LumpState oldLumps);
	~DeleteOobDataCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};

class FixSurfaceExtentsCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	bool scaleNotSubdivide;
	bool downscaleOnly;
	int maxTextureDim;

	FixSurfaceExtentsCommand(std::string desc, int mapIdx, bool scaleNotSubdivide, bool downscaleOnly, int maxTextureDim, LumpState oldLumps);
	~FixSurfaceExtentsCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};

class DeduplicateModelsCommand : public Command {
public:
	LumpState oldLumps = LumpState();

	DeduplicateModelsCommand(std::string desc, int mapIdx, LumpState oldLumps);
	~DeduplicateModelsCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};

class MoveMapCommand : public Command {
public:
	LumpState oldLumps = LumpState();
	vec3 offset;

	MoveMapCommand(std::string desc, int mapIdx, vec3 offset, LumpState oldLumps);
	~MoveMapCommand();

	void execute();
	void undo();
	void refresh();
	size_t memoryUsage();
};

