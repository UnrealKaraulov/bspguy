
#include "lang.h"
#include "BspMerger.h"
#include "CommandLine.h"
#include "remap.h"
#include "Renderer.h"
#include "Settings.h"
#include "winding.h"
#include "fmt/format.h"
#include "Sprite.h"
#include "log.h"
#include "as.h"

// todo (newbspguy):
// texture browser
// ...

// minor todo (newbspguy):
// ...
// ...

// refactoring (newbspguy):
// ...

// Notes:
// Removing HULL 0 from any model crashes when shooting unless it's EF_NODRAW or renderamt=0
// Removing HULL 0 from solid model crashes game when standing on it
// Solve: 
// Create empty hull 0 box ?

std::string g_version_string = "NewBSPGuy v4.33";

bool g_verbose = false;

#ifdef WIN32
#include <Windows.h>
#ifdef WIN_XP_86
#include <shellapi.h>
#endif
#else 
#include <csignal>
#endif


bool start_viewer(const char* map)
{
	if (map && map[0] != '\0' && !fileExists(map))
	{
		return false;
	}
	Renderer renderer{};
	if (map && map[0] != '\0')
	{
		renderer.addMap(new Bsp(map));
	}
	renderer.reloadBspModels();

#ifdef WIN32
#ifdef NDEBUG
	showConsoleWindow(false);
#endif
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif

	renderer.renderLoop();
	return true;
}

int test()
{
	std::vector<Bsp*> maps;

	for (int i = 1; i < 22; i++)
	{
		Bsp* map = new Bsp("2nd/saving_the_2nd_amendment" + (i > 1 ? std::to_string(i) : "") + ".bsp");
		maps.push_back(map);
	}

	STRUCTCOUNT removed;
	memset(&removed, 0, sizeof(removed));

	g_verbose = true;
	for (size_t i = 0; i < maps.size(); i++)
	{
		if (!maps[i]->bsp_valid)
		{
			return 1;
		}
		if (!maps[i]->validate())
		{
			print_log("");
		}
		print_log(get_localized_string(LANG_0002), maps[i]->bsp_name);
		maps[i]->delete_hull(2, 1);
		//removed.add(maps[i]->delete_unused_hulls());
		removed.add(maps[i]->remove_unused_model_structures());

		if (!maps[i]->validate())
			print_log("");
	}

	removed.print_delete_stats(1);

	BspMerger merger;
	MergeResult result = merger.merge(maps, vec3(1, 1, 1), "yabma_move", false, false, false, false);
	print_log("\n");
	if (result.map)
	{
		result.map->write("yabma_move.bsp");
		result.map->write("D:/Steam/steamapps/common/Sven Co-op/svencoop_addon/maps/yabma_move.bsp");
		result.map->print_info(false, 0, 0);
	}

	start_viewer("yabma_move.bsp");

	return 0;
}

int merge_maps()
{
	std::vector<std::string> input_maps = g_cmdLine.getOptionList("-maps");

	if (input_maps.size() < 2)
	{
		print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0003));
		return 1;
	}

	std::vector<Bsp*> maps;

	for (size_t i = 0; i < input_maps.size(); i++)
	{
		if (!fileExists(input_maps[i]))
		{
			input_maps[i] = g_startup_dir + input_maps[i];
		}

		Bsp* map = new Bsp(input_maps[i]);
		if (!map->bsp_valid)
		{
			delete map;
			return 1;
		}
		maps.push_back(map);
	}

	for (size_t i = 0; i < maps.size(); i++)
	{
		print_log(get_localized_string(LANG_0004), maps[i]->bsp_name);

		print_log(get_localized_string(LANG_0005));
		STRUCTCOUNT removed = maps[i]->remove_unused_model_structures();
		g_progress.clear();
		removed.print_delete_stats(2);

		if (g_cmdLine.hasOption("-nohull2") || (g_cmdLine.hasOption("-optimize") && !maps[i]->has_hull2_ents()))
		{
			print_log(get_localized_string(LANG_0006));
			maps[i]->delete_hull(2, 1);
			maps[i]->remove_unused_model_structures().print_delete_stats(2);
		}

		if (g_cmdLine.hasOption("-optimize"))
		{
			print_log(get_localized_string(LANG_0007));
			maps[i]->delete_unused_hulls().print_delete_stats(2);
		}

		print_log("\n");
	}

	vec3 gap = g_cmdLine.hasOption("-gap") ? g_cmdLine.getOptionVector("-gap") : vec3();

	std::string output_name = g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile;

	BspMerger merger;
	MergeResult result = merger.merge(maps, gap, output_name, g_cmdLine.hasOption("-noripent"), g_cmdLine.hasOption("-noscript"), g_cmdLine.hasOption("-nomove"), g_cmdLine.hasOption("-nostyles"));

	print_log("\n");
	if (result.map && result.map->validate() && result.map->isValid())
	{
		result.map->write(output_name);
		print_log("\n");
		result.map->print_info(false, 0, 0);
	}

	for (size_t i = 0; i < maps.size(); i++)
	{
		delete maps[i];
	}
	return 0;
}

