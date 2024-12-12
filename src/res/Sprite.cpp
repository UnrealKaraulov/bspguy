#include "lang.h"
#include "Sprite.h"
#include "log.h"
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

void Sprite::DrawSprite()
{
	animate_frame();

	sprite_groups[current_group].sprites
		[sprite_groups[current_group].current_spr].texture->bind(0);
	sprite_groups[current_group].sprites
		[sprite_groups[current_group].current_spr].spriteCube->cubeBuffer->drawFull();
}

void Sprite::DrawBBox()
{
	sprite_groups[current_group].sprites
		[sprite_groups[current_group].current_spr].spriteCube->wireframeBuffer->drawFull();
}

void Sprite::DrawAxes()
{
	sprite_groups[current_group].sprites
		[sprite_groups[current_group].current_spr].spriteCube->axesBuffer->drawFull();
}

void Sprite::animate_frame()
{
	sprite_groups[current_group].currentinterval += fabs(g_app->curTime - anim_time);
	if (sprite_groups[current_group].currentinterval > sprite_groups[current_group]
		.sprites[sprite_groups[current_group].current_spr].interval)
	{
		sprite_groups[current_group].currentinterval = 0.0f;
		sprite_groups[current_group].current_spr++;
		if (sprite_groups[current_group].current_spr >=
			sprite_groups[current_group].sprites.size())
		{
			sprite_groups[current_group].current_spr = 0;
			current_group++;
			if (current_group >= sprite_groups.size())
			{
				current_group = 0;
			}
		}
	}
	anim_time = g_app->curTime;
}

void Sprite::set_missing_sprite()
{
	sprite_groups.resize(1);
	sprite_groups[0].currentinterval = 0.1f;
	sprite_groups[0].totalinterval = 0.1f;
	sprite_groups[0].sprites.resize(1);
	sprite_groups[0].sprites[0].image.resize(64 * 64);
	memset(sprite_groups[0].sprites[0].image.data(), 255, 64 * 64 * sizeof(COLOR4));
	sprite_groups[0].currentinterval = 0.0f;
	sprite_groups[0].current_spr = 0;

	SpriteImage& tmpSpriteImage = sprite_groups[0].sprites[0];

	tmpSpriteImage.frameinfo.width = 64;
	tmpSpriteImage.frameinfo.height = 64;
	tmpSpriteImage.frameinfo.origin[0] = -32;
	tmpSpriteImage.frameinfo.origin[1] = 32;

	tmpSpriteImage.spriteCube = new EntCube();
	tmpSpriteImage.spriteCube->mins = { -5.0f, 0.0f, 0.0f };
	tmpSpriteImage.spriteCube->maxs = { 5.0f, tmpSpriteImage.frameinfo.width * 1.0f, tmpSpriteImage.frameinfo.height * 1.0f };
	tmpSpriteImage.spriteCube->mins += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);
	tmpSpriteImage.spriteCube->maxs += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);
	tmpSpriteImage.spriteCube->Textured = true;

	g_app->pointEntRenderer->genCubeBuffers(tmpSpriteImage.spriteCube);

	tmpSpriteImage.texture = new Texture(tmpSpriteImage.frameinfo.width,
		tmpSpriteImage.frameinfo.height, (unsigned char*)&tmpSpriteImage.image[0], "MISSING_SPRTE", true, false);

	tmpSpriteImage.texture->upload(Texture::TEXTURE_TYPE::TYPE_DECAL);
}

