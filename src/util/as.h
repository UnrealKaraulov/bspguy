#pragma once

// load and run all scripts from scripts dir
// global dir - load at start
// maps/name - load only if map with name loaded
void InitializeAngelScripts();


void AS_OnMapChange();
void AS_OnGuiTick();
void AS_OnSelectEntity();
void AS_OnFrameTick(double msec);