int print_info()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		bool limitMode = false;
		int listLength = 10;
		int sortMode = SORT_CLIPNODES;

		if (g_cmdLine.hasOption("-limit"))
		{
			std::string limitName = g_cmdLine.getOption("-limit");

			limitMode = true;
			if (limitName == "clipnodes")
			{
				sortMode = SORT_CLIPNODES;
			}
			else if (limitName == "nodes")
			{
				sortMode = SORT_NODES;
			}
			else if (limitName == "faces")
			{
				sortMode = SORT_FACES;
			}
			else if (limitName == "vertexes")
			{
				sortMode = SORT_VERTS;
			}
			else
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0008), limitName);
				delete map;
				return 0;
			}
		}
		if (g_cmdLine.hasOption("-all"))
		{
			listLength = 32768; // should be more than enough
		}

		map->print_info(limitMode, listLength, sortMode);
		delete map;
		return 0;
	}
	delete map;
	return 1;
}

int noclip()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		int model = -1;
		int hull = -1;
		int redirect = 0;

		if (g_cmdLine.hasOption("-hull"))
		{
			hull = g_cmdLine.getOptionInt("-hull");

			if (hull < 0 || hull >= MAX_MAP_HULLS)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0009));
				delete map;
				return 1;
			}
		}

		if (g_cmdLine.hasOption("-redirect"))
		{
			if (!g_cmdLine.hasOption("-hull"))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0010));
				delete map;
				return 1;
			}
			redirect = g_cmdLine.getOptionInt("-redirect");

			if (redirect < 1 || redirect >= MAX_MAP_HULLS)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0011));
				delete map;
				return 1;
			}
			if (redirect == hull)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0012));
				delete map;
				return 1;
			}
		}

		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
		{
			print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, get_localized_string(LANG_0013));
			removed.print_delete_stats(1);
			g_progress.clear();
			print_log("\n");
		}

		if (g_cmdLine.hasOption("-model"))
		{
			model = g_cmdLine.getOptionInt("-model");

			if (model < 0 || model >= map->modelCount)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0014), map->modelCount);
				delete map;
				return 1;
			}

			if (hull != -1)
			{
				if (redirect)
					print_log(get_localized_string(LANG_0015), hull, redirect, model);
				else
					print_log(get_localized_string(LANG_0016), hull, model);

				map->delete_hull(hull, model, redirect);
			}
			else
			{
				print_log(get_localized_string(LANG_0017), model);
				for (int i = 1; i < MAX_MAP_HULLS; i++)
				{
					map->delete_hull(i, model, redirect);
				}
			}
		}
		else
		{
			if (hull == 0)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0018));
				delete map;
				return 1;
			}

			if (hull != -1)
			{
				if (redirect)
					print_log(get_localized_string(LANG_0019), hull, redirect);
				else
					print_log(get_localized_string(LANG_0020), hull);
				map->delete_hull(hull, redirect);
			}
			else
			{
				print_log(get_localized_string(LANG_0021), hull);
				for (int i = 1; i < MAX_MAP_HULLS; i++)
				{
					map->delete_hull(i, redirect);
				}
			}
		}

		removed = map->remove_unused_model_structures();

		if (!removed.allZero())
			removed.print_delete_stats(1);
		else if (redirect == 0)
			print_log(get_localized_string(LANG_0022));
		print_log("\n");

		if (map->validate() && map->isValid())
			map->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
		print_log("\n");

		map->print_info(false, 0, 0);
		delete map;
		return 0;
	}
	delete map;
	return 1;
}

