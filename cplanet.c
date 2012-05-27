/*
 * Copyright (c) 2010, Baptiste Daroussin
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* vim:set ts=4 sw=4 sts=4: */

#include <expat.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <sys/queue.h>
#include "cplanet.h"

typedef enum {
	NONE,
	RSS,
	ATOM,
	UNKNOWN
} feed_type;

static int syslog_flag = 0; /* 0 == log to stderr */
static STRING neoerr_str; /* neoerr to string */

struct post {
	char *title;
	char *author;
	char *link;
	char *content;
	char *description;
	char *date;
	char **tags;
	int nbtags;
	SLIST_ENTRY(post) next;
};

struct feed {
	char *blog_title;
	char *blog_subtitle;
	char *encoding;
	HDF *hdf;
	struct buffer *xmlpath;
	feed_type type;
	SLIST_HEAD(,post) entries;
};

struct buffer {
	char *data;
	size_t size;
	size_t cap;
};

static void
cplanet_err(int eval, const char* message, ...)
{
	va_list args;
	va_start(args, message);
	if (syslog_flag) {
		vsyslog(LOG_ERR, message, args);
		exit(eval);
		/* NOTREACHED */
	} else {
		verrx(eval, message, args);
		/* NOTREACHED */
	}
}

static void
cplanet_warn(const char* message, ...)
{
	va_list args;
	va_start(args, message);
	if (syslog_flag)
		vsyslog(LOG_WARNING, message, args);
	else
		vwarnx(message, args);
	va_end(args);
}

static size_t
write_to_buffer(void *ptr, size_t size, size_t memb, void *data) {
	size_t realsize = size * memb;
	struct buffer *mem = (struct buffer *)data;

	if ((mem->data = realloc(mem->data, mem->size + realsize + 1)) == NULL)
		cplanet_err(1, "not enough memory");

	memcpy(&(mem->data[mem->size]), ptr, realsize);
	mem->size += realsize;
	mem->data[mem->size] = '\0';
	mem->cap = mem->size;

	return (realsize);
}

static void
post_free(struct post *post)
{
	int i;

	for (i = 0; i < post->nbtags; i++)
		free(post->tags[i]);

	free_not_null(post->tags);
	free_not_null(post->title);
	free_not_null(post->author);
	free_not_null(post->link);
	free_not_null(post->date);
	free_not_null(post->content);
	free_not_null(post->description);
}

static struct post
*post_init(void)
{
	struct post *post;

	post = malloc(sizeof(struct post));
	post->title = NULL;
	post->author = NULL;
	post->link = NULL;
	post->date = NULL;
	post->content = NULL;
	post->description = NULL;
	post->tags = NULL;
	post->nbtags = 0;

	return post;
}

static void
post_add_tags(struct post *post, const char *data) {
	post->nbtags++;

	if (post->tags == NULL) {
		post->tags = malloc(post->nbtags * sizeof(char *));
	} else {
		post->tags = realloc(post->tags, post->nbtags * sizeof(char *));
	}

	post->tags[post->nbtags - 1] = strdup(data);
}

static void
parse_atom_el(struct feed *feed, const char *elt, const char **attr)
{
	int i;
	bool getlink = false;
	struct post *post;
	char *url = NULL;

	if (!strcmp(feed->xmlpath->data, "/feed/entry")) {
		post = post_init();
		SLIST_INSERT_HEAD(&feed->entries, post, next);
	}
	if (!strcmp(feed->xmlpath->data, "/feed/entry/link")) {
		for (i = 0; attr[i] != NULL; i++) {
			if (!strcmp(attr[i], "rel")) {
				i++;
				if (!strcmp(attr[i], "alternate"))
					getlink = true;
			}
			if (!strcmp(attr[i], "href")) {
				i++;
				url=strdup(attr[i]);
			}
		}
		if (getlink && url != NULL) {
			post = SLIST_FIRST(&feed->entries);
			post->link = url;
		} else
			free_not_null(url);
	}

	if (!strcmp(feed->xmlpath->data, "/feed/entry/category")) {
		for (i = 0; attr[i] != NULL; i++) {
			if (!strcmp(attr[i], "term")) {
				i++;
				post = SLIST_FIRST(&feed->entries);
				post_add_tags(post, attr[i]);
			}
		}
	}
}

static void
parse_rss_el(struct feed *feed, const char *elt, const char **attr)
{
	struct post *post;

	if (!strcmp(feed->xmlpath->data, "/rss/channel/item")) {
		post = post_init();
		SLIST_INSERT_HEAD(&feed->entries, post, next);
	}
}

