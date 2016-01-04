cmake_minimum_required (VERSION 2.8)
project (libmdadm_unitest)

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

ADD_DEPENDENCIES(
	mdadm_unitest
	mdadmobj
)

#ADD_EXECUTABLE(mdstat_read mdstat_read.c)

#TARGET_LINK_LIBRARIES(mdstat_read
#	${CMAKE_CURRENT_BINARY_DIR}/../libmdadmobj.so
#)

#ADD_DEPENDENCIES(
#	mdstat_read
#	mdadmobj
#)
