cmake_minimum_required (VERSION 2.8)
project (mdadm_unitest)

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/../
)

#AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} SRCFILE)
SET(SRCFILE mymdadm.c)
set_source_files_properties(${SRCFILE} PROPERTIES LANGUAGE CXX )
ADD_EXECUTABLE(mdadm_unitest ${SRCFILE})

TARGET_LINK_LIBRARIES(mdadm_unitest
	pthread
	${CMAKE_CURRENT_BINARY_DIR}/../libmdadmobj.so
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
	${CMAKE_CURRENT_BINARY_DIR}/../libmdadmobj.so
)

IF (${NUUO})
TARGET_LINK_LIBRARIES(
	RAIDManager_test
	${CMAKE_CURRENT_BINARY_DIR}/../../common/lib/libcommonobj.so
)
ENDIF (${NUUO})
