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

#define W3_ATOM "http://www.w3.org/2005/Atom"
#define PURL_ATOM "http://purl.org/atom/ns#"
#define PURL_RSS "http://purl.org/rss/1.0/modules/content/"

typedef enum {
	NONE,
	RSS,
	ATOM,
	UNKNOWN
} feed_type;

static char *atom_to_rfc822(const char *s);

int syslog_flag = 0; /* 0 == log to stderr */
STRING neoerr_str; /* neoerr to string */

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
	char xmlpath[BUFSIZ];
	feed_type type;
	SLIST_HEAD(,post) entries;
};

struct buffer {
	char *data;
	size_t size;
};

void
cplanet_err(int eval, const char* message, ...)
{
	va_list args;
	va_start(args, message);
	if (syslog_flag) {
		vsyslog(LOG_ERR, message, args);
		exit(eval);
		/* NOTREACHED */
	} else {
		verr(eval, message, args);
		/* NOTREACHED */
	}
}

void
cplanet_warn(const char* message, ...)
{
	va_list args;
	va_start(args, message);
	if (syslog_flag)
		vsyslog(LOG_WARNING, message, args);
	else
		vwarn(message, args);
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

	return (realsize);
}

static void
post_free(struct post *post)
{
	int i;

	for (i = 0; i < post->nbtags; i++) {
		free(post->tags[i]);
	}
	if (post->tags != NULL)
		free(post->tags);

	if (post->title != NULL)
		free(post->title);

	if (post->author != NULL)
		free(post->author);

	if (post->link != NULL)
		free(post->link);

	if (post->date != NULL)
		free(post->date);

	if (post->content != NULL)
		free(post->content);

	if (post->description != NULL)
		free(post->description);
}

struct post
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

	if (!strcmp(feed->xmlpath, "/feed/entry")) {
		post = post_init();
		SLIST_INSERT_HEAD(&feed->entries, post, next);
	}
	if (!strcmp(feed->xmlpath, "/feed/entry/link")) {
		for (i = 0; attr[i] != NULL; i++) {
			if (!strcmp(attr[i], "rel")) {
				i++;
				if (!strcmp(attr[i], "alternate"))
					getlink = true;
			}
			if (!strcmp(attr[i], "href") && getlink) {
				i++;
				post = SLIST_FIRST(&feed->entries);
				post->link=strdup(attr[i]);
			}
		}
	}

	if (!strcmp(feed->xmlpath, "/feed/entry/category")) {
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

	if (!strcmp(feed->xmlpath, "/rss/channel/item")) {
		post = post_init();
		SLIST_INSERT_HEAD(&feed->entries, post, next);
	}
}

static void XMLCALL
xml_startel(void *userdata, const char *elt, const char **attr)
{
	struct feed *feed = (struct feed *)userdata;

	snprintf(feed->xmlpath, BUFSIZ, "%s/%s", feed->xmlpath, elt);

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

	feed->xmlpath[strlen(feed->xmlpath) - strlen(elt)]='\0';
	if (feed->xmlpath[strlen(feed->xmlpath) - 1] != '/')
		cplanet_err(1, "invalid xml"); /* TODO: change this to warnings */
	else
		feed->xmlpath[strlen(feed->xmlpath) - 1] = '\0';
}

static void XMLCALL
xml_startdec(void *userdata, const char *version, const char *encoding, int standalone)
{
	struct feed *feed = (struct feed *)userdata;

	feed->encoding = strdup(encoding);
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

	if (!strcmp(feed->xmlpath, "/feed/title"))
		push_data(&feed->blog_title, data);
	if (!strcmp(feed->xmlpath, "/feed/entry/title")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->title, data);
	}
	if (!strcmp(feed->xmlpath, "/feed/entry/author/name")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->author, data);
	}
	if (!strcmp(feed->xmlpath, "/feed/entry/published")) {
		post = SLIST_FIRST(&feed->entries);
		post->date = atom_to_rfc822(data);
	}
	if (!strcmp(feed->xmlpath, "/feed/entry/content")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->content, data);
	}
}

