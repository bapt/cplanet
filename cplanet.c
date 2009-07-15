/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */ 

/* vim:set ts=4 sw=4 sts=4: */

#define CPLANET_VERSION "0.1"
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <time.h>
#include <mrss.h>
#include <cs/cs.h>
#include <iconv.h>
#include <limits.h>
#include <util/neo_misc.h>
#include <util/neo_hdf.h>
#include <sys/stat.h>


#define CP_NAME "CPlanet.Posts.%i.Name=%s"
#define CP_FEEDNAME "CPlanet.Posts.%i.FeedName=%s"
#define CP_AUTHOR "CPlanet.Posts.%i.Author=%s"
#define CP_TITLE "CPlanet.Posts.%i.Title=%s"
#define CP_LINK "CPlanet.Posts.%i.Link=%s"
#define CP_DATE "CPlanet.Posts.%i.Date=%s"
#define CP_DESCRIPTION "CPlanet.Posts.%i.Description=%s"
#define CP_TAG "CPlanet.Posts.%i.Tags.%i.Tag=%s"

#define W3_ATOM "http://www.w3.org/2005/Atom"
#define PURL_ATOM "http://purl.org/atom/ns#"
#define PURL_RSS "http://purl.org/rss/1.0/modules/content/"

/* convert RFC822 to epoch time */

time_t
str_to_time_t(char *s)
{
	struct tm date;
	time_t t;
	errno = 0;
	char *pos = strptime(s, "%a, %d %b %Y %T", &date);
	if (pos == NULL)
		err(1, "Convert '%s' to struct tm failed", s);
	t = mktime(&date);
	if (t == (time_t)-1)
		err(1, "Convert struct tm (from '%s') to time_t failed", s);
	return t;
}

/* sort posts by date */

int 
sort_obj_by_date(const void *a, const void *b) {
	HDF **ha = (HDF **)a;
	HDF **hb = (HDF **)b;
	time_t atime = str_to_time_t(hdf_get_valuef(*ha, "Date"));
	time_t btime = str_to_time_t(hdf_get_valuef(*hb, "Date"));
	if (atime < btime) return 1;
	if (atime == btime) return 0;
	return -1;
}

/* prepare the string to be written to the html file */

static NEOERR *
cplanet_output (void *ctx, char *s)
{
	STRING *str = (STRING *)ctx;
	NEOERR *neoerr;

	neoerr = nerr_pass(string_append(str, s));
	return neoerr;
}

char * 
str_to_UTF8(char *source_encoding, char *str)
{
	size_t insize;
	size_t outsize;
	size_t ret;
	char *output;
	if (!strcmp(source_encoding, "UTF-8"))
		return str;

	iconv_t conv;
	const char * inptr = str;
	conv = iconv_open("UTF-8", source_encoding);
	if (conv == (iconv_t) -1) {
		warn("conversion from '%s' to UTF-8 not available", source_encoding);
		return str;
	}
	if (str == NULL)
		return str;
	insize = strlen(str);
	outsize = 4 * insize + 2;
	output = malloc(outsize);
	memset(output, 0, outsize);
	char *outputptr = output;
	ret = iconv(conv, (const char**) &inptr, &insize, &outputptr, &outsize);
	if (ret ==(size_t) -1) {
		warn("Conversion Failed");
		free(output);
		iconv_close(conv);
		return str;
	}
	iconv_close(conv);
	return output;
}

/* retreive ports and prepare the dataset for the template */

int 
get_posts(HDF *hdf_cfg, HDF* hdf_dest, int pos, int days)
{
	mrss_t *feed;
	mrss_error_t ret;
	mrss_item_t *item;
	CURLcode code;
	char *encoding;
	char *name = hdf_get_valuef(hdf_cfg, "Name");
	ret = mrss_parse_url_with_options_and_error(hdf_get_valuef(hdf_cfg, "URL"), &feed, NULL, &code);
	if (ret) {
		warn("MRSS return error: %s, %s\n",
				ret ==
				MRSS_ERR_DOWNLOAD ? mrss_curl_strerror(code) :
				mrss_strerror(ret), name);
		return pos;
	}
	encoding = feed->encoding;
	for (item = feed->item; item; item = item->next) {
		time_t t_now, t_comp;
		t_comp = str_to_time_t(item->pubDate);
		time(&t_now);
		if(t_now - t_comp >= days)
			continue;
		mrss_category_t *tags;
		mrss_tag_t *content; 
		hdf_set_valuef(hdf_dest, CP_NAME, pos, name);
		hdf_set_valuef(hdf_dest, CP_FEEDNAME, pos, str_to_UTF8(encoding, feed->title));
		if (item->author != NULL)
			hdf_set_valuef(hdf_dest, CP_AUTHOR, pos, str_to_UTF8(encoding, item->author));
		hdf_set_valuef(hdf_dest, CP_TITLE, pos, str_to_UTF8(encoding, item->title));
		hdf_set_valuef(hdf_dest, CP_LINK, pos, item->link);
		hdf_set_valuef(hdf_dest, CP_DATE, pos, item->pubDate);
		/* Description is only for description tag, we want content if exists */
		if (feed->version == MRSS_VERSION_ATOM_0_3 || feed->version == MRSS_VERSION_ATOM_1_0) {
			if ((mrss_search_tag(item, "content", W3_ATOM, &content) == MRSS_OK && content) || 
					(mrss_search_tag(item, "content", PURL_ATOM, &content) == MRSS_OK && content)) {
				hdf_set_valuef(hdf_dest, CP_DESCRIPTION, pos, str_to_UTF8(encoding, content->value));
			} else {
				hdf_set_valuef(hdf_dest, CP_DESCRIPTION, pos, str_to_UTF8(encoding, item->description));
			}
		} else {
			if (mrss_search_tag(item, "encoded", PURL_RSS, &content) == MRSS_OK && content) {
				hdf_set_valuef(hdf_dest, CP_DESCRIPTION, pos, str_to_UTF8(encoding, content->value));
			} else {
				hdf_set_valuef(hdf_dest, CP_DESCRIPTION, pos, str_to_UTF8(encoding, item->description));
			}
		}
		mrss_free(content);
		/* Get the categories/tags */
		int nb_tags=0;
		for (tags = item->category; tags; tags = tags->next) {
			hdf_set_valuef(hdf_dest, "CPlanet.Posts.%i.Tags.%i.Tag=%s", pos, nb_tags, str_to_UTF8(encoding, tags->category));
			nb_tags++;
		}
		mrss_free(tags);
		pos++;
	}
	mrss_free(item);

	return pos;
}


