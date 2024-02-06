#pragma once

#include "Renderer.h"
#include "util.h"
#include "Bsp.h"
#include "Entity.h"

// Undoable actions following the Command Pattern
class Command
{
public:
	std::string desc;
	int mapIdx;
	bool allowedDuringLoad = false;

	Command(std::string desc, int mapIdx);
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
	size_t entIdx;
	Entity oldEntData;
	Entity newEntData;

	EditEntityCommand(std::string desc, size_t entIdx, Entity oldEntData, Entity newEntData);
	~EditEntityCommand();

	void execute() override;
	void undo() override;
	Entity* getEnt();
	void refresh();
	size_t memoryUsage() override;
};


class DeleteEntityCommand : public Command
{
public:
	size_t entIdx;
	Entity* entData;

	DeleteEntityCommand(std::string desc, size_t entIdx);
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
	void refresh();
	size_t memoryUsage() override;
};


class DuplicateBspModelCommand : public Command
{
public:
	int oldModelIdx;
	int newModelIdx; // TODO: could break redos if this is ever not deterministic
	size_t entIdx;
	LumpState oldLumps{};
	DuplicateBspModelCommand(std::string desc, size_t entIdx);
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
	size_t entIdx;
	unsigned int targetLumps;
	vec3 oldOrigin;
	vec3 newOrigin;
	LumpState oldLumps{};
	LumpState newLumps{};

	EditBspModelCommand(std::string desc, size_t entIdx, LumpState oldLumps, LumpState newLumps, vec3 oldOrigin, unsigned int targetLumps);
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
