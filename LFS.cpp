#include <iostream>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <math.h>
#include <sstream>
#include <queue>
#include <map>
#include <algorithm>
#include <cstring>


using namespace std;

int memIndex = 8;
int iNodes = 0;

typedef struct{
	char buffer[1024];
}dataBlock;

typedef struct{
	char fileName[128];
	unsigned int fileSize;
	int blocks[128];
	char dummy[380];
}iNode;

//First 8 blocks are just SSB blocks
typedef struct{

	//pair<int, int> DATA[128];
	//pair<int, int> Inode[128];
	//pair<int, int> ImapChunk[128];
	pair<int, int> Info[1024]; /*1 pair for each block in segment. First 8 are the SSB blocks. Data = (Inode#,0-127).
						Inode = (Inode#, -1). ImapChunk = (chunk#, -2). */
}SegSumBlock;

union block{
	iNode IN;
	dataBlock db;
	int iMapPart[256];
};


SegSumBlock SSB;
int *cleanSegs = new int[64];

void import(string fName, string lfsFile, union block* inMemSegment, int* iMap, map<string, int>& files, int shutdown, int updateImap, int* CR);
void remove(string fName, union block* inMemSegment, int* iMap, map<string, int>& files, int* CR);
void list(union block* inMemSegment, int* iMap, map<string, int>& files);
void cat(string fName, union block* inMemSegment, int* iMap, map<string, int>& files);
void display(string fName, int howmany, int start, union block* inMemSegment, int* iMap, map<string, int>& files);
void overwrite(string fName, int howmany, int start, string c, union block* inMemSegment, int* iMap, map<string, int>& files, int* CR);
void shut_down(union block* inMemSegment, int* iMap, map<string, int>& files, int shutdown, int updateImap, int* CR);
void checkPoint(int shutdown, int updateImap, int* iMap, map<string, int>& files, int* CR, union block* inMemSegment);
void clean(union block* inMemSegment, int* CR, int*iMap, map<string, int>& files);

int main(int argc, char *argv[]){
	string temp;
	string fName;
	string lfsFile;
	int shutdown = 0;
	int updateImap = 0;
	block* inMemSegment;
	int* iMap;
	int* CR;
	CR = new int[40];
	memset(CR, 0, 160);
	for(int i = 0; i < 64; i++){
		cleanSegs[i] = 0;
	}
	//memset(cleanSegs, 0, 256);
	iMap = new int[10240];
	inMemSegment = new block[1024];
//	int memIndex = 0;
	map<string, int> files;

	for(int i = 0; i < 10240; i++){
		iMap[i] = -1;
	}

	if(mkdir("DRIVE", 0777) == -1){
		checkPoint(shutdown, updateImap, iMap, files, CR, inMemSegment);
		//cerr << "Error :  " << strerror(errno) << endl;
	}

	else{
		for(int i = 0; i < 64; i++){
			temp = "DRIVE/SEGMENT" + to_string(i) + ".txt";
			ofstream outfile;
			outfile.open (temp);
			outfile.seekp((1<<20) - 1);
			outfile.write("", 1);
			outfile.close();
		}
		cout << "Directory created" << endl;
	}

 //Read from checkpoint region file on disk and update iMap
	cout << "Please enter either 'Import <fileName> <lfsFileName>', 'List', 'Remove <lfsFileName>', 'Cat <lfs_filename>', 'Display <lfs_filename> <howmany> <start>', 'Overwrite <lfs_filename> <howmany> <start> <c>' or 'Shutdown'" << endl;

	while(shutdown != 1){
		if(cleanSegs[30] == 1){
			clean(inMemSegment, CR, iMap, files);
		}
		if(memIndex / 1024 > 63){
			cout << "Drive segments are full, cannot continue to write :(" << endl;
			exit(1);
		}
		getline(cin, temp);
		for(unsigned int i = 0; i < temp.size(); i++){
			if(isspace(temp[i])){
				stringstream ss(temp);
				getline(ss, temp, ' ');
				if(temp == "Import"){
					getline(ss, fName, ' ');
					getline(ss, lfsFile, '\n');
					import(fName, lfsFile, inMemSegment, iMap, files, shutdown, updateImap, CR);
				}
				else if(temp == "Remove"){
					getline(ss, fName, '\n');
					remove(fName, inMemSegment, iMap, files, CR);
				}
				else if(temp == "Cat"){
					getline(ss, fName, '\n');
					cat(fName, inMemSegment, iMap, files);
				}
				else if(temp == "Display"){
					string howmany;
					string start;
					getline(ss, fName, ' ');
					getline(ss, howmany, ' ');
					getline(ss, start, '\n');
					display(fName, stoi(howmany), stoi(start), inMemSegment, iMap, files);
				}
				else if(temp == "Overwrite"){
					string howmany;
					string start;
					string c;
					getline(ss, fName, ' ');
					getline(ss, howmany, ' ');
					getline(ss, start, ' ');
					getline(ss, c, '\n');
					overwrite(fName, stoi(howmany), stoi(start), c, inMemSegment, iMap, files, CR);
				}
			}
		}
		if(temp == "List"){
			//getline(cin, '\n');
			list(inMemSegment, iMap, files);
		}
		else if(temp == "Shutdown"){
			shutdown = 1;
			//checkpoint(shutdown); //Now shutdown is "1" so we write to disk
			shut_down(inMemSegment, iMap, files, shutdown, updateImap, CR);
		}
	}
	delete [] iMap;
	delete [] CR;
	delete [] inMemSegment;
	delete[] cleanSegs;
	return 0;
}