static void XMLCALL
xml_startel(void *userdata, const char *elt, const char **attr)
{
	struct feed *feed = (struct feed *)userdata;

	if (feed->xmlpath->cap <= feed->xmlpath->size + strlen(elt) + 1) {
		feed->xmlpath->cap *= 2;
		feed->xmlpath->data = realloc(feed->xmlpath->data, feed->xmlpath->cap);
	}
	strcat(feed->xmlpath->data, "/");
	strcat(feed->xmlpath->data, elt);
	feed->xmlpath->size += strlen(elt) + 1;

	switch (feed->type) {
		case NONE:
			if (!strcmp(elt, "feed"))
				feed->type = ATOM;
			else if (!strcmp(elt, "rss"))
				feed->type = RSS;
			else
				feed->type = UNKNOWN;
			break;
		case ATOM:
			parse_atom_el(feed, elt, attr);
			break;
		case RSS:
			parse_rss_el(feed, elt, attr);
			break;
		case UNKNOWN:
			break;
	}
}

static void XMLCALL
xml_endel(void *userdata, const char *elt)
{
	struct feed *feed = (struct feed *)userdata;

	feed->xmlpath->size -= strlen(elt);
	feed->xmlpath->data[feed->xmlpath->size] = '\0';
	if (feed->xmlpath->data[feed->xmlpath->size - 1] != '/')
		cplanet_warn("invalid xml");

	feed->xmlpath->size--;
	feed->xmlpath->data[feed->xmlpath->size] = '\0';
}

static void
push_data(char **key, const char *value)
{
	size_t size;

	if (*key == NULL)
		*key = strdup(value);
	else {
		size = strlen(*key) + strlen(value) + 1;
		*key = realloc(*key, size);
		strlcat(*key, value, size);
	}
}

static void
atom_data(struct feed *feed, const char *data)
{
	struct post *post;

	if (!strcmp(feed->xmlpath->data, "/feed/title"))
		push_data(&feed->blog_title, data);
	if (!strcmp(feed->xmlpath->data, "/feed/entry/title")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->title, data);
	}
	if (!strcmp(feed->xmlpath->data, "/feed/entry/author/name")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->author, data);
	}
	if (!strcmp(feed->xmlpath->data, "/feed/entry/published")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->date, data);
	}
	if (!strcmp(feed->xmlpath->data, "/feed/entry/content")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->content, data);
	}
}

static void
rss_data(struct feed *feed, const char *data)
{
	struct post *post;

	if (!strcmp(feed->xmlpath->data, "/rss/channel/title"))
		push_data(&feed->blog_title, data);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/title")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->title, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/dc:creator")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->author, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/link")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->link, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/pubDate")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->date, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/category")) {
		post = SLIST_FIRST(&feed->entries);
		post_add_tags(post, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/description")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->description, data);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/content:encoded")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->content, data);
	}

}

static void XMLCALL
xml_data(void *userdata, const char *s, int len)
{
	char *str;
	struct feed *feed = (struct feed *)userdata;

	str = malloc(len + 1);
	strlcpy(str, s, len + 1);

	switch (feed->type) {
		case ATOM:
			atom_data(feed, str);
			break;
		case RSS:
			rss_data(feed, str);
			break;
		case NONE:
		case UNKNOWN:
			break;
	}
	free(str);
}

/* convert the iso format as the RFC3339 is a subset of it */
static time_t
iso8601_to_time_t(char *s)
{
	struct tm date;
	time_t t;
	int garbage;
	errno = 0;
	char *pos = strptime(s, "%FT%TZ", &date);
	if (pos == NULL) {
		/* Modify the last HH:MM to HHMM if necessary */
		if (s[strlen(s) - 3] == ':' ) {
			s[strlen(s) - 3] = s[strlen(s) - 2];
			s[strlen(s) - 2] = s[strlen(s) - 1];
			s[strlen(s) - 1] = '\0';
		}
		pos = strptime(s, "%FT%T%z", &date);

	}
	if (pos == NULL) {
		memset(&date, 0, sizeof(date));
		if (sscanf(s, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", &date.tm_year,
					&date.tm_mon, &date.tm_mday, &date.tm_hour, &date.tm_min,
					&date.tm_sec, &garbage) == 7) {
			date.tm_year -= 1900;
			date.tm_mon -= 1;
			pos = s;
		}

	}
	if (pos == NULL) {
		errno = EINVAL;
		cplanet_warn("Convert  ISO8601 '%s' to struct tm failed", s);
		return 0;
	}
	t = mktime(&date);
	if (t == (time_t)-1) {
		errno = EINVAL;
		cplanet_warn("Convert struct tm (from '%s') to time_t failed", s);
		return 0;
	}
	return t;
}

