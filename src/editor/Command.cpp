#include "lang.h"
#include "Command.h"
#include "Gui.h"
#include <lodepng.h>
#include "log.h"

std::vector<unsigned char> compressData(const std::vector<unsigned char>& data) {
	unsigned char* out = NULL;
	size_t outsize = 0;
	LodePNGCompressSettings settings;
	lodepng_compress_settings_init(&settings);

	unsigned error = lodepng_zlib_compress(&out, &outsize, data.data(), data.size(), &settings);
	if (error) {
		throw std::runtime_error("Compression failed: " + std::string(lodepng_error_text(error)));
	}

	std::vector<unsigned char> compressedData(out, out + outsize);
	free(out);
	return compressedData;
}

std::vector<unsigned char> decompressData(const std::vector<unsigned char>& compressedData) {
	unsigned char* out = NULL;
	size_t outsize = 0;
	LodePNGDecompressSettings settings;
	lodepng_decompress_settings_init(&settings);

	unsigned error = lodepng_zlib_decompress(&out, &outsize, compressedData.data(), compressedData.size(), &settings);
	if (error) {
		throw std::runtime_error("Decompression failed: " + std::string(lodepng_error_text(error)));
	}

	std::vector<unsigned char> decompressedData(out, out + outsize);
	free(out);
	return decompressedData;
}


EditBspCommand::EditBspCommand(const std::string& desc, LumpState _oldLumps, LumpState _newLumps, unsigned int targetLumps) :
	desc(desc), oldLumps(std::move(_oldLumps)), newLumps(std::move(_newLumps)), targetLumps(targetLumps), memoryused(0)
{
	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		memoryused += oldLumps.lumps[i].size();
		memoryused += newLumps.lumps[i].size();

		if (oldLumps.lumps[i].size())
		{
			oldLumps.lumps[i] = compressData(oldLumps.lumps[i]);
		}

		if (newLumps.lumps[i].size())
		{
			newLumps.lumps[i] = compressData(newLumps.lumps[i]);
		}
	}
}

void EditBspCommand::execute()
{
	Bsp* map = NULL;
	BspRenderer* renderer = NULL;

	for (auto& r : mapRenderers)
	{
		if (r->map == oldLumps.map)
		{
			map = r->map;
			renderer = r;
		}
	}

	if (!map)
		return;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i].size())
		{
			oldLumps.lumps[i] = decompressData(oldLumps.lumps[i]);
		}

		if (newLumps.lumps[i].size())
		{
			newLumps.lumps[i] = decompressData(newLumps.lumps[i]);
		}
	}

	if (targetLumps & FL_ENTITIES)
	{
		map->update_ent_lump();
	}
	map->replace_lumps(newLumps);

	refresh(renderer);

	auto mdls = getDiffModels(oldLumps, newLumps);
	auto faces = getDiffFaces(oldLumps, newLumps);
	for (auto& face : faces)
	{
		if (std::find(mdls.begin(), mdls.end(), map->get_model_from_face(face)) == mdls.end())
		{
			mdls.push_back(map->get_model_from_face(face));
		}
	}

	/*if (mdls.size() > 5)
	{
		renderer->genRenderFaces();
	}
	else
	{*/
	if (mdls.size() && !(targetLumps & FL_LIGHTING))
	{
		renderer->loadLightmaps();
	}
	for (auto& mdl : mdls)
	{
		renderer->refreshModel(mdl, false);
	}
	//}


	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i].size())
		{
			oldLumps.lumps[i] = compressData(oldLumps.lumps[i]);
		}

		if (newLumps.lumps[i].size())
		{
			newLumps.lumps[i] = compressData(newLumps.lumps[i]);
		}
	}
}

void EditBspCommand::undo()
{
	Bsp* map = NULL;
	BspRenderer* renderer = NULL;

	for (auto& r : mapRenderers)
	{
		if (r->map == oldLumps.map)
		{
			map = r->map;
			renderer = r;
		}
	}

	if (!map)
		return;

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i].size())
		{
			oldLumps.lumps[i] = decompressData(oldLumps.lumps[i]);
		}

		if (newLumps.lumps[i].size())
		{
			newLumps.lumps[i] = decompressData(newLumps.lumps[i]);
		}
	}

	map->replace_lumps(oldLumps);

	refresh(renderer);

	auto mdls = getDiffModels(newLumps, oldLumps);
	auto faces = getDiffFaces(newLumps, oldLumps);
	for (auto& face : faces)
	{
		if (std::find(mdls.begin(), mdls.end(), map->get_model_from_face(face)) == mdls.end())
		{
			mdls.push_back(map->get_model_from_face(face));
		}
	}

	/*if (mdls.size() > 5)
	{
		renderer->genRenderFaces();
	}
	else
	{*/
	if (mdls.size() && !(targetLumps & FL_LIGHTING))
	{
		renderer->loadLightmaps();
	}
	for (auto& mdl : mdls)
	{
		renderer->refreshModel(mdl, false);
	}
	//}


	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		if (oldLumps.lumps[i].size())
		{
			oldLumps.lumps[i] = compressData(oldLumps.lumps[i]);
		}

		if (newLumps.lumps[i].size())
		{
			newLumps.lumps[i] = compressData(newLumps.lumps[i]);
		}
	}
}

void EditBspCommand::refresh(BspRenderer* renderer)
{
	if (renderer)
	{
		if (targetLumps & FL_TEXTURES)
		{
			renderer->reuploadTextures();
		}

		if (targetLumps & FL_LIGHTING)
		{
			renderer->loadLightmaps();
		}

		if (targetLumps & FL_VERTICES || targetLumps & FL_TEXTURES)
		{
			renderer->preRenderFaces();
		}
	}

	pickCount++;
	vertPickCount++;
}

size_t EditBspCommand::memoryUsageZip()
{
	size_t size = sizeof(EditBspCommand);

	for (int i = 0; i < HEADER_LUMPS; i++)
	{
		size += oldLumps.lumps[i].size() + newLumps.lumps[i].size();
	}

	return size;
}

size_t EditBspCommand::memoryUsage()
{
	size_t size = sizeof(EditBspCommand);

	size += memoryused;

	return size;
}
