cmake_minimum_required (VERSION 2.8)
project (libe2fsck_unitest)

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/../
)

SET(SRCFILE e2fsck_unitest.c)
ADD_EXECUTABLE(e2fsck_unitest ${SRCFILE})

TARGET_LINK_LIBRARIES(e2fsck_unitest
	pthread
	${CMAKE_CURRENT_BINARY_DIR}/../libe2fsckobj.so
	ext2fs
	blkid
	e2p
	com_err
	uuid
)

ADD_DEPENDENCIES(
	e2fsck_unitest
	e2fsckobj
)