void
generate_file(HDF *output_hdf, HDF *hdf)
{
	NEOERR *neoerr;
	CSPARSE *parse;
	STRING cs_output_data;
	string_init(&cs_output_data);
	neoerr = cs_init(&parse,hdf);
	if (neoerr != STATUS_OK) {
		nerr_log_error(neoerr);
		return;
	}
	char *cs_output=hdf_get_valuef(output_hdf,"Path");
	char *cs_path=hdf_get_valuef(output_hdf,"TemplatePath");
	neoerr = cs_parse_file(parse, cs_path);
	if (neoerr != STATUS_OK) {
		neoerr = nerr_pass(neoerr);
		nerr_log_error(neoerr);
		return;
	}
	neoerr = cs_render(parse, &cs_output_data, cplanet_output);
	if (neoerr != STATUS_OK) {
		neoerr = nerr_pass(neoerr);
		nerr_log_error(neoerr);
		return;
	}
	FILE *output = fopen(cs_output, "w+");
	if (output == NULL)
		err(1,"%s",cs_output);
	fprintf(output,"%s",cs_output_data.buf);
	fflush(output);
	fclose(output);
	cs_destroy(&parse);
	string_clear(&cs_output_data);
}


static void
usage()
{
	errx(1, "usage: cplanet -c conf.hdf\n");
}

int 
main (int argc, char *argv[])
{
	NEOERR *neoerr;
	HDF *hdf;
	HDF *feed_hdf;
	HDF *output_hdf;
	int pos = 0;
	int days = 0;
	int ch = 0;
	char *hdf_file = NULL;
	if (argc == 1)
		usage();
	while ((ch = getopt(argc, argv, "c:h")) != -1)
		switch (ch) {  
			case 'h':
				usage();
				break;
			case 'c':
				hdf_file = optarg;
				if(access(hdf_file, F_OK | R_OK) == -1 )
					err(1,"%s",hdf_file);
				break;
			case '?':
			default:
				usage();
		}
	argc -= optind;
	argv += optind;

	neoerr = hdf_init(&hdf);
	if (neoerr != STATUS_OK) {
		nerr_log_error(neoerr);
		return -1;
	}
	neoerr = hdf_init(&feed_hdf);

	/* Read the hdf file */
	neoerr = hdf_read_file(hdf, hdf_file);
	if (neoerr != STATUS_OK) {
		nerr_log_error(neoerr);
		return -1;
	}
	hdf_set_valuef(hdf, "CPlanet.Version=%s", CPLANET_VERSION);
	char * buf = hdf_get_valuef(hdf, "CPlanet.Days");
	errno = 0;
	char *ep;
	long ldays = strtol(buf,&ep,10);
	if (buf[0] == '\0' || *ep != '\0')
		err(1,"[%s]: not a number", buf);
	if((errno == ERANGE && (ldays == LONG_MAX || ldays == LONG_MIN )) ||
					(ldays > INT_MAX || ldays <INT_MIN))
		err(1,"[%s]: out of range",buf);
	days = (int)ldays;
	days = days * 24 * 60 * 60;
	feed_hdf = hdf_get_obj(hdf, "CPlanet.Feed.0");
	pos = get_posts(feed_hdf, hdf, pos, days);
	while ((feed_hdf = hdf_obj_next(feed_hdf)) != NULL) {
		pos = get_posts(feed_hdf, hdf, pos, days);
	}
	hdf_sort_obj(hdf_get_obj(hdf, "CPlanet.Posts"), sort_obj_by_date);
	/* get every output set in the hdf file and generate them */
	output_hdf = hdf_get_obj(hdf,"CPlanet.Output.0");
	generate_file(output_hdf,hdf);
	while (( output_hdf = hdf_obj_next(output_hdf)) != NULL) {
		generate_file(output_hdf,hdf);
	}
	hdf_destroy(&hdf);
	hdf_destroy(&feed_hdf);
	return EXIT_SUCCESS;
}