/* convert RFC822 to epoch time */
static time_t
rfc822_to_time_t(char *s)
{
	struct tm date;
	time_t t;
	char *pos;
	errno = 0;

	if (s == NULL) {
		cplanet_warn("Invalide empty date");
		return 0;
	}

	if ((pos = strptime(s, "%a, %d %b %Y %T", &date)) == NULL) {
		errno = EINVAL;
		cplanet_warn("Convert RFC822 '%s' to struct tm failed", s);

		return 0;
	}

	if ((t = mktime(&date)) == -1) {
		errno = EINVAL;
		cplanet_warn("Convert struct tm (from '%s') to time_t failed", s);
		return 0;
	}

	return t;
}

/* sort posts by date */

static int
sort_obj_by_date(const void *a, const void *b) {
	HDF **ha = (HDF **)a;
	HDF **hb = (HDF **)b;

	time_t atime = hdf_get_int_value(*ha, "Date",0);
	time_t btime = hdf_get_int_value(*hb, "Date",0);

	return (btime - atime);
}

/* prepare the string to be written to the html file */

static NEOERR *
cplanet_output (void *ctx, char *s)
{
	STRING *str = ctx;
	NEOERR *neoerr;

	neoerr = nerr_pass(string_append(str, s));

	return neoerr;
}

/* retreive ports and prepare the dataset for the template */
static int
fetch_posts(HDF *hdf_cfg, HDF *hdf_dest, int pos, int days)
{
	CURL *curl;
	CURLcode res;
	struct buffer rawfeed;
	struct XML_ParserStruct *parser;
	struct feed feed;
	struct post *post, *posttemp;
	time_t t_now, t_comp;
	char datestr[256];
	int i;

	char *date_format = hdf_get_valuef(hdf_dest, "CPlanet.DateFormat");

	rawfeed.data = malloc(1);
	rawfeed.size = 0;

	feed.type = NONE;
	feed.hdf = hdf_dest;
	feed.blog_title = NULL;
	feed.xmlpath = malloc(sizeof(struct buffer));
	feed.xmlpath->size = 0;
	feed.xmlpath->cap = BUFSIZ;
	feed.xmlpath->data = malloc(BUFSIZ);
	feed.xmlpath->data[0] = '\0';
	SLIST_INIT(&feed.entries);

	curl_global_init(CURL_GLOBAL_ALL);
	if ((curl = curl_easy_init()) == NULL)
		cplanet_err(1, "Unable to initalise curl");

	curl_easy_setopt(curl, CURLOPT_URL, hdf_get_valuef(hdf_cfg, "URL"));
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_HEADER, 0);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_buffer);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rawfeed);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "cplanet/"CPLANET_VERSION);
	curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "gzip");

	res = curl_easy_perform(curl);

	if (res != CURLE_OK || rawfeed.size == 0) {
		free(rawfeed.data);
		curl_easy_cleanup(curl);
		cplanet_warn("An error occured while fetching %s\n", hdf_get_valuef(hdf_cfg, "URL"));
		free(feed.xmlpath->data);
		free(feed.xmlpath);
		return pos;
	}

	if ((parser = XML_ParserCreate(NULL)) == NULL)
		cplanet_err(1, "Unable to initialise expat");

	XML_SetStartElementHandler(parser, xml_startel);
	XML_SetEndElementHandler(parser, xml_endel);
	XML_SetCharacterDataHandler(parser, xml_data);
	XML_SetUserData(parser, &feed);

	if (XML_Parse(parser, rawfeed.data, rawfeed.size, true) == XML_STATUS_ERROR) {
		cplanet_warn("Parse error at line %lu: %s for %s",
				XML_GetCurrentLineNumber(parser),
				XML_ErrorString(XML_GetErrorCode(parser)),
				hdf_get_valuef(hdf_cfg, "URL"));
	}

	time(&t_now);
	SLIST_FOREACH_SAFE(post, &feed.entries, next, posttemp) {
		if (post->date == NULL) {
			SLIST_REMOVE(&feed.entries, post, post, next);
			continue;
		}

		if (feed.type == RSS)
			t_comp = rfc822_to_time_t(post->date);
		else
			t_comp = iso8601_to_time_t(post->date);

		if (t_now - t_comp < days) {
			cp_set_name(hdf_dest, pos, hdf_get_valuef(hdf_cfg, "Name"));
			cp_set_feedname(hdf_dest, pos, feed.blog_title);

			if (post->author != NULL)
				cp_set_author(hdf_dest, pos, post->author);

			cp_set_title(hdf_dest, pos, post->title);
			cp_set_link(hdf_dest, pos, post->link);
			cp_set_date(hdf_dest, pos, (long long int)t_comp);

			setlocale(LC_ALL, "C");
			time_to_iso8601(&t_comp, datestr, 256);
			cp_set_date_iso8601(hdf_dest, pos, datestr);
			time_to_rfc822(&t_comp, datestr, 256);
			cp_set_date_rfc822(hdf_dest, pos, datestr);

			if (date_format != NULL) {
				time_format(&t_comp, datestr, 256, date_format);
				cp_set_formated_date(hdf_dest, pos, datestr);
			}

			if (post->content != NULL)
				cp_set_description(hdf_dest, pos, post->content);
			else if (post->description != NULL)
				cp_set_description(hdf_dest, pos, post->description);

			for (i = 0; i < post->nbtags; i++)
				cp_set_tag(hdf_dest, pos, i, post->tags[i]);
			pos++;
		}

		SLIST_REMOVE(&feed.entries, post, post, next);
		post_free(post);
	}

	XML_ParserFree(parser);

	free(feed.xmlpath->data);
	free(feed.xmlpath);
	return pos;
};

