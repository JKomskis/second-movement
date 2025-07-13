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

#pragma once

/*
 * PERCENT PROGRESS face
 *
 * This watch face displays the percentage progress from one date/time to another.
 * The face allows users to set start and end dates with hour/minute precision
 * and display the current progress as a percentage with 4 decimal places.
 *
 * Long press on the Alarm button to enter settings mode. The text "Year"
 * will appear with "St" on the bottom right, allowing you to set the start year.
 * Press Alarm repeatedly to advance the year. Press Light to move to the next
 * field (Month, Day, Hour, Minute), then advance to end date settings.
 *
 * The progress is calculated with high precision and displayed as XX.XXXX%
 * on custom LCD displays (using decimal point) or XX:XXXX% on classic LCD
 * displays (using colon). When progress reaches 100%, it shows as "1 00.0000"
 * on custom LCD or "100" on classic LCD.
 *
 * Edge cases:
 * - Before start time: Shows 00.0000%
 * - After end time: Shows 100.0000%
 * - Same start/end time: Shows 00.0000% or 100.0000% depending on current time
 */

#include "movement.h"

typedef enum {
    PROGRESS_PAGE_DISPLAY,
    PROGRESS_PAGE_START,
    PROGRESS_PAGE_END
} progress_page_t;

typedef enum {
    PROGRESS_SUBPAGE_YEAR = 0,
    PROGRESS_SUBPAGE_MONTH = 1,
    PROGRESS_SUBPAGE_DAY = 2,
    PROGRESS_SUBPAGE_HOUR = 3,
    PROGRESS_SUBPAGE_MINUTE = 4
} progress_setting_subpage_t;

typedef union {
    struct {
        uint16_t year : 12;   // 0-4095
        uint8_t month : 4;    // 1-12
        uint8_t day : 5;      // 1-31
        uint8_t hour : 5;     // 0-23
        uint8_t minute : 6;   // 0-59
    } bit;
    uint32_t reg;
} progress_datetime_t;

typedef struct {
    progress_datetime_t start_datetime;
    progress_datetime_t end_datetime;
} progress_dates_t;

typedef struct {
    progress_page_t current_page;
    progress_setting_subpage_t current_subpage;
    uint8_t face_index;
    progress_dates_t dates;
    bool dates_changed;
    bool quick_cycle;
} progress_state_t;

void progress_face_setup(uint8_t watch_face_index, void ** context_ptr);
void progress_face_activate(void *context);
bool progress_face_loop(movement_event_t event, void *context);
void progress_face_resign(void *context);

#define progress_face ((const watch_face_t){ \
    progress_face_setup, \
    progress_face_activate, \
    progress_face_loop, \
    progress_face_resign, \
    NULL, \
})