void import(string fName, string lfsFile, union block* inMemSegment, int* iMap, map<string, int>& files, int shutdown, int updateImap, int* CR){
	updateImap = 1;
	ifstream file;
	iNode IN;
	//IN.alive = 0;
	strcpy(IN.fileName, lfsFile.c_str());
	file.open(fName);
	block A;
	int i = 0;
	unsigned int blocks[128];
	if(file.good()){
		file.seekg(0, file.end);
		IN.fileSize = file.tellg();
		file.seekg(0, file.beg);
		while(!file.eof()){
			if(file.eof()){
				//cout << "we did it bois" << endl;
				break;
			}
			block B;
			dataBlock temp;
			//temp.buffer = new char[1024];
			char * diskBlock = new char[1024];
			file.read(diskBlock, 1024);
			//cout << diskBlock << endl;
			memcpy(temp.buffer, diskBlock, 1024);
			//cout << temp.buffer << endl;
			B.db = temp;
			blocks[i] = memIndex;
			inMemSegment[memIndex%1024] = B;
			SSB.Info[memIndex%1024] = pair<int, int>(iNodes, i);
			memIndex++;
			i++;
			if(memIndex % 1024 == 0){
				memcpy(&inMemSegment[0], &SSB, 8192);
				ofstream myfile;
				string wFile;
				wFile = "DRIVE/SEGMENT" + to_string((int)(memIndex/1024-1)) + ".txt";
				cleanSegs[(int)(memIndex/1024-1)] = 1;
				cout << "Writing to " << wFile << endl;
				myfile.open(wFile);
				//cout << "size = " << sizeof(inMemSegment) << endl;
				myfile.write((char *)inMemSegment, 1024*1024);
				myfile.close();
			}
			delete[] diskBlock;
		}
	}
	file.close();
	memcpy(&IN.blocks, &blocks, sizeof(blocks));
	memcpy(&A.IN, &IN, 1024);
	//A.IN = IN;
	inMemSegment[memIndex % 1024] = A;
	iMap[iNodes] = memIndex;
	SSB.Info[memIndex%1024] = pair<int, int>(iNodes, -1);
	memIndex++;
	if(memIndex % 1024 == 0){
		memcpy(&inMemSegment[0], &SSB, 8192);
		ofstream myfile;
		string wFile;
		wFile = "DRIVE/SEGMENT" + to_string((int)(memIndex/1024-1)) + ".txt";
		cleanSegs[(int)(memIndex/1024-1)] = 1;
		cout << "Writing to " << wFile << endl;
		myfile.open(wFile);
		myfile.write((char *)inMemSegment, 1024*1024);
		myfile.close();
	}

	if((iNodes+1) % 256 == 0){
		//write iMap part
		int ind = iNodes/256;
		ind = ind*256;
		int temp[256];
		for(int i = 0; i < 256; i++){
			temp[i] = iMap[ind];
			ind++;
		}
		block C;
		memcpy(&C.iMapPart, &temp, sizeof(temp));
		inMemSegment[memIndex%1024] = C;
		CR[((iNodes+1)/256)-1] = memIndex;
		SSB.Info[memIndex%1024] = pair<int, int>(((iNodes+1)/256)-1, -2);
		memIndex++;
	}
	//cout << "inserting" << endl;
	files.insert(pair<string, int> (IN.fileName, iNodes));
	//cout << "inserting " << IN.fileName << " into iNode " << iNodes << " " << iMap[0] << endl;
	iNodes++;

	if(memIndex % 1024 == 0){
		memcpy(&inMemSegment[0], &SSB, 8192);
		ofstream myfile;
		string wFile;
		wFile = "DRIVE/SEGMENT" + to_string((int)(memIndex/1024-1)) + ".txt";
		cleanSegs[(int)(memIndex/1024-1)] = 1;
		cout << "Writing to " << wFile << endl;
		myfile.open(wFile);
		myfile.write((char *)inMemSegment, 1024*1024);
		myfile.close();
	}

	cout << fName << " successfully imported" << endl;

	//for(int i = 0; i < 1024; i++){   //prints the SSB
		//cout << SSB.Info[i].first << ", " << SSB.Info[i].second << endl;
	//}
	//how to read certain byte from DRIVE

/*	cout << "-------------" << endl;
	char buffer[1024];
	ifstream test;
	test.open("DRIVE/SEGMENT0.txt");
	test.seekg(iMap[0]*1024, test.beg);
	test.read(buffer, sizeof(buffer));
	iNode tempp;
	memcpy(&tempp, &buffer, 1024);
	cout << tempp.fileName << endl;*/
}

