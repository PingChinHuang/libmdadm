cmake_minimum_required (VERSION 2.8)
project (mdadm_unitest)

INCLUDE_DIRECTORIES(
	${CMAKE_CURRENT_SOURCE_DIR}/../
)

AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} SRCFILE)
ADD_EXECUTABLE(mdadm_unitest ${SRCFILE})

TARGET_LINK_LIBRARIES(mdadm_unitest
	pthread
	${CMAKE_CURRENT_BINARY_DIR}/../libmdadmobj.so
)
