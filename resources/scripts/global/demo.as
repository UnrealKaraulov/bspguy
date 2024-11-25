void OnMenuCall()
{
	PrintString("[DEMO SCRIPT] Selected map:" + GetMapName(GetSelectedMap()) + "\n");
	PrintError("[DEMO SCRIPT] Selected ent:" + GetEntClassname(GetSelectedEnt()) + "\n");
	PrintString("[DEMO SCRIPT] Year " + datetime().get_year() + ".Month " + datetime().get_month() + ".Day " + datetime().get_day() + ".\n");
	
	filesystem fs;
	
	PrintString("[DEMO SCRIPT] Working directory:" + GetWorkDir() + "\n");
	
	if (!fs.isDir(GetWorkDir()))
	{
		fs.makeDir(GetWorkDir());
		PrintError("[DEMO SCRIPT] " + GetWorkDir() + " created\n");
	}
	
	if (!fs.isDir(GetWorkDir() + "DEMOSCRIPT"))
	{
		fs.makeDir(GetWorkDir() + "DEMOSCRIPT");
		PrintError("[DEMO SCRIPT] " + GetWorkDir() + "DEMOSCRIPT" + " created\n");
	}
	
	file f;
	if( f.open(GetWorkDir() + "DEMOSCRIPT/script_demo.txt", "w") >= 0 ) 
	{
		string str = f.writeString("[HELLO FROM SCRIPT]\n"); 
		f.close();
	}
	
	PrintError("[DEMO SCRIPT] vec3 TEST\n");
	
	vec3 tmpVec(100.0,10.0,1.0);
	PrintString("[DEMO SCRIPT] vec3 test 1:" + tmpVec.x + " " + tmpVec.y + " " + tmpVec.z + "\n");
	tmpVec /= 2.0;
	PrintString("[DEMO SCRIPT] vec3 test 2:" + tmpVec.x + " " + tmpVec.y + " " + tmpVec.z + "\n");
	tmpVec = tmpVec / 2.0;
	PrintString("[DEMO SCRIPT] vec3 test 3 :" + tmpVec.x + " " + tmpVec.y + " " + tmpVec.z + "\n");
	tmpVec = tmpVec * 2.0;
	PrintString("[DEMO SCRIPT] vec3 test 4:" + tmpVec.x + " " + tmpVec.y + " " + tmpVec.z + "\n");
}

int frame_counter = 5;

void OnFrameTick()
{
	if (frame_counter > 0)
	{
		float msec = GetLastFrameTime();
		PrintError("[DEMO SCRIPT] Frame time " + msec + " counter:" + frame_counter + "\n");
		frame_counter--;
	}
}

void OnMapChange()
{
	PrintString("[DEMO SCRIPT] Map changed!\n");
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