void remove(string lfsFile, union block* inMemSegment, int* iMap, map<string, int>& files, int* CR){
	map<string, int>::iterator iter;
	iter = files.find(lfsFile);
	if(iter != files.end()){
		iMap[files.at(lfsFile)] = -1;
		cout << lfsFile << " removed" << endl;
		int temp[256];
		int ind = files.at(lfsFile)/256;
		ind = ind*256;
		for(int i = 0; i < 256; i++){
			temp[i] = iMap[ind];
			ind++;
		}
		block C;
		memcpy(&C.iMapPart, &temp, sizeof(temp));
		inMemSegment[memIndex%1024] = C;
		CR[files.at(lfsFile)/256] = memIndex;
		SSB.Info[memIndex%1024] = pair<int, int>(files.at(lfsFile)/256, -2);
		memIndex++;
		if(memIndex % 1024 == 0){
			memcpy(&inMemSegment[0], &SSB, 8192);
			ofstream myfile;
			string wFile;
			wFile = "DRIVE/SEGMENT" + to_string((int)(memIndex/1024-1)) + ".txt";
			cleanSegs[(int)(memIndex/1024-1)] = 1;
		//	cout << "Writing to " << wFile << endl;
			myfile.open(wFile);
			//cout << "size = " << sizeof(inMemSegment) << endl;
			myfile.write((char *)inMemSegment, 1024*1024);
			myfile.close();
		}
	}
}

void list(union block* inMemSegment, int* iMap, map<string, int>& files){

	map<string, int>::iterator it;
       for (it = files.begin(); it != files.end(); it++)
       {
				// cout << "iNode " << it->second << " at location " << iMap[it->second] << endl;
				 if(iMap[it->second] != -1){
					 if((int)(iMap[it->second]/1024) == memIndex/1024){
						 cout << "File " << inMemSegment[iMap[it->second]%1024].IN.fileName;
						 cout << " with " << inMemSegment[iMap[it->second]%1024].IN.fileSize << " bytes" << endl;
					 }
					 else{
						char buffer[1024];
					 	ifstream test;
						string rFile;
						rFile = "DRIVE/SEGMENT" + to_string((int)((iMap[it->second]/1024))) + ".txt";
						//cout << "reading from " << rFile << " for list" << endl;
					 	test.open(rFile);
						//cout << iMap[it->second] << endl;
					 	test.seekg((iMap[it->second]%1024)*1024, test.beg);
						//cout << endl << it->second << " at lOCATION : " << iMap[it->second] << endl << endl << endl;
					 	test.read(buffer, 1024);
					 	iNode temp;
					 	memcpy(&temp, &buffer, 1024);
					 	cout << "File " << temp.fileName;
						cout << " with " << temp.fileSize << " bytes" << endl;
						test.close();
					 }
				 }
       }
}

