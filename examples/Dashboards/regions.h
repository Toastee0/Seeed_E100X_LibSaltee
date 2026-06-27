// regions.h — offline location + timezone tables for the Dashboards onboarding portal.
//
// The WiFi-setup captive portal needs lat/long (for Open-Meteo weather) and a POSIX TZ string
// (for the NTP clock) WITHOUT any internet — at portal time the device only has its own SoftAP
// up, it isn't on the home network yet. So instead of a live geocoder we ship a small curated
// city table (instant, zero-config: picking a city fills lat/long AND derives the timezone) plus
// a POSIX-TZ picker for the "custom lat/long" path. POSIX TZ strings carry their own DST rules,
// so the clock follows daylight time correctly. GPL-3.0-or-later.
#pragma once

struct RegionCity { const char* name; float lat; float lon; const char* tz; };
struct RegionZone { const char* label; const char* tz; };

// A spread of major cities, each with its POSIX TZ (DST rules included where they apply).
static const RegionCity REGION_CITIES[] = {
  // North America
  { "New York",      40.7128f,  -74.0060f, "EST5EDT,M3.2.0,M11.1.0" },
  { "Toronto",       43.6532f,  -79.3832f, "EST5EDT,M3.2.0,M11.1.0" },
  { "Chicago",       41.8781f,  -87.6298f, "CST6CDT,M3.2.0,M11.1.0" },
  { "Denver",        39.7392f, -104.9903f, "MST7MDT,M3.2.0,M11.1.0" },
  { "Phoenix",       33.4484f, -112.0740f, "MST7" },
  { "Los Angeles",   34.0522f, -118.2437f, "PST8PDT,M3.2.0,M11.1.0" },
  { "Vancouver",     49.2827f, -123.1207f, "PST8PDT,M3.2.0,M11.1.0" },
  { "Halifax",       44.6488f,  -63.5752f, "AST4ADT,M3.2.0,M11.1.0" },
  { "Mexico City",   19.4326f,  -99.1332f, "CST6" },
  { "Anchorage",     61.2181f, -149.9003f, "AKST9AKDT,M3.2.0,M11.1.0" },
  { "Honolulu",      21.3069f, -157.8583f, "HST10" },
  // South America
  { "Bogota",         4.7110f,  -74.0721f, "<-05>5" },
  { "Lima",         -12.0464f,  -77.0428f, "<-05>5" },
  { "Sao Paulo",    -23.5505f,  -46.6333f, "<-03>3" },
  { "Buenos Aires", -34.6037f,  -58.3816f, "<-03>3" },
  { "Santiago",     -33.4489f,  -70.6693f, "<-04>4" },
  // Europe
  { "London",        51.5074f,   -0.1278f, "GMT0BST,M3.5.0/1,M10.5.0" },
  { "Dublin",        53.3498f,   -6.2603f, "GMT0IST,M3.5.0/1,M10.5.0" },
  { "Lisbon",        38.7223f,   -9.1393f, "WET0WEST,M3.5.0/1,M10.5.0" },
  { "Paris",         48.8566f,    2.3522f, "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Berlin",        52.5200f,   13.4050f, "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Madrid",        40.4168f,   -3.7038f, "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Rome",          41.9028f,   12.4964f, "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Amsterdam",     52.3676f,    4.9041f, "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "Athens",        37.9838f,   23.7275f, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "Helsinki",      60.1699f,   24.9384f, "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "Istanbul",      41.0082f,   28.9784f, "<+03>-3" },
  { "Moscow",        55.7558f,   37.6173f, "MSK-3" },
  // Africa / Middle East
  { "Cairo",         30.0444f,   31.2357f, "EET-2EEST,M4.5.5/0,M10.5.4/24" },
  { "Lagos",          6.5244f,    3.3792f, "WAT-1" },
  { "Nairobi",       -1.2921f,   36.8219f, "EAT-3" },
  { "Johannesburg", -26.2041f,   28.0473f, "SAST-2" },
  { "Dubai",         25.2048f,   55.2708f, "<+04>-4" },
  { "Tel Aviv",      32.0853f,   34.7818f, "IST-2IDT,M3.4.4/26,M10.5.0" },
  // Asia / Oceania
  { "Karachi",       24.8607f,   67.0011f, "PKT-5" },
  { "Mumbai",        19.0760f,   72.8777f, "IST-5:30" },
  { "New Delhi",     28.6139f,   77.2090f, "IST-5:30" },
  { "Dhaka",         23.8103f,   90.4125f, "<+06>-6" },
  { "Bangkok",       13.7563f,  100.5018f, "<+07>-7" },
  { "Jakarta",       -6.2088f,  106.8456f, "WIB-7" },
  { "Singapore",      1.3521f,  103.8198f, "<+08>-8" },
  { "Hong Kong",     22.3193f,  114.1694f, "HKT-8" },
  { "Beijing",       39.9042f,  116.4074f, "CST-8" },
  { "Shanghai",      31.2304f,  121.4737f, "CST-8" },
  { "Seoul",         37.5665f,  126.9780f, "KST-9" },
  { "Tokyo",         35.6762f,  139.6503f, "JST-9" },
  { "Perth",        -31.9505f,  115.8605f, "AWST-8" },
  { "Sydney",       -33.8688f,  151.2093f, "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "Melbourne",    -37.8136f,  144.9631f, "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "Auckland",     -36.8509f,  174.7645f, "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
static const int REGION_NCITIES = sizeof(REGION_CITIES) / sizeof(REGION_CITIES[0]);

// POSIX-TZ picker for the manual lat/long path — one entry per common UTC offset.
static const RegionZone REGION_ZONES[] = {
  { "UTC+0  Coordinated (UTC)", "UTC0" },
  { "UTC-10 Hawaii",            "HST10" },
  { "UTC-9  Alaska",            "AKST9AKDT,M3.2.0,M11.1.0" },
  { "UTC-8  US/Canada Pacific", "PST8PDT,M3.2.0,M11.1.0" },
  { "UTC-7  US/Canada Mountain","MST7MDT,M3.2.0,M11.1.0" },
  { "UTC-7  Arizona (no DST)",  "MST7" },
  { "UTC-6  US/Canada Central", "CST6CDT,M3.2.0,M11.1.0" },
  { "UTC-5  US/Canada Eastern", "EST5EDT,M3.2.0,M11.1.0" },
  { "UTC-4  Atlantic",          "AST4ADT,M3.2.0,M11.1.0" },
  { "UTC-3  Brazil/Argentina",  "<-03>3" },
  { "UTC+0  UK/Ireland",        "GMT0BST,M3.5.0/1,M10.5.0" },
  { "UTC+0  Iceland (no DST)",  "GMT0" },
  { "UTC+1  Central Europe",    "CET-1CEST,M3.5.0,M10.5.0/3" },
  { "UTC+2  Eastern Europe",    "EET-2EEST,M3.5.0/3,M10.5.0/4" },
  { "UTC+2  South Africa",      "SAST-2" },
  { "UTC+3  Moscow/Turkey",     "<+03>-3" },
  { "UTC+3:30 Iran",            "<+0330>-3:30" },
  { "UTC+4  Gulf",              "<+04>-4" },
  { "UTC+5  Pakistan",          "PKT-5" },
  { "UTC+5:30 India",           "IST-5:30" },
  { "UTC+6  Bangladesh",        "<+06>-6" },
  { "UTC+7  SE Asia",           "<+07>-7" },
  { "UTC+8  China/Singapore",   "<+08>-8" },
  { "UTC+9  Japan/Korea",       "JST-9" },
  { "UTC+9:30 Adelaide",        "ACST-9:30ACDT,M10.1.0,M4.1.0/3" },
  { "UTC+10 Sydney",            "AEST-10AEDT,M10.1.0,M4.1.0/3" },
  { "UTC+12 New Zealand",       "NZST-12NZDT,M9.5.0,M4.1.0/3" },
};
static const int REGION_NZONES = sizeof(REGION_ZONES) / sizeof(REGION_ZONES[0]);
