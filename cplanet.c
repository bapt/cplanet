/* This program is free software. It comes without any warranty, to
 * the extent permitted by applicable law. You can redistribute it
 * and/or modify it under the terms of the Do What The Fuck You Want
 * To Public License, Version 2, as published by Sam Hocevar. See
 * http://sam.zoy.org/wtfpl/COPYING for more details. */ 

/*
 * vim: set s=4 sw=4 sts=4:
 */
#define CPLANET_VERSION "0.1"
#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <string.h>
#include <time.h>
#include <mrss.h>
#include <cs/cs.h>
#include <iconv.h>
#include <util/neo_misc.h>
#include <util/neo_hdf.h>


/* function to create the atom feed */
/*void 
  create_atom(HDF *hdf)
  {
  mrss_t *d;
  mrss_error_t err;
  HDF *subhdf;
  d=NULL; // ->this is important! If d!=NULL, mrss_new doesn't alloc memory.
  mrss_new(&d);

  err=mrss_set (d,
  MRSS_FLAG_VERSION, MRSS_VERSION_2_0,
  MRSS_FLAG_TITLE, hdf_get_valuef(hdf,"CPlanet.Name"),
  MRSS_FLAG_TTL, 12,
  MRSS_FLAG_END);

  if(err!=MRSS_OK) warn("bla%s\n",mrss_strerror(err));

  subhdf = hdf_get_obj(hdf,"CPlanet.Posts.0");
  while ((subhdf = hdf_obj_next(subhdf)) != NULL) {
  mrss_t *item = 
  }

  err=mrss_write_file( d, "/home/bapt/planet/www/index.atom");
  if(err!=MRSS_OK) warn("%s\n",mrss_strerror(err));
  }
  */
/* convert RFC822 to epoch time */

int
date_to_unix(char *s)
{
    struct tm date;
    char date_s [16];
    strptime(s,"%a, %d %b %Y %T",&date);
    strftime (date_s, sizeof date_s, "%s", &date);
    return atoi(date_s);
}

/* sort posts by date */

int 
sort_obj_by_date(const void *a, const void *b) {
    HDF **ha = (HDF **)a;
    HDF **hb = (HDF **)b;
    int atime = date_to_unix(hdf_get_valuef(*ha,"Date"));
    int btime = date_to_unix(hdf_get_valuef(*hb,"Date"));
    if  (atime<btime) return 1;
    if (atime==btime) return 0;
    return -1;
}

/* prepare the string to be written to the html file */

static NEOERR *
output (void *ctx, char *s)
{
    STRING *str = (STRING *)ctx;
    NEOERR *err;

    err = nerr_pass(string_append(str, s));
    return err;
}

