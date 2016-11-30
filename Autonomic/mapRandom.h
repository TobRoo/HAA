#pragma once
//#include <vector>
#include <fstream>
#include <iostream>
#include  <algorithm>
#include <cmath>
#include <vector>
#include <functional>   // std::multiplies
#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */
#include <string>
struct MAPCONTENT {
	int mapReveal;
	int avatarCount;
	int obstacleCount;
	int targetCount;
	float width;
	float height;
	float resolution;
};

int mapRandomizer(char* misFile, char* newMisFile, char* newPathFile, char* newLandmarkFile, char* newTargetFile, char* newLayoutFile);
int isRandom(char* misFile);
int getMapContent(char* misFile, MAPCONTENT& mapContent);
int generateRandomMap(char * misFile, char* newMisFile, char* newPathFile, char* newLandmarkFile, char* newTargetFile, char* newLayoutFile, MAPCONTENT & mapContent);


std::vector<float> linspace(float a, float b, int n);