void cat(string fName, union block* inMemSegment, int* iMap, map<string, int>& files){
	map<string, int>::iterator iter;
	iter = files.find(fName);
	if(iter != files.end()){
		display(fName, 1280000, 0, inMemSegment, iMap, files);
	}
	else{
		cout << "That file is not in the system" << endl;
	}

}

void display(string fName, int howmany, int start, union block* inMemSegment, int* iMap, map<string, int>& files){
	iNode temp;
	char buffer[1024];
	//char buffer2[1024];
	int size2 = 0;
	if(iMap[files.at(fName)] != -1){
		if((int)(iMap[files.at(fName)]/1024) == memIndex/1024){
			temp = inMemSegment[iMap[files.at(fName)] % 1024].IN;
		}
		else{
			ifstream test;
			string rFile;
			rFile = "DRIVE/SEGMENT" + to_string((int)((iMap[files.at(fName)]/1024))) + ".txt";
		//	cout << "reading from " << rFile << endl;
			test.open(rFile);
			test.seekg((iMap[files.at(fName)]%1024)*1024, test.beg);
			test.read(buffer, 1024);
		//	file.seekg(0, file.beg);
			test.close();
			memcpy(&temp, &buffer, 1024);
			//for(int i = 0; i < 128; i++){
				//cout << temp.blocks[i] << endl;
			//}
		}
	}
	else{
		cout << "That file is not in the LFS" << endl;
		return;
	}

	for(int i = (start/1024); size2 < howmany; i++){
		if(start > temp.fileSize){
			break;
		}
		if(((int)temp.blocks[i]/1024) == memIndex/1024){
			int tes = temp.fileSize-((temp.fileSize/1024)*1024);
			if(i == (temp.fileSize/1024)){
				if(size2 + 1024 > howmany && (howmany-size2) < tes){
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, howmany-size2);
					cout << buffer;
					break;
				}
				else{
					//cout << "tes: " << tes << endl;
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, tes);
					cout << buffer;
					break;
				}
			}
			else if (i == (start/1024)){
				if(howmany < (((start/1024)+1)*1024)-start){
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, howmany+(start%1024));
				}
				else{
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, 1024);
				}
				size2 += (((start/1024)+1)*1024)-start;
				cout << buffer+(start%1024);
			}
			else{
				if(size2 + 1024 > howmany){
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, howmany-size2);
				}
				else{
					memcpy(&buffer, &inMemSegment[temp.blocks[i]%1024].db.buffer, 1024);
				}
				size2 += 1024;
				cout << buffer;
			}
		}
		else{
			ifstream test;
			string rFile;
			rFile = "DRIVE/SEGMENT" + to_string((int)((temp.blocks[i]/1024))) + ".txt";
		//	cout << "reading from " << rFile << endl;
			test.open(rFile);
			if(i == (temp.fileSize/1024)){
				int tes = 0;
				if(i == (start/1024)){
					tes = temp.fileSize-start;
					test.seekg(((temp.blocks[i]%1024)*1024)+(start%1024), test.beg);
				}
				else{
					tes = temp.fileSize-((temp.fileSize/1024)*1024);
					test.seekg(((temp.blocks[i]%1024)*1024), test.beg);
				}
				if(size2 + 1024 > howmany && (howmany-size2) < tes){
					test.read(buffer, howmany-size2);
					cout << buffer;
					break;
				}
				else{
					test.read(buffer, tes);
					cout << buffer;
					break;
				}

			}
			else if(i == (start/1024)){
				test.seekg(((temp.blocks[i]%1024)*1024)+(start%1024), test.beg);
				if(howmany < (((start/1024)+1)*1024)-start){
					test.read(buffer, howmany);
				}
				else{
					test.read(buffer, (((start/1024)+1)*1024)-start);
				}
				size2 += (((start/1024)+1)*1024)-start;
				cout << buffer;
			}
			else{
				test.seekg(((temp.blocks[i]%1024)*1024), test.beg);
				if(size2 + 1024 > howmany){
					test.read(buffer, howmany-size2);
				}
				else{
					test.read(buffer, 1024);
				}
				size2 += 1024;
				cout << buffer;
			}
			test.close();
		}
		memset(buffer, 0, 1024);
	}
	cout << endl;
}

