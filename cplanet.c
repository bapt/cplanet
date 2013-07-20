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

#include <sys/param.h>

#include <assert.h>
#include <inttypes.h>
#include <sqlite3.h>
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
static sqlite3 *db;

struct feed {
	const unsigned char *name;
	char *blog_title;
	char *blog_subtitle;
	char *encoding;
	sqlite3_stmt *stmt;
	sqlite3_stmt *tags;
	char **tag;
	int nbtags;
	HDF *hdf;
	struct buffer *xmlpath;
	feed_type type;
};

struct buffer {
	char *data;
	size_t size;
	size_t cap;
};

static int
sql_int(int64_t *dest, const char *sql, ...)
{
	va_list ap;
	sqlite3_stmt *stmt = NULL;
	const char *sql_to_exec;
	char *sqlbuf = NULL;
	int ret = 0;

	assert(sql != NULL);
	assert(dest != NULL);

	if (strchr(sql, '%') != NULL) {
		va_start(ap, sql);
		sqlbuf = sqlite3_vmprintf(sql, ap);
		va_end(ap);
		sql_to_exec = sqlbuf;
	} else {
		sql_to_exec = sql;
	}

	if (sqlite3_prepare_v2(db, sql_to_exec, -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		ret = 1;
		goto cleanup;
	}

	if (sqlite3_step(stmt) == SQLITE_ROW)
		*dest = sqlite3_column_int64(stmt, 0);

cleanup:
	if (sqlbuf != NULL)
		sqlite3_free(sqlbuf);
	if (stmt != NULL)
		sqlite3_finalize(stmt);

	return (ret);
}

static int
sql_text(char **dest, const char *sql)
{
	sqlite3_stmt *stmt = NULL;
	int ret = 0;

	assert(sql != NULL);
	assert(dest != NULL);

	if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s (%s)", sqlite3_errmsg(db), sql);
		ret = 1;
		goto cleanup;
	}

	if (sqlite3_step(stmt) == SQLITE_ROW)
		asprintf(dest, "%s", sqlite3_column_text(stmt, 0));

cleanup:
	if (stmt != NULL)
		sqlite3_finalize(stmt);

	return (ret);
}
static int
sql_exec(const char *sql, ...)
{
	va_list ap;
	const char *sql_to_exec;
	char *sqlbuf = NULL;
	char *errmsg;
	int ret = -1;

	assert(sql != NULL);
	if (strchr(sql, '%') != NULL) {
		va_start(ap, sql);
		sqlbuf = sqlite3_vmprintf(sql, ap);
		va_end(ap);
		sql_to_exec = sqlbuf;
	} else {
		sql_to_exec = sql;
	}

	if (sqlite3_exec(db, sql_to_exec, NULL, NULL, &errmsg) != SQLITE_OK) {
		warnx("sqlite: %s (%s)", errmsg, sql_to_exec);
		goto cleanup;
	}


	ret = 0;
cleanup:
	if (sqlbuf != NULL)
		sqlite3_free(sqlbuf);
	return (ret);
}

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

/* convert the iso format as the RFC3339 is a subset of it */
static time_t
iso8601_to_time_t(const char *d)
{
	struct tm date;
	time_t t;
	int garbage;
	errno = 0;
	char *s = strdup(d);
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
	free(s);
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
rfc822_to_time_t(const char *s)
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

static void
add_tag(struct feed *feed, const char *data)
{
	feed->nbtags++;
	if (feed->tag == NULL) {
		feed->tag = malloc(feed->nbtags * sizeof(char *));
	} else {
		feed->tag = realloc(feed->tag, feed->nbtags * sizeof(char *));
	}
	feed->tag[feed->nbtags - 1] = strdup(data);
}

static void
parse_atom_el(struct feed *feed, const char *elt, const char **attr)
{
	int i;
	bool getlink = false;
	char *url = NULL;

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
		if (getlink && url != NULL)
			sqlite3_bind_text(feed->stmt, 5,url, -1, SQLITE_TRANSIENT);
		free(url);
	}

	if (!strcmp(feed->xmlpath->data, "/feed/entry/category")) {
		for (i = 0; attr[i] != NULL; i++) {
			if (!strcmp(attr[i], "term")) {
				i++;
				add_tag(feed, attr[i]);
			}
		}
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
		case UNKNOWN:
			break;
	}
}

