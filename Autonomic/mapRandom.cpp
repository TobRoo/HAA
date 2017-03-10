// mapRandom.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "mapRandom.h"


using std::vector;
using namespace std;

//int main()
//{
//	mapRandomizer("missionLearningTest.ini", "newMissionFile.ini", "newPathFile.path", "newLandmarkFile.ini", "newTargetFile.ini", "newLayoutFile.ini");
//
//}

int	mapRandomizer(char* misFile, char* newMisFile, char* newPathFile, char* newLandmarkFile, char* newTargetFile, char* newLayoutFile) {		//Checks the mission file for random and reveal flags, and produces new layouts when appropriate

	srand(time(NULL));

	int isRandomMap = isRandom(misFile);

	if (isRandomMap == -1) {
		cout << "Failed to open file.\n";
		return 1;
	}
	else if (isRandomMap == 1) {
		MAPCONTENT mapContent;
		if (getMapContent(misFile, mapContent) == -1) {
			cout << "Failed to open file.\n";
			return 1;
		}
		generateRandomMap(misFile, newMisFile, newPathFile, newLandmarkFile, newTargetFile, newLayoutFile, mapContent);
	}
	else if (isRandomMap == 0)
		return 1;

	return 0;
}



int isRandom(char* misFile) {

	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;
	int mapRandom = 0;
	int numObstacles;

	if (fopen_s(&fp, misFile, "r")) {
		return -1; // couldn't open file
	}

	i = 0;

	while (1) {
		ch = fgetc(fp);

		if (ch == EOF) {
			break;
		}
		else if (ch == '\n') {
			keyBuf[i] = '\0';

			if (i == 0)
				continue; // blank line

			if (!strncmp(keyBuf, "[map_options]", 64)) {
				int mapReveal;
				if (fscanf_s(fp, "mapReveal=%d\n", &mapReveal) != 1) { // 0 - Unexplored, 1 - Explored 
																	   //	Log.log(0, "AgentBase::parseMissionFile:mapReveal is %d", mapReveal);
																	   //	Log.log(0, "AgentBase::parseMissionFile: badly formatted mapReveal");
					break;
				}
				if (fscanf_s(fp, "mapRandom=%d\n", &mapRandom) != 1) { // 0 - Layout from file, 1 - Random 
					break;
				}
				if (fscanf_s(fp, "numObstacles=%d\n", &numObstacles) != 1) { // number of obstacles in the random map, bypasses any given path file
					break;
				}
			}
			i = 0;
		}
		else {
			keyBuf[i] = ch;
			i++;
		}
	}
	fclose(fp);
	return mapRandom;
}

int getMapContent(char* misFile, MAPCONTENT& mapContent)
{
	FILE *fp;
	int i;
	char keyBuf[64];
	char ch;
	if (fopen_s(&fp, misFile, "r")) {
		//Log.log(0, "AgentBase::parseMissionFile: failed to open %s", misFile);
		return 1; // couldn't open file
	}

	mapContent.avatarCount = 0;
	mapContent.targetCount = 0;
	mapContent.obstacleCount = 0;
	mapContent.width = 0;
	mapContent.height = 0;
	mapContent.resolution = 0.1;		//TODO: Currently set in ExecutiveMission, change to read from config file 


	i = 0;
	while (1) {
		ch = fgetc(fp);

		if (ch == EOF) {
			break;
		}
		else if (ch == '\n') {
			keyBuf[i] = '\0';

			if (i == 0)
				continue; // blank line
						  //
			if (!strncmp(keyBuf, "[map_options]", 64)) {
				int mapRandom;
				if (fscanf_s(fp, "mapReveal=%d\n", &mapContent.mapReveal) != 1) { // 0 - Unexplored, 1 - Explored 
																				  //Log.log(0, "AgentBase::parseMissionFile:mapReveal is %d", mapReveal);
																				  //Log.log(0, "AgentBase::parseMissionFile: badly formatted mapReveal");
					break;
				}
				if (fscanf_s(fp, "mapRandom=%d\n", &mapRandom) != 1) { // 0 - Layout from file, 1 - Random 
																	   //Log.log(0, "AgentBase::parseMissionFile:mapRandom is %d", mapRandom);
																	   //Log.log(0, "AgentBase::parseMissionFile: badly formatted mapRandom");
					break;
				}
				if (fscanf_s(fp, "numObstacles=%d\n", &mapContent.obstacleCount) != 1) { // 0 - Layout from file, 1 - Random 
																						 //Log.log(0, "AgentBase::parseMissionFile:mapRandom is %d", mapRandom);
																						 //Log.log(0, "AgentBase::parseMissionFile: badly formatted mapRandom");
					break;
				}
				if (fscanf_s(fp, "numTargets=%d\n", &mapContent.targetCount) != 1) { // 0 - Layout from file, 1 - Random 
																					 //Log.log(0, "AgentBase::parseMissionFile:mapRandom is %d", mapRandom);
																					 //Log.log(0, "AgentBase::parseMissionFile: badly formatted mapRandom");
					break;
				}

			}
			else if (!strncmp(keyBuf, "[avatar]", 64)) {
				mapContent.avatarCount++;
			}
			else if (!strncmp(keyBuf, "[mission_region]", 64)) {
				float temp1, temp2;
				if (fscanf_s(fp, "region=%f %f %f %f\n", &temp1, &temp2, &mapContent.width, &mapContent.height) != 4) {
					//Log.log(0, "AgentBase::parseMissionFile: badly formatted region (mission)");
					break;
				}
			}
			i = 0;
		}
		else {
			keyBuf[i] = ch;
			i++;
		}
	}

	fclose(fp);
	return 0;
}

