#include "RAIDManager.h"

#include <stdio.h>
#include <string.h>

#include <string>
#include <vector>

enum {
	OP_ADDDISK = 'a',
	OP_REMDISK = 'r',
	OP_CREATERAID = 'C',
	OP_STOPRAID = 'S',
	OP_DELETERAID = 'D',
	OP_ASSEMBLERAID = 'A',
	OP_MANAGEMDDISK = 'M',
	//OP_FAULTYDISK,
	//OP_REMMDDISK,
	//OP_ADDMDDISK,
	//OP_READDMDDISK,
	//OP_REPLACEMDDISK,
	OP_DONOTHING = 'N',
};

enum {
	MANAGE_MarkFaulty = 'f',
	MANAGE_Remove = 'r',
	MANAGE_Add = 'a',
	MANAGE_Readd = 'A',
	MANAGE_Replace = 'R',
};

#define OPERATION_CASE(op)	\
	case OP_##op:		\
		printf("%s\n", #op);	\
		Handle##op(content, raid_mgr);	\
		break;

#define MANAGE_CASE(op)	\
	case MANAGE_##op:				\
		printf("operation = %c\n", operation);	\
		raid_mgr.op##MDDisks(mddev, vdisks);	\
		break;


extern bool g_bLoop;

using namespace std;

static void tokenize(char* target, vector<string>& tokens)
{
	char *saveptr = NULL;
	char *p = strtok_r(target, ",", &saveptr);

	tokens.clear();
	while (p) {
		printf ("Token: %s\n", p);
		tokens.push_back(p);
		p = strtok_r(NULL, ",", &saveptr);
	}	
}

static void HandleADDDISK (char* content, RAIDManager& raid_mgr)
{
	vector<string> disks;
	tokenize(content, disks);
	for (size_t i = 0; i < disks.size(); i++) {
		raid_mgr.AddDisk(disks[i], DISK_TYPE_SATA);
	}
}

static void HandleREMDISK (char* content, RAIDManager& raid_mgr)
{
	vector<string> disks;
	tokenize(content, disks);
	for (size_t i = 0; i < disks.size(); i++) {
		raid_mgr.RemoveDisk(disks[i]);
	}
}

static void HandleCREATERAID (const char* content, RAIDManager& raid_mgr)
{
	int lv = 0;
	char disks[1024];
	vector<string> vdisks;
	string strMDName;
	sscanf(content, "%d,%1023s", &lv, disks);
	tokenize(disks, vdisks);
	printf("lv = %d\n", lv);
	raid_mgr.CreateRAID(vdisks, lv, strMDName);
	raid_mgr.Format(strMDName);
}

static void HandleMANAGEMDDISK (const char* content, RAIDManager& raid_mgr)
{
	char operation;
	char disks[1024];
	char mddev[16];
	vector<string> vdisks;
	sscanf(content, "%c,%15[^,],%1023s", &operation, mddev, disks);
	tokenize(disks, vdisks);
	printf("mddev = %s\n", mddev);

	switch(operation) {
	MANAGE_CASE(MarkFaulty)
	MANAGE_CASE(Remove)
	MANAGE_CASE(Add)
	MANAGE_CASE(Readd)
	case MANAGE_Replace:
		printf("Replace\n");
		if (vdisks.size() == 2) {
			raid_mgr.ReplaceMDDisk(mddev, vdisks[0], vdisks[1]);
		} else {
			printf("Disk number is not accepted\n");
		}
		break;
	default:;
	}
}

static void HandleSTOPRAID (const char* content, RAIDManager& raid_mgr)
{
	if (strncmp("all", content, sizeof("all")) == 0) {
		vector<RAIDInfo> list;
		raid_mgr.GetRAIDInfo(list);
		for (size_t i = 0; i < list.size(); i ++) {
			int stat = 0, progress = 0;
			//if (raid_mgr.GetFormatProgress(list[i].m_strDevNodeName, stat, progress)) {
			//	printf("stat %d, progress %d\n", stat, progress);
			//	continue;
			//}

			raid_mgr.StopRAID(list[i].m_strDevNodeName);
		}
	} else {
		raid_mgr.StopRAID(content);
	}
}

static void HandleDELETERAID (const char* content, RAIDManager& raid_mgr)
{
	if (strncmp("all", content, sizeof("all")) == 0) {
		vector<RAIDInfo> list;
		raid_mgr.GetRAIDInfo(list);
		for (size_t i = 0; i < list.size(); i ++)
			raid_mgr.DeleteRAID(list[i].m_strDevNodeName);
	} else {
		raid_mgr.DeleteRAID(content);
	}
}
#if 0
static void HandleFAULTYDISK (const char* content)
{
}

static void HandleREMMDDISK (const char* content)
{
}

static void HandleADDMDDISK (const char* content)
{
}

static void HandleREADDMDDISK (const char* content)
{
}

static void HandleREPLACEMDDISK (const char* content)
{
}
#endif 
static void HandleASSEMBLERAID (const char* content, RAIDManager& raid_mgr)
{
	char type[5];
	char info[1024];
	sscanf(content, "%4[^,],%1023s", type, info);

	printf("type = %s\n", type);
	if (strncmp("disk", type, strlen("disk")) == 0) {
		vector<string> vdisks;
		string strMDName;
		tokenize(info, vdisks);
		raid_mgr.AssembleRAID(vdisks, strMDName);
	} else if (strncmp("uuid", type, strlen("uuid")) == 0) {

	}
}

static void HandleDONOTHING (const char* content, RAIDManager& raid_mgr)
{
	raid_mgr.UpdateRAIDInfo();

	vector<RAIDInfo> list;
	raid_mgr.GetRAIDInfo(list);
	for (size_t i = 0; i < list.size(); i++) {
			list[i].Dump();
	}
	raid_mgr.Dump();
}

void TestingLoop(const char* cfg, RAIDManager& raid_mgr)
{
	FILE *pFile = fopen(cfg, "r");
	char line[1024];
	char content[1024];
	char o = OP_DONOTHING;

	if (pFile == NULL) {
		printf("Fail to open file\n");
		return;
	}

	//while (g_bLoop) {
		while (fgets(line, 1023, pFile) != NULL && g_bLoop) {
			if (line[0] == '#')
				continue;
			printf("\nRun: %s", line);
			sscanf(line, "%c:%1023[^:\n]", &o, content);

			switch (o) {
			OPERATION_CASE(ADDDISK)
			OPERATION_CASE(REMDISK)
			OPERATION_CASE(CREATERAID)
			OPERATION_CASE(STOPRAID)
			OPERATION_CASE(DELETERAID)
			//OPERATION_CASE(FAULTYDISK)
			//OPERATION_CASE(REMMDDISK)
			//OPERATION_CASE(ADDMDDISK)
			//OPERATION_CASE(READDMDDISK)
			//OPERATION_CASE(REPLACEMDDISK)
			OPERATION_CASE(ASSEMBLERAID)
			OPERATION_CASE(MANAGEMDDISK)
			OPERATION_CASE(DONOTHING)
			default:
				break;
			}

			sleep(10);
		}
	//}

	fclose(pFile);
}
