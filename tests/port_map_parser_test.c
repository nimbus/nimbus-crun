#include <arpa/inet.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NIMBUS_KRUN_PORT_MAP_MAX_ANNOTATION_LEN 8192
#define NIMBUS_KRUN_PORT_MAP_MAX_ENTRIES 256
#define UNLIKELY(value) __builtin_expect (!!(value), 0)

typedef struct
{
  char message[256];
} libcrun_error_t;

static void
cleanup_freep (void *ptr)
{
  free (*(void **) ptr);
}

#define cleanup_free __attribute__ ((cleanup (cleanup_freep)))

static void *
xmalloc0 (size_t size)
{
  void *ptr = calloc (1, size);
  if (ptr == NULL)
    abort ();
  return ptr;
}

static int
crun_make_error (libcrun_error_t *err, int code, const char *fmt, ...)
{
  va_list args;
  (void) code;

  va_start (args, fmt);
  vsnprintf (err->message, sizeof (err->message), fmt, args);
  va_end (args);
  return -1;
}

static int
libkrun_parse_port_map_port (const char *value, size_t len, uint16_t *port, libcrun_error_t *err)
{
  unsigned long parsed = 0;
  size_t i;

  if (len == 0)
    return crun_make_error (err, 0, "krun.port_map port cannot be empty");

  for (i = 0; i < len; i++)
    {
      if (value[i] < '0' || value[i] > '9')
        return crun_make_error (err, 0, "krun.port_map port contains a non-decimal character");

      parsed = (parsed * 10) + (unsigned long) (value[i] - '0');
      if (parsed > UINT16_MAX)
        return crun_make_error (err, 0, "krun.port_map port is outside the valid u16 range");
    }

  if (parsed == 0)
    return crun_make_error (err, 0, "krun.port_map port must be greater than zero");

  *port = (uint16_t) parsed;
  return 0;
}

struct libkrun_port_map_entry
{
  bool has_bind_address;
  bool bind_address_unspecified;
  int bind_address_family;
  struct in6_addr canonical_bind_address;
  uint16_t host_port;
  uint16_t guest_port;
};

static int
libkrun_parse_port_map_address (const char *value, size_t len, int family, struct libkrun_port_map_entry *parsed, libcrun_error_t *err)
{
  char address[INET6_ADDRSTRLEN];
  struct in_addr ipv4;
  struct in_addr mapped_ipv4;
  struct in6_addr ipv6;

  if (len == 0)
    return crun_make_error (err, 0, "krun.port_map bind address cannot be empty");

  if (len >= sizeof (address))
    return crun_make_error (err, 0, "krun.port_map bind address is too long");

  memcpy (address, value, len);
  address[len] = '\0';

  if (family == AF_INET)
    {
      if (inet_pton (AF_INET, address, &ipv4) != 1)
        return crun_make_error (err, 0, "krun.port_map bind address is not a valid IPv4 address");

      if (IN_MULTICAST (ntohl (ipv4.s_addr)))
        return crun_make_error (err, 0, "krun.port_map bind address cannot be multicast");

      memset (&parsed->canonical_bind_address, 0, sizeof (parsed->canonical_bind_address));
      parsed->canonical_bind_address.s6_addr[10] = 0xff;
      parsed->canonical_bind_address.s6_addr[11] = 0xff;
      memcpy (&parsed->canonical_bind_address.s6_addr[12], &ipv4.s_addr, sizeof (ipv4.s_addr));
      parsed->bind_address_unspecified = ipv4.s_addr == htonl (INADDR_ANY);
      parsed->bind_address_family = AF_INET;
    }
  else if (family == AF_INET6)
    {
      if (inet_pton (AF_INET6, address, &ipv6) != 1)
        return crun_make_error (err, 0, "krun.port_map bind address is not a valid IPv6 address");

      if (IN6_IS_ADDR_V4MAPPED (&ipv6))
        {
          memcpy (&mapped_ipv4.s_addr, &ipv6.s6_addr[12], sizeof (mapped_ipv4.s_addr));
          if (IN_MULTICAST (ntohl (mapped_ipv4.s_addr)))
            return crun_make_error (err, 0, "krun.port_map bind address cannot be multicast");
          parsed->bind_address_unspecified = mapped_ipv4.s_addr == htonl (INADDR_ANY);
          parsed->bind_address_family = AF_INET;
        }
      else
        {
          if (IN6_IS_ADDR_MULTICAST (&ipv6) || IN6_IS_ADDR_LINKLOCAL (&ipv6))
            return crun_make_error (err, 0, "krun.port_map bind address requires an unsupported IPv6 scope");
          parsed->bind_address_unspecified = IN6_IS_ADDR_UNSPECIFIED (&ipv6);
          parsed->bind_address_family = AF_INET6;
        }

      parsed->canonical_bind_address = ipv6;
    }
  else
    return crun_make_error (err, 0, "krun.port_map bind address family is unsupported");

  parsed->has_bind_address = true;
  return 0;
}