int generateRandomMap(char * misFile, char* newMisFile, char* newPathFile, char* newLandmarkFile, char* newTargetFile, char* newLayoutFile, MAPCONTENT & mapContent)
{
	float randomBorderPadding = 1.0f;	//TODO: Magic number for now, configured in the Matlab version - add in config file
	float randomPosPadding = 1.0f;	//TODO: Magic number for now, configured in the Matlab version - add in config file


									// Total potential positions

	float num_x_pos = (mapContent.width - 2 * randomBorderPadding) / mapContent.resolution - 1.0f;
	float num_y_pos = (mapContent.height - 2 * randomBorderPadding) / mapContent.resolution - 1.0f;

	//Vectors of each potential positions in each direction

	vector<float> x_grid = linspace(randomBorderPadding, (mapContent.width - randomBorderPadding), num_x_pos);
	vector<float> y_grid = linspace(randomBorderPadding, (mapContent.height - randomBorderPadding), num_y_pos);

	//Total amount of valid positions we need

	int numPositions = mapContent.avatarCount + mapContent.targetCount + mapContent.obstacleCount + 1;

	// Form empty arrays

	vector<vector<float>> positions;
	positions.resize(int(num_x_pos * num_y_pos));
	for (int i = 0; i < int(num_x_pos * num_y_pos); i++)
		positions[i].resize(3);

	vector<vector<float>> validPositions;
	validPositions.resize(numPositions);
	for (int i = 0; i < numPositions; i++)
		validPositions[i].resize(3);

	bool validPosFound = false;

	// Put all potential position combinations in an ordered array
	for (int i = 0; i < (int)num_x_pos - 1; i++) {
		for (int j = 0; j < (int)num_y_pos - 1; j++) {
			positions[(i)*int(num_y_pos) + j][0] = x_grid[i];
			positions[(i)*int(num_y_pos) + j][1] = y_grid[j];
		}
	}
	vector<vector<float>> randomPositions;
	randomPositions.resize(int(num_x_pos * num_y_pos));
	for (int i = 0; i < int(num_x_pos * num_y_pos); i++) {
		randomPositions[i].resize(3);
		randomPositions[i] = positions[i];
	}









	while (!validPosFound) {
		cout << "validPosFound!\n";
		// Take random permutations of positions in each direction
		std::random_shuffle(randomPositions.begin(), randomPositions.end());
		//	Loop through assigning random positions, while checking if any new random
		//	positions conflict with old ones.
		int index = 0;
		validPositions[index] = randomPositions[index];

		bool posValid;
		vector<float> xDeltaDist;
		xDeltaDist.resize(numPositions);
		vector<float> yDeltaDist;
		yDeltaDist.resize(numPositions);

		vector<bool>  xViolations;
		xViolations.resize(numPositions);
		vector<bool>  yViolations;
		yViolations.resize(numPositions);
		vector<bool>  violations;
		violations.resize(numPositions);

		int violationsSum = 0;

		int i = 0;
		for (i = 0; i < numPositions; i++) {

			// If we've checked all positions, break and generate
			// new ones
			if (index >= int(num_x_pos*num_y_pos))
				break;

			// Loop through random positions until one is valid
			posValid = false;



			while (!posValid) {
				//cout << "Size of xDeltaDist:" << xDeltaDist.size() << " \n";
				//cout << "Size of yDeltaDist: " << yDeltaDist.size() << " \n";
				//cout << "Size of validPositions: " << validPositions.size() << " \n";
				//cout << "Size of randomPositions: " << randomPositions.size() << " \n";




				violationsSum = 0;
				// Separation distance
				for (int i = 0; i < validPositions.size(); i++) {
				//	cout << "i is: " << i << ", index is: " << index <<" \n";
					xDeltaDist[i] = abs(validPositions[i][0] - randomPositions[index][0]);
					yDeltaDist[i] = abs(validPositions[i][1] - randomPositions[index][1]);
				}
				// Check the violations
				for (int i = 0; i < xViolations.size(); i++) {
	//				cout << "xDeltaDist:" << xDeltaDist[i] << " \n";
	//				cout << "randomPosPadding:" << randomPosPadding << " \n";
					xViolations[i] = xDeltaDist[i] < randomPosPadding;
	//				cout << "xViolations:" << xViolations[i] << " \n";
					yViolations[i] = yDeltaDist[i] < randomPosPadding;
	//				cout << "yViolations:" << yViolations[i] << " \n";

				}
				// Get instances where there are x and y violations
				for (int i = 0; i < xViolations.size(); i++) {
					violations[i] = xViolations[i] * yViolations[i];
					if (violations[i] == true)
						violationsSum++;
				}
				if (violationsSum == 0) {
					posValid = true;
					// Save our valid position
					validPositions[i] = randomPositions[index];
				}

				index++;
				// If we've checked all positions, break and generate
				// new ones
				if (index >= int(num_x_pos*num_y_pos))
					break;

	//			cout << "End of while !posValid, violationsSum is:" << violationsSum << " \n";
			}
			// If we've checked all positions, break and generate
			// new ones
			if (index >= int(num_x_pos*num_y_pos))
				break;
		}
//		cout << "After !whilePosValid, i is:" << i << " \n";
//		cout << "After !whilePosValid, index is:" << index << " \n";
		// Only finish if we have enough positions, and haven't
		// surpassed the index
		if ((i == numPositions) && (index < int(num_x_pos*num_y_pos)))
			validPosFound = true;
	}


	// Assign the random positions to robots, targets, obstacles,
	// and the goal, as well as random orientations

	vector<vector<float>> avatarPos;
	avatarPos.resize(mapContent.avatarCount);
	for (int i = 0; i < mapContent.avatarCount; i++)
		avatarPos[i].resize(3);
	vector<vector<float>> avatarRot;
	avatarRot.resize(mapContent.avatarCount);
	for (int i = 0; i < mapContent.avatarCount; i++)
		avatarRot[i].resize(3);
	vector<vector<float>> targetPos;
	targetPos.resize(mapContent.targetCount);
	for (int i = 0; i < mapContent.targetCount; i++)
		targetPos[i].resize(3);
	vector<vector<float>> targetRot;
	targetRot.resize(mapContent.targetCount);
	for (int i = 0; i < mapContent.targetCount; i++)
		targetRot[i].resize(3);
	vector<vector<float>> obstaclePos;
	obstaclePos.resize(mapContent.obstacleCount);
	for (int i = 0; i < mapContent.obstacleCount; i++)
		obstaclePos[i].resize(3);
	vector<vector<float>> obstacleRot;
	obstacleRot.resize(mapContent.obstacleCount);
	for (int i = 0; i < mapContent.obstacleCount; i++)
		obstacleRot[i].resize(3);
	vector<float> goalPos;

	const float pi = 3.1415927;
	int index = 0;

	for (int i = index; i < mapContent.avatarCount; i++) {
		avatarPos[i] = validPositions[i];
		avatarRot[i].assign({ 0.0, 0.0, (((float)rand()) / (float)RAND_MAX) * 2 * pi });
	}

	index = index + mapContent.avatarCount;

	for (int i = index; i - index < mapContent.targetCount; i++) {
		targetPos[i - index] = validPositions[i];
		targetRot[i - index].assign({ 0.0, 0.0, (((float)rand()) / (float)RAND_MAX) * 2 * pi });
	}
	index = index + mapContent.targetCount;

	for (int i = index; i - index < mapContent.obstacleCount; i++) {
		obstaclePos[i - index] = validPositions[i];
		obstacleRot[i - index].assign({ 0.0, 0.0, (((float)rand()) / (float)RAND_MAX) * 2 * pi });
	}

	index = index + mapContent.obstacleCount;
	goalPos = validPositions[index];

	//Insert calculated positions into newMisFile
	/////////////////////////////////////////////

	string		strRead;
	string		prevRead;
	std::ifstream    inFile(misFile);
	std::ofstream    outFile(newMisFile);


	//Populate landmark and path file with random obstacles, and random obstacle landmarks (lower left corner, one landmark per obstacle)

	std::ofstream    outLM(newLandmarkFile);
	std::ofstream    outTarget(newTargetFile);
	std::ofstream    outObst(newPathFile);


	bool outLMIsOpen = outLM.is_open();
	bool outTargetIsOpen = outTarget.is_open();
	bool outObstIsOpen = outObst.is_open();

	cout << "OPEN: " << outLMIsOpen << outTargetIsOpen << outObstIsOpen;
	cout << "\nPaths: " << newLandmarkFile << " " << newTargetFile << " " << newPathFile;


	int lmHigh = 0;

	for (int i = 0; i < obstaclePos.size(); i++) {
		outLM << "[obstacle]\n";
		outLM << "id=" << i << "\n";
		outLM << "pose=" << (obstaclePos[i][0] - 0.25f) << " " << (obstaclePos[i][1] - 0.25f) << " " << "0.5	0.5	1\n";		//0.5 is width and height of obstacles, 1 is z(depth). TODO: read from config.
		outLM << "landmark_type=0\n";
		outObst << "point=" << (obstaclePos[i][0] - 0.25f) << " " << (obstaclePos[i][1] - 0.25f) << "\n";
		outObst << "point=" << (obstaclePos[i][0] - 0.25f) << " " << (obstaclePos[i][1] + 0.25f) << "\n";
		outObst << "point=" << (obstaclePos[i][0] + 0.25f) << " " << (obstaclePos[i][1] + 0.25f) << "\n";
		outObst << "point=" << (obstaclePos[i][0] + 0.25f) << " " << (obstaclePos[i][1] - 0.25f) << "\n";
		outObst << "point=" << (obstaclePos[i][0] - 0.25f) << " " << (obstaclePos[i][1] - 0.25f) << "\n";
		outObst << "static=block" << (i + 1) << "\n";
		outObst << "pose=0.0	0.0	0.0	1.0\n";
		outObst << "width=2.0\n";
		outObst << "colour=0.7529	0.3137	0.3020\n";
		outObst << "solid=1\n";
		lmHigh = lmHigh + i;
	}

	for (int i = 0; i < targetPos.size(); i++) {
		outTarget << "[forage]\n";
		outTarget << "id=" << 200 + i << "\n";
		outTarget << "pose=" << (targetPos[i][0] - 0.125f) << " " << (targetPos[i][1] - 0.125f) << " " << "0.25	0.25 1\n";
		//outTarget << "landmark_type=" << i % 2 + 1<<"\n";	//Equal amounts of light and heavy objects
		outTarget << "landmark_type=" << 1 << "\n";	//Equal amounts of light and heavy objects
		lmHigh = lmHigh + i;
	}


	//Add wall landmarks for better PF localization 
	int idCount = lmHigh;

	for (int i = 0; i < 11; i++) {	//
		outLM << "[wall]\n";
		outLM << "id=" << idCount  << "\n";
		outLM << "pose=" << 0.0f << " " << i*1.0f << " " << "0.25	0.25 1\n";			//Left wall
		outLM << "landmark_type=" << 4 << "\n";	//Wall

		outLM << "[wall]\n";
		outLM << "id=" << idCount + 1 << "\n";
		outLM << "pose=" << 10.0f << " " << i*1.0f << " " << "0.25	0.25 1\n";			//Right wall
		outLM << "landmark_type=" << 4 << "\n";	//Wall

		if (i > 0 && i < 10) {	//Don't duplicate corners


			outLM << "[wall]\n";
			outLM << "id=" << idCount + 2 << "\n";
			outLM << "pose=" << i*1.0f << " " << 0.0f << " " << "0.25	0.25 1\n";			//Top wall
			outLM << "landmark_type=" << 4 << "\n";	//Wall

			outLM << "[wall]\n";
			outLM << "id=" << idCount + 3 << "\n";
			outLM << "pose=" << i*1.0f << " " << 10.0f << " " << "0.25	0.25 1\n";			//Bottom wall
			outLM << "landmark_type=" << 4 << "\n";	//Wall
		}

		idCount = idCount + 4;

	}






	int avatarIndex = 0;
	while (std::getline(inFile, strRead)) {
		if (!prevRead.compare("[mission_region]")) {
			outFile << "region=" << "0	0 " << mapContent.height << " " << mapContent.width << "\n";
		}
		else if (!prevRead.compare("[collection_region]")) {
			outFile << "region=" << goalPos[0] - 0.5 << "	" << goalPos[1] - 0.5 << "	" << "1.0	1.0\n";		//Collection region is 1.0 units, goalPos is center TODO: Set size of collection region in config.
		}
		else if (!prevRead.compare("[landmark_file]")) {
			//outFile << "file=data\\paths\\" << newLandmarkFile << "\n";
			outFile << "file=" << newLandmarkFile << "\n";
			outFile << "[landmark_file]\n" << "file=" << newTargetFile << "\n";
		}
		else if (!prevRead.compare("[path_file]")) {
			//outFile << "file=data\\paths\\" << newPathFile << "\n\n";
			//outFile << "[path_file]\n" << "file=data\\paths\\" << newTargetFile << "\n";
			outFile << "file=" << newPathFile << "\n\n";
			
		}
		else if (strRead.find("pose=") != std::string::npos) {
			outFile << "pose=" << avatarPos[avatarIndex][0] << " " << avatarPos[avatarIndex][1] << " " << avatarRot[avatarIndex][2] << "\n";
			avatarIndex++;
		}
		else {
			outFile << strRead << "\n";
		}

		prevRead = strRead;
	}


	//Generate layout file if mapReveal is set

	if (mapContent.mapReveal) {
		char* newRandomLayout = newLayoutFile;
		int rows = (int)mapContent.height / mapContent.resolution;
		int cols = (int)mapContent.width / mapContent.resolution;


		vector<vector<int>>	obstacleCoords;
		obstacleCoords.resize(mapContent.obstacleCount);
		for (int i = 0; i < mapContent.obstacleCount; i++)
			obstacleCoords[i].resize(4);

		for (int i = 0; i < mapContent.obstacleCount; i++) {
			obstacleCoords[i][0] = (int)((obstaclePos[i][0] - 0.25f) / mapContent.resolution);	//Low x
			obstacleCoords[i][1] = (int)((obstaclePos[i][0] + 0.25f) / mapContent.resolution);	//High x
			obstacleCoords[i][2] = (int)((obstaclePos[i][1] - 0.25f) / mapContent.resolution);	//Low y
			obstacleCoords[i][3] = (int)((obstaclePos[i][1] + 0.25f) / mapContent.resolution);	//High y
		}

		std::ofstream    outLayout(newRandomLayout);

		bool obstacleFlag = false;
		for (int i = rows - 1; i >= 0; i--) {							//Iterate over all obstacles coordinates, mark as filled if obstacle, empty otherwise. Filled = 1.0, empty = 0.0

			for (int j = 0; j < cols; j++) {

				for (int k = 0; k < mapContent.obstacleCount; k++) {	//Check all obstacle coordinates, does point fall between?
					if (i >= obstacleCoords[k][2] && i <= obstacleCoords[k][3] && j >= obstacleCoords[k][0] && j <= obstacleCoords[k][1])
						obstacleFlag = true;
				}
				if (obstacleFlag)
					outLayout << "0.0 ";
				else
					outLayout << "1.0 ";
				obstacleFlag = false;

			}
			outLayout << "\n\n";
		}
		outLayout.close();
	}




	outLM.close();
	outTarget.close();
	outObst.close();
	inFile.close();
	outFile.close();

	return 0;
}


vector<float> linspace(float a, float b, int n) {
	vector<float> array;
	float step = (b - a) / (n - 1);

	while (a <= b) {
		array.push_back(a);
		a += step;           // could recode to better handle rounding errors
	}
	return array;
}