#include "lang.h"
#include "Sprite.h"
#include "util.h"
#include "lodepng.h"
#include "Renderer.h"
#include "Settings.h"
#include "forcecrc32.h"

Sprite::~Sprite()
{
	for (auto& g : sprite_groups)
	{
		for (auto& s : g.sprites)
		{
			if (s.texture)
			{
				delete s.texture;
			}
			if (s.spriteCube)
			{
				delete s.spriteCube;
			}
		}
		g.sprites.clear();
	}
	sprite_groups.clear();
}

Sprite::Sprite(const std::string& filename)
{
	if (!filename.size())
	{
		return;
	}

	this->name = stripExt(basename(filename));

	std::ifstream spr(filename, std::ios::binary);
	if (!spr) {
		print_log(PRINT_RED, "Failed to open file {}\n", filename);
		return;
	}

	int id, version;
	spr.read(reinterpret_cast<char*>(&id), sizeof(id));
	if (id != 'PSDI')
	{
		print_log(PRINT_RED, "Not a sprite {}\n", filename);
		return;
	}
	spr.read(reinterpret_cast<char*>(&version), sizeof(version));
	if (version != 2) {
		print_log(PRINT_RED, "Wrong version {}\n", filename);
		return;
	}
	spr.seekg(0);
	spr.read(reinterpret_cast<char*>(&header), sizeof(header));
	spr.read(reinterpret_cast<char*>(&colors), sizeof(short));

	palette.resize(colors);
	spr.read(reinterpret_cast<char*>(palette.data()), colors * sizeof(COLOR3));

	sprite_groups.resize(header.numframes);

	for (int i = 0; i < header.numframes; ++i)
	{
		int is_group;
		spr.read(reinterpret_cast<char*>(&is_group), sizeof(int));

		int group_frames = 1;

		if (is_group != 0) {
			spr.read(reinterpret_cast<char*>(&group_frames), sizeof(int));
			sprite_groups[i].sprites.resize(group_frames);
			for (int j = 0; j < group_frames; ++j) {
				spr.read(reinterpret_cast<char*>(&sprite_groups[i].sprites[j].interval), sizeof(float));
				sprite_groups[i].totalinterval += sprite_groups[i].sprites[j].interval;
			}
		}
		else
		{
			sprite_groups[i].sprites.resize(group_frames);
			sprite_groups[i].totalinterval = 0.1f;
			sprite_groups[i].sprites[0].interval = 0.1f;
		}

		for (int j = 0; j < group_frames; ++j)
		{
			SpriteImage& tmpSpriteImage = sprite_groups[i].sprites[j];

			spr.read(reinterpret_cast<char*>(&tmpSpriteImage.frameinfo), sizeof(dspriteframe_t));

			int frame_size = tmpSpriteImage.frameinfo.width * tmpSpriteImage.frameinfo.height;
			tmpSpriteImage.raw_image.resize(frame_size);

			spr.read(reinterpret_cast<char*>(tmpSpriteImage.raw_image.data()), frame_size);

			tmpSpriteImage.image.resize(frame_size);
			for (int s = 0; s < frame_size; s++)
			{
				tmpSpriteImage.image[s] = palette[tmpSpriteImage.raw_image[s]];
			}

			tmpSpriteImage.spriteCube = new EntCube();
			tmpSpriteImage.spriteCube->mins = { -4.0f, 0.0f, 0.0f };
			tmpSpriteImage.spriteCube->maxs = { 4.0f, tmpSpriteImage.frameinfo.width * 1.0f, tmpSpriteImage.frameinfo.height * 1.0f };
			tmpSpriteImage.spriteCube->mins += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);
			tmpSpriteImage.spriteCube->maxs += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);
			tmpSpriteImage.spriteCube->Textured = true;
			g_app->pointEntRenderer->genCubeBuffers(tmpSpriteImage.spriteCube);

			tmpSpriteImage.texture = new Texture(tmpSpriteImage.frameinfo.width,
				tmpSpriteImage.frameinfo.height, (unsigned char*)&tmpSpriteImage.image[0], fmt::format("{}_g{}_f{}", name, i, j), false, false);
			tmpSpriteImage.texture->upload(Texture::TEXTURE_TYPE::TYPE_DECAL);
		}
	}
}


std::map<int, Sprite*> spr_models;


Sprite* AddNewSpriteToRender(const std::string & path, unsigned int sum)
{
	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), path.size(), sum);

	if (spr_models.find(crc32) != spr_models.end())
	{
		return spr_models[crc32];
	}
	else
	{
		Sprite* newModel = new Sprite(path);
		spr_models[crc32] = newModel;
		return newModel;
	}
}



void TestSprite()
{
	Sprite * tmpSprite = AddNewSpriteToRender("d:\\SteamLibrary\\steamapps\\common\\Half-Life\\cstrike\\sprites\\pistol_smoke1.spr");
	int fileid = 0;
	int groupid = 0;
	for (auto& g : tmpSprite->sprite_groups)
	{
		groupid++;
		for (auto& s : g.sprites)
		{
			fileid++;
			lodepng_encode24_file(fmt::format("{}_group{}_file{}.png", tmpSprite->name, groupid, fileid).c_str(), (unsigned char*)&s.image[0], s.frameinfo.width, s.frameinfo.height);
		}
		fileid = 0;
	}
	tmpSprite = AddNewSpriteToRender("d:/SteamLibrary/steamapps/common/Half-Life/valve/sprites/glow01.spr");
	fileid = 0;
	groupid = 0;
	for (auto& g : tmpSprite->sprite_groups)
	{
		groupid++;
		for (auto& s : g.sprites)
		{
			fileid++;
			lodepng_encode24_file(fmt::format("{}_group{}_file{}.png", tmpSprite->name, groupid, fileid).c_str(), (unsigned char*)&s.image[0], s.frameinfo.width, s.frameinfo.height);
		}
		fileid = 0;
	}
}