static void XMLCALL
xml_endel(void *userdata, const char *elt)
{
	struct feed *feed = (struct feed *)userdata;
	int i;

	if (!strcmp(feed->xmlpath->data, "/rss/channel/item") ||
	    !strcmp(feed->xmlpath->data, "/feed/entry")) {
		sqlite3_bind_text(feed->stmt, 2, (const char *)feed->name, -1, SQLITE_STATIC);
		sqlite3_bind_text(feed->stmt, 3, (const char *)feed->blog_title, -1, SQLITE_STATIC);
		if (sqlite3_step(feed->stmt) != SQLITE_DONE)
			warnx("sqlite3: grr: %s", sqlite3_errmsg(db));
		sqlite3_reset(feed->stmt);

		for (i = 0; i < feed->nbtags; i++) {
			sqlite3_bind_text(feed->tags, 2, feed->tag[i], -1, SQLITE_STATIC);
			sqlite3_step(feed->tags);
			free(feed->tag[i]);
		}
		free(feed->tag);
		feed->tag = NULL;
		feed->nbtags = 0;
	}
	feed->xmlpath->size -= strlen(elt);
	feed->xmlpath->data[feed->xmlpath->size] = '\0';
	if (feed->xmlpath->data[feed->xmlpath->size - 1] != '/')
		cplanet_warn("invalid xml");

	feed->xmlpath->size--;
	feed->xmlpath->data[feed->xmlpath->size] = '\0';
}