char * 
convert_to_Unicode(char *source_encoding, char *str)
{
    size_t insize;
    size_t outsize;
    size_t ret;
    char *output;
    if (!strcmp(source_encoding,"UTF-8"))
	return str;

    iconv_t conv;
    const char * inptr = str;
    conv = iconv_open("UTF-8",source_encoding);
    if (conv == (iconv_t) -1) {
	warn("conversion from '%s' to UTF-8ot available",source_encoding);
	return str;
    }
    if (str == NULL)
	return str;
    insize = strlen(str);
    outsize = 4 * insize + 2;
    output=malloc(outsize);
    memset(output,0,outsize);
    char *outputptr = output;
    ret = iconv (conv,(const char**) &inptr,&insize,&outputptr,&outsize);
    if(ret ==(size_t) -1) {
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
    char *name=hdf_get_valuef(hdf_cfg,"Name");
    ret = mrss_parse_url_with_options_and_error (hdf_get_valuef(hdf_cfg,"URL"), &feed, NULL, &code);
    if(ret)
    {
	warn( "MRSS return error: %s, %s\n",
		ret ==
		MRSS_ERR_DOWNLOAD ? mrss_curl_strerror (code) :
		mrss_strerror (ret),name);
	return pos;
    }
    encoding=feed->encoding;
    for (item = feed->item;item;item=item->next)
    {
	struct tm date;
	time_t t;
	char date_now[16];
	char date_comp[16];
	strptime(item->pubDate,"%a, %d %b %Y %T",&date);
	strftime (date_comp, sizeof date_comp, "%s", &date);
	time(&t);
	strftime(date_now, sizeof date_now,"%s",localtime(&t));
	if ( ( atoi(date_now) - atoi(date_comp) ) >= days ) {
	    continue;
	}
	mrss_category_t *tags;
	mrss_tag_t *content; 
	hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Name=%s",pos,name);
	hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.FeedName=%s",pos,convert_to_Unicode(encoding,feed->title));
	if(item->author != NULL)
	    hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Author=%s",pos,convert_to_Unicode(encoding,item->author));
	hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Title=%s",pos,convert_to_Unicode(encoding,item->title));
	hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Link=%s",pos,item->link);
	hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Date=%s",pos,item->pubDate);
	/* Description is only for description tag, we want content if exists */
	if(feed->version == MRSS_VERSION_ATOM_0_3 || feed->version == MRSS_VERSION_ATOM_1_0 ) {
	    if ((mrss_search_tag(item, "content", "http://www.w3.org/2005/Atom", &content) == MRSS_OK && content) || 
		    (mrss_search_tag(item, "content", "http://purl.org/atom/ns#", &content) == MRSS_OK && content)) {
		hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Description=%s",pos,convert_to_Unicode(encoding,content->value));
	    } else {
		hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Description=%s",pos,convert_to_Unicode(encoding,item->description));
	    }
	} else {
	    if (mrss_search_tag(item, "encoded", "http://purl.org/rss/1.0/modules/content/", &content) == MRSS_OK && content) {
		hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Description=%s",pos,convert_to_Unicode(encoding,content->value));
	    } else {
		hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Description=%s",pos,convert_to_Unicode(encoding,item->description));
	    }
	}
	mrss_free(content);
	/* Get the categories/tags */
	int nb_tags=0;
	for (tags = item->category;tags;tags=tags->next) 
	{
	    hdf_set_valuef(hdf_dest,"CPlanet.Posts.%i.Tags.%i.Tag=%s",pos,nb_tags,convert_to_Unicode(encoding,tags->category));
	    nb_tags++;
	}
	mrss_free(tags);
	pos++;
    }
    mrss_free(item);

    return pos;
}

static void
usage()
{
    errx(1,"usage: cplanet -c conf.hdf\n");
}

int 
main (int argc, char *argv[])
{
    NEOERR *err;
    CSPARSE *parse;
    STRING cs_output_data;
    HDF *hdf;
    HDF *feed_hdf;
    int pos=0;
    int days=0;
    int ch=0;
    char *cspath, *cs_output, *hdf_file=NULL;
    if (argc == 1)
	usage();
    while ((ch = getopt(argc, argv, "c:h")) != -1)
	switch (ch) {  
	    case 'h':
		usage();
		break;
	    case 'c':
		hdf_file=optarg;
		/* TODO check file existence here */
		break;
	    case '?':
	    default:
		usage();
	}
    argc -= optind;
    argv += optind;

    string_init(&cs_output_data);
    err = hdf_init(&hdf);
    if (err != STATUS_OK)
    {
	nerr_log_error(err);
	return -1;
    }
    err = hdf_init(&feed_hdf);

    /* Read the hdf file */
    err = hdf_read_file(hdf,hdf_file);
    if (err != STATUS_OK) {
	nerr_log_error(err);
	return -1;
    }
    hdf_set_valuef(hdf,"CPlanet.Version=%s",CPLANET_VERSION);
    cspath = hdf_get_valuef(hdf,"CPlanet.TemplatePath");
    cs_output = hdf_get_valuef(hdf,"CPlanet.Path");
    days = atoi(hdf_get_valuef(hdf,"CPlanet.Days"));
    days = days * 24 * 60 * 60;
    feed_hdf = hdf_get_obj(hdf,"CPlanet.Feed.0");
    pos=get_posts(feed_hdf,hdf,pos, days);
    while ((feed_hdf = hdf_obj_next(feed_hdf)) != NULL) {
	pos=get_posts(feed_hdf,hdf,pos, days);
    }
    hdf_sort_obj(hdf_get_obj(hdf,"CPlanet.Posts"),sort_obj_by_date);
    /*	hdf_dump(hdf,NULL); */
    err = cs_init (&parse, hdf);
    if (err != STATUS_OK) {
	nerr_log_error(err);
	return -1;
    }
    err = cs_parse_file(parse,cspath);
    if (err != STATUS_OK) {
	err = nerr_pass(err);
	nerr_log_error(err);
	return -1;
    }
    err = cs_render(parse, &cs_output_data, output);
    if (err != STATUS_OK) {
	err = nerr_pass(err);
	nerr_log_error(err);
	return -1;
    }
    FILE *output = fopen(cs_output,"w+");
    fprintf(output,"%s",cs_output_data.buf);
    fflush(output);
    fclose(output);
    cs_destroy(&parse);
    /*create_atom(hdf);*/
    hdf_destroy(&hdf);
    hdf_destroy(&feed_hdf);
    return 0;
}
