
int gui_counter = 10;

void OnMenuCall()
{
	if (gui_counter > 0)
	{
		gui_counter--;
		PrintString("Gui counter:" + gui_counter + "\n");
	}
	PrintString("Selected map:" + GetMapName(GetSelectedMap()) + "\n");
	PrintError("Selected ent:" + GetEntClassname(GetSelectedEnt()) + "\n");
}

int frame_counter = 10;

void OnFrameTick()
{
	if (frame_counter > 0)
	{
		PrintError("Frame counter:" + frame_counter + "\n");
		frame_counter--;
	}
}

void OnMapChange()
{
	PrintString("Map changed!\n");
}

string GetScriptName()
{
	return "Demo script";
}

string GetScriptDirectory()
{
	return "DEMO CATEGORY";
}

string GetScriptDescription()
{
	return "DEMO SCRIPT\nNOT SURE WHAT IT'S FOR";
}