void overwrite(string fName, int howmany, int start, string c, union block* inMemSegment, int* iMap, map<string, int>& files, int* CR){
	map<string, int>::iterator iter;
	iter = files.find(fName);
	char buffer[1024];
	iNode temp;
	if(iter != files.end()){
		if(iMap[files.at(fName)] != -1){
			if((int)(iMap[files.at(fName)]/1024) == memIndex/1024){
				temp = inMemSegment[iMap[files.at(fName)] % 1024].IN;
			}
			else{
				ifstream test;
				string rFile;
				rFile = "DRIVE/SEGMENT" + to_string((int)((iMap[files.at(fName)]/1024))) + ".txt";
				//cout << "reading from " << rFile << " for list" << endl;
				test.open(rFile);
				test.seekg((iMap[files.at(fName)]%1024)*1024, test.beg);
				test.read(buffer, 1024);
			//	file.seekg(0, file.beg);
				test.close();
				memcpy(&temp, &buffer, 1024);
			}
		}


		ofstream myfile;
		string wFile;
		wFile = "DRIVE/SEGMENT" + to_string((int)(temp.blocks[start/1024]/1024)) + ".txt";
		//cout << "Writing to " << wFile << endl;
		myfile.open(wFile, ios::in);
		myfile.seekp((temp.blocks[start/1024]%1024)*1024 + (start%1024), myfile.beg);

	//	if(start + howmany > temp.fileSize){
		//	temp.fileSize = start+howmany;
			//for(int i = 0; i < )
		//}

		//for(int i = 0 ; i < howmany; i++){
		//	myfile << c;
		//}
		myfile.close();

		int startFlag = 0;
		int j = 0;
		for(int i = 0; i < 128; i++){
			if(j == howmany){
				break;
			}
			if(i+(start/1024) > temp.fileSize/1024){
				dataBlock temp2;
				for(int i = 0; j < howmany; i++){
					if(i > 1023){
						break;
					}
					temp2.buffer[i] = c[0];
					j++;
				}
				block B;
				B.db = temp2;
				inMemSegment[memIndex % 1024] = B;
				temp.blocks[(start/1024)+i] = memIndex;
				SSB.Info[memIndex%1024] = pair<int, int>(files.at(temp.fileName), (start/1024)+i);
				memIndex++;
				temp.fileSize = start+howmany;
			}
			if(temp.blocks[(start/1024)+i]/1024 == memIndex/1024){
				dataBlock temp2;
				memcpy(&temp2.buffer, &inMemSegment[temp.blocks[(start/1024)+i]%1024], 1024);
				for(int i = 0; i < 1024; i++){
					if(startFlag == 0){
						if(i+(start%1024) > 1023){
							break;
						}
						temp2.buffer[i+(start%1024)] = c[0];
					}
					else{
						temp2.buffer[i] = c[0];
					}
					if(j == howmany){
						break;
					}
					j++;
				}
				block B;
				B.db = temp2;
				inMemSegment[memIndex % 1024] = B;
				temp.blocks[(start/1024)+i] = memIndex;
				SSB.Info[memIndex%1024] = pair<int, int>(files.at(temp.fileName), (start/1024)+i);
				memIndex++;
				startFlag = 1;
			}
			else{
				ifstream test2;
				string rFile2;
				rFile2 = "DRIVE/SEGMENT" + to_string((int)(temp.blocks[start/1024]/1024)+i) + ".txt";
				test2.open(rFile2);
				test2.seekg((temp.blocks[start/1024]%1024)*1024, test2.beg);
				test2.read(buffer, 1024);
				dataBlock temp2;
				memcpy(&temp2.buffer, &buffer, 1024);
				for(int i = 0; i < 1024; i++){
					if(startFlag == 0){
						if(i+(start%1024) > 1023){
							break;
						}
					//	cout << start << endl;
					//	cout << temp2.buffer[i+(start%1024)] << endl;
						temp2.buffer[i+(start%1024)] = c[0];
					//	cout << "new- " << temp2.buffer[i+(start%1024)] << endl;
					}
					else{
						temp2.buffer[i] = c[0];
					}
					if(j == howmany){
						break;
					}
					j++;
				}
				block B;
				B.db = temp2;
				inMemSegment[memIndex % 1024] = B;
				temp.blocks[(start/1024)+i] = memIndex;
				SSB.Info[memIndex%1024] = pair<int, int>(files.at(temp.fileName), (start/1024)+i);
				memIndex++;
				startFlag = 1;
				test2.close();
			}
		}

		block A;
		A.IN = temp;
		inMemSegment[memIndex%1024] = A;
		iMap[files.at(temp.fileName)] = memIndex;
		SSB.Info[memIndex%1024] = pair<int, int>(files.at(temp.fileName), -1);
		memIndex++;

		int temp2[256];
		int ind = files.at(temp.fileName)/256;
		ind = ind*256;
		for(int i = 0; i < 256; i++){
			temp2[i] = iMap[ind];
			ind++;
		}
		block C;
		memcpy(&C.iMapPart, &temp2, sizeof(temp));
		inMemSegment[memIndex%1024] = C;
		CR[files.at(temp.fileName)/256] = memIndex;
		SSB.Info[memIndex%1024] = pair<int, int>(files.at(temp.fileName)/256, -2);
		memIndex++;
	//	file.seekg(0, file.beg);
	}
	cout << "Successfully overwritten" << endl;
}

