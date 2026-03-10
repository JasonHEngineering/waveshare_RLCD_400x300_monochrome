#include "PCF85063A.h"
#include "i2c_bsp.h"

// ---------------------- GLOBALS ----------------------
// Pointer to the shared I2C bus
static I2cMasterBus* rtcBus = nullptr;
// Device handle for the RTC
static i2c_master_dev_handle_t rtcDevHandle;

// ---------------------- CONSTRUCTOR ----------------------
PCF85063A::PCF85063A() {
    // nothing to do here
}

// ---------------------- BUS SETTER ----------------------
// Call this before begin()
void PCF85063A::setBus(I2cMasterBus &bus) {
    rtcBus = &bus;

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000; // standard RTC speed

    ESP_ERROR_CHECK(i2c_master_bus_add_device(rtcBus->Get_I2cBusHandle(), &dev_cfg, &rtcDevHandle));
}

// ---------------------- BEGIN ----------------------
void PCF85063A::begin() {
    // nothing extra to do; bus must be set via setBus()
}

// ---------------------- TIME & DATE ----------------------
void PCF85063A::setTime(uint8_t h, uint8_t m, uint8_t s) {
    uint8_t buf[3] = { decToBcd(s), decToBcd(m), decToBcd(h) };
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_SECOND_ADDR, buf, 3);
}

void PCF85063A::setDate(uint8_t wday, uint8_t d, uint8_t mon, uint16_t yr) {
    year = yr - 1970;
    uint8_t buf[4] = { decToBcd(d), decToBcd(wday), decToBcd(mon), decToBcd(year) };
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_DAY_ADDR, buf, 4);
}

void PCF85063A::readTime() {
    uint8_t buf[7];
    rtcBus->i2c_read_buff(rtcDevHandle, RTC_SECOND_ADDR, buf, 7);

    second  = bcdToDec(buf[0] & 0x7F);
    minute  = bcdToDec(buf[1] & 0x7F);
    hour    = bcdToDec(buf[2] & 0x3F);
    day     = bcdToDec(buf[3] & 0x3F);
    weekday = bcdToDec(buf[4] & 0x07);
    month   = bcdToDec(buf[5] & 0x1F);
    year    = bcdToDec(buf[6]) + 1970;
}

// ---------------------- GETTERS ----------------------
uint8_t  PCF85063A::getSecond()  { readTime(); return second; }
uint8_t  PCF85063A::getMinute()  { readTime(); return minute; }
uint8_t  PCF85063A::getHour()    { readTime(); return hour; }
uint8_t  PCF85063A::getDay()     { readTime(); return day; }
uint8_t  PCF85063A::getWeekday() { readTime(); return weekday; }
uint8_t  PCF85063A::getMonth()   { readTime(); return month; }
uint16_t PCF85063A::getYear()    { readTime(); return year; }

// ---------------------- ALARM ----------------------
void PCF85063A::enableAlarm() {
    control_2 = RTC_CTRL_2_DEFAULT | RTC_ALARM_AIE;
    control_2 &= ~RTC_ALARM_AF;
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_CTRL_2, &control_2, 1);
}

void PCF85063A::setAlarm(uint8_t s, uint8_t m, uint8_t h, uint8_t d, uint8_t w) {
    auto encode = [](uint8_t val, uint8_t max) -> uint8_t {
        if (val < 99) {
            val = constrain(val, 0, max);
            val = ((val / 10) * 16 + (val % 10));
            val &= ~RTC_ALARM;
        } else {
            val = RTC_ALARM;
        }
        return val;
    };
    uint8_t buf[5] = { encode(s,59), encode(m,59), encode(h,23), encode(d,31), encode(w,6) };
    enableAlarm();
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_SECOND_ALARM, buf, 5);
}

void PCF85063A::readAlarm() {
    uint8_t buf[5];
    rtcBus->i2c_read_buff(rtcDevHandle, RTC_SECOND_ALARM, buf, 5);

    auto decode = [](uint8_t val, uint8_t mask) -> uint8_t {
        return (val & mask) ? 99 : ((val / 16 * 10) + (val & 0x0F));
    };

    alarm_second  = decode(buf[0], RTC_ALARM);
    alarm_minute  = decode(buf[1], RTC_ALARM);
    alarm_hour    = decode(buf[2], 0x3F);
    alarm_day     = decode(buf[3], 0x3F);
    alarm_weekday = decode(buf[4], 0x07);
}

