#ifndef CPLANET_H
#define CPLANET_H 1

#include <err.h>
#include <iconv.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <time.h>
#include <locale.h>
#include <unistd.h>

/* libmrss */
#include <mrss.h>

/* clearsilver */
#include <ClearSilver.h>

#define CPLANET_VERSION "0.2"

#define CP_NAME "CPlanet.Posts.%i.Name=%s"
#define CP_FEEDNAME "CPlanet.Posts.%i.FeedName=%s"
#define CP_AUTHOR "CPlanet.Posts.%i.Author=%s"
#define CP_TITLE "CPlanet.Posts.%i.Title=%s"
#define CP_LINK "CPlanet.Posts.%i.Link=%s"
#define CP_DATE "CPlanet.Posts.%i.Date=%lld"
#define CP_FORMATED_DATE  "CPlanet.Posts.%i.FormatedDate=%s"
#define CP_DESCRIPTION "CPlanet.Posts.%i.Description=%s"
#define CP_TAG "CPlanet.Posts.%i.Tags.%i.Tag=%s"
#define CP_VERSION "CPlanet.Version=%s"
#define CP_GEN_DATE "CPlanet.GenerationDate=%s"

/* CPlanet Setters */

#define cp_set_name(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_NAME, ##__VA_ARGS__)
#define cp_set_feedname(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_FEEDNAME, ##__VA_ARGS__)
#define cp_set_author(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_AUTHOR, ##__VA_ARGS__)
#define cp_set_title(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_TITLE, ##__VA_ARGS__)
#define cp_set_link(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_LINK, ##__VA_ARGS__)
#define cp_set_date(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_DATE, ##__VA_ARGS__)
#define cp_set_formated_date(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_FORMATED_DATE, ##__VA_ARGS__)
#define cp_set_description(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_DESCRIPTION, ##__VA_ARGS__)
#define cp_set_tag(hdf_dest, ...) hdf_set_valuef(hdf_dest, CP_TAG, ##__VA_ARGS__)
#define cp_set_version(hdf_dest) hdf_set_valuef(hdf_dest, CP_VERSION, CPLANET_VERSION)

/* CPlanet Getters */
#define HDF_FOREACH(var, hdf, node) 		\
	for ((var) = hdf_get_obj((hdf),node); 	\
	     (var);				\
	     (var) = hdf_obj_next((var)))

#endif