void shut_down(union block* inMemSegment, int* iMap, map<string, int>& files, int shutdown, int updateImap, int* CR){
	//if(shutdown == 1){
		//Call List
		//Write to checkpoint
		list(inMemSegment, iMap, files);
		checkPoint(shutdown, updateImap, iMap, files, CR, inMemSegment);
		exit(1);
	//}
}

void checkPoint(int shutdown, int updateImap, int*iMap, map<string, int>& files, int* CR, union block* inMemSegment){

	//Read from Check_Point.txt to update existing iMap
	if(shutdown == 0 && updateImap == 0){
		char buffer[160];
		//char buffer2[256];
		ifstream test;
		test.open("DRIVE/CHECKPOINT_REGION.txt");
		if(test.good()){
			test.read(buffer, 160);
			memcpy(CR, &buffer, 160);
			//cout << "CR[0] = " << CR[0] << endl;

			for(int i = 0; i < 40; i++){
				if(CR[i] != 0){
					char buffer[1024];
					ifstream test;
					string rFile;
					rFile = "DRIVE/SEGMENT" + to_string((int)((CR[i]/1024))) + ".txt";
					//cout << "reading from " << rFile << endl;
					test.open(rFile);
					test.seekg((CR[i]%1024)*1024, test.beg);
					test.read(buffer, 1024);
					int temp[256];
					memcpy(&temp, &buffer, 1024);
					//cout << temp[0] << endl;
					memcpy(&iMap[iNodes / 256], &temp, 1024);
					//cout << "iMap[0]: " << iMap[0] << endl;
				//	cout << temp.fileName << endl;
				//	cout << temp.fileSize << " bytes" << endl;
					test.close();
				}
			}
		}


		ifstream file2;
		string name;
		string key;
		string temp;
		int flag = 0;
		file2.open("DRIVE/HACK_MAP.txt");
		if(file2.good()){
			while(!file2.eof() && flag == 0){
				getline(file2, temp);
				for(unsigned int i = 0; i < temp.size(); i++){
					if(isspace(temp[i])){
						stringstream ss(temp);
						getline(ss, name, ',');
						getline(ss, key, '\n');
						files.insert(pair<string, int> (name, stoi(key)));
						break;
					}
					else if(i == temp.size()-1){
						flag = 1;
					}
				}
			}
			memIndex = stoi(temp);
			getline(file2, temp);
			iNodes = stoi(temp);
		//	file2.read(buffer2, 256);
		//	memcpy(&cleanSegs, &buffer2, 256);
			file2.close();
			memIndex = (((memIndex / 1024) + 1) * 1024)+8;
			//cout << memIndex << endl;
		}


		ifstream test2;
		char buffer3[256];
				//cout << "reading from " << rFile << endl;
		test2.open("DRIVE/Clean_Segs.txt");
		test2.read(buffer3, 256);
		memcpy(&cleanSegs[0], &buffer3, 256);

	}


	else if(shutdown == 1){ //Write to disk after shutdown is called
		int ind = iNodes/256;
		ind = ind*256;
		int temp[256];
		for(int i = 0; i < (iNodes % 256); i++){
			temp[i] = iMap[ind];
			ind++;
		}
		block C;
		memcpy(&C.iMapPart, &temp, sizeof(temp));
		inMemSegment[memIndex%1024] = C;
		//cout << "part: " << C.iMapPart[0] << " at index " << memIndex << endl;
		CR[iNodes / 256] = memIndex;
		SSB.Info[memIndex%1024] = pair<int, int>(iNodes/256, -2);
	//	cout << iNodes << endl;
	//	cout << "CR[" << iNodes / 256  << "] " << " = " << memIndex << endl;
		memIndex++;

		memcpy(&inMemSegment[0], &SSB, 8192);

	//	cout << "writing to CR" << endl;
		ofstream myfile;
		myfile.open("DRIVE/CHECKPOINT_REGION.txt");
		myfile.write((char *)CR, 160);
		myfile.close();

		ofstream myfile3;
		string wFile;
		wFile = "DRIVE/SEGMENT" + to_string((int)(memIndex/1024)) + ".txt";
		cleanSegs[(int)(memIndex/1024)] = 1;
	//	cout << "Writing to " << wFile << endl;
		myfile3.open(wFile);
		//cout << "size = " << sizeof(inMemSegment) << endl;
		myfile3.write((char *)inMemSegment, 1024*1024);
		myfile3.close();

		//	cout << "writing to hack map" << endl;
		ofstream myfile2;
		myfile2.open("DRIVE/HACK_MAP.txt");
		for(map<string, int>::iterator it = files.begin(); it != files.end(); it++){
			myfile2 << it->first << ", " << it->second << endl;
		}
		myfile2 << memIndex << endl;
		myfile2 << iNodes << endl;
		//myfile2.write((char *)cleanSegs, 256);
		//cout << "worked" << endl;
		myfile2.close();

		ofstream myfile4;
		myfile4.open("DRIVE/CLEAN_SEGS.txt");
		myfile4.write((char *) cleanSegs, 256);
		myfile4.close();

	}
}