int simplify()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		int hull = 0;

		if (!g_cmdLine.hasOption("-model"))
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0023));
			delete map;
			return 1;
		}

		if (g_cmdLine.hasOption("-hull"))
		{
			hull = g_cmdLine.getOptionInt("-hull");

			if (hull < 1 || hull >= MAX_MAP_HULLS)
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0024));
				delete map;
				return 1;
			}
		}

		int modelIdx = g_cmdLine.getOptionInt("-model");

		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
		{
			print_log(get_localized_string(LANG_1017));
			removed.print_delete_stats(1);
			g_progress.clear();
			print_log("\n");
		}

		STRUCTCOUNT oldCounts(map);

		if (modelIdx < 0 || modelIdx >= map->modelCount)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_1018), map->modelCount);
			return 1;
		}

		if (hull != 0)
		{
			print_log(get_localized_string(LANG_0025), hull, modelIdx);
		}
		else
		{
			print_log(get_localized_string(LANG_0026), modelIdx);
		}

		map->simplify_model_collision(modelIdx, hull);

		map->remove_unused_model_structures();

		STRUCTCOUNT newCounts(map);

		STRUCTCOUNT change = oldCounts;
		change.sub(newCounts);

		if (!change.allZero())
			change.print_delete_stats(1);

		print_log("\n");

		if (map->validate() && map->isValid())
			map->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
		print_log("\n");

		map->print_info(false, 0, 0);
		delete map;
		return 0;
	}
	return 1;
}

int deleteCmd()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		STRUCTCOUNT removed = map->remove_unused_model_structures();

		if (!removed.allZero())
		{
			print_log(get_localized_string(LANG_1145));
			removed.print_delete_stats(1);
			g_progress.clear();
			print_log("\n");
		}

		if (g_cmdLine.hasOption("-model"))
		{
			int modelIdx = g_cmdLine.getOptionInt("-model");

			print_log(get_localized_string(LANG_0027), modelIdx);
			map->delete_model(modelIdx);
			map->update_ent_lump();
			removed = map->remove_unused_model_structures();

			if (!removed.allZero())
				removed.print_delete_stats(1);
			print_log("\n");
		}

		if (map->validate() && map->isValid())
			map->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
		print_log("\n");

		map->print_info(false, 0, 0);
		delete map;
		return 0;
	}
	delete map;
	return 1;
}

int transform()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		vec3 move;

		if (g_cmdLine.hasOptionVector("-move"))
		{
			move = g_cmdLine.getOptionVector("-move");

			print_log(fmt::format(fmt::runtime(get_localized_string("APPLY_OFFSET_STR")),
				move.x, move.y, move.z));

			map->move(move);
		}
		else
		{
			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0028));
			delete map;
			return 1;
		}

		if (map->validate() && map->isValid())
			map->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
		print_log("\n");

		map->print_info(false, 0, 0);
		delete map;
		return 0;
	}
	delete map;
	return 1;
}

int unembed()
{
	Bsp* map = new Bsp(g_cmdLine.bspfile);
	if (map->bsp_valid)
	{
		int deleted = map->delete_embedded_textures();
		print_log(get_localized_string(LANG_0029), deleted);

		if (map->validate() && map->isValid())
			map->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
		print_log("\n");
		delete map;
		return 0;
	}
	delete map;
	return 1;
}

