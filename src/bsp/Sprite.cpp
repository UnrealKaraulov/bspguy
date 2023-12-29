#include "lang.h"
#include "Sprite.h"
#include "util.h"
#include "lodepng.h"

Sprite::Sprite(const std::string& filename)
{
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

	SpriteImage tmpSpriteImage{};
	sprite_groups.resize(header.numframes);

	for (int i = 0; i < header.numframes; ++i) {
		int frametype;
		spr.read(reinterpret_cast<char*>(&frametype), sizeof(int));

		if (frametype == 0) {
			spr.read(reinterpret_cast<char*>(&tmpSpriteImage.frameinfo), sizeof(dspriteframe_t));
			int frame_size = tmpSpriteImage.frameinfo.width * tmpSpriteImage.frameinfo.height;
			std::vector<COLOR3> sprite;
			tmpSpriteImage.raw_image.resize(frame_size);
			spr.read(reinterpret_cast<char*>(tmpSpriteImage.raw_image.data()), frame_size);
			for (int s = 0; s < frame_size; s++)
			{
				sprite.push_back(palette[tmpSpriteImage.raw_image[s]]);
			}
			tmpSpriteImage.image = sprite;
			sprite_groups[i].totalinterval = tmpSpriteImage.interval = 0.1f;
			sprite_groups[i].sprites.push_back(tmpSpriteImage);
			continue;
		}

		int group_frames;
		spr.read(reinterpret_cast<char*>(&group_frames), sizeof(int));

		sprite_groups[i].sprites.resize(group_frames);

		for (int j = 0; j < group_frames; ++j) {
			spr.read(reinterpret_cast<char*>(&sprite_groups[i].sprites[j].interval), sizeof(float));
			sprite_groups[i].totalinterval += sprite_groups[i].sprites[j].interval;
		}

		for (int j = 0; j < group_frames; ++j) {
			spr.read(reinterpret_cast<char*>(&sprite_groups[i].sprites[j].frameinfo), sizeof(dspriteframe_t));
			int frame_size = sprite_groups[i].sprites[j].frameinfo.width * sprite_groups[i].sprites[j].frameinfo.height;
			std::vector<COLOR3> sprite;
			sprite_groups[i].sprites[j].raw_image.resize(frame_size);
			spr.read(reinterpret_cast<char*>(sprite_groups[i].sprites[j].raw_image.data()), frame_size);
			for (int s = 0; s < frame_size; s++)
			{
				sprite.push_back(palette[sprite_groups[i].sprites[j].raw_image[s]]);
			}
			sprite_groups[i].sprites[j].image = sprite;
		}
	}
}

void TestSprite()
{
	Sprite tmpSprite("d:\\SteamLibrary\\steamapps\\common\\Half-Life\\cstrike\\sprites\\pistol_smoke1.spr");
	int fileid = 0;
	int groupid = 0;
	for (auto& g : tmpSprite.sprite_groups)
	{
		groupid++;
		for (auto& s : g.sprites)
		{
			fileid++;
			lodepng_encode24_file(fmt::format("TestFile_group{}_file{}.png", groupid, fileid).c_str(), (unsigned char*)&s.image[0], s.frameinfo.width, s.frameinfo.height);
		}
		fileid = 0;
	}
}