uint8_t PCF85063A::getAlarmSecond()  { readAlarm(); return alarm_second; }
uint8_t PCF85063A::getAlarmMinute()  { readAlarm(); return alarm_minute; }
uint8_t PCF85063A::getAlarmHour()    { readAlarm(); return alarm_hour; }
uint8_t PCF85063A::getAlarmDay()     { readAlarm(); return alarm_day; }
uint8_t PCF85063A::getAlarmWeekday() { readAlarm(); return alarm_weekday; }

// ---------------------- TIMER ----------------------
void PCF85063A::timerSet(CountdownSrcClock clk, uint8_t val, bool int_en, bool int_pulse) {
    uint8_t timer_reg[2] = { val, RTC_TIMER_TE | (int_en? RTC_TIMER_TIE:0) | (int_pulse? RTC_TIMER_TI_TP:0) | (clk<<3) };
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_TIMER_VAL, timer_reg, 2);
}

bool PCF85063A::checkTimerFlag() {
    uint8_t ctrl2;
    rtcBus->i2c_read_buff(rtcDevHandle, RTC_CTRL_2, &ctrl2, 1);
    return (ctrl2 & RTC_TIMER_FLAG);
}

void PCF85063A::reset() {
    uint8_t val = 0x58;
    rtcBus->i2c_write_buff(rtcDevHandle, RTC_CTRL_1, &val, 1);
}

// ---------------------- HELPER ----------------------
uint8_t PCF85063A::decToBcd(uint8_t val) { return (val/10)*16 + (val%10); }
uint8_t PCF85063A::bcdToDec(uint8_t val) { return (val/16)*10 + (val%16); }
// #include "PCF85063A.h"

// #define PCF85063A_ADDR  0x51

// #define RTC_SECOND_ADDR 0x04
// #define RTC_DAY_ADDR    0x07

// PCF85063A::PCF85063A(I2cMasterBus& bus)
//     : i2cbus_(bus)
// {
//     i2c_master_bus_handle_t handle = i2cbus_.Get_I2cBusHandle();

//     i2c_device_config_t dev_cfg = {};
//     dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
//     dev_cfg.device_address  = PCF85063A_ADDR;
//     dev_cfg.scl_speed_hz    = 100000;

//     ESP_ERROR_CHECK(
//         i2c_master_bus_add_device(handle, &dev_cfg, &rtc_dev)
//     );
// }

// void PCF85063A::setTime(uint8_t hour, uint8_t minute, uint8_t second)
// {
//     uint8_t buf[4];
//     buf[0] = RTC_SECOND_ADDR;
//     buf[1] = decToBcd(second);
//     buf[2] = decToBcd(minute);
//     buf[3] = decToBcd(hour);

//     i2cbus_.i2c_write_buff(rtc_dev, -1, buf, 4);
// }

// void PCF85063A::setDate(uint8_t weekday, uint8_t day, uint8_t month, uint16_t yr)
// {
//     uint8_t buf[5];
//     uint8_t rtc_year = yr - 1970;

//     buf[0] = RTC_DAY_ADDR;
//     buf[1] = decToBcd(day);
//     buf[2] = decToBcd(weekday);
//     buf[3] = decToBcd(month);
//     buf[4] = decToBcd(rtc_year);

//     i2cbus_.i2c_write_buff(rtc_dev, -1, buf, 5);
// }

// void PCF85063A::readTime()
// {
//     uint8_t reg = RTC_SECOND_ADDR;
//     uint8_t data[7];

//     i2cbus_.i2c_master_write_read_dev(rtc_dev, &reg, 1, data, 7);

//     second  = bcdToDec(data[0] & 0x7F);
//     minute  = bcdToDec(data[1] & 0x7F);
//     hour    = bcdToDec(data[2] & 0x3F);
//     day     = bcdToDec(data[3] & 0x3F);
//     weekday = bcdToDec(data[4] & 0x07);
//     month   = bcdToDec(data[5] & 0x1F);
//     year    = bcdToDec(data[6]) + 1970;
// }

// uint8_t PCF85063A::getSecond() { readTime(); return second; }
// uint8_t PCF85063A::getMinute() { readTime(); return minute; }
// uint8_t PCF85063A::getHour()   { readTime(); return hour; }
// uint8_t PCF85063A::getDay()    { readTime(); return day; }
// uint8_t PCF85063A::getMonth()  { readTime(); return month; }
// uint16_t PCF85063A::getYear()  { readTime(); return year; }

// uint8_t PCF85063A::decToBcd(uint8_t val)
// {
//     return ((val / 10 * 16) + (val % 10));
// }

// uint8_t PCF85063A::bcdToDec(uint8_t val)
// {
//     return ((val / 16 * 10) + (val % 16));
// }