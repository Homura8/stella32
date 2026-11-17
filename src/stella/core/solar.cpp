#include "solar.h"

#include <math.h>

namespace stella {
namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kRad = kPi / 180.0;
}  // namespace

SunTimes sunTimes(int64_t localNoon, float latitude, float longitude) {
  SunTimes out;
  // Julian date of that moment, and the whole number of days since J2000 of the
  // solar noon nearest to it at this longitude.
  const double jd = (double)localNoon / 86400.0 + 2440587.5;
  const double n = floor(jd - 2451545.0 + (double)longitude / 360.0 + 0.5);
  const double jStar = n - (double)longitude / 360.0;  // mean solar noon (days since J2000)
  const double m = fmod(357.5291 + 0.98560028 * jStar, 360.0) * kRad;
  const double c = 1.9148 * sin(m) + 0.02 * sin(2 * m) + 0.0003 * sin(3 * m);
  const double lambda = fmod(fmod(357.5291 + 0.98560028 * jStar, 360.0) + c + 180.0 + 102.9372, 360.0) * kRad;
  const double transit = 2451545.0 + jStar + 0.0053 * sin(m) - 0.0069 * sin(2 * lambda);
  const double sinDec = sin(lambda) * sin(23.4397 * kRad);
  const double cosDec = cos(asin(sinDec));
  const double phi = (double)latitude * kRad;
  const double cosOmega = (sin(-0.833 * kRad) - sin(phi) * sinDec) / (cos(phi) * cosDec);
  if (cosOmega > 1.0) {
    out.polarNight = true;
    return out;
  }
  if (cosOmega < -1.0) {
    out.polarDay = true;
    return out;
  }
  const double omega = acos(cosOmega) / kRad;  // half the day's length, in degrees of the Sun's travel
  const double rise = transit - omega / 360.0;
  const double set = transit + omega / 360.0;
  out.valid = true;
  out.sunrise = (int64_t)floor((rise - 2440587.5) * 86400.0 + 0.5);
  out.sunset = (int64_t)floor((set - 2440587.5) * 86400.0 + 0.5);
  return out;
}

}  // namespace stella
