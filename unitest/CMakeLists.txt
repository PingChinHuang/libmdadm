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
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libraidmgrobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libfuserobj.so
	sgutils2.a
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libmdadmobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libfsmgrobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libmke2fsobj.so

	ext2fs
	blkid
	com_err
	e2p
	uuid
	udev
	atasmart.a
)

ADD_DEPENDENCIES(
	RAIDManager_test
	raidmgrobj mdadmobj
	fsmgrobj mke2fsobj
)


SET(FILESYSTEMMANAGER_SRC
	FilesystemManager_test.cpp
)

ADD_EXECUTABLE(FSManager_test
	${FILESYSTEMMANAGER_SRC}
)

TARGET_LINK_LIBRARIES(
	FSManager_test

	${CMAKE_CURRENT_BINARY_DIR}/../lib/libfsmgrobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libmke2fsobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../lib/libfuserobj.so

	pthread
	ext2fs
	blkid
	com_err
	e2p
	uuid
	
	#${CMAKE_CURRENT_BINARY_DIR}/../libfsmgrobj.so
	#${CMAKE_CURRENT_BINARY_DIR}/../libmke2fs/libmke2fsobj.so

)

ADD_DEPENDENCIES(
	RAIDManager_test
	fsmgrobj mke2fsobj
)

IF (${NUUO})
TARGET_LINK_LIBRARIES(
	RAIDManager_test
	${CMAKE_CURRENT_BINARY_DIR}/../../common/lib/libcommonobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../../apr/lib/libaprobj.so
)

TARGET_LINK_LIBRARIES(
	FSManager_test
	${CMAKE_CURRENT_BINARY_DIR}/../../common/lib/libcommonobj.so
	${CMAKE_CURRENT_BINARY_DIR}/../../apr/lib/libaprobj.so
)

ADD_DEPENDENCIES(
	RAIDManager_test
	commonobj aprobj
)
SET(Target RAIDManager_test)
SET(USE_APR1 "1")
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/../../../../thirdparty_cmake.txt)

ADD_DEPENDENCIES(
	FSManager_test
	commonobj aprobj
)

SET(Target FSManager_test)
#SET(USE_APR1 "1")
INCLUDE(${CMAKE_CURRENT_SOURCE_DIR}/../../../../thirdparty_cmake.txt)

ENDIF (${NUUO})