void print_help(const std::string& command)
{
	if (command == "merge")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"merge - Merges two or more maps together\n\n"

			"Usage:   bspguy merge <mapname> -maps \"map1, map2, ... mapN\" [options]\n"
			"Example: bspguy merge merged.bsp -maps \"svencoop1, svencoop2\"\n"

			"\n[Options]\n"
			"  -optimize    : Deletes unused model hulls before merging.\n"
			"                 This can be risky and crash the game if assumptions about\n"
			"                 entity visibility/solidity are wrong.\n"
			"  -nohull2     : Forces redirection of hull 2 to hull 1 in each map before merging.\n"
			"                 This reduces clipnodes at the expense of less accurate collision\n"
			"                 for large monsters and pushables.\n"
			"  -noripent    : By default, the input maps are assumed to be part of a series.\n"
			"                 Level changes and other things are updated so that the merged\n"
			"                 maps can be played one after another. This flag prevents any\n"
			"                 entity edits from being made (except for origins).\n"
			"  -noscript    : By default, the output map is expected to run with the bspguy\n"
			"                 map script loaded, which ensures only entities for the current\n"
			"                 map section are active. This flag replaces that script with less\n"
			"                 effective entity logic. This may cause lag in maps with lots of\n"
			"                 entities, and some ents might not spawn properly. The benefit\n"
			"                 to this flag is that you don't have deal with script setup.\n"
			"  -gap \"X,Y,Z\" : Amount of extra space to add between each map\n"
			"  -v\n"
			"  -verbose     : Verbose console output.\n"
		);
	}
	else if (command == "info")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"info - Show BSP data summary\n\n"

			"Usage:   bspguy info <mapname> [options]\n"
			"Example: bspguy info svencoop1.bsp -limit clipnodes -all\n"

			"\n[Options]\n"
			"  -limit <name> : List the models contributing most to the named limit.\n"
			"                  <name> can be one of: [clipnodes, nodes, faces, vertexes]\n"
			"  -all          : Show the full list of models when using -limit.\n"
		);
	}
	else if (command == "noclip")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"noclip - Delete some clipnodes from the BSP\n\n"

			"Usage:   bspguy noclip <mapname> [options]\n"
			"Example: bspguy noclip svencoop1.bsp -hull 2\n"

			"\n[Options]\n"
			"  -model #    : Model to strip collision from. By default, all models are stripped.\n"
			"  -hull #     : Collision hull to delete (0-3). By default, hulls 1-3 are deleted.\n"
			"                0 = Point-sized entities. Required for rendering\n"
			"                1 = Human-sized monsters and standing players\n"
			"                2 = Large monsters and pushables\n"
			"                3 = Small monsters, crouching players, and melee attacks\n"
			"  -redirect # : Redirect to this hull after deleting the target hull's clipnodes.\n"
			"                For example, redirecting hull 2 to hull 1 would allow large\n"
			"                monsters to function normally instead of falling out of the world.\n"
			"                Must be used with the -hull option.\n"
			"  -o <file>   : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "simplify")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"simplify - Replaces model hulls with a simple bounding box\n\n"

			"Usage:   bspguy simplify <mapname> [options]\n"
			"Example: bspguy simplify svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #    : Model to simplify. Required.\n"
			"  -hull #     : Collision hull to simplify. By default, all hulls are simplified.\n"
			"                1 = Human-sized monsters and standing players\n"
			"                2 = Large monsters and pushables\n"
			"                3 = Small monsters, crouching players, and melee attacks\n"
			"  -o <file>   : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "delete")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"delete - Delete BSP models.\n\n"

			"Usage:   bspguy delete <mapname> [options]\n"
			"Example: bspguy delete svencoop1.bsp -model 3\n"

			"\n[Options]\n"
			"  -model #  : Model to delete. Entities that reference the deleted\n"
			"              model will be updated to use error.mdl instead.\n"
			"  -o <file> : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "transform")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"transform - Apply 3D transformations\n\n"

			"Usage:   bspguy transform <mapname> [options]\n"
			"Example: bspguy transform svencoop1.bsp -move \"0,0,1024\"\n"

			"\n[Options]\n"
			"  -move \"X,Y,Z\" : Units to move the map on each axis.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "unembed")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"unembed - Deletes embedded texture data, so that they reference WADs instead.\n\n"

			"Usage:   bspguy unembed <mapname>\n"
			"Example: bspguy unembed c1a0.bsp\n"
			"\n[Options]\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "exportobj")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"exportobj - Export bsp geometry to obj [WIP].\n\n"

			"Usage:   bspguy exportobj -scale \"-16\" <mapname>\n"
			"Example: bspguy exportobj c1a0.bsp\n"
		);
	}
	else if (command == "exportlit")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"exportlit   : Export .lit (Quake) lightdata file.\n\n"

			"Usage:   bspguy exportlit <mapname>\n"
			"Example: bspguy exportlit c1a0.bsp\n"
			"\n[Options]\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "exportrad")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"exportrad   : Export RAD.exe .ext & .wa_ files.\n\n"

			"Usage:   bspguy exportrad <mapname>\n"
			"Example: bspguy exportrad c1a0.bsp\n"
			"\n[Options]\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "exportwad")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"exportwad   : Export all map textures to .wad file.\n\n"

			"Usage:   bspguy exportwad <mapname>\n"
			"Example: bspguy exportwad c1a0.bsp\n"
			"\n[Options]\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "importwad")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"  importwad   : Import all .wad textures to map.\n\n"

			"Usage:   bspguy importwad <mapname>\n"
			"Example: bspguy importwad c1a0.bsp\n"
			"\n[Options]\n"
			"  -i <file>     : Input file. By default, <mapname> is overwritten.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "importlit")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"importlit   : Import .lit (Quake) lightdata file to map.\n\n"

			"Usage:   bspguy importlit <mapname>\n"
			"Example: bspguy importlit c1a0.bsp\n"
			"\n[Options]\n"
			"  -i <file>     : Input file. By default, <mapname> is overwritten.\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else if (command == "cullfaces")
	{
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"cullfaces - Remove leaf faces from map.\n\n"

			"Usage:   bspguy cullfaces -leaf \"0\" <mapname>\n"
			"Example: bspguy cullfaces c1a0.bsp - clean solid outside faces\n"
			"\n[Options]\n"
			"  -o <file>     : Output file. By default, <mapname> is overwritten.\n"
		);
	}
	else
	{
		print_log(PRINT_RED | PRINT_INTENSITY, "{}\n\n", g_version_string);
		print_log(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY, "{}",
			"This tool modifies Sven Co-op BSPs without having to decompile them.\n\n"
			"Usage: bspguy <command> <mapname> [options]\n"

			"\n<Commands>\n"
			"  info        : Show BSP data summary\n"
			"  merge       : Merges two or more maps together\n"
			"  noclip      : Delete some clipnodes/nodes from the BSP\n"
			"  delete      : Delete BSP models\n"
			"  simplify    : Simplify BSP models\n"
			"  transform   : Apply 3D transformations to the BSP\n"
			"  unembed     : Deletes embedded texture data\n"
			"  exportobj   : Export bsp geometry to obj [WIP]\n"
			"  cullfaces   : Remove leaf faces from map.\n"
			"  exportlit   : Export .lit (Quake) lightdata file.\n"
			"  importlit   : Import .lit (Quake) lightdata file to map.\n"
			"  exportrad   : Export RAD.exe .ext & .wa_ files.\n"
			"  exportwad   : Export all map textures to .wad file.\n"
			"  importwad   : Import all .wad textures to map.\n"
			" "
			" "
			"  no command  : Open empty bspguy window\n"

			"\nRun 'bspguy <command> help' to read about a specific command.\n"
			"\nTo launch the 3D editor. Drag and drop a .bsp file onto the executable,\n"
			"or run 'bspguy <mapname>'"
		);
	}
}
#ifdef WIN32
#ifndef NDEBUG

