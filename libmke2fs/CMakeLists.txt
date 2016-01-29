cmake_minimum_required (VERSION 2.8)
project (libmke2fs)

ADD_DEFINITIONS(-DDEBUG -g)

#AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} LIB_SRCFILE)
#MESSAGE(${LIB_SRCFILE})
SET(MKE2FS_SRC
	mke2fs.c
	util.c
	profile.c
	default_profile.c
	prof_err.c
)
#ADD_EXECUTABLE(mke2fs ${MKE2FS_SRC})
ADD_LIBRARY(mke2fsobj SHARED ${MKE2FS_SRC})

#TARGET_LINK_LIBRARIES(
#	mke2fs
#
#	e2p
#	uuid
#	blkid
#	com_err
#	ext2fs.a
#)

SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/../lib)

INSTALL(TARGETS mke2fsobj
		RUNTIME DESTINATION bin
		LIBRARY DESTINATION lib
		ARCHIVE DESTINATION lib_static)