static bool
libkrun_unspecified_bind_address_conflicts (const struct libkrun_port_map_entry *entry, const struct libkrun_port_map_entry *other)
{
  if (!entry->bind_address_unspecified)
    return false;

  return entry->bind_address_family == AF_INET6 || other->bind_address_family == AF_INET;
}

static int
libkrun_parse_port_map_entry (const char *entry, size_t len, struct libkrun_port_map_entry *parsed, libcrun_error_t *err)
{
  const char *first_separator;
  const char *second_separator;
  const char *closing_bracket;
  int ret;

  memset (parsed, 0, sizeof (*parsed));

  if (len == 0)
    return crun_make_error (err, 0, "krun.port_map entry cannot be empty");

  if (entry[0] == '[')
    {
      closing_bracket = memchr (entry + 1, ']', len - 1);
      if (closing_bracket == NULL || (size_t) (closing_bracket - entry + 2) >= len || closing_bracket[1] != ':')
        return crun_make_error (err, 0, "krun.port_map IPv6 bind address must use [addr]:host_port:guest_port");

      ret = libkrun_parse_port_map_address (entry + 1, closing_bracket - entry - 1, AF_INET6, parsed, err);
      if (UNLIKELY (ret < 0))
        return ret;

      first_separator = closing_bracket + 1;
      second_separator = memchr (first_separator + 1, ':', len - (first_separator - entry) - 1);
      if (second_separator == NULL || memchr (second_separator + 1, ':', len - (second_separator - entry) - 1) != NULL)
        return crun_make_error (err, 0, "krun.port_map IPv6 entry must be [addr]:host_port:guest_port");

      ret = libkrun_parse_port_map_port (first_separator + 1, second_separator - first_separator - 1, &parsed->host_port, err);
      if (UNLIKELY (ret < 0))
        return ret;

      return libkrun_parse_port_map_port (second_separator + 1, len - (second_separator - entry) - 1, &parsed->guest_port, err);
    }

  first_separator = memchr (entry, ':', len);
  if (first_separator == NULL)
    return crun_make_error (err, 0, "krun.port_map entry must be host_port:guest_port or address:host_port:guest_port");

  second_separator = memchr (first_separator + 1, ':', len - (first_separator - entry) - 1);
  if (second_separator == NULL)
    {
      ret = libkrun_parse_port_map_port (entry, first_separator - entry, &parsed->host_port, err);
      if (UNLIKELY (ret < 0))
        return ret;

      return libkrun_parse_port_map_port (first_separator + 1, len - (first_separator - entry) - 1, &parsed->guest_port, err);
    }

  if (memchr (second_separator + 1, ':', len - (second_separator - entry) - 1) != NULL)
    return crun_make_error (err, 0, "krun.port_map IPv6 bind addresses must be bracketed");

  ret = libkrun_parse_port_map_address (entry, first_separator - entry, AF_INET, parsed, err);
  if (UNLIKELY (ret < 0))
    return ret;

  ret = libkrun_parse_port_map_port (first_separator + 1, second_separator - first_separator - 1, &parsed->host_port, err);
  if (UNLIKELY (ret < 0))
    return ret;

  return libkrun_parse_port_map_port (second_separator + 1, len - (second_separator - entry) - 1, &parsed->guest_port, err);
}