void clean(union block* inMemSegment, int* CR, int*iMap, map<string, int>& files){
	int cleanSeg;
	int dirtySeg;
	int newCleanSegs = 0;
	int index = 8;
	char buffer[8192];
	char buffer2[1024];
	int blocks2[128];
	int j = 63;
	iNode updatedNode;
	block* segment;
	segment = new block[1024*1024];
	SegSumBlock ssbTemp;
	SegSumBlock cleanSSB;

	for(int i = j; i > 0; i--){
		if(cleanSegs[i] == 0){
			cleanSeg = i;
			j = i+1;
			break;
		}
	}

	cout << "Cleaning..." << endl;
	while(newCleanSegs < 4){
		//cout << "while" << endl;
		for(int i = 0; i < 64; i++){
			if(cleanSegs[i] == 1){
				dirtySeg = i;
				break;
			}
		}
		ifstream test;
		string rFile;
		rFile = "DRIVE/SEGMENT" + to_string(dirtySeg) + ".txt";
		//cout << "reading from " << rFile << endl;
		test.open(rFile);
		test.seekg(0, test.beg);
		test.read(buffer, 8192);
		memcpy(&ssbTemp, &buffer, 8192);

		//if ssbTemp.Info[j] is an iNode we check if its live
		for(int i = 8; i < 1024; i++){
			if(newCleanSegs >= 1) break;
			if(ssbTemp.Info[i].second == -1){
			//	cout << "iNode #: " << ssbTemp.Info[i].first << endl;
			//	cout << iMap[0] << endl;
				if(i+(1024*dirtySeg) == iMap[ssbTemp.Info[i].first]){
					test.seekg(i*1024, test.beg);
					block A;
					iNode IN;
					test.read(buffer2, 1024);
					memcpy(&IN, &buffer2, 1024);
					A.IN = IN;
					segment[index] = A;
					cleanSSB.Info[index] = pair<int, int>(ssbTemp.Info[i].first, -1);
				//	cout << ssbTemp.Info[i].first << " at location " << index+(1024*cleanSeg) << endl;
					index++;

					int temp[256];
					int ind = files.at(IN.fileName)/256;
					ind = ind*256;
					for(int i = 0; i < 256; i++){
						temp[i] = iMap[ind];
						ind++;
					}
					block C;
					memcpy(&C.iMapPart, &temp, sizeof(temp));
					segment[index] = C;
					CR[files.at(IN.fileName)/256] = index+(1024*cleanSeg);
					cleanSSB.Info[index] = pair<int, int>(files.at(IN.fileName)/256, -2);
					index++;
				}
			}
			else if(ssbTemp.Info[i].second > -1){
				iNode temp;	//if ssbTemp.Info[j] is a dataBlock we read in the corresponding iNode to check if its live
				if(iMap[ssbTemp.Info[i].first]/1024 == dirtySeg){
					test.seekg((iMap[ssbTemp.Info[i].first]%1024)*1024, test.beg);
					test.read(buffer2, 1024);
					memcpy(&temp, &buffer2, 1024);
				}
				else{
					ifstream test3;
					string rFile;
					rFile = "DRIVE/SEGMENT" + to_string(iMap[ssbTemp.Info[i].first]/1024) + ".txt";
					//cout << "reading from " << rFile << endl;
					test3.open(rFile);
					test3.seekg(iMap[ssbTemp.Info[i].first], test3.beg);
					test3.read(buffer2, 1024);
					memcpy(&temp, &buffer2, 1024);
					test3.close();
				}
				updatedNode = temp;
				if(temp.blocks[ssbTemp.Info[i].second] == i+(1024*dirtySeg)){
					test.seekg(i*1024, test.beg);
					block B;
					dataBlock db;
					test.read(buffer2, 1024);
					memcpy(&db.buffer, &buffer2, 1024);
					B.db = db;
					segment[index] = B;
					blocks2[ssbTemp.Info[i].second] = index+(1024*cleanSeg);
					//updatedNode.blocks[ssbTemp.Info[i].second] = index+(1024*cleanSeg);
					cleanSSB.Info[index] = pair<int, int>(ssbTemp.Info[i].first, ssbTemp.Info[i].second);
					index++;
					if(ssbTemp.Info[i].second == updatedNode.fileSize/1024 || index == 1022 || i == 1022){
						block D;
						//writes updated iNode for the dataBlocks and iMapPart
						for(int j = 0; j < 128; j++){
							updatedNode.blocks[j] = blocks2[j];
						}
						D.IN = updatedNode;
						segment[index] = D;
						iMap[files.at(updatedNode.fileName)] = index+(1024*cleanSeg);
				//		cout << "updating " << files.at(updatedNode.fileName) << "to go to " << index+(1024*cleanSeg) << endl;
						cleanSSB.Info[index] = pair<int, int>(files.at(updatedNode.fileName), -1);
						index++;
						int temp[256];
						int ind = files.at(updatedNode.fileName)/256;
						ind = ind*256;
						for(int i = 0; i < 256; i++){
							temp[i] = iMap[ind];
							ind++;
						}
						block C;
						memcpy(&C.iMapPart, &temp, sizeof(temp));
						segment[index] = C;
						CR[files.at(updatedNode.fileName)/256] = index+(1024*cleanSeg);
						cleanSSB.Info[index] = pair<int, int>(files.at(updatedNode.fileName)/256, -2);
						index++;
					}
				}
			}
			else if(ssbTemp.Info[i].second == -2){
				if(i+(1024*dirtySeg) == CR[i]){
					test.seekg(i*1024, test.beg);
					block C;
					test.read(buffer2, 1024);
					memcpy(&C.iMapPart, &buffer2, 1024);
					segment[index] = C;
					index++;
				}
			}

			if(index == 1024){
				index = 8;
				memcpy(&segment[0], &cleanSSB, 8192);
				ofstream myfile;
				string wFile;
				wFile = "DRIVE/SEGMENT" + to_string(cleanSeg) + ".txt";
				cout << "Writing to " << wFile << endl;
				myfile.open(wFile);
				myfile.write((char *)segment, 1024*1024);
				myfile.close();
				newCleanSegs++;
				cleanSegs[cleanSeg] = 1;
				for(int i = j; i < 64; i++){
					if(cleanSegs[i] == 0){
						cleanSeg = i;
						//cout << "Clean seg: " << cleanSeg << endl;
						j = i+1;
						break;
					}
				}
			//	cout << memIndex << endl;
			//	cout << memIndex << endl;
			}
			if(i == 1023){
				cleanSegs[dirtySeg] = 0;
			}
		}

		cleanSegs[dirtySeg] = 0;
		test.close();
	}

}
