/*
 * MIT License
 *
 * Copyright (c) 2025 Joseph Komskis
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include "progress_face.h"
#include "watch.h"
#include "watch_utility.h"
#include "filesystem.h"

static int progress_instances;

// Convert datetime to minutes since epoch for calculation (using Julian Day Number)
static uint64_t _progress_datetime_to_minutes(progress_datetime_t dt) {
    // Use Julian Day Number calculation (same as days_since_face.c)
    // From: https://en.wikipedia.org/wiki/Julian_day#Julian_day_number_calculation
    uint64_t julian_day = (1461 * (dt.bit.year + 4800 + (dt.bit.month - 14) / 12)) / 4
                        + (367 * (dt.bit.month - 2 - 12 * ((dt.bit.month - 14) / 12))) / 12
                        - (3 * ((dt.bit.year + 4900 + (dt.bit.month - 14) / 12) / 100)) / 4
                        + dt.bit.day - 32075;

    // Convert Julian Day to minutes: (days * 24 * 60) + (hours * 60) + minutes
    return julian_day * 24 * 60 + dt.bit.hour * 60 + dt.bit.minute;
}

// Compare two datetimes - returns true if dt1 >= dt2
static int64_t _progress_datetime_compare(progress_datetime_t dt1, progress_datetime_t dt2) {
    return (int64_t)(_progress_datetime_to_minutes(dt2)) - (int64_t)(_progress_datetime_to_minutes(dt1));
}

// Ensure end datetime is >= start datetime
static void _progress_face_validate_end_datetime(progress_state_t *state) {
    if (_progress_datetime_compare(state->dates.start_datetime, state->dates.end_datetime) < 0) {
        // End is before start, set end = start
        state->dates.end_datetime = state->dates.start_datetime;
        state->dates_changed = true;
    }
}

// Get pointer to active datetime being edited (start or end)
static progress_datetime_t* _progress_face_get_active_datetime(progress_state_t *state) {
    if (state->current_page == PROGRESS_PAGE_START) {
        return &state->dates.start_datetime;
    } else {
        return &state->dates.end_datetime;
    }
}

// Increment current setting value with wraparound
static void _progress_face_increment_current(progress_state_t *state) {
    progress_datetime_t *active_dt = _progress_face_get_active_datetime(state);
    watch_date_time_t now = movement_get_local_date_time();

    state->dates_changed = true;

    switch (state->current_subpage) {
        case PROGRESS_SUBPAGE_YEAR:
            active_dt->bit.year = active_dt->bit.year + 1;

            uint16_t current_year = now.unit.year + WATCH_RTC_REFERENCE_YEAR;
            if (active_dt->bit.year > current_year + 100) {
                active_dt->bit.year = current_year - 100;
            }
            break;
        case PROGRESS_SUBPAGE_MONTH:
            active_dt->bit.month = (active_dt->bit.month % 12) + 1;
            break;
        case PROGRESS_SUBPAGE_DAY:
            active_dt->bit.day = (active_dt->bit.day % watch_utility_days_in_month(active_dt->bit.month, active_dt->bit.year)) + 1;
            break;
        case PROGRESS_SUBPAGE_HOUR:
            active_dt->bit.hour = (active_dt->bit.hour + 1) % 24;
            break;
        case PROGRESS_SUBPAGE_MINUTE:
            active_dt->bit.minute = (active_dt->bit.minute + 1) % 60;
            break;
    }

    // If we're editing end datetime, ensure it's not before start
    if (state->current_page == PROGRESS_PAGE_END) {
        _progress_face_validate_end_datetime(state);
    }
}

// Update the progress display with 4 decimal precision
static void _progress_face_update_display(progress_state_t *state) {
    watch_date_time_t current_time = movement_get_local_date_time();
    progress_datetime_t current_dt;

    // Convert current time to progress_datetime_t format
    current_dt.bit.year = current_time.unit.year + WATCH_RTC_REFERENCE_YEAR;
    current_dt.bit.month = current_time.unit.month;
    current_dt.bit.day = current_time.unit.day;
    current_dt.bit.hour = current_time.unit.hour;
    current_dt.bit.minute = current_time.unit.minute;

    // Calculate progress with high precision
    uint64_t start_minutes = _progress_datetime_to_minutes(state->dates.start_datetime);
    uint64_t end_minutes = _progress_datetime_to_minutes(state->dates.end_datetime);
    uint64_t current_minutes = _progress_datetime_to_minutes(current_dt);

    uint64_t percentage_x10000; // Progress * 10000 for 4 decimal places

    if (current_minutes <= start_minutes) {
        percentage_x10000 = 0;
    } else if (current_minutes >= end_minutes) {
        percentage_x10000 = 1000000; // 100.0000%
    } else {
        // Calculate with high precision: (current - start) * 1000000 / (end - start)
        uint64_t progress = (current_minutes - start_minutes) * 1000000;
        uint64_t duration = end_minutes - start_minutes;
        if (duration > 0) {
            percentage_x10000 = progress / duration;
        } else {
            percentage_x10000 = 1000000; // Same start/end = 100%
        }
    }

    // Display top text
    watch_display_text_with_fallback(WATCH_POSITION_TOP, "PROG ", "PR   ");

    if (percentage_x10000 >= 1000000) {
        watch_display_text_with_fallback(WATCH_POSITION_BOTTOM, "1000000", "100   ");

        if (watch_get_lcd_type() != WATCH_LCD_TYPE_CUSTOM) {
            watch_clear_colon();
        }
    } else {
        // Format and display percentage based on LCD type
        char buf[7];

        // Less than 100% - show with 4 decimal precision
        sprintf(buf, "%06llu", percentage_x10000);

        if (watch_get_lcd_type() != WATCH_LCD_TYPE_CUSTOM) {
            watch_set_colon();
        }

        watch_display_text(WATCH_POSITION_BOTTOM, buf);
    }

    if (watch_get_lcd_type() == WATCH_LCD_TYPE_CUSTOM) {
        watch_set_decimal_if_available();
    }
}

// Display current setting value with blinking
static void _progress_face_display_current_value(progress_state_t *state, uint8_t subsecond) {
    progress_datetime_t *active_dt = _progress_face_get_active_datetime(state);
    char buf[7];

    if (state->current_subpage == PROGRESS_SUBPAGE_YEAR) {
        // Display 4 digit year
        watch_clear_colon();
        sprintf(buf, "%4u", active_dt->bit.year);
    } else if (state->current_subpage <= PROGRESS_SUBPAGE_DAY) {
        // Dispaly month and day together
        watch_clear_colon();
        sprintf(buf, "%02u%02u",
                active_dt->bit.month,
                active_dt->bit.day);
    } else {
        // Dispaly hour and minute
        watch_set_colon();
        sprintf(buf, "%02u%02u",
                active_dt->bit.hour,
                active_dt->bit.minute);
    }

    watch_display_text(WATCH_POSITION_BOTTOM, buf);

    // Blink the specific field being edited
    if (subsecond % 2 && !state->quick_cycle) {
        switch (state->current_subpage) {
            case PROGRESS_SUBPAGE_YEAR:
                // For year, blank the first 4 characters (year is in hours+minutes positions)
                watch_display_text(WATCH_POSITION_BOTTOM, "    ");
                break;
            case PROGRESS_SUBPAGE_MONTH:
                watch_display_text(WATCH_POSITION_HOURS, "  ");
                break;
            case PROGRESS_SUBPAGE_DAY:
                watch_display_text(WATCH_POSITION_MINUTES, "  ");
                break;
            case PROGRESS_SUBPAGE_HOUR:
                watch_display_text(WATCH_POSITION_HOURS, "  ");
                break;
            case PROGRESS_SUBPAGE_MINUTE:
                watch_display_text(WATCH_POSITION_MINUTES, "  ");
                break;
        }
    }
}

// Load dates from filesystem - returns true if file exists and dates loaded
static bool _progress_face_load_dates(progress_state_t *state) {
    char filename[13];
    progress_dates_t dates;

    sprintf(filename, "prog%03d.u64", state->face_index);

    if (filesystem_file_exists(filename)) {
        if (filesystem_read_file(filename, (char *) &dates, sizeof(progress_dates_t))) {
            state->dates = dates;
            return true;
        }
    }

    // File doesn't exist or read failed - set reasonable defaults
    watch_date_time_t now = movement_get_local_date_time();
    uint16_t current_year = now.unit.year + WATCH_RTC_REFERENCE_YEAR;

    state->dates.start_datetime.bit.year = current_year;
    state->dates.start_datetime.bit.month = 1;
    state->dates.start_datetime.bit.day = 1;
    state->dates.start_datetime.bit.hour = 0;
    state->dates.start_datetime.bit.minute = 0;

    state->dates.end_datetime.bit.year = current_year;
    state->dates.end_datetime.bit.month = 12;
    state->dates.end_datetime.bit.day = 31;
    state->dates.end_datetime.bit.hour = 23;
    state->dates.end_datetime.bit.minute = 59;

    return false;
}

// Save both dates - only called when both start and end are set
static void _progress_face_persist_dates(progress_state_t *state) {
    char filename[13];
    progress_dates_t old_dates;
    sprintf(filename, "prog%03d.u64", state->face_index);
    bool needs_write = true;

    if (filesystem_file_exists(filename)) {
        if (filesystem_read_file(filename, (char *)&old_dates, sizeof(progress_dates_t))) {
            if (memcmp(&old_dates, &state->dates, sizeof(progress_dates_t)) == 0) {
                needs_write = false; // No change, skip write
            }
        }
    }

    if (needs_write) {
        filesystem_write_file(filename, (char *)&state->dates, sizeof(progress_dates_t));
    }
    state->dates_changed = false;
}

// Abort quick cycle mode
static void _progress_face_abort_quick_cycle(progress_state_t *state) {
    if (state->quick_cycle) {
        state->quick_cycle = false;
        movement_request_tick_frequency(4);
    }
}

void progress_face_setup(uint8_t watch_face_index, void ** context_ptr) {
    (void) watch_face_index;
    if (*context_ptr == NULL) {
        *context_ptr = malloc(sizeof(progress_state_t));
        memset(*context_ptr, 0, sizeof(progress_state_t));
        progress_state_t *state = (progress_state_t *)*context_ptr;
        state->face_index = progress_instances++;

        // Check if dates file exists and load if present
        bool dates_initialized = _progress_face_load_dates(state);

        if (!dates_initialized) {
            // No file exists - go directly to start date settings
            state->current_page = PROGRESS_PAGE_START;
            state->current_subpage = PROGRESS_SUBPAGE_YEAR;
        } else {
            // File exists and dates loaded - show progress
            state->current_page = PROGRESS_PAGE_DISPLAY;
            state->current_subpage = PROGRESS_SUBPAGE_YEAR; // Not used in display mode
        }

        state->dates_changed = false;
        state->quick_cycle = false;
    }
}

void progress_face_activate(void *context) {
    progress_state_t *state = (progress_state_t *)context;

    if (state->current_page == PROGRESS_PAGE_DISPLAY) {
        movement_request_tick_frequency(1);
        _progress_face_update_display(state);
    } else {
        movement_request_tick_frequency(4);
    }
}

bool progress_face_loop(movement_event_t event, void *context) {
    progress_state_t *state = (progress_state_t *)context;

    switch (event.event_type) {
        case EVENT_ACTIVATE:
            if (watch_sleep_animation_is_running()) watch_stop_sleep_animation();
            if (state->current_page == PROGRESS_PAGE_DISPLAY) {
                _progress_face_update_display(state);
            }
            break;

        case EVENT_LOW_ENERGY_UPDATE:
        case EVENT_TICK:
            if (state->quick_cycle) {
                if (HAL_GPIO_BTN_ALARM_read()) {
                    _progress_face_increment_current(state);
                } else {
                    _progress_face_abort_quick_cycle(state);
                }
            }

            switch (state->current_page) {
                case PROGRESS_PAGE_START:
                case PROGRESS_PAGE_END: {
                    // Settings mode - show subpage title and page indicator
                    const char *field_titles[] = {"Year ", "Month", "Day  ", "Hour ", "Minut"};
                    const char *field_fallback_titles[] = {"YR", "MO", "DA", "HR", "M1"};

                    watch_display_text_with_fallback(WATCH_POSITION_TOP,
                        field_titles[state->current_subpage],
                        field_fallback_titles[state->current_subpage]);

                    // Page indicator on bottom right
                    watch_display_text(WATCH_POSITION_SECONDS,
                        (state->current_page == PROGRESS_PAGE_START) ? "St" : "En");

                    // Show current value
                    _progress_face_display_current_value(state, event.subsecond);
                    break;
                }

                case PROGRESS_PAGE_DISPLAY:
                    // Update display at the top of each minute
                    {
                        watch_date_time_t date_time = movement_get_local_date_time();
                        if (event.event_type == EVENT_LOW_ENERGY_UPDATE || date_time.unit.second == 0) {
                            _progress_face_update_display(state);
                        }

                        if (event.event_type == EVENT_LOW_ENERGY_UPDATE)
                        {
                            if (watch_get_lcd_type() == WATCH_LCD_TYPE_CLASSIC) {
                                // clear out the last two digits and replace them with the sleep mode indicator
                                watch_display_text(WATCH_POSITION_SECONDS, "  ");
                            }
                            if (!watch_sleep_animation_is_running()) watch_start_sleep_animation(1000);
                        }
            break;
                    }
                    break;
            }
            break;

        case EVENT_LIGHT_BUTTON_DOWN:
            // Only illuminate if we're in display mode
            if (state->current_page == PROGRESS_PAGE_DISPLAY) {
                movement_illuminate_led();
            }
            break;

        case EVENT_LIGHT_BUTTON_UP:
            switch (state->current_page) {
                case PROGRESS_PAGE_START:
                case PROGRESS_PAGE_END:
                    // Advance subpage
                    state->current_subpage = (state->current_subpage + 1) % 5;

                    // Check if we've completed all subpages for current page
                    if (state->current_subpage == PROGRESS_SUBPAGE_YEAR) {
                        if (state->current_page == PROGRESS_PAGE_START) {
                            // Move to end date settings
                            state->current_page = PROGRESS_PAGE_END;
                            // Ensure end datetime is >= start datetime
                            _progress_face_validate_end_datetime(state);
                        } else {
                            // Completed both start and end, return to display
                            state->current_page = PROGRESS_PAGE_DISPLAY;
                            _progress_face_persist_dates(state);
                            watch_clear_decimal_if_available();
                            watch_clear_colon();
                            _progress_face_update_display(state);
                            movement_request_tick_frequency(1);
                        }
                    }
                    break;
                default:
                    break;
            }
            break;

        case EVENT_ALARM_BUTTON_UP:
            // If we are on a settings page, increment whatever value we're setting.
            switch (state->current_page) {
                case PROGRESS_PAGE_START:
                case PROGRESS_PAGE_END:
                    _progress_face_abort_quick_cycle(state);
                    _progress_face_increment_current(state);
                    break;
                case PROGRESS_PAGE_DISPLAY:
                    // Does nothing in display mode
                    break;
            }
            break;

        case EVENT_ALARM_LONG_PRESS:
            switch (state->current_page) {
                case PROGRESS_PAGE_DISPLAY:
                    // Enter settings mode
                    state->current_page = PROGRESS_PAGE_START;
                    state->current_subpage = PROGRESS_SUBPAGE_YEAR;

                    watch_clear_decimal_if_available();
                    watch_clear_colon();

                    movement_request_tick_frequency(4);
                    break;
                case PROGRESS_PAGE_START:
                case PROGRESS_PAGE_END:
                    // Enable quick cycle for rapid increments
                    state->quick_cycle = true;
                    movement_request_tick_frequency(8);
                    break;
            }
            break;

        case EVENT_ALARM_LONG_UP:
            _progress_face_abort_quick_cycle(state);
            break;

        case EVENT_TIMEOUT:
            _progress_face_abort_quick_cycle(state);
            break;

        default:
            movement_default_loop_handler(event);
            break;
    }

    return true;
}

void progress_face_resign(void *context) {
    progress_state_t *state = (progress_state_t *)context;

    // If the user changed their dates, store them
    if (state->dates_changed) {
        _progress_face_persist_dates(state);
    }
}
