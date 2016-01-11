cmake_minimum_required (VERSION 2.8)
project (libmdadm)

ADD_DEFINITIONS(-DBINDIR="/sbin" -DDEBUG -g)

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

SET(LIBRARY_OUTPUT_PATH ${PROJECT_BINARY_DIR}/../lib)