static void
rss_data(struct feed *feed, const char *data)
{
	struct post *post;

	if (!strcmp(feed->xmlpath, "/rss/channel/title"))
		push_data(&feed->blog_title, data);
	if (!strcmp(feed->xmlpath, "/rss/channel/item/title")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->title, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/dc:creator")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->author, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/link")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->link, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/pubDate")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->date, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/category")) {
		post = SLIST_FIRST(&feed->entries);
		post_add_tags(post, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/description")) {
		post = SLIST_FIRST(&feed->entries);
		push_data(&post->description, data);
	}
	if (!strcmp(feed->xmlpath, "/rss/channel/item/content:encoded")) {
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

static char *
atom_to_rfc822(const char *s)
{
	struct tm stm;
	char datebuf[256];
	if (!s)
		return (NULL);

	memset(&stm, 0, sizeof(stm));
	if (sscanf(s, "%04d-%02d-%02dT%02d:%02d:%02dZ", &stm.tm_year,
				&stm.tm_mon, &stm.tm_mday, &stm.tm_hour, &stm.tm_min,
				&stm.tm_sec) == 6)
	{
		stm.tm_year -= 1900;
		stm.tm_mon -= 1;
		strftime (datebuf, sizeof (datebuf), "%a, %d %b %Y %H:%M:%S %z", &stm);
		return strdup(datebuf);
	}
	return NULL;
}

/* convert the iso format as the RFC3339 is a subset of it */
time_t
iso8601_to_time_t(char *s)
{
	struct tm date;
	time_t t;
	errno = 0;
	char *pos = strptime(s, "%Y-%m-%dT%H:%M:%S.%fZ", &date);
	if (pos == NULL) {
		/* Modify the last HH:MM to HHMM if necessary */
		if (s[strlen(s) - 3] == ':' ) {
			s[strlen(s) - 3] = s[strlen(s) - 2];
			s[strlen(s) - 2] = s[strlen(s) - 1];
			s[strlen(s) - 1] = '\0';
		}
		pos =strptime(s, "%Y-%m-%dT%H:%M:%S%z", &date);
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
time_t
rfc822_to_time_t(char *s)
{
	struct tm date;
	time_t t;
	errno = 0;

	if (s == NULL) {
		cplanet_warn("Invalide empty date");
		return 0;
	}

	char *pos = strptime(s, "%a, %d %b %Y %T", &date);
	if (pos == NULL) {
		errno = EINVAL;
		cplanet_warn("Convert RFC822 '%s' to struct tm failed", s);
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

/* sort posts by date */

int
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

static char * 
str_to_UTF8(char *source_encoding, char *str)
{
	size_t insize;
	size_t outsize;
	size_t ret;
	char *output;
	if (!strcasecmp(source_encoding, "UTF-8"))
		return str;

	iconv_t conv;
	conv = iconv_open("UTF-8", source_encoding);
	if (conv == (iconv_t)-1) {
		cplanet_warn("Conversion from '%s' to UTF-8 not available", source_encoding);
		return str;
	}
	if (str == NULL)
		return str;
	insize = strlen(str);
	outsize = 4 * insize + 2;
	output = malloc(outsize);
	if ( output == NULL )
		cplanet_err(ENOMEM, "malloc");
	memset(output, 0, outsize);
	char *outputptr = output;
#ifdef _LIBICONV_H
	ret = iconv(conv, (const char **) &str, &insize, &outputptr, &outsize);
#else
	ret = iconv(conv, &str, &insize, &outputptr, &outsize);
#endif
	if (ret == (size_t)-1) {
		cplanet_warn("Conversion Failed");
		free(output);
		iconv_close(conv);
		return str;
	}
	iconv_close(conv);
	return output;
}

/* retreive ports and prepare the dataset for the template */
int
fetch_posts(HDF *hdf_cfg, HDF *hdf_dest, int pos, int days)
{
	CURL *curl;
	CURLcode res;
	struct buffer rawfeed;
	struct XML_ParserStruct *parser;
	struct feed feed;
	struct post *post, *posttemp;
	time_t t_now, t_comp;
	int i;

	char *date_format = hdf_get_valuef(hdf_dest, "CPlanet.DateFormat");

	rawfeed.data = malloc(1);
	rawfeed.size = 0;

	feed.type = NONE;
	feed.hdf = hdf_dest;
	feed.blog_title = NULL;
	feed.xmlpath[0]='\0';
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

	res = curl_easy_perform(curl);

	if (res != CURLE_OK || rawfeed.size == 0) {
		free(rawfeed.data);
		curl_easy_cleanup(curl);
		cplanet_warn("An error occured while fetching %s\n", hdf_get_valuef(hdf_cfg, "URL"));
		return pos;
	}

	if ((parser = XML_ParserCreate(NULL)) == NULL)
		cplanet_err(1, "Unable to initialise expat");

	XML_SetXmlDeclHandler(parser, xml_startdec);
	XML_SetStartElementHandler(parser, xml_startel);
	XML_SetEndElementHandler(parser, xml_endel);
	XML_SetCharacterDataHandler(parser, xml_data);
	XML_SetUserData(parser, &feed);

	if (XML_Parse(parser, rawfeed.data, rawfeed.size, true) == XML_STATUS_ERROR)
		cplanet_err(1, "Parse error at line %lu: %s",
				XML_GetCurrentLineNumber(parser),
				XML_ErrorString(XML_GetErrorCode(parser)));

	time(&t_now);
	SLIST_FOREACH_SAFE(post, &feed.entries, next, posttemp) {
		if (post->date == NULL) {
			SLIST_REMOVE(&feed.entries, post, post, next);
			continue;
		}

		t_comp = rfc822_to_time_t(post->date);
		if (t_now - t_comp < days) {
			cp_set_name(hdf_dest, pos, hdf_get_valuef(hdf_cfg, "Name"));
			cp_set_feedname(hdf_dest, pos, str_to_UTF8(feed.encoding, feed.blog_title));

			if (post->author != NULL)
				cp_set_author(hdf_dest, pos, str_to_UTF8(feed.encoding, post->author));

			cp_set_title(hdf_dest, pos, str_to_UTF8(feed.encoding, post->title));
			cp_set_link(hdf_dest, pos, post->link);
			cp_set_date(hdf_dest, pos, post->date);
			if (date_format != NULL) {
				char formated_date[256];
				struct tm *ptr;
				ptr = localtime(&t_comp);
				strftime(formated_date, 256, date_format, ptr);
				cp_set_formated_date(hdf_dest, pos, formated_date);
			}

			if (post->content != NULL)
				cp_set_description(hdf_dest, pos, str_to_UTF8(feed.encoding, post->content));
			else if (post->description != NULL)
				cp_set_description(hdf_dest, pos, str_to_UTF8(feed.encoding, post->description));

			for (i = 0; i < post->nbtags; i++)
				cp_set_tag(hdf_dest, pos, i, str_to_UTF8(feed.encoding, post->tags[i]));

			pos++;
		}

		SLIST_REMOVE(&feed.entries, post, post, next);
		post_free(post);
	}

	XML_ParserFree(parser);

	return pos;
};

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
		cplanet_warn("MRSS return error: %s, %s\n",
				ret ==
				MRSS_ERR_DOWNLOAD ? mrss_curl_strerror(code) :
				mrss_strerror(ret), name);
		return pos;
	}
	encoding = feed->encoding;
	for (item = feed->item; item; item = item->next) {
		time_t t_now, t_comp;
		if (item->pubDate == NULL) {
			cplanet_warn("Invalid date format in the %s feed: skeeping", hdf_get_valuef(hdf_cfg, "Name"));
			continue;
		}
		/* when the feed is not corrupted, mrss stores it in RFC822 format */
		t_comp = rfc822_to_time_t(item->pubDate);
		if (t_comp == 0) {
			cplanet_warn("Invalid date format in the %s feed: skeeping", hdf_get_valuef(hdf_cfg, "Name"));
			continue;
		}
		time(&t_now);
		if(t_now - t_comp >= days)
			continue;
		mrss_category_t *tags;
		mrss_tag_t *content; 
		cp_set_name(hdf_dest, pos, name);
		cp_set_feedname(hdf_dest, pos, str_to_UTF8(encoding, feed->title));
		if (item->author != NULL)
			cp_set_author(hdf_dest, pos, str_to_UTF8(encoding, item->author));
		cp_set_title(hdf_dest, pos, str_to_UTF8(encoding, item->title));
		cp_set_link(hdf_dest, pos, item->link);
		/*cp_set_date(hdf_dest, pos, (long long int)t_comp);*/
		/* Description is only for description tag, we want content if exists */
		if (feed->version == MRSS_VERSION_ATOM_0_3 || feed->version == MRSS_VERSION_ATOM_1_0) {
			if ((mrss_search_tag(item, "content", W3_ATOM, &content) == MRSS_OK && content) || 
					(mrss_search_tag(item, "content", PURL_ATOM, &content) == MRSS_OK && content)) {
				cp_set_description(hdf_dest, pos, str_to_UTF8(encoding, content->value));
			} else {
				cp_set_description(hdf_dest, pos, str_to_UTF8(encoding, item->description));
			}
		} else {
			if (mrss_search_tag(item, "encoded", PURL_RSS, &content) == MRSS_OK && content) {
				cp_set_description(hdf_dest, pos, str_to_UTF8(encoding, content->value));
			} else {
				cp_set_description(hdf_dest, pos, str_to_UTF8(encoding, item->description));
			}
		}
		mrss_free(content);
		/* Get the categories/tags */
		int nb_tags = 0;
		for (tags = item->category; tags; nb_tags++, tags = tags->next) {
			cp_set_tag(hdf_dest, pos, nb_tags, str_to_UTF8(encoding, tags->category));
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
	HDF *post_hdf;
	int pos = 0;
	int days = 0;
	int ch = 0;
	char *hdf_file = NULL;
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

	days = hdf_get_int_value(hdf,"CPlanet.Days",0);
	days=days *  24 * 60 * 60;

	HDF_FOREACH(feed_hdf,hdf,"CPlanet.Feed.0") {
//		pos = get_posts(feed_hdf, hdf, pos, days);
		pos = fetch_posts(feed_hdf, hdf, pos, days);
	}

	hdf_sort_obj(hdf_get_obj(hdf, "CPlanet.Posts"), sort_obj_by_date);

	/* get every output set in the hdf file and generate them */
	HDF_FOREACH(output_hdf,hdf,"CPlanet.Output.0") {
		/* format the date according to the output type */
		char *type = hdf_get_value(output_hdf, "Type", "HTML");
		/* for the posts */
		HDF_FOREACH(post_hdf, hdf, "CPlanet.Posts.0") {
			if(strcasecmp(type, "RSS") == 0) {
				setlocale(LC_ALL, "C");
				time_t date = (time_t)hdf_get_int_value(post_hdf, "Date", time(NULL));
				char formated_date[256];
				/* format to RFC822 */
				struct tm *ptr = gmtime(&date);
				strftime(formated_date, 256, "%a, %d %b %Y %H:%M:%S %z", ptr);
				hdf_set_valuef(post_hdf, "FormatedDate=%s", formated_date);
			} else if (strcasecmp(type, "ATOM") == 0 ){
				time_t posttime = hdf_get_int_value(post_hdf, "Date", time(NULL));
				struct tm *date = gmtime(&posttime);
				hdf_set_valuef(post_hdf, "FormatedDate=%04d-%02d-%02dT%02d:%02d:%02dZ",
						date->tm_year + 1900, date->tm_mon + 1, date->tm_mday,
						date->tm_hour, date->tm_min, date->tm_sec);
			} else {
				/* case html */
				char *date_format = hdf_get_value(hdf, "CPlanet.DateFormat", "%F at %T");
				time_t posttime = hdf_get_int_value(hdf, "Date", time(NULL));
				char formated_date[256];
				struct tm *ptr;
				ptr = localtime(&posttime);
				strftime(formated_date, 256, date_format, ptr);
				hdf_set_valuef(post_hdf, "FormatedDate=%s", formated_date);
			}
		}
		/* the the publication date */
		char genDate[256];
		time_t lt;
		struct tm *ptr;
		lt = time(NULL);
		ptr = gmtime(&lt);
		if (strcasecmp(type, "RSS") == 0) {
			strftime(genDate, 256, "%a, %d %b %Y %H:%M:%S %z", ptr);
			hdf_set_valuef(hdf, CP_GEN_DATE, genDate);
		} else if (strcasecmp(type, "ATOM") == 0) {
			hdf_set_valuef(hdf,"CPlanet.GenerationDate=%04d-%02d-%02dT%02d:%02d:%02dZ",
					ptr->tm_year + 1900, ptr->tm_mon + 1, ptr->tm_mday,
					ptr->tm_hour, ptr->tm_min, ptr->tm_sec);
		} else {
			char *date_format = hdf_get_value(hdf, "CPlanet.DateFormat", "%F at %T");
			ptr = localtime(&lt);
			strftime(genDate, 256, date_format, ptr);
			hdf_set_valuef(hdf, CP_GEN_DATE, genDate);
		}
		generate_file(output_hdf, hdf);
	}

	hdf_destroy(&hdf);

	if (syslog_flag)
		closelog();

	return EXIT_SUCCESS;
}
