cmake_minimum_required (VERSION 2.8.12)
project (mdadm_library)

ADD_DEFINITIONS(-DBINDIR="/sbin" -DDEBUG)

#AUX_SOURCE_DIRECTORY(${CMAKE_CURRENT_SOURCE_DIR} LIB_SRCFILE)
#MESSAGE(${LIB_SRCFILE})
#ADD_LIBRARY(mdadmobj_s STATIC ${LIB_SRCFILE})

SET(MDADM_SRCFILE 
	Manage.c
	Grow.c
	maps.c
	xmalloc.c
	policy.c
	Create.c
	super-gpt.c
	Incremental.c
	platform-intel.c
	sysfs.c
	sg_io.c
	super1.c
	Query.c
	config.c
	mdopen.c
	Detail.c
	util.c
	sha1.c
	lib.c
	probe_roms.c
	Build.c
	mdstat.c
	mapfile.c
	super0.c
	bitmap.c
	super-ddf.c
	Kill.c
	Dump.c
	msg.c
	Assemble.c
	super-mbr.c
	Monitor.c
	dlink.c
	Examine.c
	super-intel.c
	crc32.c
	restripe.c
)

SET(MDMON_SRCFILE
	monitor.c
	managemon.c
)

ADD_LIBRARY(mdadmobj SHARED ${MDADM_SRCFILE})
ADD_LIBRARY(mdmonobj SHARED ${MDMON_SRCFILE})

ADD_SUBDIRECTORY(unitest)