static void
atom_data(struct feed *feed, const char *data)
{
	if (!strcmp(feed->xmlpath->data, "/feed/title") && feed->blog_title == NULL)
		feed->blog_title = strdup(data);
	if (!strcmp(feed->xmlpath->data, "/feed/entry/id")) {
		sqlite3_bind_text(feed->stmt, 1, data, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(feed->tags, 1, data, -1, SQLITE_TRANSIENT);
	}
	if (!strcmp(feed->xmlpath->data, "/feed/entry/title"))
		sqlite3_bind_text(feed->stmt, 4, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/feed/entry/author/name"))
		sqlite3_bind_text(feed->stmt, 5, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/feed/entry/published"))
		sqlite3_bind_int64(feed->stmt, 9, iso8601_to_time_t(data));
	if (!strcmp(feed->xmlpath->data, "/feed/entry/updated"))
		sqlite3_bind_int64(feed->stmt, 10, iso8601_to_time_t(data));
	if (!strcmp(feed->xmlpath->data, "/feed/entry/content"))
		sqlite3_bind_text(feed->stmt, 7, data, -1, SQLITE_TRANSIENT);
}

static void
rss_data(struct feed *feed, const char *data)
{
	if (!strcmp(feed->xmlpath->data, "/rss/channel/title") && feed->blog_title == NULL)
		feed->blog_title = strdup(data);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/guid")) {
		sqlite3_bind_text(feed->stmt, 1, data, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(feed->tags, 1, data, -1, SQLITE_TRANSIENT);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/title"))
		sqlite3_bind_text(feed->stmt, 4, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/dc:creator"))
		sqlite3_bind_text(feed->stmt, 5, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/link"))
		sqlite3_bind_text(feed->stmt, 6, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/pubDate")) {
		sqlite3_bind_int64(feed->stmt, 9, rfc822_to_time_t(data));
		sqlite3_bind_text(feed->stmt, 10, data, -1, SQLITE_TRANSIENT);
	}
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/category"))
		add_tag(feed, data);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/description"))
		sqlite3_bind_text(feed->stmt, 8, data, -1, SQLITE_TRANSIENT);
	if (!strcmp(feed->xmlpath->data, "/rss/channel/item/content:encoded"))
		sqlite3_bind_text(feed->stmt, 7, data, -1, SQLITE_TRANSIENT);

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
fetch_posts(const unsigned char *name, const unsigned char *url)
{
	CURL *curl;
	CURLcode res;
	struct buffer rawfeed;
	struct XML_ParserStruct *parser;
	struct feed feed;

	/*char *date_format = hdf_get_valuef(hdf_dest, "CPlanet.DateFormat");*/

	rawfeed.data = malloc(1);
	rawfeed.size = 0;

	feed.type = NONE;
	feed.name = name;
	feed.tag = NULL;
	feed.nbtags = 0;
	feed.blog_title = NULL;
	feed.xmlpath = malloc(sizeof(struct buffer));
	feed.xmlpath->size = 0;
	feed.xmlpath->cap = BUFSIZ;
	feed.xmlpath->data = malloc(BUFSIZ);
	feed.xmlpath->data[0] = '\0';

	if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO posts "
	    "(uid, name, blog_title, title, author, link, content, description, "
	    "date, updated, tags) values ("
	    "?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);", -1, &feed.stmt, NULL) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (0);
	}

	if (sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO tags "
	    "(uid, tag) values (?1, ?2)", -1, &feed.tags, NULL) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (0);
	}

	curl_global_init(CURL_GLOBAL_ALL);
	if ((curl = curl_easy_init()) == NULL)
		cplanet_err(1, "Unable to initalise curl");

	curl_easy_setopt(curl, CURLOPT_URL, url);
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
		cplanet_warn("An error occured while fetching %s\n", url);
		free(feed.xmlpath->data);
		free(feed.xmlpath);
		return (0);
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
		    url);
	}

/*	time(&t_now);
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
	}*/

	XML_ParserFree(parser);
	sqlite3_finalize(feed.stmt);
	sqlite3_finalize(feed.tags);

	free(feed.xmlpath->data);
	free(feed.xmlpath);
	return (0);
};

void
generate_file(const unsigned char *cs_output, const unsigned char *cs_path, HDF *hdf)
{
	NEOERR *neoerr;
	CSPARSE *parse;
	STRING cs_output_data;
	string_init(&cs_output_data);
	neoerr = cs_init(&parse, hdf);
	if (neoerr != STATUS_OK)
		goto warn1;

	neoerr = cgi_register_strfuncs(parse);

	if (neoerr != STATUS_OK)
		goto warn0;

	neoerr = cs_parse_file(parse, (char *)cs_path);

	if (neoerr != STATUS_OK)
		goto warn0;

	neoerr = cs_render(parse, &cs_output_data, cplanet_output);

	if (neoerr != STATUS_OK)
		goto warn0;

	FILE *output = fopen((const char *)cs_output, "w+");
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

static void
usage_feed(void)
{
}

static void
usage_config(void)
{
}

static void
usage_update(void)
{
}

static void
usage_output(void)
{
}

static int
exec_output(int argc, char **argv)
{
	sqlite3_stmt *stmt;
	int i;

	if (argc == 0) {
		if (sqlite3_prepare_v2(db,
		  "SELECT path, template FROM output ORDER by path",
		  -1, &stmt, NULL) != SQLITE_OK) {
			warnx("%s", sqlite3_errmsg(db));
			return (EXIT_FAILURE);
		}
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			for (i = 0; i < sqlite3_column_count(stmt); i++) {
				printf("%s%s: %s\n", i == 0 ? "- " : "  ", sqlite3_column_name(stmt, i), sqlite3_column_text(stmt, i));
			}
		}
		sqlite3_finalize(stmt);
		return (EXIT_SUCCESS);
	}

	if (argc != 2) {
		usage_output();
		return (EXIT_FAILURE);
	}

	if (sqlite3_prepare_v2(db,
	  "REPLACE INTO output VALUES (?1, ?2);",
	  -1, &stmt, NULL) != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	sqlite3_bind_text(stmt, 1, argv[0], -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, argv[1], -1, SQLITE_STATIC);

	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (EXIT_SUCCESS);
}

static int
exec_feed(int argc, char **argv)
{
	sqlite3_stmt *stmt;
	int i;

	if (argc == 0) {
		if (sqlite3_prepare_v2(db,
		  "SELECT name, home, url FROM feed ORDER by name",
		  -1, &stmt, NULL) != SQLITE_OK) {
			warnx("%s", sqlite3_errmsg(db));
			return (EXIT_FAILURE);
		}
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			for (i = 0; i < sqlite3_column_count(stmt); i++) {
				printf("%s%s: %s\n", i == 0 ? "- " : "  ", sqlite3_column_name(stmt, i), sqlite3_column_text(stmt, i));
			}
		}
		sqlite3_finalize(stmt);
		return (EXIT_SUCCESS);
	}

	if (argc != 3) {
		usage_feed();
		return (EXIT_FAILURE);
	}

	if (sqlite3_prepare_v2(db,
	  "REPLACE INTO feed VALUES (?1, ?2, ?3);",
	  -1, &stmt, NULL) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	sqlite3_bind_text(stmt, 1, argv[0], -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, argv[1], -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 3, argv[2], -1, SQLITE_STATIC);

	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (EXIT_SUCCESS);
}

static int
exec_config(int argc, char **argv)
{
	sqlite3_stmt *stmt;
	int i;
	int64_t integer;
	const char *errstr;

	if (argc == 0) {
		if (sqlite3_prepare_v2(db,
		  "SELECT key, value FROM config",
		  -1, &stmt, NULL) != SQLITE_OK) {
			warnx("sqlite: %s", sqlite3_errmsg(db));
			return (EXIT_FAILURE);
		}
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			for (i = 0; i < sqlite3_column_count(stmt); i++) {
				switch (sqlite3_column_type(stmt, i)) {
				case SQLITE_TEXT:
					printf("%s%s", sqlite3_column_text(stmt, i), i == 0 ? ": " : " ");
					break;
				case SQLITE_INTEGER:
					printf("%lld%s", sqlite3_column_int64(stmt, i), i == 0 ? ": " : " ");
					break;
				}
			}
			printf("\n");
		}
		sqlite3_finalize(stmt);
		return (EXIT_SUCCESS);
	}

	if (argc != 2) {
		usage_config();
		return (EXIT_FAILURE);
	}

	sql_int(&integer, "select count(*) from config where key='%s'", argv[0]);
	if (integer != 1) {
		warnx("Unknown key: %s", argv[0]);
		return (EXIT_FAILURE);
	}
	if (sqlite3_prepare_v2(db,
	  "REPLACE INTO config VALUES (?1, ?2);",
	  -1, &stmt, NULL) != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	sqlite3_bind_text(stmt, 1, argv[0], -1, SQLITE_STATIC);
	integer = strtonum(argv[1], 0, INT64_MAX, &errstr);
	if (errstr)
		sqlite3_bind_text(stmt, 2, argv[1], -1, SQLITE_STATIC);
	else
		sqlite3_bind_int64(stmt, 2, integer);

	sqlite3_step(stmt);
	sqlite3_finalize(stmt);

	return (EXIT_SUCCESS);
}

static int
exec_update(int argc, char **argv)
{
	sqlite3_stmt *stmt;
	int pos = 0;
	NEOERR *neoerr;
	HDF *hdf;
	char *val;

	sql_exec("BEGIN;");
	if (sqlite3_prepare_v2(db, "SELECT name, url from feed;",
	    -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
		fetch_posts(sqlite3_column_text(stmt, 0) ,sqlite3_column_text(stmt, 1));

	sqlite3_finalize(stmt);

	sql_exec("DELETE from tags where uid not in (select uid from posts);");
	sql_exec("COMMIT;");

	if (sqlite3_prepare_v2(db, "SELECT "
	    "name, "
	    "blog_title, "
	    "title, "
	    "author, "
	    "link, "
	    "date, "
	    "strftime('%a, %d %b %Y %H:%M:%S %z', date) as rfc822, "
	    "strftime('%Y-%m-%dT%H:%M:%SZ', date) as iso8601, "
	    "strftime((select value from config where key='date_format'), date), "
	    "description "
	    "from posts config order by date DESC LIMIT (SELECT value from config where key='max_post');",
	    -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	string_init(&neoerr_str);
	neoerr = hdf_init(&hdf);
	if (neoerr != STATUS_OK) {
		nerr_error_string(neoerr, &neoerr_str);
		warnx("hdf: %s", neoerr_str.buf);
		return (EXIT_FAILURE);
	}

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		cp_set_name(hdf, pos, sqlite3_column_text(stmt, 0));
		cp_set_feedname(hdf, pos, sqlite3_column_text(stmt, 1));
		cp_set_author(hdf, pos, sqlite3_column_text(stmt, 2));
		cp_set_title(hdf, pos, sqlite3_column_text(stmt, 3));
		cp_set_date(hdf, pos, sqlite3_column_int64(stmt, 4));
		cp_set_date_rfc822(hdf, pos, sqlite3_column_text(stmt, 5));
		cp_set_date_iso8601(hdf, pos, sqlite3_column_text(stmt, 6));
		cp_set_formated_date(hdf, pos, sqlite3_column_text(stmt, 7));
		cp_set_description(hdf, pos, sqlite3_column_text(stmt, 8));
		pos++;
	}

	sqlite3_finalize(stmt);

	sql_text(&val, "SELECT value FROM config WHERE key='title';");
	hdf_set_valuef(hdf, "CPlanet.Name=%s", val);
	free(val);

	sql_text(&val, "SELECT value FROM config WHERE key='description';");
	hdf_set_valuef(hdf, "CPlanet.Description=%s", val);
	free(val);

	sql_text(&val, "SELECT value FROM config WHERE key='url';");
	hdf_set_valuef(hdf, "CPlanet.URL=%s", val);
	free(val);

	sql_text(&val, "SELECT strftime(value, 'now') from config where key='date_format';");
	cp_set_gen_date(hdf, val);
	free(val);

	sql_text(&val, "SELECT strftime('%Y-%m-%dT%H:%M:%SZ', 'now');");
	cp_set_gen_iso8601(hdf, val);
	free(val);

	sql_text(&val, "SELECT strftime('%a, %d %b %Y %H:%M:%S %z', 'now');");
	cp_set_gen_rfc822(hdf, val);
	free(val);

	cp_set_version(hdf);

	if (sqlite3_prepare_v2(db, "SELECT name, home, url from feed order by name;",
	    -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	pos=0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		hdf_set_valuef(hdf, "CPlanet.Feed.%i.Name=%s", pos, sqlite3_column_text(stmt, 0));
		hdf_set_valuef(hdf, "CPlanet.Feed.%i.Home=%s", pos, sqlite3_column_text(stmt, 1));
		hdf_set_valuef(hdf, "CPlanet.Feed.%i.URL=%s", pos, sqlite3_column_text(stmt, 2));
		pos++;
	}

	sqlite3_finalize(stmt);

	if (sqlite3_prepare_v2(db, "SELECT path, template from output;",
	    -1, &stmt, 0) != SQLITE_OK) {
		warnx("sqlite: %s", sqlite3_errmsg(db));
		return (EXIT_FAILURE);
	}

	while (sqlite3_step(stmt) == SQLITE_ROW)
		generate_file(sqlite3_column_text(stmt, 0), sqlite3_column_text(stmt, 1), hdf);

	sqlite3_finalize(stmt);

	return (EXIT_SUCCESS);
}

static struct commands {
	const char * const name;
	const char * const desc;
	int (*exec)(int argc, char **argv);
	void (* const usage)(void);
} cmd[] = {
	{ "feed", "Manipulate feeds", exec_feed, usage_feed },
	{ "config", "Modify configuration", exec_config, usage_config },
	{ "output", "Configure output files", exec_output, usage_output },
	{ "update", "Update the planet", exec_update, usage_update },
};

static const unsigned int cmd_len = sizeof(cmd) / sizeof(cmd[0]);

static bool
db_open(const char *dbpath)
{
	int ret;

	if (sqlite3_open(dbpath, &db) != SQLITE_OK) {
		warnx("%s", sqlite3_errmsg(db));
		return (false);
	}

	ret = sql_exec(
	    "CREATE TABLE IF NOT EXISTS config "
	      "(key TEXT NOT NULL UNIQUE, "
	      "value);"
	    "CREATE TABLE IF NOT EXISTS feed "
	      "(name TEXT NOT NULL UNIQUE, "
	      "url TEXT NOT NULL UNIQUE, "
	      "home TEXT NOT NULL UNIQUE); "
	    "CREATE TABLE IF NOT EXISTS output "
	      "(path UNIQUE, template);"
	    "CREATE TABLE IF NOT EXISTS posts "
	      "(uid UNIQUE, name, blog_title, title, "
	      "author, link, content, "
	      "description, date, updated, tags);"
	    "CREATE TABLE IF NOT EXISTS tags "
	      "(uid, tag);"

/* Popupate with default data */
	    "INSERT OR IGNORE INTO config values "
	      "('title', 'default');"
	    "INSERT OR IGNORE INTO config values "
	      "('description', 'default');"
	    "INSERT OR IGNORE INTO config values "
	      "('date_format', '%%d/%%m/%%Y');"
	    "INSERT OR IGNORE INTO config values "
	      "('max_post', 10);"
	    "INSERT OR IGNORE INTO config values "
	      "('url', 'http://undefined');"
	      );

	if (ret < 0) {
		warnx("%s", sqlite3_errmsg(db));
		return (false);
	}

	return (true);
}

int
main (int argc, char *argv[])
{
	int ch = 0;
	char *hdf_file = NULL;
	time_t t_now;
	int i, ambiguous, ret;
	size_t len;
	struct commands *command = NULL;
	char tmpdbpath[MAXPATHLEN];
	const char *dbpath = NULL;

	t_now = time(NULL);

	if (argc == 1)
		usage();

	while ((ch = getopt(argc, argv, "c:lhd:")) != -1)
		switch (ch) {  
			case 'h':
				usage();
				break;
			case 'd':
				dbpath = optarg;
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

	if (argc == 0)
		usage();

	ambiguous = 0;
	len = strlen(argv[0]);
	for (i = 0; i < cmd_len; i++) {
		if (strncmp(argv[0], cmd[i].name, len) == 0) {
			/* if we have the exact cmd */
			if (len == strlen(cmd[i].name)) {
				command = &cmd[i];
				ambiguous = 0;
				break;
			}
			/*
			 * we already found a partial match so `argv[0]' is
			 * an ambiguous shortcut
			 */
			ambiguous++;

			command = &cmd[i];
		}
	}

	if (command == NULL)
		usage();

	if (ambiguous > 1) {
		warnx("'%s' is not a valid command.\n", argv[0]);
		return (EXIT_FAILURE);
	}

	if (dbpath == NULL) {
		const char *s = getenv("HOME");
		if (s == NULL)
			errx(EXIT_FAILURE, "Unable to determine the home directory");
		snprintf(tmpdbpath, sizeof(tmpdbpath), "%s/.cplanet", s);
		dbpath = tmpdbpath;
	}

	sqlite3_initialize();

	if (!db_open(dbpath))
		return (EXIT_FAILURE);

	argc -= optind;
	argv += optind;

	assert(command->exec != NULL);
	ret = command->exec(argc, argv);
	sqlite3_shutdown();

	return (ret);
}