static int
libkrun_reject_duplicate_port_map_entry (const struct libkrun_port_map_entry *entries, size_t entries_len, const struct libkrun_port_map_entry *candidate, libcrun_error_t *err)
{
  size_t i;

  for (i = 0; i < entries_len; i++)
    {
      if (entries[i].guest_port == candidate->guest_port)
        return crun_make_error (err, 0, "krun.port_map contains a duplicate guest port");

      if (entries[i].host_port == candidate->host_port
          && (!entries[i].has_bind_address || !candidate->has_bind_address
              || libkrun_unspecified_bind_address_conflicts (&entries[i], candidate)
              || libkrun_unspecified_bind_address_conflicts (candidate, &entries[i])
              || memcmp (&entries[i].canonical_bind_address, &candidate->canonical_bind_address,
                         sizeof (candidate->canonical_bind_address)) == 0))
        return crun_make_error (err, 0, "krun.port_map contains a conflicting host endpoint");
    }

  return 0;
}

static int
libkrun_validate_port_map_annotation (const char *annotation, size_t *entries_len, bool *requires_bind_address, libcrun_error_t *err)
{
  cleanup_free struct libkrun_port_map_entry *entries = NULL;
  size_t annotation_len;
  size_t offset = 0;
  int ret;

  *entries_len = 0;
  *requires_bind_address = false;

  annotation_len = strlen (annotation);
  if (annotation_len == 0)
    return crun_make_error (err, 0, "krun.port_map annotation cannot be empty");

  if (annotation_len > NIMBUS_KRUN_PORT_MAP_MAX_ANNOTATION_LEN)
    return crun_make_error (err, 0, "krun.port_map annotation is too long");

  entries = xmalloc0 (sizeof (*entries) * NIMBUS_KRUN_PORT_MAP_MAX_ENTRIES);

  while (offset < annotation_len)
    {
      struct libkrun_port_map_entry parsed;
      const char *entry = annotation + offset;
      const char *comma = memchr (entry, ',', annotation_len - offset);
      size_t entry_len = comma != NULL ? (size_t) (comma - entry) : annotation_len - offset;

      if (entry_len == 0)
        return crun_make_error (err, 0, "krun.port_map entry cannot be empty");

      if (*entries_len == NIMBUS_KRUN_PORT_MAP_MAX_ENTRIES)
        return crun_make_error (err, 0, "krun.port_map contains too many entries");

      ret = libkrun_parse_port_map_entry (entry, entry_len, &parsed, err);
      if (UNLIKELY (ret < 0))
        return ret;

      ret = libkrun_reject_duplicate_port_map_entry (entries, *entries_len, &parsed, err);
      if (UNLIKELY (ret < 0))
        return ret;

      if (parsed.has_bind_address)
        *requires_bind_address = true;

      entries[*entries_len] = parsed;
      *entries_len = *entries_len + 1;

      if (comma == NULL)
        break;

      offset = (comma - annotation) + 1;
      if (offset == annotation_len)
        return crun_make_error (err, 0, "krun.port_map entry cannot be empty");
    }

  return 0;
}

static void
expect_valid (const char *annotation, size_t expected_entries_len, bool expected_requires_bind_address)
{
  libcrun_error_t err = { 0 };
  size_t entries_len = 0;
  bool requires_bind_address = false;

  if (libkrun_validate_port_map_annotation (annotation, &entries_len, &requires_bind_address, &err) < 0)
    {
      fprintf (stderr, "expected valid annotation %s, got error: %s\n", annotation, err.message);
      exit (1);
    }

  if (entries_len != expected_entries_len || requires_bind_address != expected_requires_bind_address)
    {
      fprintf (stderr, "unexpected parse result for %s: entries=%zu requires_bind_address=%d\n",
               annotation, entries_len, requires_bind_address);
      exit (1);
    }
}