Sprite::Sprite(const std::string& filename, const vec3& mins , const vec3& maxs , float scale, bool useOwnSettigns)
{
	current_group = 0;
	colors = 0;
	anim_time = 0.0f;
	header = {};
	if (!filename.size())
	{
		return;
	}
	this->name = stripExt(basename(filename));

	std::ifstream spr(filename, std::ios::binary);
	if (!spr) {
		print_log(PRINT_RED, "Failed to open file {}\n", filename);
		set_missing_sprite();
		return;
	}

	int id, version;
	spr.read((char*)(&id), sizeof(id));
	if (id != 'PSDI' || !spr)
	{
		print_log(PRINT_RED, "Not a sprite {}\n", filename);
		set_missing_sprite();
		return;
	}
	spr.read((char*)(&version), sizeof(version));
	if (version != 2 || !spr) {
		print_log(PRINT_RED, "Wrong version {}\n", filename);
		set_missing_sprite();
		return;
	}
	spr.seekg(0);
	spr.read((char*)(&header), sizeof(header));
	spr.read((char*)(&colors), sizeof(short));

	palette.resize(colors);
	spr.read((char*)(palette.data()), colors * sizeof(COLOR3));

	sprite_groups.resize(header.numframes);
	if (!spr)
	{
		print_log(PRINT_RED, "Bad read {}\n", filename);
		set_missing_sprite();
		return;
	}

	bool is_valid = false;

	for (int i = 0; i < header.numframes; ++i)
	{
		int is_group;
		spr.read((char*)(&is_group), sizeof(int));

		int group_frames = 1;
		sprite_groups[i].currentinterval = 0.0f;
		sprite_groups[i].current_spr = 0;

		if (is_group != 0) {
			spr.read((char*)(&group_frames), sizeof(int));
			sprite_groups[i].sprites.resize(group_frames);
			for (int j = 0; j < group_frames; ++j) {
				spr.read((char*)(&sprite_groups[i].sprites[j].interval), sizeof(float));
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

			spr.read((char*)(&tmpSpriteImage.frameinfo), sizeof(dspriteframe_t));

			int frame_size = tmpSpriteImage.frameinfo.width * tmpSpriteImage.frameinfo.height;

			std::vector<unsigned char> raw_image;
			raw_image.resize(frame_size);

			spr.read((char*)(raw_image.data()), frame_size);

			tmpSpriteImage.image.resize(frame_size);

			tmpSpriteImage.last_color = palette[colors - 1];

			for (int s = 0; s < frame_size; s++)
			{
				if (header.texFormat == SPR_ALPHTEST && raw_image[s] == colors - 1)
				{
					tmpSpriteImage.image[s] = COLOR4(0, 0, 0, 0);
				}
				else if (header.texFormat == SPR_ADDITIVE && palette[raw_image[s]] == COLOR3(0, 0, 0))
				{
					tmpSpriteImage.image[s] = COLOR4(0, 0, 0, 0);
				}
				else if (header.texFormat == SPR_INDEXALPHA)
				{
					tmpSpriteImage.image[s] = tmpSpriteImage.last_color;
					tmpSpriteImage.image[s].a = raw_image[s];
				}
				else
				{
					tmpSpriteImage.image[s] = palette[raw_image[s]];
				}
			}

			tmpSpriteImage.spriteCube = new EntCube();

			if (!useOwnSettigns || (mins == maxs && mins == vec3()))
			{
				tmpSpriteImage.spriteCube->mins = { -5.0f, 0.0f, 0.0f };
				tmpSpriteImage.spriteCube->maxs = { 5.0f, tmpSpriteImage.frameinfo.width * 1.0f, tmpSpriteImage.frameinfo.height * 1.0f };

				tmpSpriteImage.spriteCube->mins += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);
				tmpSpriteImage.spriteCube->maxs += vec3(0.0f, tmpSpriteImage.frameinfo.origin[0] * 1.0f, tmpSpriteImage.frameinfo.origin[1] * -1.0f);

				if (useOwnSettigns)
				{
					tmpSpriteImage.spriteCube->mins *= scale;
					tmpSpriteImage.spriteCube->maxs *= scale;
				}
			}
			else
			{
				tmpSpriteImage.spriteCube->mins = (mins * scale);
				tmpSpriteImage.spriteCube->maxs = (maxs * scale);
			}

			tmpSpriteImage.spriteCube->Textured = true;

			g_app->pointEntRenderer->genCubeBuffers(tmpSpriteImage.spriteCube);

			tmpSpriteImage.texture = new Texture(tmpSpriteImage.frameinfo.width,
				tmpSpriteImage.frameinfo.height, (unsigned char*)&tmpSpriteImage.image[0], fmt::format("{}_g{}_f{}", name, i, j), true, false);
			tmpSpriteImage.texture->upload(Texture::TEXTURE_TYPE::TYPE_DECAL);

			is_valid = true;
		}
	}

	if (!is_valid)
	{
		set_missing_sprite();
	}
}


std::map<unsigned int, Sprite*> spr_models;


Sprite* AddNewSpriteToRender(const std::string& path, unsigned int sum)
{
	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), (unsigned int)path.size(), sum);

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

Sprite* AddNewSpriteToRender(const std::string& path, float scale)
{
	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), (unsigned int)path.size(), *(int*)&scale);

	if (spr_models.find(crc32) != spr_models.end())
	{
		return spr_models[crc32];
	}
	else
	{
		Sprite* newModel = new Sprite(path, vec3(),vec3(), scale, true);
		spr_models[crc32] = newModel;
		return newModel;
	}
}

Sprite* AddNewSpriteToRender(const std::string& path, vec3 mins, vec3 maxs, float scale)
{
	auto sum = (mins + maxs * scale).toString();

	unsigned int crc32 = GetCrc32InMemory((unsigned char*)path.data(), (unsigned int)path.size(), 
		GetCrc32InMemory((unsigned char*)sum.data(), (unsigned int)sum.size(), 0));

	if (spr_models.find(crc32) != spr_models.end())
	{
		return spr_models[crc32];
	}
	else
	{
		Sprite* newModel = new Sprite(path, mins, maxs, scale, true);
		spr_models[crc32] = newModel;
		return newModel;
	}
}

void TestSprite()
{
	Sprite* tmpSprite = AddNewSpriteToRender("d:\\SteamLibrary\\steamapps\\common\\Half-Life\\cstrike\\sprites\\pistol_smoke1.spr");
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
