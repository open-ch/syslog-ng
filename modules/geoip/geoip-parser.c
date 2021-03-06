/*
 * Copyright (c) 2015 Balabit
 * Copyright (c) 2015 Gergely Nagy
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "geoip-parser.h"
#include "parser/parser-expr.h"
#include "scratch-buffers.h"

#include <GeoIPCity.h>

typedef struct
{
  LogParser super;
  GeoIP *gi;

  gchar *database;
  gchar *prefix;

  struct
  {
    gchar *country_code;
    gchar *longitude;
    gchar *latitude;
  } dest;
} GeoIPParser;

void
geoip_parser_set_prefix(LogParser *s, const gchar *prefix)
{
  GeoIPParser *self = (GeoIPParser *) s;

  g_free(self->prefix);
  self->prefix = g_strdup(prefix);
}

static void
geoip_parser_reset_fields(GeoIPParser *self)
{
  g_free(self->dest.country_code);
  self->dest.country_code = g_strdup_printf("%scountry_code", self->prefix);

  g_free(self->dest.longitude);
  self->dest.longitude = g_strdup_printf("%slongitude", self->prefix);

  g_free(self->dest.latitude);
  self->dest.latitude = g_strdup_printf("%slatitude", self->prefix);
}

void
geoip_parser_set_database(LogParser *s, const gchar *database)
{
  GeoIPParser *self = (GeoIPParser *) s;

  g_free(self->database);
  self->database = g_strdup(database);
}

static gboolean
geoip_parser_process(LogParser *s, LogMessage **pmsg,
                     const LogPathOptions *path_options,
                     const gchar *input, gsize input_len)
{
  GeoIPParser *self = (GeoIPParser *) s;
  LogMessage *msg = log_msg_make_writable(pmsg, path_options);
  GeoIPRecord *record;
  SBGString *value;

  if (!self->dest.country_code &&
      !self->dest.latitude &&
      !self->dest.longitude)
    return TRUE;

  record = GeoIP_record_by_name(self->gi, input);

  if (!record)
    {
      const char *country;

      country = GeoIP_country_code_by_name(self->gi, input);
      if (country)
        log_msg_set_value_by_name(msg, self->dest.country_code,
                                  country,
                                  strlen(country));

      return TRUE;
    }

  if (record->country_code)
    log_msg_set_value_by_name(msg, self->dest.country_code,
                              record->country_code,
                              strlen(record->country_code));

  value = sb_gstring_acquire();

  g_string_printf(sb_gstring_string(value), "%f",
                  record->latitude);
  log_msg_set_value_by_name(msg, self->dest.latitude,
                            sb_gstring_string(value)->str,
                            sb_gstring_string(value)->len);

  g_string_printf(sb_gstring_string(value), "%f",
                  record->longitude);
  log_msg_set_value_by_name(msg, self->dest.longitude,
                            sb_gstring_string(value)->str,
                            sb_gstring_string(value)->len);

  GeoIPRecord_delete(record);
  sb_gstring_release(value);

  return TRUE;
}

static LogPipe *
geoip_parser_clone(LogPipe *s)
{
  GeoIPParser *self = (GeoIPParser *) s;
  GeoIPParser *cloned;

  cloned = (GeoIPParser *) geoip_parser_new(s->cfg);

  geoip_parser_set_database(&cloned->super, self->database);
  geoip_parser_set_prefix(&cloned->super, self->prefix);
  geoip_parser_reset_fields(cloned);

  return &cloned->super.super;
}

static void
geoip_parser_free(LogPipe *s)
{
  GeoIPParser *self = (GeoIPParser *) s;

  g_free(self->dest.country_code);
  g_free(self->dest.latitude);
  g_free(self->dest.longitude);
  g_free(self->database);
  g_free(self->prefix);

  GeoIP_delete(self->gi);

  log_parser_free_method(s);
}

static gboolean
geoip_parser_init(LogPipe *s)
{
  GeoIPParser *self = (GeoIPParser *) s;

  geoip_parser_reset_fields(self);

  self->gi = GeoIP_open(self->database, GEOIP_MMAP_CACHE);

  if (!self->gi)
    return FALSE;
  return log_parser_init_method(s);
}

LogParser *
geoip_parser_new(GlobalConfig *cfg)
{
  GeoIPParser *self = g_new0(GeoIPParser, 1);

  log_parser_init_instance(&self->super, cfg);
  self->super.super.init = geoip_parser_init;
  self->super.super.free_fn = geoip_parser_free;
  self->super.super.clone = geoip_parser_clone;
  self->super.process = geoip_parser_process;

  geoip_parser_set_database(&self->super, "/usr/share/GeoIP/GeoIP.dat");
  geoip_parser_set_prefix(&self->super, ".geoip.");

  return &self->super;
}