static void
expect_invalid (const char *annotation, const char *expected_message)
{
  libcrun_error_t err = { 0 };
  size_t entries_len = 0;
  bool requires_bind_address = false;

  if (libkrun_validate_port_map_annotation (annotation, &entries_len, &requires_bind_address, &err) == 0)
    {
      fprintf (stderr, "expected invalid annotation %s\n", annotation);
      exit (1);
    }

  if (strstr (err.message, expected_message) == NULL)
    {
      fprintf (stderr, "unexpected error for %s: %s\n", annotation, err.message);
      exit (1);
    }
}

int
main (void)
{
  char long_annotation[NIMBUS_KRUN_PORT_MAP_MAX_ANNOTATION_LEN + 2];

  expect_valid ("18080:8080", 1, false);
  expect_valid ("18080:8080,15432:5432", 2, false);
  expect_valid ("127.0.0.1:18080:8080", 1, true);
  expect_valid ("[::1]:18080:8080", 1, true);
  expect_valid ("127.0.0.1:18080:8080,[::1]:18443:8443", 2, true);
  expect_valid ("127.0.0.1:18080:8080,127.0.0.2:18080:8081,[::1]:18080:8082", 3, true);
  expect_valid ("0.0.0.0:18080:8080,[::1]:18080:8081", 2, true);
  expect_valid ("[::1]:18080:8080,0.0.0.0:18080:8081", 2, true);

  expect_invalid ("", "annotation cannot be empty");
  expect_invalid (",18080:8080", "entry cannot be empty");
  expect_invalid ("18080:8080,", "entry cannot be empty");
  expect_invalid ("18080::8080", "not a valid IPv4");
  expect_invalid ("localhost:18080:8080", "not a valid IPv4");
  expect_invalid ("::1:18080:8080", "IPv6 bind addresses must be bracketed");
  expect_invalid ("999999:8080", "outside the valid u16 range");
  expect_invalid ("0:8080", "must be greater than zero");
  expect_invalid ("18080:0", "must be greater than zero");
  expect_invalid ("224.0.0.1:18080:8080", "cannot be multicast");
  expect_invalid ("[::ffff:224.0.0.1]:18080:8080", "cannot be multicast");
  expect_invalid ("[ff02::1]:18080:8080", "unsupported IPv6 scope");
  expect_invalid ("[fe80::1]:18080:8080", "unsupported IPv6 scope");
  expect_invalid ("127.0.0.1:18080:8080,127.0.0.1:18080:9090", "conflicting host endpoint");
  expect_invalid ("18080:8080,127.0.0.1:18080:9090", "conflicting host endpoint");
  expect_invalid ("0.0.0.0:18080:8080,127.0.0.1:18080:9090", "conflicting host endpoint");
  expect_invalid ("[::]:18080:8080,[::1]:18080:9090", "conflicting host endpoint");
  expect_invalid ("[::]:18080:8080,127.0.0.1:18080:9090", "conflicting host endpoint");
  expect_invalid ("[::ffff:0.0.0.0]:18080:8080,127.0.0.1:18080:9090", "conflicting host endpoint");
  expect_invalid ("127.0.0.1:18080:8080,[::ffff:127.0.0.1]:18080:9090", "conflicting host endpoint");
  expect_invalid ("127.0.0.1:18080:8080,127.0.0.1:18081:8080", "duplicate guest port");

  memset (long_annotation, '1', sizeof (long_annotation));
  long_annotation[sizeof (long_annotation) - 1] = '\0';
  expect_invalid (long_annotation, "annotation is too long");

  return 0;
}
