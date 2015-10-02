cmake_minimum_required (VERSION 2.8)
project (raid_manager_unitest)

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/..
	${CMAKE_CURRENT_SOURCE_DIR}/../libmdadm
)

SET(RAIDMANAGER_SRC
	RAIDManager_test.cpp
	TestingLoop.cpp
)

ADD_EXECUTABLE(RAIDManager_test
	${RAIDMANAGER_SRC}
)

TARGET_LINK_LIBRARIES(
	RAIDManager_test
	pthread
	${CMAKE_CURRENT_BINARY_DIR}/../libraidmgrobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../libmdadm/libmdadmobj.so
)

ADD_DEPENDENCIES(
	RAIDManager_test
	raidmgrobj mdadmobj
)

IF (${NUUO})
TARGET_LINK_LIBRARIES(
	RAIDManager_test
	${CMAKE_CURRENT_BINARY_DIR}/../../common/lib/libcommonobj.so
)

ADD_DEPENDENCIES(
	RAIDManager_test
	commonobj
)
ENDIF (${NUUO})
