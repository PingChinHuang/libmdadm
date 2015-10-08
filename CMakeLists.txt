cmake_minimum_required (VERSION 2.8)
project (raid_manager)

OPTION(NUUO "Enable to use NUUO utility libraries" ON)
INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/libmdadm
	${CMAKE_CURRENT_SOURCE_DIR}/libmke2fs
)

IF (${NUUO})
INCLUDE_DIRECTORIES(${SysUtilsPath})
ADD_DEFINITIONS(-DNUUO)
ENDIF (${NUUO})

SET(RAIDMANAGER_SRCFILE
	RAIDManager.cpp
	test_utils.cpp
)

SET(FILESYSTEMMANAGER_SRCFILE
	FilesystemManager.cpp
	test_utils.cpp
)

ADD_LIBRARY(raidmgrobj SHARED ${RAIDMANAGER_SRCFILE})
ADD_LIBRARY(fsmgrobj SHARED ${FILESYSTEMMANAGER_SRCFILE})

ADD_SUBDIRECTORY(libmdadm)
ADD_SUBDIRECTORY(libmke2fs)
ADD_SUBDIRECTORY(unitest)
