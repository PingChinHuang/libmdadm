#include "RAIDManager.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

bool g_bLoop = true;

extern void TestingLoop(const char* cfg, RAIDManager& raid_mgr);

static void sig_handler(int sig)
{
	if (sig == SIGINT) {
		g_bLoop = false;
		printf("received SIGINT\n");
	}
}

int main(int argc, char* argv[])
{
	int opt;
	RAIDManager raid_mgr;
	int level = -1;
	string strMDName;
	bool bCreate = false;
	bool bAddDisk = false;
	bool bRemove = false;
	bool bFile = false;
	vector<string> vDevList;
	vector<RAIDInfo> list;
	char testing_scenario_file[256];
	
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	while ((opt = getopt(argc, argv, "arcl:f:")) != -1) {
		switch (opt) {
		case 'a':
			bAddDisk = true;
			break;
		case 'r':
			bRemove = true;
			break;
		case 'c':
			bCreate = true;
			break;
		case 'l':
			level = atoi(optarg);
			break;
		case 'f':
			bFile = true;
			strncpy(testing_scenario_file, optarg, sizeof(testing_scenario_file) - 1);
			break;
		}
	}
	
	if (bFile) {
		TestingLoop(testing_scenario_file, raid_mgr);
		return 0;
	}

	if (argc > optind) {
		for (int i = optind; i < argc; i++) {
			vDevList.push_back(argv[i]);
		}
	}

	if (bAddDisk) {
		for (int i = 0; i < vDevList.size(); i++)
			raid_mgr.AddRAIDDisk(vDevList[i]);
	}
	
	if (bRemove) {
		for (int i = 0; i < vDevList.size(); i++)
			raid_mgr.RemoveRAIDDisk(vDevList[i]);	
	}
	
	if (bCreate) {
		if (level < 0) return 1;
		if (vDevList.empty()) return 1;
		raid_mgr.CreateRAID(vDevList, level, strMDName);
	}

	while (g_bLoop) {
		raid_mgr.GetRAIDInfo(list);

		for (size_t i = 0; i < list.size(); i ++) {
			list[i].Dump();	
		}
		
		sleep(10);
	
		raid_mgr.UpdateRAIDInfo();
	}
	
	for (size_t i = 0; i < list.size(); i ++)
		raid_mgr.StopRAID(list[i].m_strDevNodeName);
	
	for (int i = 0; i < vDevList.size(); i++)
		raid_mgr.RemoveRAIDDisk(vDevList[i]);

	return 0;
}