#include <Dbghelp.h>


int crashdumps = 3;
void make_minidump(EXCEPTION_POINTERS* e)
{
	if (!e)
	{
		e = new	_EXCEPTION_POINTERS();
	}
	if (!e->ContextRecord)
	{
		e->ContextRecord = new CONTEXT();
	}

	if (!e->ExceptionRecord)
	{
		e->ExceptionRecord = new EXCEPTION_RECORD();
	}

	auto hDbgHelp = LoadLibraryA("dbghelp");
	if (hDbgHelp == nullptr)
		return;
	auto pMiniDumpWriteDump = (decltype(&MiniDumpWriteDump))GetProcAddress(hDbgHelp, "MiniDumpWriteDump");
	if (pMiniDumpWriteDump == nullptr)
		return;

	SYSTEMTIME t;
	GetSystemTime(&t);
	createDir("./crashes");
	std::string name = fmt::format("./crashes/{}_{:04}{:02}{:02}_{:02}{:02}{:02}{:02}.dmp", "bspguy", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, crashdumps);


	print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0030), name);

	auto hFile = CreateFileA(name.c_str(), GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
	if (hFile == INVALID_HANDLE_VALUE)
		return;

	MINIDUMP_EXCEPTION_INFORMATION exceptionInfo = MINIDUMP_EXCEPTION_INFORMATION();
	exceptionInfo.ThreadId = GetCurrentThreadId();
	exceptionInfo.ExceptionPointers = e;
	exceptionInfo.ClientPointers = FALSE;

	pMiniDumpWriteDump(
		GetCurrentProcess(),
		GetCurrentProcessId(),
		hFile,
		MINIDUMP_TYPE(MiniDumpNormal),
		e ? &exceptionInfo : nullptr,
		nullptr,
		nullptr);

	CloseHandle(hFile);
}

