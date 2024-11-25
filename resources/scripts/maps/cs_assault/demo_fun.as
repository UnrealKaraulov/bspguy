vec3 startPos(71.0f, -146.0f, 0.0f);
vec3 targetPos(223.0f, 254.0f, 0.0f);
vec3 currentPos;

vec3 startAngles(0.0f, 85.0f, 0.0f);
vec3 targetAngles(0.0f, 0.0f, 0.0f);
vec3 currentAngles;

float totalTime = 0.0;
float moveDuration = 2.0;
bool activateMagic = false;
int fun_Stage = 0;
int selected_Map = -1;
int fun_Entity = -1;

enum Entity_RefreshFlags
{
    Entity_RefreshAnglesOrigin      = 1 << 0,
    Entity_RefreshModel             = 1 << 1,
    Entity_RefreshBodySkin          = 1 << 2,
    Entity_RefreshSequence          = 1 << 3,
    Entity_RefreshOther             = 1 << 4,
    Entity_RefreshAll               = 0xFFFFFFF
}

void OnMenuCall()
{
    if (fun_Stage != 0 || activateMagic)
    {
        PrintError("[FUN] Please wait for fun end!");
    }
    else 
    {
        selected_Map = GetSelectedMap();
        if (selected_Map == -1)
        {
            PrintError("[FUN] Please select map!");
        }
        else 
        {
            PrintString("[CS_ASSAUL DEMO SCRIPT] Fun magic activated!\n");
            activateMagic = true;
        }
    }
}

void OnFrameTick()
{
    float msec = GetLastFrameTime();
    totalTime += msec;

    if (activateMagic)
    {
        activateMagic = false;
        fun_Stage = 1;
    }

    if (fun_Stage == 1)
    {
        PrintString("[FUN] Create entity for fun magic!\n");
        fun_Entity = CreateEntity(GetSelectedMap(), "info_player_start");
        if (fun_Entity == -1)
        {
            PrintError("[FUN] Fatal error! Can't create entity for fun!");
        }
        else 
        {
            fun_Stage = 2;
            PrintError("Entity_RefreshAll="+int(Entity_RefreshAll) + "\n");
            PrintError("Entity_RefreshAnglesOrigin="+int(Entity_RefreshAnglesOrigin) + "\n");
            
            string posStr = "" + startPos.x + " " + startPos.y + " " + startPos.z;
            SetEntKeyVal(fun_Entity, "origin", posStr);
            SetEntKeyVal(fun_Entity, "model", "models/hostage.mdl");
            SetEntKeyVal(fun_Entity, "angles", "0 85 0");
			SetEntKeyVal(fun_Entity, "sequence", "0"); 
            RefreshEnt(fun_Entity, int(Entity_RefreshAll));
            totalTime = 0.0f;
        }
    }
    else if (fun_Stage == 2)
    {
        if (totalTime < moveDuration)
        {
            float t = totalTime / moveDuration;
            currentPos = startPos + (targetPos - startPos) * t;
            string posStr = "" + currentPos.x + " " + currentPos.y + " " + currentPos.z;
            SetEntKeyVal(fun_Entity, "origin", posStr);
            RefreshEnt(fun_Entity, Entity_RefreshAnglesOrigin);
        }
        else
        {
            fun_Stage = 3;
            totalTime = 0.0f;
            moveDuration = 2.0f;
            PrintString("[FUN] Entity moved forward by 500 units on Y axis!\n");
        }
    }
	else if (fun_Stage == 3)
    {
        if (totalTime < moveDuration)
        {
			float t = totalTime / moveDuration;
            currentAngles = startAngles + (targetAngles - startAngles) * t;
            string anglesStr = "" + currentAngles.x + " " + currentAngles.y + " " + currentAngles.z;
            SetEntKeyVal(fun_Entity, "angles", anglesStr);
            RefreshEnt(fun_Entity, Entity_RefreshAnglesOrigin);
        }
        else
        {
            PrintString("[FUN] Entity rotated to 0 0 0 angles!\n");
			SetEntKeyVal(fun_Entity, "sequence", "50"); 
			RefreshEnt(fun_Entity, Entity_RefreshSequence);
			int wheelEnt = FindEntityByKeyVal(selected_Map,"model", "*47");
			SetEntKeyVal(wheelEnt, "origin", "0 -12 0");
			RefreshEnt(wheelEnt, Entity_RefreshAnglesOrigin);
            fun_Stage = 4;
            totalTime = 0.0f;
            moveDuration = 4.0f;
        }
    }
    else if (fun_Stage == 4) 
    {
        if (totalTime < moveDuration)
        {
            
        }
        else
        {
            fun_Stage = 5;
			SetEntKeyVal(fun_Entity, "sequence", "99"); 
			RefreshEnt(fun_Entity, Entity_RefreshSequence);
            totalTime = 0.0f;
            moveDuration = 1.0f;
        }
    }
	else if (fun_Stage == 5) 
    {
        if (totalTime > moveDuration)
        {
            fun_Stage = 6;
			//.... the end?
        }
    }
}

void OnMapChange()
{
    if (fun_Entity != -1)
    {
        fun_Stage = 0;
        activateMagic = false;
        PrintString("[FUN] Map changed! Need remove fun entity with id " + fun_Entity + "!\n");
        RemoveEntity(fun_Entity);
        fun_Entity = -1;
    }
}

string GetScriptName()
{
    return "cs_assault fun demo";
}

string GetScriptDirectory()
{
    return "FUN";
}

string GetScriptDescription()
{
    return "DEMO SCRIPT FOR CS_ASSAULT MAP\nNOT SURE WHAT IT'S FOR";
}