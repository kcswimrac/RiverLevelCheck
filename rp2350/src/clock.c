#include "clock.h"

#include "hardware/i2c.h"
#include "pico/stdlib.h"

#include "DEV_Config.h"
#include "pacific_time.h"

#define RTC_ADDR      0x51
#define RTC_CTRL1     0x00
#define RTC_SECONDS   0x04
#define RTC_OS_FLAG   0x80        /* oscillator stopped => time invalid */
#define RTC_CAP_12PF5 0x01
/* Year register is 0..99 and the chip treats multiples of 4 as leap years,
 * so the base must be a leap year: 2000 (the vendor driver used 1970, which
 * would make 2028 a non-leap year). */
#define RTC_YEAR_BASE 2000
#define I2C_TIMEOUT_US 5000

static int64_t  base_unix;        /* wall time at base_us */
static uint64_t base_us;
static bool     have_time;
static bool     rtc_ok;

static uint8_t to_bcd(int v)   { return (uint8_t)(((v / 10) << 4) | (v % 10)); }
static int     from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }

static bool rtc_read(uint8_t reg, uint8_t *buf, size_t n)
{
    if (i2c_write_timeout_us(I2C_PORT, RTC_ADDR, &reg, 1, true, I2C_TIMEOUT_US) != 1) return false;
    return i2c_read_timeout_us(I2C_PORT, RTC_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

static bool rtc_write(const uint8_t *buf, size_t n)
{
    return i2c_write_timeout_us(I2C_PORT, RTC_ADDR, buf, n, false, I2C_TIMEOUT_US) == (int)n;
}

static bool rtc_get(int64_t *out)
{
    uint8_t r[7];
    if (!rtc_read(RTC_SECONDS, r, sizeof r)) return false;
    if (r[0] & RTC_OS_FLAG) return false;
    int year = RTC_YEAR_BASE + from_bcd(r[6]);
    int mon  = from_bcd(r[5] & 0x1F);
    int day  = from_bcd(r[3] & 0x3F);
    if (year < 2024 || mon < 1 || mon > 12 || day < 1 || day > 31) return false;
    *out = pt_utc_epoch(year, mon, day, from_bcd(r[2] & 0x3F), from_bcd(r[1] & 0x7F),
                        from_bcd(r[0] & 0x7F));
    return true;
}

static bool rtc_put(int64_t t)
{
    pt_tm_t u;
    pt_utc_to_civil(t, &u);
    if (u.year < RTC_YEAR_BASE || u.year > RTC_YEAR_BASE + 99) return false;
    uint8_t b[8] = {RTC_SECONDS,      to_bcd(u.sec),  to_bcd(u.min),
                    to_bcd(u.hour),   to_bcd(u.day),  (uint8_t)u.wday,
                    to_bcd(u.month),  to_bcd(u.year - RTC_YEAR_BASE)};
    return rtc_write(b, sizeof b);   /* writing seconds also clears OS */
}

void clock_init(void)
{
    uint8_t ctrl[2] = {RTC_CTRL1, RTC_CAP_12PF5};   /* run, 24 h, 12.5 pF */
    rtc_ok = rtc_write(ctrl, sizeof ctrl);
    int64_t t;
    if (rtc_ok && rtc_get(&t)) {
        base_unix = t;
        base_us   = time_us_64();
        have_time = true;
    }
}

int64_t clock_now(void)
{
    if (!have_time) return 0;
    return base_unix + (int64_t)((time_us_64() - base_us) / 1000000ULL);
}

bool clock_set(int64_t t)
{
    if (t < 1704067200LL || t > 4102444800LL) return false;   /* 2024..2100 */
    base_unix = t;
    base_us   = time_us_64();
    have_time = true;
    if (rtc_ok) rtc_put(t);
    return true;
}

bool clock_rtc_present(void) { return rtc_ok; }