LONG CALLBACK unhandled_handler(EXCEPTION_POINTERS* e)
{
	if (e)
	{
		if (e->ExceptionRecord)
		{
			DWORD exceptionCode = e->ExceptionRecord->ExceptionCode;

			// Not interested in non-error exceptions. In this category falls exceptions
			// like:
			// 0x40010006 - OutputDebugStringA. Seen when no debugger is attached
			//              (otherwise debugger swallows the exception and prints
			//              the string).
			// 0x406D1388 - DebuggerProbe. Used by debug CRT - for example see source
			//              code of isatty(). Used to name a thread as well.
			// RPC_E_DISCONNECTED and Co. - COM IPC non-fatal warnings
			// STATUS_BREAKPOINT and Co. - Debugger related breakpoints
			if ((exceptionCode & ERROR_SEVERITY_ERROR) != ERROR_SEVERITY_ERROR)
			{
				return ExceptionContinueExecution;
			}
			if (e->ExceptionRecord->ExceptionCode == 0x406D1388)
				return EXCEPTION_CONTINUE_EXECUTION;
			// Ignore custom exception codes.
			// MSXML likes to raise 0xE0000001 while parsing.
			// Note the C++ SEH (0xE06D7363) also fails in that range.
			if (exceptionCode & APPLICATION_ERROR_MASK)
			{
				return ExceptionContinueExecution;
			}

			print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0031), GetLastError(), e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress, (void*)GetModuleHandleA(0));

			if (crashdumps > 0)
			{
				crashdumps--;
				make_minidump(e);
			}
		}
	}

	return EXCEPTION_CONTINUE_SEARCH;
}
#endif
#else 
void signalHandler(int signal) {
	print_log("Caught signal: {}", signal);
	exit(signal);
}
#endif


struct UVVERT {
	vec3 vert;
	float u, v;
	char texname[16];
};

vec3 vec3mix(const vec3& a, const vec3& b, float t) {
	return a * (1.0f - t) + b * t;
}

void CreateSkybox(std::vector<UVVERT>& vertices, vec3 mins, vec3 maxs) {
	std::string sides[6] = { "right", "left", "up", "down", "front", "back" };
	vec3 directions[6][4] = {
		{maxs, {maxs.x, mins.y, maxs.z}, {maxs.x, mins.y, mins.z}, {maxs.x, maxs.y, mins.z}}, // Right
		{mins, {mins.x, mins.y, maxs.z}, {mins.x, maxs.y, maxs.z}, {mins.x, maxs.y, mins.z}}, // Left
		{maxs, {maxs.x, maxs.y, mins.z}, {mins.x, maxs.y, mins.z}, {mins.x, maxs.y, maxs.z}}, // Up
		{mins, {maxs.x, mins.y, mins.z}, {maxs.x, mins.y, maxs.z}, {mins.x, mins.y, maxs.z}}, // Down
		{{mins.x, mins.y, maxs.z}, {maxs.x, mins.y, maxs.z}, {maxs.x, maxs.y, maxs.z}, {mins.x, maxs.y, maxs.z}}, // Front
		{mins, {mins.x, maxs.y, mins.z}, {maxs.x, maxs.y, mins.z}, {maxs.x, mins.y, mins.z}}, // Back
	};

	int divisions = 8;
	for (int side = 0; side < 6; ++side) {
		for (int y = 0; y < divisions; ++y) {
			for (int x = 0; x < divisions; ++x) {
				float xFraction = static_cast<float>(x) / divisions;
				float yFraction = static_cast<float>(y) / divisions;
				float nextXFrac = static_cast<float>(x + 1) / divisions;
				float nextYFrac = static_cast<float>(y + 1) / divisions;

				vec3 topLeft = vec3mix(vec3mix(directions[side][0], directions[side][1], yFraction),
					vec3mix(directions[side][3], directions[side][2], yFraction), xFraction);

				vec3 bottomRight = vec3mix(vec3mix(directions[side][0], directions[side][1], nextYFrac),
					vec3mix(directions[side][3], directions[side][2], nextYFrac), nextXFrac);

				vec3 topRight = vec3mix(vec3mix(directions[side][0], directions[side][1], yFraction),
					vec3mix(directions[side][3], directions[side][2], yFraction), nextXFrac);

				vec3 bottomLeft = vec3mix(vec3mix(directions[side][0], directions[side][1], nextYFrac),
					vec3mix(directions[side][3], directions[side][2], nextYFrac), xFraction);

				UVVERT quad[6] = {
					{topLeft, xFraction, 1.0f - yFraction}, {topRight, nextXFrac, 1.0f - yFraction}, {bottomLeft, xFraction, 1.0f - nextYFrac},
					{bottomLeft, xFraction, 1.0f - nextYFrac}, {topRight, nextXFrac, 1.0f - yFraction}, {bottomRight, xFraction, 1.0f - yFraction}
				};

				for (int i = 0; i < 6; ++i) {
					vertices.push_back(quad[i]);
					std::string texname = sides[side] + "_" + std::to_string(x) + "_" + std::to_string(y);
					strcpy(vertices.back().texname, texname.c_str());
				}
			}
		}
	}
}


