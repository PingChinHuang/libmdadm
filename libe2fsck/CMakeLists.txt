cmake_minimum_required (VERSION 2.8)
project (libe2fsck)

INCLUDE_DIRECTORIES(${PROJECT_SOURCE_DIR}/../libmke2fs)

ADD_DEFINITIONS(-DDEBUG -g -DROOT_SYSCONFDIR="/usr" -DHAVE_SIGNAL_H -DENABLE_HTREE -DENABLE_COMPRESSION)

SET(E2FSCK_SRC
	e2fsck.c
	dict.c
	super.c
	pass1.c
	pass1b.c
	pass2.c
	pass3.c
	pass4.c
	pass5.c
	journal.c
	recovery.c
	revoke.c
	badblocks.c
	util.c
	e2fsck_lib.c
	dirinfo.c
	dx_dirinfo.c
	ehandler.c
	problem.c
	message.c
	ea_refcount.c
	rehash.c
	region.c
	../libmke2fs/profile.c
	logfile.c
	../libmke2fs/prof_err.c
	sigcatcher.c
	crc32.c
)

#ADD_EXECUTABLE(mke2fs ${MKE2FS_SRC})
ADD_LIBRARY(e2fsckobj SHARED ${E2FSCK_SRC})

#SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/../lib)

ADD_SUBDIRECTORY(./unitest)