void
generate_file(HDF *output_hdf, HDF *hdf)
{
	NEOERR *neoerr;
	CSPARSE *parse;
	STRING cs_output_data;
	string_init(&cs_output_data);
	neoerr = cs_init(&parse, hdf);
	if (neoerr != STATUS_OK)
		goto warn1;

	char *cs_output = hdf_get_valuef(output_hdf, "Path");
	char *cs_path = hdf_get_valuef(output_hdf, "TemplatePath");
	neoerr = cgi_register_strfuncs(parse);

	if (neoerr != STATUS_OK)
		goto warn0;

	neoerr = cs_parse_file(parse, cs_path);

	if (neoerr != STATUS_OK)
		goto warn0;

	neoerr = cs_render(parse, &cs_output_data, cplanet_output);

	if (neoerr != STATUS_OK)
		goto warn0;

	FILE *output = fopen(cs_output, "w+");
	if (output == NULL)
		cplanet_err(1, "%s", cs_output);
	fprintf(output, "%s", cs_output_data.buf);
	fflush(output);
	fclose(output);
	cs_destroy(&parse);
	string_clear(&cs_output_data);

	return;

warn0:
	cs_destroy(&parse);
warn1:
	string_clear(&cs_output_data);
	nerr_error_string(neoerr,&neoerr_str);
	cplanet_warn(neoerr_str.buf);
}


static void
usage(void)
{
	errx(1, "usage: cplanet -c conf.hdf [-l]\n");
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
	char datestr[256];
	time_t t_now;
	char *date_format = NULL;

	t_now = time(NULL);

	if (argc == 1)
		usage();

	while ((ch = getopt(argc, argv, "c:lh")) != -1)
		switch (ch) {  
			case 'h':
				usage();
				break;
			case 'c':
				hdf_file = optarg;
				if(access(hdf_file, F_OK | R_OK) == -1 )
					err(1, "%s", hdf_file);
				break;
			case 'l':
				syslog_flag++;
				break;
			case '?':
			default:
				usage();
		}
	argc -= optind;
	argv += optind;

	if (syslog_flag)
		openlog("cplanet", LOG_CONS, LOG_USER);

	string_init(&neoerr_str);
	neoerr = hdf_init(&hdf);
	if (neoerr != STATUS_OK) {
		nerr_error_string(neoerr, &neoerr_str);
		cplanet_err(-1, neoerr_str.buf);
	}
	/* Read the hdf file */
	neoerr = hdf_read_file(hdf, hdf_file);
	if (neoerr != STATUS_OK) {
		nerr_error_string(neoerr, &neoerr_str);
		cplanet_err(-1, neoerr_str.buf);
	}
	cp_set_version(hdf);

	date_format  = hdf_get_valuef(hdf, "CPlanet.DateFormat");
	days = hdf_get_int_value(hdf,"CPlanet.Days",0);
	days=days *  24 * 60 * 60;

	HDF_FOREACH(feed_hdf,hdf,"CPlanet.Feed.0")
		pos = fetch_posts(feed_hdf, hdf, pos, days);

	hdf_sort_obj(hdf_get_obj(hdf, "CPlanet.Posts"), sort_obj_by_date);

	/* get every output set in the hdf file and generate them */
	HDF_FOREACH(output_hdf,hdf,"CPlanet.Output.0") {
		/* format the date according to the output type */
		setlocale(LC_ALL, "C");
		time_to_rfc822(&t_now, datestr, 256);
		cp_set_gen_rfc822(hdf, datestr);
		time_to_iso8601(&t_now, datestr, 256);
		cp_set_gen_iso8601(hdf, datestr);
		time_format(&t_now, datestr, 256, date_format);
		cp_set_gen_date(hdf, datestr);

		generate_file(output_hdf, hdf);
	}

	hdf_destroy(&hdf);

	if (syslog_flag)
		closelog();

	return EXIT_SUCCESS;
}