int main(int argc, char* argv[])
{
	setlocale(LC_ALL, ".utf8");
	setlocale(LC_NUMERIC, "C");

	set_console_colors(PRINT_RED | PRINT_GREEN | PRINT_INTENSITY);
	std::cout << "BSPGUY:" << g_version_string << std::endl;
	set_console_colors();

	// console ouput speed up? //fixme
	std::cout.sync_with_stdio(false);
	std::cout.tie(NULL);
	std::cin.tie(NULL);

	try
	{
#ifdef WIN32
		showConsoleWindow(true);
#ifndef NDEBUG
		SetUnhandledExceptionFilter(unhandled_handler);
		AddVectoredExceptionHandler(1, unhandled_handler);
#endif
		DisableProcessWindowsGhosting();
#else 
		signal(SIGSEGV, signalHandler);
		signal(SIGFPE, signalHandler);
		signal(SIGBUS, signalHandler);
#endif
		std::string bspguy_dir = "./";
		g_startup_dir = fs::current_path().string() + "/";

#ifdef WIN32
		int nArgs;
		LPWSTR* szArglist = CommandLineToArgvW(GetCommandLineW(), &nArgs);
		bspguy_dir = GetExecutableDir(szArglist[0]);
#else
		if (argv && argv[0])
		{
			bspguy_dir = GetExecutableDir(argv[0]);
		}
		else
		{
			bspguy_dir = GetExecutableDir("./");
		}
#endif
		std::error_code err;
		fs::current_path(bspguy_dir,err);

		/*CSMFile tmpFile;
		tmpFile.read("d:/SteamLibrary/steamapps/common/Half-Life/bspguy_work/de_dust2.smd/de_dust2_1.csm");
		tmpFile.validate();
		tmpFile.write("d:/SteamLibrary/steamapps/common/Half-Life/bspguy_work/de_dust2.smd/de_dust2_1_new.csm");*/
		/*std::vector<UVVERT> vertices;
		vec3 mins(-64.0f, -64.0f, -64.0f);
		vec3 maxs(64.0f, 64.0f, 64.0f);

		CreateSkybox(vertices, mins, maxs);*/

		if (fileExists("./log.txt"))
		{
			try
			{
				fs::remove("./log.txt",err);
			}
			catch (...)
			{

			}
		}

		g_settings_path = "./bspguy.ini";

		if (!fileExists("./bspguy.ini") && fileExists("./bspguy.cfg"))
		{
			int length;
			char* bspguy_cfg = loadFile("./bspguy.cfg", length);
			if (bspguy_cfg && length > 0)
			{
				if (bspguy_cfg[0] != '[')
				{
					std::string bspgu_cfgToIni = ConvertFromCFGtoINI(bspguy_cfg);
					print_log(PRINT_GREEN, "Migrating from CFG file to INI...\n");
					writeFile("./bspguy.ini", bspgu_cfgToIni);
					delete[] bspguy_cfg;
				}
			}
		}

		g_settings.loadDefaultSettings();
		g_settings.loadSettings();

		InitializeAngelScripts();

		g_cmdLine = CommandLine(argc, argv);

		if (g_cmdLine.command == "version" || g_cmdLine.command == "--version" || g_cmdLine.command == "-version")
		{
			return 0;
		}

		if (!g_cmdLine.bspfile.empty() && !fileExists(g_cmdLine.bspfile))
		{
			g_cmdLine.bspfile = g_startup_dir + g_cmdLine.bspfile;
		}

		if (g_cmdLine.hasOption("-v") || g_cmdLine.hasOption("-verbose"))
		{
			g_verbose = true;
		}

		if constexpr (sizeof(vec4) != 16 ||
			sizeof(vec3) != 12 ||
			sizeof(vec2) != 8 ||
			sizeof(COLOR3) != 3 ||
			sizeof(COLOR4) != 4)
		{
			print_log(PRINT_RED | PRINT_INTENSITY, "sizeof() fatal error!\n");
			return 1;
		}

		g_progress.simpleMode = false;

		if (g_cmdLine.command == "exportobj")
		{
			int scale = 1;
			if (g_cmdLine.hasOption("-scale"))
			{
				scale = str_to_int(getValueInQuotes(g_cmdLine.getOption("-scale")));
			}
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ExportToObjWIP(g_cmdLine.bspfile);
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "exportlit")
		{
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ExportLightFile(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "importlit")
		{
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ImportLightFile(g_cmdLine.hasOption("-i") ? g_cmdLine.getOption("-i") : g_cmdLine.bspfile);
			tmpBsp->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "exportrad")
		{
			std::string newpath;
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ExportExtFile(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile, newpath);
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "exportwad")
		{
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ExportEmbeddedWad(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile + ".wad");
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "importwad")
		{
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->ImportWad(g_cmdLine.hasOption("-i") ? g_cmdLine.getOption("-i") : g_cmdLine.bspfile + ".wad");
			tmpBsp->validate();
			tmpBsp->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
			delete tmpBsp;
			return 0;
		}
		else if (g_cmdLine.command == "cullfaces")
		{
			int leafIdx = 0;
			if (g_cmdLine.hasOption("-leaf"))
			{
				leafIdx = str_to_int(getValueInQuotes(g_cmdLine.getOption("-leaf")));
			}
			Bsp* tmpBsp = new Bsp(g_cmdLine.bspfile);
			tmpBsp->cull_leaf_faces(leafIdx);
			if (tmpBsp->validate() && tmpBsp->isValid())
				tmpBsp->write(g_cmdLine.hasOption("-o") ? g_cmdLine.getOption("-o") : g_cmdLine.bspfile);
			delete tmpBsp;
			return 0;
		}

		int retval = 0;

		if (g_cmdLine.command == "info")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = print_info();
		}
		else if (g_cmdLine.command == "noclip")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = noclip();
		}
		else if (g_cmdLine.command == "simplify")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = simplify();
		}
		else if (g_cmdLine.command == "delete")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = deleteCmd();
		}
		else if (g_cmdLine.command == "transform")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = transform();
		}
		else if (g_cmdLine.command == "merge")
		{
			retval = merge_maps();
		}
		else if (g_cmdLine.command == "unembed")
		{
			if (!fileExists(g_cmdLine.bspfile))
			{
				print_log(PRINT_RED | PRINT_INTENSITY, get_localized_string(LANG_0034), g_cmdLine.bspfile);
				retval = 1;
			}
			else
				retval = unembed();
		}
		else
		{
			if (g_cmdLine.askingForHelp)
			{
				print_help(g_cmdLine.command);
				return 0;
			}
			else if (g_cmdLine.bspfile.size() == 0)
				print_log("{}\n", get_localized_string(LANG_0032));
			else
				print_log("{}\n", ("Start bspguy editor with: " + g_cmdLine.bspfile));

			print_log(get_localized_string(LANG_0033), g_settings_path);
			if (!start_viewer(g_cmdLine.bspfile.c_str()))
			{
				print_log(get_localized_string(LANG_0034), g_cmdLine.bspfile);
			}
		}

		if (retval != 0)
		{
			print_help(g_cmdLine.command);
			return 1;
		}
	}
	catch (fs::filesystem_error& ex)
	{
		std::cout << "std::filesystem fatal error." << std::endl << "what():  " << ex.what() << '\n'
			<< "path1(): " << ex.path1() << '\n'
			<< "path2(): " << ex.path2() << '\n'
			<< "code().value():    " << ex.code().value() << '\n'
			<< "code().message():  " << ex.code().message() << '\n'
			<< "code().category(): " << ex.code().category().name() << '\n';
		return 1;
	}
	catch (std::exception& ex)
	{
		std::cout << g_version_string << "FATAL ERROR \"" << ex.what() << "\"" << std::endl;
		return 1;
	}
	catch (...)
	{
		std::cout << g_version_string << "UNKNOWN FATAL ERROR" << std::endl;
		return 1;
	}
	return 0;
}

